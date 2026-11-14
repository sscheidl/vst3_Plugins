// Layer 3 of the Elements instrument: runs an engine that renders fixed
// 24-sample quanta at 48 kHz inside a host that asks for blocks of any length
// at any sample rate, and puts events on the engine's quantum grid by
// absolute time. Free of engine and VST3 types.
//
// Taken from Torus Classic 0.2.0 (adapter/stream_adapter.*) and adapted: the
// quantum is 24 samples instead of 12, notes end as well as begin, the pitch
// wheel carries semitones, and Torus' output stage (low-pass and limiter) is
// gone. The sample rate conversion (sinc_kernel.*) is unchanged.
//
// Time. Host sample n (counted from the last Reset) stands for the internal
// time n * 48000 / host_rate. The output is delayed against that by
// LatencySamples() host samples, the group delay of the interpolation filter;
// at 48 kHz there is no filter and no delay. An event at host sample n acts
// on the first quantum boundary that is not before its internal time: less
// than 24 internal samples (0.5 ms) late, never early. Positions are kept as
// exact fractions, so the result depends on the absolute positions only, not
// on how the host cuts its blocks.
//
// Overload. Waiting events are bounded: kNoteCapacity note events, and
// kControlCapacity points for each control and for the pitch wheel. A
// note-on that finds its queue full is dropped. A note-off is never dropped:
// if the queue is full, it takes the place of the newest waiting note event
// as "all notes off", so that no note can hang. A control point that finds
// its queue full takes the place of the newest waiting point of the same
// control: one step of the automation is lost, the latest value arrives, at
// its own time. Nothing ever acts before its time, full queues or not.
#pragma once

#include "sinc_kernel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace elements_synth {

constexpr const char* kAdapterVersion = "0.1.0";

struct EngineEvent {
    enum Type : std::uint8_t { kNoteOn, kNoteOff, kAllNotesOff, kControl, kPitchBend };
    Type type = kNoteOn;
    std::uint8_t index = 0;   // note or control
    std::uint16_t data = 0;   // velocity
    float value = 0.0f;       // control value, or the wheel in semitones
};

// What the adapter drives.
class QuantumEngine {
  public:
    virtual ~QuantumEngine() = default;
    // Called on the quantum boundary the event belongs to, before Render().
    virtual void HandleEvent(const EngineEvent& event) = 0;
    // Renders exactly StreamAdapter::kQuantum samples per channel.
    virtual void Render(float* left, float* right) = 0;
};

class StreamAdapter {
  public:
    static constexpr int kQuantum = 24;
    static constexpr double kInternalRate = 48000.0;
    static constexpr int kNoteCapacity = 1024;   // note events waiting for their quantum
    static constexpr int kControlKinds = 24;     // control events with index 0..23
    static constexpr int kControlCapacity = 256; // waiting points per control and for the wheel

    explicit StreamAdapter(QuantumEngine& engine) : engine_(engine) {}

    // Sets the host rate and allocates everything Process() needs, then
    // resets. False for a rate outside kMinHostRate..kMaxHostRate; the
    // adapter then keeps its previous rate. Not for the audio thread.
    bool Prepare(double host_rate);

    // Time back to zero: no waiting events, empty buffers, filter history
    // cleared. Does not touch the engine. Does not allocate or lock.
    void Reset();

    double HostRate() const { return host_rate_; }
    bool Resampling() const { return !ratio_.Unity(); }
    // Delay of the output against the host's time line, in host samples.
    int LatencySamples() const { return latency_; }
    const SincKernel& Kernel() const { return kernel_; }
    const RateRatio& Ratio() const { return ratio_; }

    // Queues an event `offset` host samples after the start of the next
    // Process() call; negative offsets count as 0. Events of one queue (the
    // notes; each control; the wheel) keep the order in which they were
    // added: one that names an earlier time than its predecessor acts
    // together with it, not before. A control or wheel value that meets a
    // waiting point of its queue in the same quantum replaces its value.
    //
    // False if the event was not taken as it is: a note-on with
    // kNoteCapacity note events waiting (counted in DroppedNotes()), a
    // note-off in the same situation (which then releases every note, see
    // above), or a control index of kControlKinds or more. A control point
    // that finds its queue full is taken (true): it replaces the newest
    // waiting point of that queue, which is counted in
    // DroppedControlPoints().
    bool AddEvent(std::int32_t offset, const EngineEvent& event);

    // The next `frames` host samples. Does not allocate or lock.
    void Process(float* left, float* right, int frames);

    // For tests and diagnostics.
    std::uint64_t HostPosition() const { return host_pos_; }
    std::int64_t RenderedSamples() const { return rendered_; }
    int PendingEvents() const;
    std::uint64_t DroppedNotes() const { return dropped_notes_; }
    std::uint64_t DroppedControlPoints() const { return dropped_control_points_; }
    std::size_t AllocatedBytes() const;

  private:
    struct Pending {
        std::int64_t quantum = 0;
        EngineEvent event;
    };
    // Waiting events of one kind in the order of their quanta: a ring of
    // fixed size, allocated by Prepare().
    struct Queue {
        std::vector<Pending> items;
        int head = 0, count = 0;

        void Allocate(int capacity) {
            items.assign(static_cast<std::size_t>(capacity), Pending{});
            head = count = 0;
        }
        void Clear() { head = count = 0; }
        bool Full() const { return count == static_cast<int>(items.size()); }
        const Pending& First() const { return items[static_cast<std::size_t>(head)]; }
        Pending& Last() {
            return items[static_cast<std::size_t>((head + count - 1) % static_cast<int>(items.size()))];
        }
        void Push(const Pending& pending) {
            items[static_cast<std::size_t>((head + count) % static_cast<int>(items.size()))] = pending;
            ++count;
        }
        void Pop() {
            head = (head + 1) % static_cast<int>(items.size());
            --count;
        }
    };
    static constexpr int kBendQueue = kControlKinds;
    static constexpr int kControlQueues = kControlKinds + 1;

    void RenderQuantum();
    void Compact(std::int64_t first_needed);

    QuantumEngine& engine_;
    double host_rate_ = kInternalRate;
    RateRatio ratio_;
    SincKernel kernel_;
    int latency_ = 0;
    int left_ = 0, right_ = 0; // input samples used around an output position

    // Host time: frames produced, and the same as internal time, floor and
    // remainder over ratio_.den.
    std::uint64_t host_pos_ = 0;
    std::int64_t time_int_ = 0;
    std::uint64_t time_rem_ = 0;
    // Internal position of the next output sample: host time minus latency.
    std::int64_t pos_int_ = 0;
    std::uint64_t pos_rem_ = 0;
    // Internal samples rendered so far; a multiple of kQuantum.
    std::int64_t rendered_ = 0;

    // Rendered samples not yet done with. buffer[0] has internal index base_.
    std::vector<float> buffer_left_, buffer_right_;
    std::int64_t base_ = 0;
    int count_ = 0;

    Queue notes_;
    std::array<Queue, kControlQueues> controls_;
    std::uint64_t dropped_notes_ = 0, dropped_control_points_ = 0;
};

} // namespace elements_synth
