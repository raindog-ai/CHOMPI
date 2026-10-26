#include "StepGrid.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <random>
using tempo_grid::StepGrid;

static void tick(StepGrid& g, uint32_t p, float frames=6000, bool run=true, int mode=0, float pitch=1, bool rev=false) {
    g.Prepare(run,p,frames,mode,pitch,rev);
}
static void silence(StepGrid& g, unsigned n=100) {
    float l,r;
    for(unsigned i=0;i<n;++i) { g.Process(l,r); assert(std::isfinite(l)&&std::isfinite(r)); }
    assert(l==0 && r==0);
}
int main(int argc,char** argv) {
    using tempo_grid::LoopBeatsFromName;
    assert(LoopBeatsFromName(nullptr)==4);
    for (const char* name : {"", "slice_a1.wav", "_", "_b", "_b7.wav", "_b160.wav", "_b16x.wav", "_b9999999999.wav"})
        assert(LoopBeatsFromName(name)==4);
    assert(LoopBeatsFromName("slice_a1_b8_think.wav")==8);
    assert(LoopBeatsFromName("slice_a14_b16.wav")==16);
    assert(LoopBeatsFromName("slice_a14_b4_amen.wav")==4);
    // Verify both clock duration and audible duration for each supported span.
    for (unsigned beats : {4u, 8u, 16u}) {
        StepGrid span;
        std::vector<int16_t> dc(16000*2,10000);
        span.SetSample(dc.data(),16000,0,16000);
        span.SetLoopBeats(beats); span.SetEnabled(true);
        const unsigned pulses=beats*24/16, frames=600*beats/4;
        for (unsigned s=0;s<33;++s) {
            tick(span,s*pulses,600); assert(span.Playhead()==int(s%16));
            float l,r;
            for(unsigned i=0;i<frames-60;++i) span.Process(l,r);
            assert(l>.1f && r>.1f); // no premature cutoff on longer steps
            silence(span,200);
            tick(span,(s+1)*pulses-1,600); assert(span.Playhead()==int(s%16));
        }
        span.SetLoopBeats(3); assert(span.LoopBeats()==4);
        tick(span,1000,600); assert(span.Playhead()==0);
        tick(span,1006,600); assert(span.Playhead()==1);
    }
    // Exact one-to-one hardware mapping, including independent steps 15 and 16.
    unsigned used=0;
    for(unsigned s=0;s<16;++s) {
        assert(tempo_grid::StepForKey(tempo_grid::keys[s])==int(s));
        assert(!(used & (1u<<tempo_grid::keys[s]))); used |= 1u<<tempo_grid::keys[s];
    }
    assert(tempo_grid::StepForKey(99)==-1);
    StepGrid g; assert(!g.Enabled()); assert(g.Mask()==0xffff);
    tick(g,0); silence(g); assert(g.Playhead()==-1);
    g.SetEnabled(true); tick(g,100); assert(g.Playhead()==0);
    tick(g,105); assert(g.Playhead()==0); tick(g,106); assert(g.Playhead()==1);
    for(unsigned s=2;s<33;++s) {tick(g,100+s*6); assert(g.Playhead()==int(s%16));}
    g.SetMask(0); tick(g,298); assert(g.Playhead()==1); // muted steps still advance
    g.ToggleStep(15); assert(g.Mask()==0x8000); g.ToggleStep(20); assert(g.Mask()==0x8000);
    tick(g,298,6000,false); assert(g.Playhead()==-1);
    tick(g,300); assert(g.Playhead()==0);
    g.Restart(); tick(g,302); assert(g.Playhead()==0);
    tick(g,308); assert(g.Playhead()==1);
    tick(g,308,6000,true,1); assert(g.Playhead()==0); // clock mode change
    tick(g,1,6000,false); tick(g,UINT32_MAX-2); tick(g,3); assert(g.Playhead()==1);

    // Each slice contains a unique stereo DC level. Muted slices must be silent,
    // and no granular read may bleed into its neighboring slice.
    constexpr unsigned slice=1200, total=slice*16;
    std::vector<int16_t> pcm(total*2);
    for(unsigned s=0;s<16;++s) for(unsigned i=s*slice;i<(s+1)*slice;++i) {
        pcm[i*2]=1000*(s+1); pcm[i*2+1]=-pcm[i*2];
    }
    g.SetSample(pcm.data(),total,0,total); g.SetMask(0xaaaa);
    tick(g,0,2400,false); tick(g,0,2400);
    float l,r;
    for(unsigned s=0;s<16;++s) {
        tick(g,s*6,2400); assert(g.Playhead()==int(s));
        double energy=0;
        for(unsigned i=0;i<2400;++i) {
            g.Process(l,r); assert(std::isfinite(l)); assert(std::fabs(l+r)<1e-6);
            if(i>50&&i<2340) {
                const float expected=s%2 ? float(1000*(s+1))/32768.f : 0.f;
                assert(std::fabs(l-expected)<1e-5);
            }
            energy+=l*l;
        }
        assert((s%2) ? energy>1 : energy==0);
    }
    silence(g,200); // lost clock does not run forever
    g.SetMask(0xffff); g.Restart(); tick(g,0);
    for(int i=0;i<300;++i)g.Process(l,r);
    g.ToggleStep(0); silence(g); // live mute ramp
    g.ToggleStep(0); for(int i=0;i<100;++i)g.Process(l,r); assert(l>0);
    tick(g,0,6000,false); silence(g); // stop tail bounded

    // Null, tiny and reversed windows. Sanitizers check every granular access.
    for(unsigned size=0;size<65;++size) {
        std::vector<int16_t> tiny(size*2,12345);
        for(unsigned reverse=0;reverse<2;++reverse) {
            g.SetSample(tiny.data(),size,0,size); tick(g,0,137,false);
            for(unsigned s=0;s<16;++s) {
                tick(g,s*6,137,true,0,2,reverse);
                for(int i=0;i<137;++i) {g.Process(l,r);assert(std::isfinite(l)&&std::fabs(l)<=1);}
            }
        }
    }
    g.SetSample(nullptr,0,0,0);tick(g,0);silence(g);
    g.SetSample(pcm.data(),total,total,1);tick(g,0);silence(g);
    g.SetSample(pcm.data(),total,total+99,total+999);tick(g,0);silence(g);
    // Random bounded windows / tempo / pitch / direction, with ASan + UBSan.
    std::mt19937 rng(1337);
    for(int n=0;n<2000;++n) {
        unsigned a=rng()%(total+100),b=rng()%(total+100);
        g.SetSample(pcm.data(),total,a,b);g.SetMask(rng());
        tick(g,rng(),1+rng()%12000,true,rng()%2,.01f+(rng()%199)/100.f,rng()%2);
        for(int i=0;i<100;++i) {g.Process(l,r);assert(std::isfinite(l)&&std::isfinite(r));assert(std::fabs(l)<=1);}
    }
    // Output stereo float renders for objective spectral checks and WAV previews.
    if(argc>1) {
        for(int ratio=0;ratio<3;++ratio) {
            const unsigned duration = ratio==0 ? 3000 : ratio==1 ? 6000 : 12000;
            const unsigned sourceSlice=6000;
            std::vector<int16_t> sine(sourceSlice*16*2);
            for(unsigned i=0;i<sine.size()/2;++i) {
                sine[2*i]=int16_t(20000*std::sin(2*3.141592653589793*440*i/48000));
                sine[2*i+1]=int16_t(15000*std::sin(2*3.141592653589793*660*i/48000));
            }
            StepGrid render;render.SetEnabled(true);render.SetSample(sine.data(),sine.size()/2,0,sine.size()/2);
            std::ofstream out(std::string(argv[1])+"/tone-"+std::to_string(duration)+".f32",std::ios::binary);
            for(unsigned s=0;s<16;++s) {
                tick(render,s*6,duration);
                for(unsigned i=0;i<duration;++i) {render.Process(l,r);out.write((char*)&l,4);out.write((char*)&r,4);}
            }
        }
    }
    std::puts("PASS: mapping, 16-step wrap, mute/rest timing, transport/restart, clock wrap/mode, stereo isolation, live edits, clock loss, invalid/tiny windows, 2000 randomized windows");
}
