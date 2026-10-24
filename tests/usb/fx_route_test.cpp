// Compile the production FxEngine class with deterministic stand-ins for DSP
// units and sample storage: verifies routing/isolation, not DSP sound quality.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
using std::size_t;
enum class InputSource {MIC,LINE_IN,RESAMPLE};
inline void fonepole(float& v,float target,float amount) { v+=(target-v)*amount; }
inline int16_t f2s16(float v) { return int16_t(std::max(-1.f,std::min(v,1.f))*32767); }
struct sampleManager { int writes=0; bool stereoWrite(int16_t,int16_t) { ++writes;return true; } void startRecording(){} void stopRecording(){} };
struct granularDelay {
    float l=0,r=0; int calls=0;
    void write(float a,float b){l=a;r=b;++calls;} void read(float* a,float* b){*a=l*.5f;*b=r*.5f;}
    void setMainControl(float){} void setAltControl(float){} void setFeedback(float){}
    void toggleBufferLock(){} void setBufferLock(bool){} void getColors(float*){}
};
struct Reverb { void SetAmount(float){} void SetInputGain(float){} void SetLowpass(float){} void SetTime(float){} void SetDiffusion(float){} void Process(float*,float*){} };
struct SimpleCompressor {void Init(){} void Process(float*,float*,float*,float*){} void setAmount(float){} };
struct EnvFollower { float value=0;void Init(){}void Process(float v){value=v;}float GetLastSamp(){return value;} };
struct MicFilter {void Init(float){}float Process(float v){return v;} };
namespace daisysp { struct DcBlock {void Init(float){}float Process(float v){return v;} };inline float SoftClip(float v){return v;} }
namespace chompi { struct Limiter {void Init(){}float ProcessComp(float v,float,float,float,float){return v;} }; }
#include "FxEngine.host.h"
int main() {
    for(auto source: {InputSource::MIC,InputSource::LINE_IN,InputSource::RESAMPLE}) for(size_t mode=0;mode<3;++mode) {
        sampleManager samples;granularDelay delay;Reverb reverb;
        fxEngine fx{};fx.Init(&samples,48000,&delay,&reverb,mode);fx.SetInputMonitor(true);fx.SetInputSource(source);fx.StartNewRecording();
        float analog[4][24], output[4][24]{}, usb[2][24], slice[2][24]{};
        for(auto& a:analog) std::fill(a,a+24,.75f);
        std::fill(usb[0],usb[0]+24,.1f);std::fill(usb[1],usb[1]+24,-.2f);
        const float* in[]={analog[0],analog[1],analog[2],analog[3]};
        float* out[]={output[0],output[1],output[2],output[3]};float* u[]={usb[0],usb[1]};float* s[]={slice[0],slice[1]};
        fx.Process(in,out,24,u,s,true);
        assert(samples.writes==0 && delay.calls==24);
        for(int i=0;i<24;++i) {assert(std::abs(out[0][i]-.15f)<1e-6);assert(std::abs(out[1][i]+.3f)<1e-6);assert(out[0][i]==out[2][i] && out[1][i]==out[3][i]);}
        fx.ApplyOutputFX(in,out,24);
        for(int i=0;i<24;++i) {assert(std::abs(out[2][i]-.18f)<1e-6);assert(std::abs(out[3][i]+.36f)<1e-6);}
        // Compare legacy route: analog monitor/capture remains available when
        // usb_only is false. This also catches accidentally unconditional guards.
        fx.Process(in,out,24,u,s,false);assert(samples.writes==24);
    }
    puts("PASS production FX routing: stereo USB dry/wet + output FX; MIC/Aux/resample excluded in every monitor mode; legacy capture preserved");
}
