#include "UsbAudioMidi.h"
#include "UsbAudioCore.h"
#include "UsbDescriptors.h"
#include "UsbIsoRecovery.h"
extern "C" {
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;
USBD_HandleTypeDef hUsbDeviceHS;
}

namespace chompi_usb {
namespace {
constexpr uint8_t AudioOut=0x02, AudioIn=0x82, FeedbackIn=0x83, MidiOut=0x01, MidiIn=0x81;
constexpr uint16_t AudioMax=196;
Ring<Frame,512> playback, capture;
Ring<MidiPacket,128> midiRx, midiTx;
Clock clock;
Stm32IsoRecoveryPort recoveryPort(hpcd_USB_OTG_HS);
UsbIsoRecovery recovery(recoveryPort);
constexpr uint8_t allEndpoints[]={AudioOut,AudioIn,FeedbackIn,MidiOut,MidiIn};
bool retiring[5]{};
int EpIndex(uint8_t ep) { for(int i=0;i<5;++i) if(allEndpoints[i]==ep) return i; return -1; }
bool Retiring(uint8_t ep) { const int i=EpIndex(ep); return i>=0 && retiring[i]; }
void StartWanted(USBD_HandleTypeDef* d);
uint32_t codecFrames=0, sof=0, inStarted=0, feedbackStarted=0, lastOut=0;
uint8_t alternate[5]{};
uint16_t previousUsbFrame=0;
bool haveUsbFrame=false;
uint32_t usbElapsed=1;
bool configured=false, suspended=false, playPrimed=false, capturePrimed=false;
bool inBusy=false, feedbackBusy=false, midiBusy=false, initialized=false;
float inputFade=0.f;
Frame lastInput{};
MidiTransport::Callback receiveCallback=nullptr;
void* receiveContext=nullptr;
// Snapshot exposed via read-only vendor request 0x5a, protocol version 1.
// Counters persist across stream restarts; reset by power cycle only.
struct Stats {
    uint32_t version=1, sofCount=0, codecCount=0, playUnderruns=0, playOverruns=0,
             captureUnderruns=0, captureOverruns=0, malformed=0, midiDrops=0,
             isoRecoveries=0, maxAudioUs=0, audioOverBudget=0, endpointFailures=0, missedSofs=0;
} stats;
alignas(4) Stats statsReply;
alignas(4) uint8_t outPacket[AudioMax]{}, inPacket[AudioMax]{};
alignas(4) uint8_t feedbackPacket[4]{}, midiOutPacket[64]{}, midiInPacket[64]{};
alignas(4) uint8_t controlReply[4]{};
bool ratePending=false;
// Only the audio callback and USB ISR access audio rings; both priority 0.
// Outbound MIDI can also be queued by the clock timer, so its producer is guarded.
class Guard {
    uint32_t mask;
public:
    Guard(): mask(__get_PRIMASK()) { __disable_irq(); }
    ~Guard() { __set_PRIMASK(mask); }
};
void Mark(USBD_HandleTypeDef* d, uint8_t ep, bool used) {
    auto& e = (ep&0x80) ? d->ep_in[ep&15] : d->ep_out[ep&15];
    e.is_used=used; e.maxpacket=(ep==AudioOut || ep==AudioIn)?AudioMax:(ep==FeedbackIn?3:64);
}
bool Open(USBD_HandleTypeDef* d, uint8_t ep, uint8_t type, uint16_t max) {
    if(USBD_LL_OpenEP(d,ep,type,max)!=USBD_OK) return false;
    Mark(d,ep,true); return true;
}
void Retire(USBD_HandleTypeDef* d, uint8_t ep) {
    const auto& e=(ep&0x80)?d->ep_in[ep&15]:d->ep_out[ep&15];
    if(e.is_used) retiring[EpIndex(ep)]=true;
}
void Silence() {
    playback.Reset(); capture.Reset(); playPrimed=capturePrimed=false;
    inputFade=0; lastInput={0,0}; clock.Reset(codecFrames);
}
void StopPlayback(USBD_HandleTypeDef* d) {
    Retire(d,AudioOut); Retire(d,FeedbackIn); alternate[1]=0;
    playback.Reset(); playPrimed=false; feedbackBusy=false; inputFade=0; lastInput={0,0};
}
void StopCapture(USBD_HandleTypeDef* d) {
    Retire(d,AudioIn); alternate[2]=0; capture.Reset(); capturePrimed=false; inBusy=false;
}
uint8_t ClassInit(USBD_HandleTypeDef* d, uint8_t) {
    d->pClassData=&stats; // The ST core gates reset/disconnect DeInit on this.
    for(auto& a: alternate) a=0;
    Silence(); midiRx.Reset(); midiTx.Reset(); midiBusy=false; ratePending=false;
    suspended=false;
    configured=true;
    StartWanted(d);
    return USBD_OK;
}
uint8_t ClassDeInit(USBD_HandleTypeDef* d, uint8_t) {
    configured=false; suspended=false;
    StopPlayback(d); StopCapture(d); Retire(d,MidiOut); Retire(d,MidiIn);
    Silence(); midiRx.Reset(); midiTx.Reset(); midiBusy=false; ratePending=false;
    d->pClassData=nullptr; return USBD_OK;
}
uint8_t Fail(USBD_HandleTypeDef* d, USBD_SetupReqTypedef* r) { USBD_CtlError(d,r); return USBD_FAIL; }
uint8_t Setup(USBD_HandleTypeDef* d, USBD_SetupReqTypedef* r) {
    ratePending=false;
    if(r->bmRequest==0xc0 && r->bRequest==0x5a && r->wValue==0 && r->wIndex==0) {
        stats.sofCount=sof; stats.codecCount=codecFrames; statsReply=stats;
        return USBD_CtlSendData(d,reinterpret_cast<uint8_t*>(&statsReply),
                               r->wLength<sizeof(statsReply)?r->wLength:sizeof(statsReply));
    }
    if(!configured) return Fail(d,r);
    if(r->bmRequest==0x81 && r->bRequest==USB_REQ_GET_INTERFACE && r->wIndex<5 && r->wValue==0 && r->wLength==1) {
        controlReply[0]=alternate[r->wIndex]; return USBD_CtlSendData(d,controlReply,1);
    }
    if(r->bmRequest==0x81 && r->bRequest==USB_REQ_GET_STATUS && r->wIndex<5 && r->wValue==0 && r->wLength==2) {
        controlReply[0]=controlReply[1]=0; return USBD_CtlSendData(d,controlReply,2);
    }
    if(r->bmRequest==0x01 && r->bRequest==USB_REQ_SET_INTERFACE && r->wIndex<5 && r->wLength==0) {
        const uint16_t iface=r->wIndex, alt=r->wValue;
        if(alt>1 || (iface!=1 && iface!=2 && alt!=0)) return Fail(d,r);
        if(iface==1) {
            StopPlayback(d);
            alternate[1]=alt;
        } else if(iface==2) {
            StopCapture(d);
            alternate[2]=alt;
        }
        StartWanted(d);
        return USBD_OK; // Standard-interface dispatcher sends the status stage.
    }
    // UAC1 endpoint Sampling Frequency. Fixed-rate endpoints round to their sole
    // supported rate (48 kHz); a 3-byte SET never changes the codec clock.
    if((r->bmRequest==0xa2 || r->bmRequest==0x22) && (r->wIndex==AudioOut || r->wIndex==AudioIn)
        && r->wValue==0x0100 && r->wLength==3) {
        if(r->bmRequest==0x22 && r->bRequest==0x01) {
            ratePending=true; return USBD_CtlPrepareRx(d,controlReply,3);
        }
        if(r->bmRequest==0xa2 && r->bRequest>=0x81 && r->bRequest<=0x84) {
            const uint32_t rate=r->bRequest==0x84 ? 0 : 48000;
            controlReply[0]=rate; controlReply[1]=rate>>8; controlReply[2]=rate>>16;
            return USBD_CtlSendData(d,controlReply,3);
        }
    }
    return Fail(d,r);
}
uint8_t ControlReady(USBD_HandleTypeDef*) { ratePending=false; return USBD_OK; }
uint8_t DataOut(USBD_HandleTypeDef* d, uint8_t ep) {
    if(!configured || suspended || Retiring(ep)) return USBD_OK;
    const auto bytes=USBD_LL_GetRxDataSize(d,ep);
    if(ep==(AudioOut&15) && alternate[1]) {
        lastOut=sof;
        if(bytes>AudioMax || (bytes&3)) ++stats.malformed;
        else for(uint32_t i=0;i<bytes;i+=4) if(!playback.Push(Decode(outPacket+i))) ++stats.playOverruns;
        return USBD_LL_PrepareReceive(d,AudioOut,outPacket,AudioMax);
    }
    if(ep==(MidiOut&15)) {
        if(bytes>64 || (bytes&3)) ++stats.malformed;
        else for(uint32_t i=0;i<bytes;i+=4) {
            MidiPacket p{{midiOutPacket[i],midiOutPacket[i+1],midiOutPacket[i+2],midiOutPacket[i+3]}};
            if((p.b[0]>>4)!=0 || MidiLength(p.b[0])==0) { ++stats.malformed; continue; }
            if(!midiRx.Push(p)) ++stats.midiDrops;
        }
        return USBD_LL_PrepareReceive(d,MidiOut,midiOutPacket,64);
    }
    return USBD_OK;
}
void QueueFeedback(USBD_HandleTypeDef* d) {
    if(feedbackBusy || !alternate[1] || suspended || Retiring(FeedbackIn) || !d->ep_in[3].is_used) return;
    const uint32_t f=clock.Feedback();
    feedbackPacket[0]=f; feedbackPacket[1]=f>>8; feedbackPacket[2]=f>>16;
    if(USBD_LL_Transmit(d,FeedbackIn,feedbackPacket,3)==USBD_OK) { feedbackBusy=true; feedbackStarted=sof; }
}
void QueueCapture(USBD_HandleTypeDef* d) {
    if(inBusy || !alternate[2] || suspended || Retiring(AudioIn) || !d->ep_in[2].is_used) return;
    if(!capturePrimed && capture.Size()>=144) capturePrimed=true;
    const auto count=clock.CaptureFrames(capture.Size(),capturePrimed);
    bool underrun=false;
    for(uint32_t i=0;i<count;++i) {
        Frame f{};
        if(capturePrimed && !capture.Pop(f)) underrun=true;
        Encode(f,inPacket+i*4);
    }
    if(underrun) { ++stats.captureUnderruns; capturePrimed=false; }
    if(USBD_LL_Transmit(d,AudioIn,inPacket,count*4)==USBD_OK) { inBusy=true; inStarted=sof; }
}
uint8_t DataIn(USBD_HandleTypeDef* d, uint8_t ep) {
    if(!configured || suspended || Retiring(ep|0x80)) return USBD_OK;
    // The HAL arms the NEXT USB frame. Requeue on completion, not just SOF:
    // SOF-only submission would skip alternating frames while the old IN is busy.
    if(ep==(AudioIn&15)) { inBusy=false; if(configured) QueueCapture(d); }
    else if(ep==(FeedbackIn&15)) { feedbackBusy=false; if(configured) QueueFeedback(d); }
    else if(ep==(MidiIn&15)) midiBusy=false;
    return USBD_OK;
}
void StartWanted(USBD_HandleTypeDef* d) {
    if(!configured || suspended) return;
    for(const auto ep:allEndpoints) {
        const bool wanted=(ep==AudioOut || ep==FeedbackIn)?alternate[1]:(ep==AudioIn?alternate[2]:true);
        auto& e=(ep&0x80)?d->ep_in[ep&15]:d->ep_out[ep&15];
        if(!wanted || Retiring(ep) || e.is_used) continue;
        const bool midi=ep==MidiOut || ep==MidiIn;
        if(!Open(d,ep,midi?USBD_EP_TYPE_BULK:USBD_EP_TYPE_ISOC,midi?64:(ep==FeedbackIn?3:AudioMax))) {
            retiring[EpIndex(ep)]=true; ++stats.endpointFailures; continue;
        }
        if(ep==AudioOut) {lastOut=sof;USBD_LL_PrepareReceive(d,ep,outPacket,AudioMax);}
        if(ep==MidiOut) USBD_LL_PrepareReceive(d,ep,midiOutPacket,64);
    }
}
void ServiceRetirement(USBD_HandleTypeDef* d) {
    const auto before=recovery.Result();
    const auto result=recovery.Poll(sof);
    if(result==IsoRetireResult::Failed) {
        if(before!=IsoRetireResult::Failed) ++stats.endpointFailures;
        return; // Quarantine until physical bus reset; never strand global NAK.
    }
    if(result==IsoRetireResult::Ready) {
        const auto ep=recovery.Endpoint();
        recovery.Release(false);
        // The transfer is disabled and its own IN FIFO (if any) is now clean.
        // NEVER FlushEP for OUT: that would erase MIDI/EP0 in the shared RX FIFO.
        USBD_LL_CloseEP(d,ep); Mark(d,ep,false); retiring[EpIndex(ep)]=false;
        if(ep==AudioOut) {playback.Reset();playPrimed=false;inputFade=0;lastInput={0,0};}
        if(ep==AudioIn) {capture.Reset();capturePrimed=false;inBusy=false;}
        if(ep==FeedbackIn) feedbackBusy=false;
        if(ep==MidiIn) midiBusy=false;
    }
    if(recovery.Result()==IsoRetireResult::Idle && !suspended) {
        for(const auto ep:allEndpoints) if(Retiring(ep)) {recovery.Start(ep,sof);break;}
    }
    StartWanted(d);
}
void RecoverIn(USBD_HandleTypeDef* d,uint8_t ep,bool busy,uint32_t started) {
    if(busy && !Retiring(ep) && sof-started>=3) {Retire(d,ep);++stats.isoRecoveries;}
}
uint8_t Sof(USBD_HandleTypeDef* d) {
    if(suspended) return USBD_OK;
    clock.Sof(codecFrames,playback.Size(),playPrimed,usbElapsed);
    if(alternate[1]) {
        RecoverIn(d,FeedbackIn,feedbackBusy,feedbackStarted);
        QueueFeedback(d);
        // A missed OUT frame must not leave the endpoint unarmed forever.
        if(!Retiring(AudioOut) && d->ep_out[2].is_used && sof-lastOut>=4) {
            Retire(d,AudioOut); ++stats.isoRecoveries;
            playback.Reset(); playPrimed=false; inputFade=0; lastInput={0,0};
        }
    }
    if(alternate[2]) {
        RecoverIn(d,AudioIn,inBusy,inStarted);
        QueueCapture(d);
    }
    if(!midiBusy && !Retiring(MidiIn) && d->ep_in[1].is_used) {
        MidiPacket p; uint16_t bytes=0;
        while(bytes<64 && midiTx.Pop(p)) { for(int j=0;j<4;++j) midiInPacket[bytes++]=p.b[j]; }
        if(bytes && USBD_LL_Transmit(d,MidiIn,midiInPacket,bytes)==USBD_OK) midiBusy=true;
    }
    return USBD_OK;
}
// HAL reports an unreliable epnum for global ISO-incomplete interrupts. Recovery
// is based on per-endpoint SOF age above, not that stale HAL loop variable.
uint8_t Incomplete(USBD_HandleTypeDef*, uint8_t) { return USBD_OK; }
uint8_t* Config(uint16_t* n) { *n=sizeof(configuration); return configuration; }
uint8_t* NoOtherSpeed(uint16_t* n) { *n=0; return nullptr; }
alignas(4) uint8_t deviceDescriptor[]={18,1,0,2,0xef,2,1,64,0x83,0x04,0x40,0x57,0x00,0x02,1,2,3,1};
alignas(4) uint8_t lang[]={4,3,9,4};
alignas(4) uint8_t stringBuffer[128]{};
uint8_t* Device(USBD_SpeedTypeDef,uint16_t* n) { *n=sizeof(deviceDescriptor); return deviceDescriptor; }
uint8_t* Lang(USBD_SpeedTypeDef,uint16_t* n) { *n=sizeof(lang); return lang; }
uint8_t* String(const char* s,uint16_t* n) {
    size_t len=0; while(s[len] && len<63) ++len;
    stringBuffer[0]=*n=2+len*2; stringBuffer[1]=3;
    for(size_t i=0;i<len;++i) {stringBuffer[2+i*2]=s[i];stringBuffer[3+i*2]=0;}
    return stringBuffer;
}
uint8_t* Manufacturer(USBD_SpeedTypeDef,uint16_t* n) { return String("CHOMPI experimental",n); }
uint8_t* Product(USBD_SpeedTypeDef,uint16_t* n) { return String("CHOMPI USB FX + MIDI",n); }
uint8_t* Serial(USBD_SpeedTypeDef,uint16_t* n) {
    char s[29]="FX-"; constexpr char hex[]="0123456789ABCDEF";
    const volatile uint32_t* words=reinterpret_cast<const volatile uint32_t*>(UID_BASE);
    for(int i=0;i<3;++i) for(int j=0;j<8;++j) s[3+i*8+j]=hex[(words[i]>>(28-j*4))&15];
    s[27]=0; return String(s,n);
}
uint8_t* Name(USBD_SpeedTypeDef,uint16_t* n) { return String("Stereo FX and MIDI",n); }
USBD_DescriptorsTypeDef descriptors={Device,Lang,Manufacturer,Product,Serial,Name,Name};
USBD_ClassTypeDef usbClass={ClassInit,ClassDeInit,Setup,nullptr,ControlReady,DataIn,DataOut,Sof,
                           Incomplete,Incomplete,NoOtherSpeed,Config,NoOtherSpeed,NoOtherSpeed,nullptr};
}
void Init() {
    if(initialized) return;
    initialized=true;
    HAL_PWREx_EnableUSBVoltageDetector();
    if(USBD_Init(&hUsbDeviceHS,&descriptors,DEVICE_HS)!=USBD_OK) return;
    USBD_RegisterClass(&hUsbDeviceHS,&usbClass);
    USBD_Start(&hUsbDeviceHS);
}
void AudioInput(float* l,float* r,size_t frames) {
    codecFrames+=frames;
    if(!configured || suspended || !alternate[1] || Retiring(AudioOut)) {
        for(size_t i=0;i<frames;++i) l[i]=r[i]=0;
        return;
    }
    if(!playPrimed && playback.Size()>=192) playPrimed=true;
    bool underrun=false;
    for(size_t i=0;i<frames;++i) {
        Frame f{};
        bool available=playPrimed && playback.Pop(f);
        if(available) { lastInput=f; inputFade+=1.f/48.f; if(inputFade>1) inputFade=1; }
        else { if(playPrimed) underrun=true; inputFade-=1.f/48.f; if(inputFade<0) inputFade=0; }
        l[i]=float(lastInput.l)*(inputFade/32768.f); r[i]=float(lastInput.r)*(inputFade/32768.f);
    }
    if(underrun) { ++stats.playUnderruns; playPrimed=false; }
}
void AudioOutput(const float* l,const float* r,size_t frames) {
    if(!configured || suspended || !alternate[2] || Retiring(AudioIn)) return;
    for(size_t i=0;i<frames;++i) if(!capture.Push({PCM(l[i]),PCM(r[i])})) ++stats.captureOverruns;
}
void PollMidi() {
    if(!receiveCallback) return;
    MidiPacket p;
    for(unsigned i=0;i<8;++i) {
        bool got;
        { Guard guard; got=midiRx.Pop(p); }
        if(!got) break;
        receiveCallback(p.b+1,MidiLength(p.b[0]),receiveContext);
    }
}
void MidiTransport::StartRx(Callback c,void* ctx) { receiveCallback=c; receiveContext=ctx; }
void MidiTransport::Reset() { /* Never reinitialize USB while audio is streaming. */ }
bool MidiTransport::Tx(uint8_t* data,size_t size) {
    MidiPacket p;
    if(!Packetize(data,size,p)) return false;
    Guard guard;
    if(!configured || suspended) return false;
    if(!midiTx.Push(p)) { ++stats.midiDrops; return false; }
    return true;
}
// Called by the patched platform layer in USB IRQ context.
extern "C" void ChompiUsbSuspend() {
    // Retire queued packets before any resume/rearm; MIDI bulk transfers are
    // also quarantined so a pre-suspend note cannot be emitted after reconnect.
    for(const auto ep:allEndpoints) {
        Retire(&hUsbDeviceHS,ep);
        if((ep&0x80) && hUsbDeviceHS.ep_in[ep&15].is_used) {
            recoveryPort.Mask(ep);
            if(recoveryPort.Enabled(ep)) recoveryPort.Disable(ep);
        }
    }
    __DSB(); // Commit disable requests before the platform gates the PHY.
    recovery.Suspend();
    suspended=true; Silence(); midiRx.Reset(); midiTx.Reset();
}
extern "C" void ChompiUsbResume() {
    Silence(); suspended=false; haveUsbFrame=false; usbElapsed=1; recovery.Resume(sof);
    inStarted=feedbackStarted=lastOut=sof;
}
extern "C" void ChompiUsbSofTick(uint32_t frameNumber) {
    const uint16_t frame=frameNumber & 0x7ff;
    usbElapsed=haveUsbFrame ? ((frame-previousUsbFrame)&0x7ff) : 1;
    if(!usbElapsed) usbElapsed=1;
    previousUsbFrame=frame; haveUsbFrame=true;
    sof+=usbElapsed;
    if(usbElapsed>1) stats.missedSofs+=usbElapsed-1;
    ServiceRetirement(&hUsbDeviceHS);
}
extern "C" void ChompiUsbBusReset() {
    recovery.AfterBusReset(); haveUsbFrame=false;
    for(int i=0;i<5;++i) {retiring[i]=false;Mark(&hUsbDeviceHS,allEndpoints[i],false);}
}
extern "C" void ChompiUsbAudioTiming(uint32_t us) {
    if(us>stats.maxAudioUs) stats.maxAudioUs=us;
    if(us>=500) ++stats.audioOverBudget;
}
}
