// Layer 3 of Torus Classic: runs an engine that renders fixed 12-sample
// quanta at 48 kHz inside a host that asks for blocks of any length at any
// sample rate, and puts events on the engine's quantum grid by absolute time.
// Free of Torus and VST3 types.
//
// Component id "torus-classic.adapter", version kAdapterVersion.
//
// At the end of the chain, at the host's rate, sits the output stage: a
// low-pass and a limiter (output_stage.h). Its filter control is an event
// like the others and changes when its quantum is rendered: at or after the
// host sample it was sent for, by at most the grid and the latency.
//
// Time. Host sample n (counted from the last Reset) stands for the internal
// time n * 48000 / host_rate. The output is delayed against that by
// LatencySamples() host samples, the group delay of the interpolation filter;
// at 48 kHz there is no filter and no delay. An event at host sample n acts
// on the first quantum boundary that is not before its internal time: less
// than 12 internal samples late, never early. Positions are kept as exact
// fractions, so the result depends on the absolute positions only, not on how
// the host cuts its blocks.
//
// Overload. Waiting events are bounded: kNoteCapacity notes, and
// kControlCapacity points for each control, for the model and for the pitch
// bend. A note that finds its queue full is dropped. A control point that
// finds its queue full takes the place of the newest waiting point of the
// same control: one step of the automation is lost, the latest value arrives,
// at its own time. Nothing ever acts before its time, full queues or not.
#pragma once

#include "output_stage.h"
#include "sinc_kernel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace torus_classic {

constexpr const char* kAdapterVersion = "0.2.1";

struct EngineEvent {
    // kOutputFilter is the adapter's own: it sets the low-pass of the output
    // stage and does not reach the engine.
    enum Type : std::uint8_t { kNoteOn, kControl, kModel, kPitchBend, kOutputFilter };
    Type type = kNoteOn;
    std::uint8_t index = 0;   // note, control or model
    std::uint16_t data = 0;   // velocity or 14-bit bend
    float value = 0.0f;       // control value
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
    static constexpr int kQuantum = 12;
    static constexpr double kInternalRate = 48000.0;
    static constexpr int kNoteCapacity = 1024;   // notes waiting for their quantum
    static constexpr int kControlKinds = 14;     // control events with index 0..13
    static constexpr int kControlCapacity = 256; // waiting points per control, model, bend

    explicit StreamAdapter(QuantumEngine& engine) : engine_(engine) {}

    // Sets the host rate and allocates everything Process() needs, then
    // resets. False for a rate outside kMinHostRate..kMaxHostRate; the
    // adapter then keeps its previous rate. Not for the audio thread.
    bool Prepare(double host_rate);

    // Time back to zero: no waiting events, empty buffers, filter history
    // cleared, output stage at rest. Does not touch the engine. Does not
    // allocate or lock.
    void Reset();

    // The output filter's control at once, without waiting for a quantum and
    // without a glide: for a start from a known state.
    void JumpOutputFilter(float value) { output_.JumpFilter(value); }
    const OutputStage& Output() const { return output_; }

    double HostRate() const { return host_rate_; }
    bool Resampling() const { return !ratio_.Unity(); }
    // Delay of the output against the host's time line, in host samples.
    int LatencySamples() const { return latency_; }
    const SincKernel& Kernel() const { return kernel_; }
    const RateRatio& Ratio() const { return ratio_; }

    // Queues an event `offset` host samples after the start of the next
    // Process() call; negative offsets count as 0. Events of one queue (the
    // notes; each control; the model; the bend) keep the order in which they
    // were added: one that names an earlier time than its predecessor acts
    // together with it, not before. A control, model or bend that meets a
    // waiting point of its queue in the same quantum replaces its value.
    //
    // False if the event was not taken: a note with kNoteCapacity notes
    // waiting (counted in DroppedNotes()), or a control index of
    // kControlKinds or more. A control point that finds its queue full is
    // taken (true): it replaces the newest waiting point of that queue, which
    // is counted in DroppedControlPoints().
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
    static constexpr int kModelQueue = kControlKinds;
    static constexpr int kBendQueue = kControlKinds + 1;
    static constexpr int kFilterQueue = kControlKinds + 2;
    static constexpr int kControlQueues = kControlKinds + 3;

    void RenderQuantum();
    void Compact(std::int64_t first_needed);

    QuantumEngine& engine_;
    double host_rate_ = kInternalRate;
    RateRatio ratio_;
    SincKernel kernel_;
    OutputStage output_;
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

} // namespace torus_classic
