#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
static unsigned newCalls=0, oldCalls=0;
#define TEMPO_GRID_MATCH_PROBE() (++newCalls)
#include "StepGrid.h"
#include "StepGrid11.host.h"

int main() {
    std::mt19937 random(42);
    std::vector<int16_t> pcm(96000*2);
    for(auto& p:pcm) p=int16_t(random());
    unsigned maxNew=0,maxOld=0;
    double maxDifference=0;
    // Compare production 1.1 and 1.2 on the same stereo noise, tempo, pitch,
    // reverse, and span. No platform clock or desktop speed assumptions.
    for(unsigned beats:{4u,8u,16u}) for(float pitch:{.01f,.5f,1.f,1.7f,2.f})
    for(bool reverse:{false,true}) for(unsigned duration:{3000u,4500u,6000u,8999u,12000u}) {
        tempo_grid::StepGrid now;
        tempo_grid_11::StepGrid before;
        now.SetSample(pcm.data(),96000,0,96000); before.SetSample(pcm.data(),96000,0,96000);
        now.SetEnabled(true); before.SetEnabled(true);
        now.SetLoopBeats(beats); before.SetLoopBeats(beats);
        for(unsigned step=0;step<16;++step) {
            const unsigned pulses=step*beats*24/16;
            const float frames=float(duration)/(beats/4);
            now.Prepare(true,pulses,frames,0,pitch,reverse);
            before.Prepare(true,pulses,frames,0,pitch,reverse);
            for(unsigned i=0;i<duration;i+=24) {
                newCalls=oldCalls=0;
                for(unsigned j=0;j<24;++j) {
                    float l,r,ol,orr;
                    now.Process(l,r);before.Process(ol,orr);
                    maxDifference=std::max(maxDifference,double(std::max(std::fabs(l-ol),std::fabs(r-orr))));
                    assert(std::isfinite(l)&&std::isfinite(r));
                    assert(l==ol && r==orr); // unchanged waveform at fixed controls
                }
                maxNew=std::max(maxNew,newCalls);maxOld=std::max(maxOld,oldCalls);
                assert(newCalls<=8);
            }
        }
    }
    // Real-time control changes, tiny steps, arbitrary callback/step boundaries:
    // invalidated lookahead falls back without any synchronous search burst.
    tempo_grid::StepGrid live;live.SetEnabled(true);live.SetSample(pcm.data(),96000,0,96000);
    for(unsigned block=0;block<20000;++block) {
        newCalls=0;
        live.Prepare(block%173!=0,block/3,1+random()%12000,block%97==0,
                     .01f+(random()%199)*.01f,random()%2);
        if(block%29==0) live.Restart();
        for(unsigned j=0;j<24;++j) {
            float l,r;live.Process(l,r);
            assert(std::isfinite(l)&&std::isfinite(r));
            assert(std::fabs(l)<=1.001f && std::fabs(r)<=1.001f);
        }
        assert(newCalls<=8);
    }
    assert(maxNew==8 && maxOld==137);
    std::printf("PASS: 24-frame block maximum search candidates: 1.1=%u, 1.2=%u; fixed-control sample difference=%g; dynamic control output bounded\n",maxOld,maxNew,maxDifference);
}
