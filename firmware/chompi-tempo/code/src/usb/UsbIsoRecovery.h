#pragma once
#include <cstdint>

// Nonblocking retirement of one ISO transfer on the vendored STM32H7 OTG stack.
// Sequence follows ST's current USB_EPStopXfer, but never spins or pops/flushes
// the shared RX FIFO. The existing HAL RXFLVL handler drains it normally.
// https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Src/stm32h7xx_ll_usb.c
//
// Integration contract:
// - Serialize ALL methods in USB IRQ context (or an equivalent short exclusion).
// - One machine per controller. Only EP01/81/02/82/83, slave mode, dedicated
//   FIFOs. Bulk MIDI retirement is for config0/reconfigure, not audio recovery.
// - Start(ep, monotonic_ms) masks that endpoint. Blocks(ep) must also suppress
//   class DataIn/DataOut callbacks and ALL submissions for the owned endpoint.
// - Keep its HAL staging buffer alive. Do not Close/Open/Flush it meanwhile.
// - Poll once per platform SOF, BEFORE the configured-only class SOF dispatch.
//   Config0/ALT0 must still poll; desired alt and hardware retirement differ.
// - Ready means old transfer is retired. Reset the direction's ring/priming,
//   Release(true), then submit next transfer; or Release(false), then close it
//   WITHOUT FlushEP. Other endpoints retain their interrupt masks/FIFO contents.
// - Suspend BEFORE PHY gating; Resume after ungating. Do not rearm while paused.
// - AfterBusReset is ONLY for a real hardware USB/core reset. Not for config0,
//   ClassInit, ClassDeInit or ALT changes. It invalidates an outstanding job.
// - Failed quarantines the endpoint until hardware reset: no automatic rearm.
//   Arrange controlled restart outside real-time IRQs. No proof of silicon timing
//   is implied by the accompanying state-machine tests.
namespace chompi_usb {
enum class IsoRetireResult : uint8_t { Idle, Busy, Ready, Failed };

template<class Port> class IsoRecoveryMachine {
    enum class Phase : uint8_t {
        Idle, GlobalNak, Disabled, RxDrain, FlushAvailable, Flushing, Ready, Failed
    };
    Port& port_;
    Phase phase_=Phase::Idle;
    uint32_t started_=0;
    uint8_t ep_=0;
    bool ownsNak_=false, paused_=false;
    static constexpr uint32_t TimeoutMs=12;
    bool In() const { return (ep_ & 0x80)!=0; }
    void ReleaseNak() {
        if(ownsNak_) { port_.ClearGlobalOutNak(); ownsNak_=false; }
    }
    IsoRetireResult Fail() {
        ReleaseNak(); // Do not strand MIDI/EP0 behind our global OUT NAK.
        phase_=Phase::Failed;
        return IsoRetireResult::Failed;
    }
    void Ready() {
        port_.Retire(ep_); // Clear old W1C completion flags before unmask/rearm.
        ReleaseNak();
        phase_=Phase::Ready;
    }
public:
    explicit IsoRecoveryMachine(Port& port): port_(port) {}
    IsoRetireResult Result() const {
        if(phase_==Phase::Idle) return IsoRetireResult::Idle;
        if(phase_==Phase::Ready) return IsoRetireResult::Ready;
        if(phase_==Phase::Failed) return IsoRetireResult::Failed;
        return IsoRetireResult::Busy;
    }
    uint8_t Endpoint() const { return ep_; }
    bool Blocks(uint8_t ep) const { return phase_!=Phase::Idle && ep==ep_; }
    bool Paused() const { return paused_; }
    bool Start(uint8_t ep, uint32_t now_ms) {
        if(phase_!=Phase::Idle || paused_ || !port_.Valid(ep)) return false;
        // Another owner (or an uncompleted release) must not be commandeered.
        if(!(ep&0x80) && port_.GlobalOutNakActive()) return false;
        ep_=ep; started_=now_ms;
        port_.Mask(ep_);
        if(In()) {
            if(port_.Enabled(ep_)) port_.Disable(ep_);
            phase_=Phase::Disabled;
        } else {
            ownsNak_=true;
            port_.SetGlobalOutNak();
            phase_=Phase::GlobalNak;
        }
        return true;
    }
    IsoRetireResult Poll(uint32_t now_ms) {
        if(Result()!=IsoRetireResult::Busy || paused_) return Result();
        if(uint32_t(now_ms-started_)>=TimeoutMs) return Fail();
        // At most one phase transition per call; no register-polling loops.
        switch(phase_) {
        case Phase::GlobalNak:
            if(port_.GlobalOutNakEffective()) {
                if(port_.Enabled(ep_)) port_.Disable(ep_);
                phase_=Phase::Disabled;
            }
            break;
        case Phase::Disabled:
            if(!port_.Enabled(ep_)) phase_=In()?Phase::FlushAvailable:Phase::RxDrain;
            break;
        case Phase::RxDrain:
            // Global NAK remains asserted until pre-abort RX/status is drained.
            // Never read GRXSTSP here: it may contain MIDI or an EP0 SETUP.
            if(port_.RxEmpty()) Ready();
            break;
        case Phase::FlushAvailable:
            if(port_.CanFlush()) { port_.FlushIn(ep_); phase_=Phase::Flushing; }
            break;
        case Phase::Flushing:
            if(port_.FlushDone()) Ready();
            break;
        default: break;
        }
        return Result();
    }
    bool Release(bool restore_irq=true) {
        if(phase_!=Phase::Ready || paused_) return false;
        if(restore_irq) port_.Unmask(ep_);
        phase_=Phase::Idle;
        return true;
    }
    void Suspend() {
        if(paused_) return;
        paused_=true;
        if(ownsNak_) {
            ReleaseNak();
            // Re-establish the barrier on resume, even if EPENA has cleared.
            phase_=Phase::GlobalNak;
        }
    }
    void Resume(uint32_t now_ms) {
        if(!paused_) return;
        paused_=false; started_=now_ms;
        if(phase_==Phase::GlobalNak) {
            ownsNak_=true; port_.SetGlobalOutNak();
        }
    }
    void AfterBusReset() {
        ReleaseNak();
        phase_=Phase::Idle; ep_=0; paused_=false;
    }
};
} // namespace chompi_usb

