#define CHOMPI_USB_ISO_RECOVERY_CORE_ONLY
#include "usb/UsbIsoRecovery.h"
#include <cassert>
#include <cstdio>
struct Port {
 bool enabled=true,nak=false,effective=false,empty=true,flushable=true,flushed=false,masked=false,retired=false;
 unsigned masks=0,unmasks=0,disables=0,flushes=0,clears=0; uint8_t flushEp=0;
 bool Valid(uint8_t e){return e==2||e==0x82||e==0x83||e==1||e==0x81;}
 bool GlobalOutNakActive(){return nak;}
 void SetGlobalOutNak(){nak=true;}
 void ClearGlobalOutNak(){nak=false;effective=false;clears++;}
 void Mask(uint8_t){masked=true;masks++;}
 void Unmask(uint8_t){masked=false;unmasks++;}
 bool Enabled(uint8_t){return enabled;}
 void Disable(uint8_t){disables++;}
 bool GlobalOutNakEffective(){return effective;}
 bool RxEmpty(){return empty;}
 bool CanFlush(){return flushable;}
 void FlushIn(uint8_t e){flushes++;flushEp=e;flushed=false;}
 bool FlushDone(){return flushed;}
 void Retire(uint8_t){retired=true;}
};
using R=chompi_usb::IsoRetireResult;
using M=chompi_usb::IsoRecoveryMachine<Port>;
int main(){
 {Port p;M m(p);assert(!m.Start(0,0));assert(!p.masked);assert(m.Start(0x82,0));assert(m.Blocks(0x82)&&!m.Blocks(0x83));assert(!m.Start(2,0));assert(p.disables==1);assert(m.Poll(1)==R::Busy);assert(!p.flushes);p.enabled=false;m.Poll(2);p.flushable=false;m.Poll(3);assert(!p.flushes);p.flushable=true;m.Poll(4);assert(p.flushes==1&&p.flushEp==0x82);m.Poll(5);assert(!p.retired);p.flushed=true;assert(m.Poll(6)==R::Ready);assert(p.retired&&p.masked);assert(m.Release());assert(!p.masked&&m.Result()==R::Idle);}
 {Port p;M m(p);assert(m.Start(2,0));assert(p.nak&&!p.disables);m.Poll(1);assert(!p.disables);p.effective=true;m.Poll(2);assert(p.disables==1);p.enabled=false;m.Poll(3);p.empty=false;m.Poll(4);assert(!p.retired&&p.nak);p.empty=true;assert(m.Poll(5)==R::Ready);assert(!p.nak&&!p.flushes);assert(m.Release(false)&&p.masked);}
 {Port p;M m(p);m.Start(2,0);m.Suspend();assert(!p.nak&&m.Paused());assert(m.Poll(9000)==R::Busy);m.Resume(9000);assert(p.nak);p.effective=true;m.Poll(9001);p.enabled=false;m.Poll(9002);assert(m.Poll(9003)==R::Ready);m.Release();}
 {Port p;M m(p);m.Start(2,100);assert(m.Poll(111)==R::Busy);assert(m.Poll(112)==R::Failed);assert(!p.nak&&p.masked);assert(!m.Release());assert(!m.Start(0x82,120));m.AfterBusReset();assert(m.Result()==R::Idle&&!m.Blocks(2));}
 {Port p;M m(p);m.Start(0x83,0xfffffff8U);assert(m.Poll(3)==R::Busy);assert(m.Poll(4)==R::Failed);assert(!p.flushes);}
 {Port p;M m(p);p.enabled=false;m.Start(0x81,0);m.Poll(1);m.Poll(2);assert(p.flushEp==0x81&&!p.disables);m.Suspend();assert(m.Poll(5000)==R::Busy);m.Resume(5000);p.flushed=true;assert(m.Poll(5001)==R::Ready);assert(m.Release());}
 {Port p;M m(p);p.nak=true;assert(!m.Start(1,0));assert(!p.masked);p.nak=false;m.Start(1,0);m.AfterBusReset();assert(!p.nak&&m.Result()==R::Idle);}
 // Every waiting phase times out, including a stuck selected TX FIFO flush.
 {Port p;M m(p);p.enabled=false;m.Start(0x82,0);m.Poll(1);m.Poll(2);assert(p.flushes==1);assert(m.Poll(12)==R::Failed);assert(!p.retired&&p.masked);}
 std::puts("8 recovery scenarios passed (normal IN/OUT, ownership, drain, suspend/resume, timeout, wrap, reset, MIDI).");
}
