#include "arp_adapter.h"
#include "ArpeggiatorSequencer.host.h"
static BaseEngine chroma;
static sliceEngine slice;
static BaseEngine* engines[]={&chroma,&slice};
static clockManager clock_;
static Hardware hardware;
static OptionsManager options;
static ArpeggiatorSequencer arp;
int main(){
 arp.Init(engines,&clock_,&hardware,&options);
 // Empty patterns are expected on a fresh card: playing, LEDs and stop must work.
 for(int i=0;i<20;++i){
  arp.setPlay(true);arp.setPlay(false);arp.Prepare();
  for(int k=7;k<32;++k)arp.isKeyPlaying(k);
  assert(!arp.checkNotePlaying(0));
  arp.setPlay(true);arp.setPlay(false);arp.Prepare();
 }
 arp.setEngine(false);arp.toggleGrid();assert(slice.grid.Enabled());
 arp.setPlayTransport(true);
 for(int i=0;i<40;++i){arp.setClockEdge(1);arp.Prepare();}
 assert(arp.notes[1].empty());assert(slice.request_fifo.IsEmpty());
 arp.setPlayTransport(false);arp.toggleGrid();assert(!slice.grid.Enabled());
 // Keep a stock Tempo slice pattern, use grid for a while, then restore it.
 arp.setLatch(true);arp.setLatch(false);
 for(int k: {15,8,9}){
  arp.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START,float(k),k,127));arp.Prepare();
 }
 assert(arp.notes[1].size()==3);
 arp.setPlayTransport(true);
 arp.toggleGrid();slice.request_fifo.q.clear();const unsigned ons=hardware.noteOns;
 for(int i=0;i<200;++i){arp.setClockEdge(1);arp.Prepare();}
 assert(arp.notes[1].size()==3);assert(slice.request_fifo.IsEmpty());assert(hardware.noteOns==ons);
 arp.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START,0,20,127));arp.Prepare();
 assert(arp.notes[1].size()==3); // incoming notes cannot overwrite grid/Tempo pattern
 arp.toggleGrid();slice.request_fifo.q.clear();
 arp.setClockEdge(1);arp.Prepare();assert(!slice.request_fifo.IsEmpty());assert(hardware.noteOns>ons);
 // Switching a running saved pattern to an empty A/B state must be safe.
 arp.setTempNote();std::vector<NoteInfo> empty;arp.setSequence(1,&empty);
 arp.setClockEdge(1);arp.Prepare();assert(arp.notes[1].empty());
 arp.setPlayTransport(false);
 // Unequal engine pattern lengths exercise cross-engine stop safety.
 arp.setEngine(true);arp.setLatch(true);arp.setLatch(false);arp.setLatch(true);arp.setLatch(false);
 std::puts("PASS: actual Tempo sequencer empty transport/LEDs, grid bypass, preserved pattern and normal-mode restoration");
}
