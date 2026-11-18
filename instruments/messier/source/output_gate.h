#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace messier {

// Bypass and STOP on the sample (0.1.10, MS-R35). While either is on, the output's
// gain glides to 0 (one step per sample, 5 ms); at 0 the engine rests, its time
// stands. Released, the engine goes on where it stopped and the gain glides back.
//
// The host's switch points take effect at their own sample. Before a block is
// rendered, Plan() cuts it into segments: in each the gain glides toward one target,
// and the engine either renders or rests. The same plan says where the engine rests
// (a note struck there is dropped, MS-R36) and how many samples the engine has
// rendered before a host sample (events are put on the engine's own time, so that an
// event after a pause in the same block comes after the pause). The output is
// therefore the same for any division of the host's blocks.
//
// Audio thread only. Fixed size, allocates nothing.
class OutputGate {
  public:
    enum Switch { kBypass = 0, kStop = 1 };
    // Points per switch and block. One more takes the place of the last, so that the
    // switch ends the block as the host left it.
    static constexpr int kMaxPoints = 64;
    struct Segment {
        std::int32_t start = 0, end = 0; // host samples [start, end) of the block
        float gain = 1.0f;               // the gain before the segment's first sample
        float target = 1.0f;             // where the gain glides
        bool rendered = true;            // false: the engine rests, the output is 0
    };

    // The same arithmetic in the plan and in the rendering.
    static float Step(float gain, float target, float step) {
        return target > gain ? std::min(target, gain + step) : std::max(target, gain - step);
    }

    void SetFadeStep(float step) { step_ = step; }
    float FadeStep() const { return step_; }

    // A start: the switches as given, the gain at its goal, nothing planned.
    void Reset(bool bypass, bool stop) {
        on_ = {bypass, stop};
        gain_ = bypass || stop ? 0.0f : 1.0f;
        count_ = {0, 0};
        segment_count_ = 0;
    }

    void AddPoint(Switch which, std::int32_t offset, bool on) {
        const auto w = static_cast<std::size_t>(which);
        int& count = count_[w];
        if(count > 0)
            offset = std::max(offset, points_[w][static_cast<std::size_t>(count - 1)].offset);
        if(count == kMaxPoints)
            --count;
        points_[w][static_cast<std::size_t>(count++)] = Point{offset, on};
    }

    // Plans a block of `frames` host samples from the points added since the last
    // plan. Afterwards the switches and the gain are those at the block's end. A block
    // of 0 samples only takes the switches' last values.
    void Plan(std::int32_t frames) {
        segment_count_ = 0;
        std::array<int, 2> next = {0, 0};
        float gain = gain_;
        std::int32_t position = 0;
        while(position < frames) {
            for(std::size_t w = 0; w < 2; ++w)
                while(next[w] < count_[w] && points_[w][static_cast<std::size_t>(next[w])].offset <= position)
                    on_[w] = points_[w][static_cast<std::size_t>(next[w]++)].on;
            std::int32_t end = frames;
            for(std::size_t w = 0; w < 2; ++w)
                if(next[w] < count_[w])
                    end = std::min(end, points_[w][static_cast<std::size_t>(next[w])].offset);
            const float target = on_[0] || on_[1] ? 0.0f : 1.0f;
            if(target == 0.0f && gain == 0.0f) {
                Push(Segment{position, end, 0.0f, 0.0f, false});
            } else if(gain == target) {
                Push(Segment{position, end, gain, target, true});
            } else {
                // Gliding: the engine renders up to the sample whose gain reaches 0, and
                // rests from the next one on.
                float glide = gain;
                std::int32_t at = position;
                while(at < end && glide != target) {
                    glide = Step(glide, target, step_);
                    ++at;
                }
                if(target == 0.0f && glide == 0.0f && at < end) {
                    Push(Segment{position, at, gain, target, true});
                    Push(Segment{at, end, 0.0f, 0.0f, false});
                } else {
                    Push(Segment{position, end, gain, target, true});
                }
                gain = glide;
            }
            position = end;
        }
        for(std::size_t w = 0; w < 2; ++w) // points at the end (none for a valid block)
            while(next[w] < count_[w])
                on_[w] = points_[w][static_cast<std::size_t>(next[w]++)].on;
        gain_ = gain;
        count_ = {0, 0};
    }

    int SegmentCount() const { return segment_count_; }
    const Segment& SegmentAt(int i) const { return segments_[static_cast<std::size_t>(i)]; }

    // Whether the engine rests at this sample of the planned block. After the block
    // (or without a plan): whether it rests now.
    bool RestingAt(std::int32_t offset) const {
        for(int i = 0; i < segment_count_; ++i) {
            const Segment& s = segments_[static_cast<std::size_t>(i)];
            if(offset >= s.start && offset < s.end)
                return !s.rendered;
        }
        return Resting();
    }

    // How many samples the engine renders in the planned block before this sample.
    std::int32_t EngineOffset(std::int32_t offset) const {
        std::int32_t rendered = 0;
        for(int i = 0; i < segment_count_; ++i) {
            const Segment& s = segments_[static_cast<std::size_t>(i)];
            if(offset <= s.start)
                break;
            if(s.rendered)
                rendered += std::min(offset, s.end) - s.start;
        }
        return rendered;
    }

    bool Bypass() const { return on_[0]; }
    bool Stop() const { return on_[1]; }
    float Gain() const { return gain_; }
    bool Resting() const { return (on_[0] || on_[1]) && gain_ == 0.0f; }

  private:
    struct Point {
        std::int32_t offset = 0;
        bool on = false;
    };
    // Each step of the plan adds at most two segments; there are at most 2 * kMaxPoints
    // + 1 steps.
    static constexpr int kMaxSegments = 4 * kMaxPoints + 2;

    void Push(const Segment& segment) {
        if(segment.end <= segment.start)
            return;
        if(segment_count_ > 0) {
            Segment& last = segments_[static_cast<std::size_t>(segment_count_ - 1)];
            if(!last.rendered && !segment.rendered) { // one rest, however many points
                last.end = segment.end;
                return;
            }
        }
        if(segment_count_ < kMaxSegments)
            segments_[static_cast<std::size_t>(segment_count_++)] = segment;
    }

    float step_ = 1.0f / (0.005f * 48000.0f);
    std::array<bool, 2> on_{false, false};
    float gain_ = 1.0f;
    std::array<std::array<Point, kMaxPoints>, 2> points_{};
    std::array<int, 2> count_{0, 0};
    std::array<Segment, kMaxSegments> segments_{};
    int segment_count_ = 0;
};

} // namespace messier
