#pragma once

// Hardware-independent sequencer and stereo overlap/add stretcher.
// No allocation, file I/O, or locks on the audio path.
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>

// Host-only work counter; no counter or branch in production firmware.
#ifndef TEMPO_GRID_MATCH_PROBE
#define TEMPO_GRID_MATCH_PROBE() ((void)0)
#endif

namespace tempo_grid {
// Optional filename token, e.g. slice_a1_b16_amen.wav. Legacy files use 4 beats.
inline unsigned LoopBeatsFromName(const char* name) {
    if (!name) return 4;
    for (const char* p = name; *p; ++p) {
        if (p[0] != '_' || p[1] != 'b') continue;
        const char* q = p + 2;
        unsigned beats = 0;
        while (*q >= '0' && *q <= '9') {
            beats = beats * 10 + unsigned(*q++ - '0');
            if (beats > 16) return 4;
        }
        if ((*q == '_' || *q == '.' || *q == '\0') &&
            (beats == 4 || beats == 8 || beats == 16)) return beats;
    }
    return 4;
}
constexpr uint8_t keys[16] = {15, 8, 9, 10, 11, 16, 17, 18,
                              19, 20, 24, 25, 26, 27, 28, 31};
inline int StepForKey(unsigned key) {
    for (int i = 0; i < 16; ++i) if (keys[i] == key) return i;
    return -1;
}

class StepGrid {
  public:
    bool Enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void SetEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
    void ToggleStep(unsigned step) {
        if (step < 16) mask_.fetch_xor(1u << step, std::memory_order_relaxed);
    }
    uint16_t Mask() const { return mask_.load(std::memory_order_relaxed); }
    void SetMask(uint16_t mask) { mask_.store(mask, std::memory_order_relaxed); }
    int Playhead() const { return playhead_.load(std::memory_order_relaxed); }
    // Swallow a Shift+Loop release even if Shift was released first.
    bool shiftLoopOwned = false;
    void Restart() { restart_.fetch_add(1, std::memory_order_relaxed); }
    void SetLoopBeats(unsigned beats) {
        loopBeats_.store(beats == 8 || beats == 16 ? beats : 4, std::memory_order_relaxed);
        Restart();
    }
    unsigned LoopBeats() const { return loopBeats_.load(std::memory_order_relaxed); }

    // Call with audio interrupts excluded when changing the sample/window.
    // Counts are stereo frames, never bytes. Reversed/empty windows are silent.
    void SetSample(const int16_t* pcm, size_t frames, size_t begin, size_t end) {
        pcm_ = pcm;
        frames_ = frames;
        begin_ = std::min(begin, frames);
        end_ = std::min(end, frames);
        Restart();
    }

    // One MIDI quarter note = 24 pulses. Input stepFrames is one sixteenth note;
    // 4/8/16-beat loops use 6/12/24 pulses per step respectively.
    // The free clock uses the same pulse rate. Unsigned subtraction wraps safely.
    void Prepare(bool transport, uint32_t pulses, float stepFrames, int clockMode,
                 float pitch, bool reverse) {
        const bool run = Enabled() && transport;
        const uint32_t revision = restart_.load(std::memory_order_relaxed);
        const unsigned beats = LoopBeats();
        const unsigned pulsesPerStep = beats * 24 / 16;
        duration_ = std::max(1.f, std::min(stepFrames * float(beats / 4), 480000.f));
        pitch_ = std::isfinite(pitch) ? std::max(.01f, std::min(std::fabs(pitch), 2.f)) : 1.f;
        reverse_ = reverse;
        if (!run) {
            running_ = false;
            playhead_.store(-1, std::memory_order_relaxed);
            lastPulse_ = pulses;
            revision_ = revision;
            return;
        }
        if (!running_ || revision != revision_ || clockMode != clockMode_) {
            running_ = true;
            revision_ = revision;
            clockMode_ = clockMode;
            lastPulse_ = pulses;
            pendingPulses_ = 0;
            BeginStep(0);
        } else {
            const uint32_t delta = pulses - lastPulse_;
            lastPulse_ = pulses;
            // A discontinuous clock source must not cause an unbounded loop.
            const uint32_t total = pendingPulses_ + std::min(delta, uint32_t(96));
            pendingPulses_ = total % pulsesPerStep;
            if (total >= pulsesPerStep) BeginStep((step_ + total / pulsesPerStep) % 16);
        }
    }

    void Process(float& left, float& right) {
        left = right = 0.f;
        const bool valid = running_ && pcm_ && end_ > begin_ && count_ > 0;
        if (valid && age_ < duration_) {
            const float scan = float(count_) / duration_;
            for (unsigned n = 0; n < 2; ++n) {
                auto& grain = grains_[n];
                if (grain.age >= kGrain) {
                    grain.age = 0;
                    // Search was spread over the preceding hop. Never perform
                    // a whole WSOLA search in this 24-frame audio callback.
                    const bool ready = searchIndex_ == kSearchCandidates
                        && searchPitch_ == pitch_ && searchReverse_ == reverse_
                        && searchDuration_ == duration_;
                    grain.origin = ready ? searchBest_ : float(age_) * scan;
                    StartSearch(float(age_ + kHop) * scan, {kHop, grain.origin});
                }
                // Complementary triangular windows, 50% overlap.
                const float w = grain.age < kHop ? float(grain.age) / kHop
                                                        : float(kGrain - grain.age) / kHop;
                float pos = grain.origin + float(grain.age) * pitch_;
                pos -= std::floor(pos / float(count_)) * float(count_);
                if (reverse_) pos = float(count_ - 1) - pos;
                if (pos < 0.f) pos += float(count_);
                const size_t i = std::min(size_t(pos), count_ - 1);
                const size_t j = (i + 1) % count_;
                const float frac = pos - float(i);
                const size_t a = 2 * (offset_ + i), b = 2 * (offset_ + j);
                left += (pcm_[a] + (float(pcm_[b]) - pcm_[a]) * frac) * w / 32768.f;
                right += (pcm_[a+1] + (float(pcm_[b+1]) - pcm_[a+1]) * frac) * w / 32768.f;
                ++grain.age;
            }
            SearchTick();
            float edge = std::min(1.f, std::min(float(age_) / 48.f, (duration_ - age_) / 48.f));
            const float target = (Mask() & (1u << step_)) ? 1.f : 0.f;
            gain_ += std::max(-1.f/48.f, std::min(target - gain_, 1.f/48.f));
            left *= edge * gain_;
            right *= edge * gain_;
            ++age_;
            lastL_ = left;
            lastR_ = right;
            tail_ = 48;
        } else if (tail_) {
            // Short click-suppression tail on stop / missing clock / empty sample.
            --tail_;
            left = lastL_ * float(tail_) / 48.f;
            right = lastR_ * float(tail_) / 48.f;
        }
    }