#ifndef CHOMPI_USB_ISO_RECOVERY_CORE_ONLY
#include "stm32h7xx_hal.h"
namespace chompi_usb {
// No HAL Close/Abort/Flush functions: those either omit disable sequencing or
// busy-wait. The peripheral's RXFLVL IRQ remains enabled throughout OUT retire.
class Stm32IsoRecoveryPort {
    PCD_HandleTypeDef& h_;
    USB_OTG_DeviceTypeDef* Dev() const {
        return reinterpret_cast<USB_OTG_DeviceTypeDef*>(
            reinterpret_cast<uintptr_t>(h_.Instance)+USB_OTG_DEVICE_BASE);
    }
    USB_OTG_INEndpointTypeDef* In(uint8_t ep) const {
        return reinterpret_cast<USB_OTG_INEndpointTypeDef*>(
            reinterpret_cast<uintptr_t>(h_.Instance)+USB_OTG_IN_ENDPOINT_BASE+
            (ep&15)*USB_OTG_EP_REG_SIZE);
    }
    USB_OTG_OUTEndpointTypeDef* Out(uint8_t ep) const {
        return reinterpret_cast<USB_OTG_OUTEndpointTypeDef*>(
            reinterpret_cast<uintptr_t>(h_.Instance)+USB_OTG_OUT_ENDPOINT_BASE+
            (ep&15)*USB_OTG_EP_REG_SIZE);
    }
    static uint32_t MaskBit(uint8_t ep) {
        return 1UL<<((ep&15)+((ep&0x80)?0:16));
    }
public:
    explicit Stm32IsoRecoveryPort(PCD_HandleTypeDef& h): h_(h) {}
    bool Valid(uint8_t ep) const {
        if((ep!=0x01 && ep!=0x81 && ep!=0x02 && ep!=0x82 && ep!=0x83) || !h_.Instance ||
           h_.Init.dma_enable || (ep&15)>=h_.Init.dev_endpoints) return false;
        const auto& p=(ep&0x80)?h_.IN_ep[ep&15]:h_.OUT_ep[ep&15];
        return p.type==EP_TYPE_ISOC || ((ep==0x01 || ep==0x81) && p.type==EP_TYPE_BULK);
    }
    void Mask(uint8_t ep) {
        Dev()->DAINTMSK &= ~MaskBit(ep);
        if(ep&0x80) Dev()->DIEPEMPMSK &= ~(1UL<<(ep&15));
    }
    void Unmask(uint8_t ep) { Dev()->DAINTMSK |= MaskBit(ep); }
    bool Enabled(uint8_t ep) const {
        return ((ep&0x80)?In(ep)->DIEPCTL:Out(ep)->DOEPCTL)&USB_OTG_DIEPCTL_EPENA;
    }
    void Disable(uint8_t ep) {
        // Two writes match the documented/current ST abort order.
        if(ep&0x80) {
            In(ep)->DIEPCTL |= USB_OTG_DIEPCTL_SNAK;
            In(ep)->DIEPCTL |= USB_OTG_DIEPCTL_EPDIS;
        } else {
            Out(ep)->DOEPCTL |= USB_OTG_DOEPCTL_SNAK;
            Out(ep)->DOEPCTL |= USB_OTG_DOEPCTL_EPDIS;
        }
    }
    bool GlobalOutNakActive() const {
        return (Dev()->DCTL&USB_OTG_DCTL_GONSTS)!=0 ||
               (h_.Instance->GINTSTS&USB_OTG_GINTSTS_BOUTNAKEFF)!=0;
    }
    bool GlobalOutNakEffective() const {
        return (h_.Instance->GINTSTS&USB_OTG_GINTSTS_BOUTNAKEFF)!=0;
    }
    void SetGlobalOutNak() {
        // Old HAL does not service GONAKEFF IRQ. Poll the status instead;
        // RXFLVL stays enabled to pop its status entry through the normal path.
        h_.Instance->GINTMSK &= ~USB_OTG_GINTMSK_GONAKEFFM;
        Dev()->DCTL |= USB_OTG_DCTL_SGONAK;
    }
    void ClearGlobalOutNak() { Dev()->DCTL |= USB_OTG_DCTL_CGONAK; }
    bool RxEmpty() const { return !(h_.Instance->GINTSTS&USB_OTG_GINTSTS_RXFLVL); }
    bool CanFlush() const {
        const auto r=h_.Instance->GRSTCTL;
        return (r&USB_OTG_GRSTCTL_AHBIDL) &&
               !(r&(USB_OTG_GRSTCTL_TXFFLSH|USB_OTG_GRSTCTL_RXFFLSH|USB_OTG_GRSTCTL_CSRST));
    }
    void FlushIn(uint8_t ep) {
        // This layout deliberately assigns FIFO number == IN endpoint number.
        h_.Instance->GRSTCTL=USB_OTG_GRSTCTL_TXFFLSH|(uint32_t(ep&15)<<6);
    }
    bool FlushDone() const { return !(h_.Instance->GRSTCTL&USB_OTG_GRSTCTL_TXFFLSH); }
    void Retire(uint8_t ep) {
        // Same W1C mask used by this vendored HAL's bus-reset handler. Write,
        // never read-modify-write, so a stale XFRC cannot clear the new job.
        if(ep&0x80) {
            In(ep)->DIEPINT=0xFB7FU;
            h_.IN_ep[ep&15].xfer_len=0;
            h_.IN_ep[ep&15].xfer_count=0;
        } else {
            Out(ep)->DOEPINT=0xFB7FU;
            h_.OUT_ep[ep&15].xfer_len=0;
            h_.OUT_ep[ep&15].xfer_count=0;
        }
    }
};
using UsbIsoRecovery=IsoRecoveryMachine<Stm32IsoRecoveryPort>;
} // namespace chompi_usb
#endif
