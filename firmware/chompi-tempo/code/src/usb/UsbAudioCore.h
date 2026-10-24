#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace chompi_usb {
static_assert(ATOMIC_INT_LOCK_FREE == 2, "USB audio requires lock-free 32-bit atomics");
struct Frame { int16_t l, r; };
static_assert(sizeof(Frame) == 4, "stereo PCM frame");
// Single producer/single consumer, whole stereo frames. Reset is permitted only
// before interrupts start or with both consumers excluded (USB/audio IRQ priority 0).
template<typename T, uint32_t N> class Ring {
    static_assert((N & (N - 1)) == 0, "power-of-two capacity");
    std::atomic<uint32_t> write_{0}, read_{0};
    T data_[N]{};
public:
    uint32_t Size() const { return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire); }
    void Reset() { read_.store(0); write_.store(0); }
    bool Push(T v) {
        const auto w = write_.load(std::memory_order_relaxed);
        if(w - read_.load(std::memory_order_acquire) == N) return false;
        data_[w & (N-1)] = v;
        write_.store(w+1, std::memory_order_release); return true;
    }
    bool Pop(T& v) {
        const auto r = read_.load(std::memory_order_relaxed);
        if(r == write_.load(std::memory_order_acquire)) return false;
        v = data_[r & (N-1)]; read_.store(r+1, std::memory_order_release); return true;
    }
};
inline int16_t PCM(float v) {
    if(!std::isfinite(v)) return 0;
    if(v >= 32767.f / 32768.f) return 32767;
    if(v <= -1.f) return -32768;
    return static_cast<int16_t>(v * 32768.f);
}
inline Frame Decode(const uint8_t* p) {
    return {static_cast<int16_t>(uint16_t(p[0]) | uint16_t(p[1]) << 8),
            static_cast<int16_t>(uint16_t(p[2]) | uint16_t(p[3]) << 8)};
}
inline void Encode(Frame f, uint8_t* p) {
    p[0] = uint16_t(f.l); p[1] = uint16_t(f.l) >> 8;
    p[2] = uint16_t(f.r); p[3] = uint16_t(f.r) >> 8;
}
// Codec frame counter measured against USB SOF, with a slow occupancy correction.
// The 24-frame DMA quantum makes individual SOF deltas unsuitable as feedback.
class Clock {
    uint32_t previous_ = 0, elapsed_ = 0, accumulated_ = 0;
    float rate_ = 48.f, phase_ = 0.f, level_ = 120.f;
public:
    void Reset(uint32_t frames) { previous_=frames; elapsed_=accumulated_=0; rate_=48.f; phase_=0; level_=120; }
    void Sof(uint32_t frames, uint32_t level, bool primed, uint32_t usbMilliseconds=1) {
        accumulated_ += frames - previous_; previous_=frames;
        elapsed_ += usbMilliseconds;
        if(elapsed_ >= 128) {
            float measured = float(accumulated_) / float(elapsed_);
            // Ignore boot/paused codec and pathological scheduling stalls.
            if(measured > 47.5f && measured < 48.5f) rate_ += (measured-rate_) * .0625f;
            accumulated_=elapsed_=0;
        }
        if(primed) level_ += (float(level)-level_) * (1.f/64.f);
        else level_=120.f;
    }
    uint32_t Feedback() const {
        float correction = (120.f-level_) / 8192.f;
        if(correction > .04f) correction=.04f;
        if(correction < -.04f) correction=-.04f;
        return static_cast<uint32_t>((rate_ + correction) * 16384.f + .5f);
    }
    uint32_t CaptureFrames(uint32_t level, bool primed) {
        float correction = primed ? (float(level)-144.f) / 4096.f : 0.f;
        if(correction > .04f) correction=.04f;
        if(correction < -.04f) correction=-.04f;
        phase_ += rate_ + correction;
        uint32_t n=static_cast<uint32_t>(phase_); phase_-=float(n);
        return n < 47 ? 47 : (n > 49 ? 49 : n);
    }
};
struct MidiPacket { uint8_t b[4]; };
inline uint8_t MidiLength(uint8_t cin) {
    constexpr uint8_t sizes[16]={0,0,2,3,3,1,2,3,3,3,3,3,2,2,3,1};
    return sizes[cin & 15];
}
// Outbound TEMPO messages are complete 1-3 byte messages. Refuse incomplete
// streams/SysEx rather than emit invalid USB packets; incoming SysEx is supported.
inline bool Packetize(const uint8_t* p, size_t n, MidiPacket& packet) {
    if(!p || n == 0 || n > 3 || p[0] < 0x80) return false;
    uint8_t cin = p[0] >> 4;
    if(p[0] >= 0xf8) cin=15;
    else if(p[0] == 0xf1 || p[0] == 0xf3) cin=2;
    else if(p[0] == 0xf2) cin=3;
    else if(p[0] == 0xf6) cin=5;
    else if(p[0] >= 0xf0) return false;
    if(MidiLength(cin) != n) return false;
    packet = {{cin,0,0,0}};
    for(size_t i=0;i<n;++i) {
        if(i && p[i]>=0x80) return false;
        packet.b[i+1]=p[i];
    }
    return true;
}
}
