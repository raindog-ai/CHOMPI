#pragma once
// Host substitutes for peripherals only. The sequencer class body is generated
// unchanged from the production header (only its embedded includes are omitted).
#include "StepGrid.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <cstdio>
#define NUM_VOICES 8
static unsigned hostNow=10;
struct System { static unsigned GetNow(){return hostNow;} };
constexpr int FREE=0, SYNC=1, NoteOn=1, NoteOff=0;
struct clockManager {
 int getClockMode(){return FREE;} float getDiv(size_t){return 1;}
 size_t getInterval(){return 187500;} void setNow(size_t){}
};
struct OptionsManager { int midi_ch_out_chroma=0,midi_ch_out_slice=1,transport_type=0; };
struct Hardware {
 unsigned noteOns=0,noteOffs=0;
 void queueMidiNote(int,int,int,int type){type ? ++noteOns : ++noteOffs;}
 void queueMidiTransport(int,bool){}
};
template<class T,size_t N> struct FIFO {
 std::deque<T> q; bool IsEmpty(){return q.empty();}
 void PushBack(T t){q.push_back(t);} T PopFront(){assert(!q.empty());T t=q.front();q.pop_front();return t;}
};
struct KeyRequest {
 enum class Type {START,STOP,DUMMY}; enum class Source {USER,SEQUENCER};
 Type type_; float transpose_nn_; int key_; float vel_; Source src_;
 KeyRequest(Type t=Type::DUMMY,float n=0,int k=0,float v=127,Source s=Source::USER):type_(t),transpose_nn_(n),key_(k),vel_(v),src_(s){}
};
struct BaseEngine {
 FIFO<KeyRequest,64> request_fifo;
 void handleRetrigEnv(bool,size_t){} void setLastSlice(bool){}
 bool isKeyPlaying(size_t){return false;} void getActiveKeys(uint16_t*,float*){}
};
struct sliceEngine:BaseEngine {
 struct Voice {bool gate_=false,activeFromUser=false,activeFromSequencer=false;} myVoices[8];
 tempo_grid::StepGrid grid;
};
