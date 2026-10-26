#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cassert>
#include <cstdio>
namespace daisy {
static uint32_t hostUs=0;
struct TimerHandle { uint32_t period=0; void SetPeriod(uint32_t p){period=p;} };
struct System {
 static uint32_t GetUs(){return hostUs;} static uint32_t GetNow(){return hostUs/1000;}
 static uint32_t GetTick(){return hostUs;} static uint32_t GetTickFreq(){return 1000000;}
};
}
namespace daisysp {}
