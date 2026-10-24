// Exercise the REAL composite implementation and vendored USB control core with
// a fake peripheral. This is not a model of STM32 electrical/timing behavior.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
extern "C" {
#include "usbd_core.h"
uint32_t mock_uid[3]={1,2,3};
}
struct Endpoint { bool open=false, armed=false, stalled=false; uint32_t due=0, received=0; uint8_t* rx=nullptr; uint32_t capacity=0; std::vector<uint8_t> tx; } endpoints[256];
uint32_t now=0, capturePackets=0, returnedFrames=0;
std::vector<uint8_t> control;
#define CHOMPI_USB_ISO_RECOVERY_CORE_ONLY
#include "usb/UsbIsoRecovery.h"
namespace chompi_usb {
struct Stm32IsoRecoveryPort {
    bool nak=false;
    Stm32IsoRecoveryPort(PCD_HandleTypeDef&) {}
    bool Valid(uint8_t ep){return ep==1 || ep==0x81 || ep==2 || ep==0x82 || ep==0x83;}
    void Mask(uint8_t){} void Unmask(uint8_t){}
    bool Enabled(uint8_t ep){return endpoints[ep].armed;}
    void Disable(uint8_t ep){endpoints[ep].armed=false;}
    bool GlobalOutNakActive(){return nak;} bool GlobalOutNakEffective(){return nak;}
    void SetGlobalOutNak(){nak=true;} void ClearGlobalOutNak(){nak=false;}
    bool RxEmpty(){return true;} bool CanFlush(){return true;}
    void FlushIn(uint8_t){} bool FlushDone(){return true;} void Retire(uint8_t){}
};
using UsbIsoRecovery=IsoRecoveryMachine<Stm32IsoRecoveryPort>;
}
#include "usb/UsbAudioMidi.cpp"
using namespace chompi_usb;
extern "C" { PCD_HandleTypeDef hpcd_USB_OTG_HS; }
void Tick() { ++now; ChompiUsbSofTick(now&0x7ff); USBD_LL_SOF(&hUsbDeviceHS); }
void BusReset() {
    for(auto& ep:endpoints) ep=Endpoint{};
    ChompiUsbBusReset(); USBD_LL_Reset(&hUsbDeviceHS);
}
extern "C" {
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef*) { return USBD_OK; }
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef*) { return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef*) { return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef*) { return USBD_OK; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef*,uint8_t ep,uint8_t,uint16_t) { endpoints[ep].open=true; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef*,uint8_t ep) { endpoints[ep].open=endpoints[ep].armed=false; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef*,uint8_t ep) { endpoints[ep].armed=false; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef*,uint8_t ep) { endpoints[ep].stalled=true; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef*,uint8_t ep) { endpoints[ep].stalled=false; return USBD_OK; }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef*,uint8_t ep) { return endpoints[ep].stalled; }
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef*,uint8_t) { return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef*,uint8_t ep,uint8_t* data,uint16_t n) {
    ep |= 0x80;
    auto& e=endpoints[ep]; assert(e.open); assert(!e.armed || ep==0x80);
    e.tx.assign(data,data+n); e.armed=true; e.due=now+1;
    if(ep==0x80) { control=e.tx; e.armed=false; }
    return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef*,uint8_t ep,uint8_t* data,uint16_t n) {
    auto& e=endpoints[ep]; assert(e.open); e.rx=data; e.capacity=n; return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef*,uint8_t ep) { return endpoints[ep].received; }
