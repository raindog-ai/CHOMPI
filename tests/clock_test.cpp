#include "clock_adapter.h"
#include "clockManager.host.h"
static clockManager clock_;
int main(){
 daisy::TimerHandle timer;clock_.Init(&timer,120000000);
 assert(clock_.getGridPulses()==0);
 assert(clock_.getGridStepFrames()==4500); // 160 quarter-note BPM
 for(int i=0;i<6;++i)clock_.incrementCounters();
 assert(clock_.getGridPulses()==6);
 clock_.setTempo(160);assert(clock_.getGridStepFrames()==9000); // 80 BPM
 clock_.setTempo(480);assert(clock_.getGridStepFrames()==3000); // 240 BPM
 clock_.toggleClockMode();assert(clock_.getGridPulses()==0);
 for(int i=0;i<25;++i){daisy::hostUs+=20833;clock_.processMidiClock();}
 assert(clock_.getGridPulses()==25);
 assert(std::fabs(clock_.getGridStepFrames()-6000)<1); // incoming MIDI 120 BPM
 // A missing MIDI clock must not stretch the interval estimator into seconds.
 daisy::hostUs+=3000000;clock_.processMidiClock();
 assert(std::fabs(clock_.getGridStepFrames()-6000)<1);
 clock_.toggleClockMode();assert(clock_.getGridPulses()==6);
 std::puts("PASS: actual firmware clock, internal 80/160/240 BPM, independent counters, external 120 BPM and missing-clock rejection");
}
