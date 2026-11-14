// Layer 3 of Torus Classic: runs an engine that renders fixed 12-sample
// quanta at 48 kHz inside a host that asks for blocks of any length at any
// sample rate, and puts events on the engine's quantum grid by absolute time.
// Free of Torus and VST3 types.
//
// Component id "torus-classic.adapter", version kAdapterVersion.
//
// Time. Host sample n (counted from the last Reset) stands for the internal
// time n * 48000 / host_rate. The output is delayed against that by
// LatencySamples() host samples, the group delay of the interpolation filter;
// at 48 kHz there is no filter and no delay. An event at host sample n acts
// on the first quantum boundary that is not before its internal time: less
// than 12 internal samples late, never early. Positions are kept as exact
// fractions, so the result depends on the absolute positions only, not on how
// the host cuts its blocks.
#pragma once

#include "sinc_kernel.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace torus_classic {

constexpr const char* kAdapterVersion = "0.1.0";

struct EngineEvent {
    enum Type : std::uint8_t { kNoteOn, kControl, kModel, kPitchBend };
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
    static constexpr int kEventCapacity = 1024; // events waiting for their quantum

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
    // Process() call; negative offsets count as 0. A control or bend that
    // meets a waiting one of the same kind in the same quantum replaces its
    // value. If kEventCapacity events already wait: a note is dropped and
    // false returned; a control, model or bend replaces the newest waiting
    // one of its kind or, failing that, goes to the engine at once (true).
    bool AddEvent(std::int32_t offset, const EngineEvent& event);

    // The next `frames` host samples. Does not allocate or lock.
    void Process(float* left, float* right, int frames);

    // For tests and diagnostics.
    std::uint64_t HostPosition() const { return host_pos_; }
    std::int64_t RenderedSamples() const { return rendered_; }
    int PendingEvents() const { return pending_count_; }
    std::uint64_t DroppedNotes() const { return dropped_notes_; }
    std::uint64_t DisplacedControls() const { return displaced_controls_; }
    std::size_t AllocatedBytes() const;

  private:
    struct Pending {
        std::int64_t quantum = 0;
        EngineEvent event;
    };
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

    // Waiting events, ordered by quantum, then by arrival.
    std::vector<Pending> pending_;
    int pending_count_ = 0;
    std::uint64_t dropped_notes_ = 0, displaced_controls_ = 0;
};

} // namespace torus_classic