void USBD_LL_Delay(uint32_t) {}
}
void Request(uint8_t bm,uint8_t req,uint16_t value,uint16_t index,uint16_t len) {
    uint8_t data[]={bm,req,uint8_t(value),uint8_t(value>>8),uint8_t(index),uint8_t(index>>8),uint8_t(len),uint8_t(len>>8)};
    control.clear(); endpoints[0].stalled=endpoints[0x80].stalled=false;
    USBD_LL_SetupStage(&hUsbDeviceHS,data);
}
void Alt(unsigned iface,unsigned alt) { Request(1,11,alt,iface,0); assert(!endpoints[0x80].stalled); }
void Configure() {
    BusReset(); USBD_LL_SetSpeed(&hUsbDeviceHS,USBD_SPEED_FULL);
    Request(0,5,1,0,0); Request(0,9,1,0,0);
    assert(configured && hUsbDeviceHS.pClassData && endpoints[1].open && endpoints[0x81].open);
}
void Send(unsigned ep,const uint8_t* data,unsigned bytes) {
    auto& e=endpoints[ep]; assert(e.rx && e.open && bytes<=e.capacity);
    std::memcpy(e.rx,data,bytes); e.received=bytes;
    USBD_LL_DataOutStage(&hUsbDeviceHS,ep,e.rx);
}
void Descriptors() {
    Request(0x80,6,0x100,0,18); assert(control.size()==18 && control[4]==0xef);
    Request(0x80,6,0x200,0,512); assert(control.size()==sizeof(configuration));
    assert(size_t(control[2]|control[3]<<8)==control.size() && control[4]==5);
    int iface=-1,alt=-1; int interfaces=0,formats=0; bool pairs[5][2]{};
    unsigned endpointMask=0, acBytes=0, midiBytes=0;
    for(size_t i=9;i<control.size();) {
        unsigned n=control[i],type=control[i+1]; assert(n>=2 && i+n<=control.size());
        const auto* p=control.data()+i;
        if(type==4) { iface=p[2];alt=p[3]; assert(iface<5 && alt<2 && !pairs[iface][alt]); pairs[iface][alt]=true; ++interfaces; }
        if(type==0x24 && iface==0) {
            acBytes+=n;
            if(p[2]==1) assert(n==10 && p[8]==1 && p[9]==2);
            if(p[2]==2) {assert(n==12 && p[7]==2 && p[8]==3);assert((p[3]==1 && p[4]==1 && p[5]==1) || (p[3]==3 && p[4]==0x13 && p[5]==7));}
            if(p[2]==3) assert((p[3]==2 && p[4]==3 && p[5]==6 && p[7]==1) || (p[3]==4 && p[4]==1 && p[5]==1 && p[7]==3));
        }
        if(iface==4 && type!=4) midiBytes+=n;
        if(type==0x24 && p[2]==2 && (iface==1 || iface==2)) { assert(n==11 && p[4]==2 && p[5]==2 && p[6]==16 && p[7]==1); assert((p[8]|p[9]<<8|p[10]<<16)==48000); ++formats; }
        if(type==5) { unsigned ep=p[2]; unsigned bit=(ep&15)+((ep&128)?16:0); assert(!(endpointMask & (1u<<bit))); endpointMask |= 1u<<bit; if(ep==2) assert(p[3]==5 && p[4]==196 && p[8]==0x83); }
        i+=n;
    }
    assert(interfaces==7 && formats==2 && acBytes==52 && midiBytes==65);
    Request(0x80,6,0x399,0x409,64); assert(endpoints[0x80].stalled); // unknown string used to null-deref
    Request(0x80,6,0x600,0,10); assert(endpoints[0x80].stalled); // full speed has no qualifier
    Request(0x80,6,0x700,0,64); assert(endpoints[0x80].stalled);
    puts("PASS descriptors: UAC1 + MIDI, terminals, endpoints, rates, lengths, invalid strings");
}
void Controls() {
    for(unsigned iface=0;iface<5;++iface) { Request(0x81,10,0,iface,1); assert(control.size()==1 && control[0]==0); }
    Alt(1,1); assert(alternate[1]==1 && alternate[2]==0 && endpoints[2].open && endpoints[0x83].open);
    Alt(2,1); Alt(1,0); assert(alternate[2]==1 && endpoints[0x82].open && Retiring(AudioOut));
    Alt(2,0);
    for(auto iface: {0,3,4,5,255}) { Request(1,11,1,iface,0); assert(endpoints[0x80].stalled); }
    Request(1,11,2,1,0); assert(endpoints[0x80].stalled);
    for(unsigned ep: {2,0x82}) {
        Request(0xa2,0x81,0x100,ep,3); assert(control==std::vector<uint8_t>({0x80,0xbb,0}));
        Request(0x22,1,0x100,ep,3); assert(ratePending);
        uint8_t otherRate[]={0x44,0xac,0}; Send(0,otherRate,3);
        Request(0xa2,0x81,0x100,ep,3); assert(control[0]==0x80 && control[1]==0xbb);
        Request(0xa2,0x81,0x100,ep,4); assert(endpoints[0x80].stalled);
    }
    Request(0xa2,0x81,0x100,0x83,3); assert(endpoints[0x80].stalled);
    for(unsigned ep=0;ep<256;++ep) {
        if(ep==0 || ep==0x80 || ep==1 || ep==0x81 || ep==2 || ep==0x82 || ep==0x83) continue;
        Request(0x82,0,0,ep,2); assert(endpoints[0x80].stalled);
    }
    puts("PASS controls: independent alternate settings, fixed rate, invalid requests");
}
std::vector<uint8_t> midiBytes;
void Receive(uint8_t* p,size_t n,void*) { midiBytes.insert(midiBytes.end(),p,p+n); }
void Midi() {
    MidiTransport t; t.StartRx(Receive,nullptr);
    uint8_t packets[]={9,0x90,60,100,15,0xf8,0,0,8,0x80,60,0}; Send(1,packets,sizeof(packets)); PollMidi();
    assert(midiBytes==std::vector<uint8_t>({0x90,60,100,0xf8,0x80,60,0}));
    uint8_t note[]={0x90,64,100}; assert(t.Tx(note,3));
    uint8_t clockByte=0xf8; assert(t.Tx(&clockByte,1));
    uint8_t bad[]={0xf0,1,2}; assert(!t.Tx(bad,3));
    Tick(); assert(endpoints[0x81].tx==std::vector<uint8_t>({9,0x90,64,100,15,0xf8,0,0}));
    endpoints[0x81].armed=false; USBD_LL_DataInStage(&hUsbDeviceHS,1,nullptr);
    const auto before=stats.malformed; Send(1,packets,3); assert(stats.malformed==before+1);
    puts("PASS MIDI: note/clock interleave, nonblocking transmit, malformed packets");
}
// Simulates HAL's next-frame ISO scheduling, with completion AFTER the next SOF.
// Drives the codec in 24-frame blocks, independent of USB SOF, at +/- clock drift.
void Stream(double ppm,unsigned milliseconds,bool play=true,bool cap=true) {
    Configure(); if(play) Alt(1,1); if(cap) Alt(2,1);
    const auto initial=stats;
    double nextAudio=.1, audioPeriod=.5/(1+ppm/1e6), hostPhase=0;
    double feedback=48; uint32_t capStart=capturePackets;
    float left[24],right[24];
    uint8_t packet[196]{};
    for(int i=0;i<49;++i) Encode({8192,-4096},packet+4*i);
    for(unsigned ms=0;ms<milliseconds;++ms) {
        while(nextAudio<=ms) {
            PollMidi(); AudioInput(left,right,24);
            for(int i=0;i<24;++i) { if(play && ms>20) { if(std::abs(left[i]-.25f)>=.001) { fprintf(stderr,"signal ppm=%f ms=%u size=%u under=%u ratefeedback=%f value=%f\n",ppm,ms,playback.Size(),stats.playUnderruns,feedback,left[i]); abort(); } assert(std::abs(right[i]+.125f)<.001); } }
            AudioOutput(left,right,24); nextAudio+=audioPeriod;
        }
        Tick();
        for(uint8_t ep: {uint8_t(0x82),uint8_t(0x83),uint8_t(0x81)}) {
            auto& e=endpoints[ep];
            if(e.armed && e.due<=now) {
                if(ep==0x82) { assert(e.tx.size()>=188 && e.tx.size()<=196); ++capturePackets; returnedFrames+=e.tx.size()/4; }
                else if(ep==0x83 && (ms%16)==0) { assert(e.tx.size()==3); feedback=double(e.tx[0]|e.tx[1]<<8|e.tx[2]<<16)/16384.; }
                e.armed=false; USBD_LL_DataInStage(&hUsbDeviceHS,ep&15,nullptr);
            }
        }
        if(ms%37==0) {
            uint8_t messages[]={9,0x90,64,100,15,0xf8,0,0,8,0x80,64,0};
            Send(1,messages,sizeof(messages));
            MidiTransport t; uint8_t cc[]={0xb0,20,64};assert(t.Tx(cc,3));
        }
        if(play) { hostPhase+=feedback; unsigned frames=unsigned(hostPhase);hostPhase-=frames; assert(frames>=47 && frames<=49); Send(2,packet,frames*4); }
    }
    assert(stats.playUnderruns==initial.playUnderruns && stats.playOverruns==initial.playOverruns);
    assert(stats.captureUnderruns==initial.captureUnderruns && stats.captureOverruns==initial.captureOverruns);
    assert(stats.midiDrops==initial.midiDrops);
    assert(!cap || capturePackets-capStart>=milliseconds-2); // catches SOF-only half-rate bug
    printf("PASS simulated %.0f ppm, %u ms, playback=%d capture=%d: zero FIFO faults, every-frame IN\n",ppm,milliseconds,play,cap);
}
void Lifecycle() {
    ChompiUsbSuspend(); USBD_LL_Suspend(&hUsbDeviceHS);
    float l[24],r[24]; AudioInput(l,r,24); for(auto v:l) assert(v==0); assert(playback.Size()==0 && capture.Size()==0);
    const auto failures=stats.endpointFailures, missed=stats.missedSofs;
    now+=3000; // Hardware frame wraps while no active SOF callbacks are delivered.
    USBD_LL_Resume(&hUsbDeviceHS); ChompiUsbResume();
    Tick(); assert(stats.endpointFailures==failures && stats.missedSofs==missed);
    // Missed transactions recover without re-enumerating or resetting MIDI.
    for(int i=0;i<10;++i) {Tick();}
    assert(stats.isoRecoveries>0 && endpoints[1].open && endpoints[0x81].open);
    BusReset(); assert(!configured && !alternate[1] && !alternate[2] && !hUsbDeviceHS.pClassData);
    Configure(); Alt(1,1); Alt(2,1); USBD_LL_DevDisconnected(&hUsbDeviceHS); assert(!configured);
    Configure(); Alt(1,1); Alt(2,1); Tick();
    Request(0,9,0,0,0); assert(!configured);
    // Continue retirement while unconfigured, including MIDI OUT. Reconfigure
    // before it finishes; requests must not reopen endpoints over old transfers.
    for(int i=0;i<3;++i) Tick();
    Request(0,9,1,0,0); assert(configured);
    for(int i=0;i<30;++i) Tick();
    assert(!Retiring(MidiOut) && !Retiring(MidiIn) && endpoints[1].open && endpoints[0x81].open);
    assert(stats.endpointFailures==failures);
    const auto oldMissed=stats.missedSofs; now+=2; Tick(); assert(stats.missedSofs==oldMissed+2);
    puts("PASS lifecycle: suspend silence, missed-frame recovery, reset, reconnect, disconnect");
}
void Core() {
    Ring<Frame,4> q; Frame f;
    for(int j=0;j<1000;++j) { for(int i=0;i<4;++i) assert(q.Push({int16_t(i),int16_t(-i)})); assert(!q.Push({0,0})); for(int i=0;i<4;++i) {assert(q.Pop(f)); assert(f.l==i && f.r==-i);} assert(!q.Pop(f)); }
    assert(PCM(NAN)==0 && PCM(INFINITY)==0 && PCM(1)==32767 && PCM(-1)==-32768);
    Clock c; c.Reset(0xfffffff0); c.Sof(32,120,false); assert(c.Feedback()==48*16384);
    puts("PASS bounded queues, PCM clipping/nonfinite input, counter wrap");
}
int main() {
    Core(); Init(); Configure(); Descriptors(); Controls(); Midi();
    Stream(0,60000); Stream(1000,3600000); Stream(-1000,3600000);
    Stream(1000,60000,false,true); Stream(-1000,60000,true,false);
    Configure(); Alt(1,1); Alt(2,1); Lifecycle();
    puts("ALL USB SOFTWARE TESTS PASSED; hardware behavior remains unverified.");
}