  private:
    struct Grain { unsigned age; float origin; };
    size_t ReadIndex(float pos, bool reverse) const {
        int index = int(pos) % int(count_);
        if (index < 0) index += int(count_);
        if (reverse) index = int(count_ - 1) - index;
        return 2 * (offset_ + size_t(index));
    }
    float MatchCost(float candidate, const Grain& other) const {
        TEMPO_GRID_MATCH_PROBE();
        float cost = 0.f;
        // Compare both channels with the continuing grain over the overlap.
        // This waveform alignment prevents regular grain resets shifting pitch.
        for (unsigned t = 0; t < kHop; t += 20) {
            const size_t a = ReadIndex(candidate + float(t) * searchPitch_, searchReverse_);
            const size_t b = ReadIndex(other.origin + float(other.age + t) * searchPitch_, searchReverse_);
            const float dl = float(pcm_[a]) - pcm_[b];
            const float dr = float(pcm_[a+1]) - pcm_[b+1];
            cost += dl * dl + dr * dr;
        }
        return cost;
    }
    void StartSearch(float nominal, Grain reference) {
        searchNominal_ = searchBest_ = searchCoarse_ = nominal;
        searchCost_ = INFINITY;
        searchReference_ = reference;
        searchPitch_ = pitch_;
        searchReverse_ = reverse_;
        searchDuration_ = duration_;
        searchIndex_ = 0;
        // Leave searchCountdown_ alone: step/grain restarts cannot introduce
        // extra search work into a callback that straddles the boundary.
    }
    void SearchTick() {
        if (searchCountdown_) { --searchCountdown_; return; }
        searchCountdown_ = 2;
        if (searchIndex_ >= kSearchCandidates) return;
        // Same ordered coarse/refinement candidates as 1.1, one every three
        // samples: <=8 MatchCost calls per 24-frame block, previously 137.
        // All 137 candidates finish in <=411 frames, before the 480-frame hop.
        float candidate = searchNominal_;
        if (searchIndex_ >= 1 && searchIndex_ <= 121)
            candidate += -480.f + float(searchIndex_ - 1) * 8.f;
        else if (searchIndex_ >= 122) {
            if (searchIndex_ == 122) searchCoarse_ = searchBest_;
            candidate = searchCoarse_ + float(int(searchIndex_) - 129);
        }
        const float cost = MatchCost(candidate, searchReference_);
        if (cost < searchCost_) { searchCost_ = cost; searchBest_ = candidate; }
        ++searchIndex_;
    }
    void BeginStep(unsigned step) {
        step_ = step;
        playhead_.store(int(step), std::memory_order_relaxed);
        const size_t length = end_ > begin_ ? end_ - begin_ : 0;
        // Partition using endpoints, including remainder frames in the last slice.
        offset_ = begin_ + length * step / 16;
        count_ = begin_ + length * (step + 1) / 16 - offset_;
        age_ = 0;
        gain_ = (Mask() & (1u << step)) ? 1.f : 0.f;
        grains_[0] = {0, 0.f};
        grains_[1] = {kHop, -float(kHop) * pitch_};
        StartSearch(float(kHop) * (float(count_) / duration_), {kHop, 0.f});
    }
    static constexpr unsigned kGrain = 960, kHop = kGrain / 2;
    static constexpr unsigned kSearchCandidates = 137;
    Grain searchReference_ = {};
    float searchNominal_ = 0.f, searchBest_ = 0.f, searchCoarse_ = 0.f;
    float searchCost_ = 0.f, searchPitch_ = 1.f, searchDuration_ = 4500.f;
    bool searchReverse_ = false;
    unsigned searchIndex_ = kSearchCandidates, searchCountdown_ = 0;
    Grain grains_[2] = {};
    std::atomic<uint32_t> enabled_{0}, mask_{0xffff}, restart_{0};
    std::atomic<uint32_t> loopBeats_{4};
    std::atomic<int> playhead_{-1};
    const int16_t* pcm_ = nullptr;
    size_t frames_ = 0, begin_ = 0, end_ = 0, offset_ = 0, count_ = 0;
    uint32_t lastPulse_ = 0, pendingPulses_ = 0, revision_ = 0, age_ = 0;
    unsigned step_ = 0, tail_ = 0;
    int clockMode_ = 0;
    bool running_ = false, reverse_ = false;
    float duration_ = 4500.f, pitch_ = 1.f, gain_ = 0.f, lastL_ = 0.f, lastR_ = 0.f;
};
} // namespace tempo_grid
