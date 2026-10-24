#pragma once
#include <cstddef>
#include <cstdint>
namespace chompi_usb {
void Init();
void AudioInput(float* l, float* r, size_t frames);
void AudioOutput(const float* l, const float* r, size_t frames);
void PollMidi();
// Keeps the existing Daisy MIDI parser, but packet transport is nonblocking and
// delivers received bytes in the audio callback, never concurrently in USB IRQ.
class MidiTransport {
public:
    struct Config {};
    using Callback = void(*)(uint8_t*, size_t, void*);
    void Init(Config) { chompi_usb::Init(); }
    void Reset();
    void StartRx(Callback callback, void* context);
    bool RxActive() { return true; }
    void FlushRx() {}
    bool Tx(uint8_t* data, size_t size);
};
}
