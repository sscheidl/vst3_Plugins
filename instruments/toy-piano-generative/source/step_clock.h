// Turns the host's transport into sixteenths: for every audio block, the
// samples at which a new sixteenth begins. Free of VST3 types.
#pragma once

#include <cmath>
#include <cstdint>

namespace taureon::toy_piano_generative {

// What a host says about its transport at the first sample of a block.
struct Transport {
    bool playing = false;
    bool has_tempo = false;
    double tempo = 0.0; // quarter notes per minute
    bool has_position = false;
    double quarters = 0.0; // song position in quarter notes
};

class StepClock {
  public:
    // The tempo taken when the host names none.
    static constexpr double kFallbackTempo = 130.0;

    // Before each block. True if the transport starts with this block.
    bool Begin(const Transport& transport, double sample_rate, int num_samples) {
        samples_ = 0;
        if(!transport.playing || !(sample_rate > 0.0) || num_samples <= 0) {
            if(!transport.playing)
                playing_ = false;
            return false;
        }
        const bool valid_tempo = transport.has_tempo && transport.tempo >= kMinTempo
                                 && transport.tempo <= kMaxTempo;
        rate_ = (valid_tempo ? transport.tempo : kFallbackTempo) * 4.0 / 60.0 / sample_rate;
        const bool started = !playing_;
        // Without a song position the sixteenths are counted from the start.
        const bool has_position = transport.has_position && std::isfinite(transport.quarters)
                                  && std::fabs(transport.quarters) < kMaxQuarters;
        const double position =
            has_position ? 4.0 * transport.quarters : (started ? 0.0 : expected_);
        // The first sixteenth at or after the block's first sample.
        const auto first = static_cast<std::int64_t>(std::ceil(position - kSnap * rate_));
        const bool jumped = !started && std::fabs(position - expected_) > kJump;
        // While the song runs on, every sixteenth comes once and in order,
        // also if a tempo change moved it across a block boundary: one that
        // was just missed still comes, one that already came does not come
        // again. After a start or a jump, and when more than one was left
        // behind, counting begins anew at the song position.
        if(started || jumped || next_ < first - 1)
            next_ = first;
        position_ = position;
        samples_ = num_samples;
        expected_ = position + num_samples * rate_;
        playing_ = true;
        return started;
    }

    // The next sixteenth that begins in this block, in order; false when
    // there is none left. Call until it says false.
    bool Next(int& offset) {
        if(samples_ <= 0)
            return false;
        const double exact = (static_cast<double>(next_) - position_) / rate_;
        const double sample = exact <= 0.0 ? 0.0 : std::ceil(exact - kSnap);
        if(!(sample < samples_))
            return false;
        offset = static_cast<int>(sample);
        ++next_;
        return true;
    }

    bool Playing() const { return playing_; }
    // After the audio was off: the next playing block counts as a start.
    void Reset() {
        playing_ = false;
        samples_ = 0;
    }

  private:
    // A sixteenth this close before a sample, in samples, belongs to it:
    // rounding in the host's song position must not push it one sample on.
    static constexpr double kSnap = 1e-4;
    // The song position moved by more than this many sixteenths against the
    // expectation: the host jumped (a loop, a locate).
    static constexpr double kJump = 0.25;
    static constexpr double kMinTempo = 1.0;
    static constexpr double kMaxTempo = 2000.0;
    static constexpr double kMaxQuarters = 1e12;

    bool playing_ = false;
    double rate_ = 0.0;      // sixteenths per sample
    double position_ = 0.0;  // this block's first sample, in sixteenths
    double expected_ = 0.0;  // where the next block should begin
    std::int64_t next_ = 0;  // the next sixteenth to come
    int samples_ = 0;
};

} // namespace taureon::toy_piano_generative
