#include "stream_adapter.h"

#include <algorithm>
#include <cstring>
#include <xmmintrin.h>

namespace torus_classic {

namespace {

// Decaying resonators end in denormal numbers, which x86 computes very
// slowly. While the adapter renders, they count as zero (FTZ and DAZ).
class DenormalsAreZero {
  public:
    DenormalsAreZero() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | 0x8040u); }
    ~DenormalsAreZero() { _mm_setcsr(saved_); }
    DenormalsAreZero(const DenormalsAreZero&) = delete;
    DenormalsAreZero& operator=(const DenormalsAreZero&) = delete;

  private:
    unsigned int saved_;
};

} // namespace

bool StreamAdapter::Prepare(double host_rate) {
    RateRatio ratio;
    if(!MakeRateRatio(host_rate, ratio))
        return false;
    host_rate_ = host_rate;
    ratio_ = ratio;
    if(ratio_.Unity()) {
        left_ = right_ = 0;
        latency_ = 0;
    } else {
        kernel_.Design(ratio_);
        left_ = kernel_.Left();
        right_ = kernel_.Right();
        // The smallest delay for which no output sample needs a quantum that
        // an event of a later block could still belong to:
        // (latency + 1) * num / den >= Right() + 1.
        const std::uint64_t needed = static_cast<std::uint64_t>(right_ + 1) * ratio_.den;
        latency_ = static_cast<int>((needed + ratio_.num - 1) / ratio_.num) - 1;
        if(latency_ < 0)
            latency_ = 0;
    }
    // Room for the filter's window, one quantum and the samples one output
    // step can leave behind; twice that, so compacting is rare.
    const std::uint64_t step = ratio_.num / ratio_.den + 1;
    const std::size_t window = static_cast<std::size_t>(left_ + right_ + 1);
    const std::size_t capacity = 2 * (window + kQuantum + static_cast<std::size_t>(step)) + 64;
    buffer_left_.assign(capacity, 0.0f);
    buffer_right_.assign(capacity, 0.0f);
    notes_.Allocate(kNoteCapacity);
    for(Queue& queue : controls_)
        queue.Allocate(kControlCapacity);
    Reset();
    return true;
}

void StreamAdapter::Reset() {
    host_pos_ = 0;
    time_int_ = 0;
    time_rem_ = 0;
    // Output 0 lies `latency_` host samples before internal time 0.
    const std::uint64_t back = static_cast<std::uint64_t>(latency_) * ratio_.num;
    const std::uint64_t whole = back / ratio_.den, part = back % ratio_.den;
    pos_int_ = -static_cast<std::int64_t>(whole) - (part ? 1 : 0);
    pos_rem_ = part ? ratio_.den - part : 0;
    rendered_ = 0;
    // The filter reads silence before internal time 0.
    base_ = pos_int_ - left_;
    count_ = base_ < 0 ? static_cast<int>(-base_) : 0;
    if(base_ > 0)
        base_ = 0;
    std::fill(buffer_left_.begin(), buffer_left_.end(), 0.0f);
    std::fill(buffer_right_.begin(), buffer_right_.end(), 0.0f);
    notes_.Clear();
    for(Queue& queue : controls_)
        queue.Clear();
    dropped_notes_ = 0;
    dropped_control_points_ = 0;
}

int StreamAdapter::PendingEvents() const {
    int count = notes_.count;
    for(const Queue& queue : controls_)
        count += queue.count;
    return count;
}

std::size_t StreamAdapter::AllocatedBytes() const {
    std::size_t waiting = notes_.items.capacity();
    for(const Queue& queue : controls_)
        waiting += queue.items.capacity();
    return (buffer_left_.capacity() + buffer_right_.capacity()) * sizeof(float)
           + waiting * sizeof(Pending) + kernel_.TableBytes();
}

bool StreamAdapter::AddEvent(std::int32_t offset, const EngineEvent& event) {
    if(notes_.items.empty())
        return false; // not prepared
    Queue* queue = &notes_;
    switch(event.type) {
        case EngineEvent::kNoteOn: break;
        case EngineEvent::kControl:
            if(event.index >= kControlKinds)
                return false;
            queue = &controls_[event.index];
            break;
        case EngineEvent::kModel: queue = &controls_[kModelQueue]; break;
        case EngineEvent::kPitchBend: queue = &controls_[kBendQueue]; break;
    }
    if(offset < 0)
        offset = 0;
    // Internal time of the event, then the first quantum starting at or after it.
    const std::uint64_t rem = time_rem_ + static_cast<std::uint64_t>(offset) * ratio_.num;
    const std::int64_t whole = time_int_ + static_cast<std::int64_t>(rem / ratio_.den);
    const std::int64_t first_sample = whole + (rem % ratio_.den ? 1 : 0);
    std::int64_t quantum = (first_sample + kQuantum - 1) / kQuantum;
    // Cannot be in the past for a valid offset; a bound in any case.
    quantum = std::max(quantum, rendered_ / kQuantum);
    // Arrival order is kept within a queue: never before the one added last.
    if(queue->count > 0)
        quantum = std::max(quantum, queue->Last().quantum);

    if(event.type == EngineEvent::kNoteOn) {
        if(queue->Full()) {
            ++dropped_notes_;
            return false;
        }
        queue->Push({quantum, event});
        return true;
    }
    if(queue->count > 0 && queue->Last().quantum == quantum) {
        queue->Last().event = event; // the later value of one quantum wins
        return true;
    }
    if(queue->Full()) {
        // The newest waiting point gives way to this one, which keeps its own,
        // later time: a step of the automation is lost, nothing acts early.
        ++dropped_control_points_;
        queue->Last() = {quantum, event};
        return true;
    }
    queue->Push({quantum, event});
    return true;
}

void StreamAdapter::Compact(std::int64_t first_needed) {
    const std::int64_t drop = std::min<std::int64_t>(first_needed - base_, count_);
    if(drop <= 0)
        return;
    const int keep = count_ - static_cast<int>(drop);
    std::memmove(buffer_left_.data(), buffer_left_.data() + drop, keep * sizeof(float));
    std::memmove(buffer_right_.data(), buffer_right_.data() + drop, keep * sizeof(float));
    base_ += drop;
    count_ = keep;
}

void StreamAdapter::RenderQuantum() {
    const std::int64_t quantum = rendered_ / kQuantum;
    // What belongs to this quantum: the controls first, then the notes in the
    // order they came. (For the engine the two commute: it reads both when
    // it renders.) Each loop ends with its queue, which is bounded.
    for(Queue& queue : controls_)
        while(queue.count > 0 && queue.First().quantum <= quantum) {
            engine_.HandleEvent(queue.First().event);
            queue.Pop();
        }
    while(notes_.count > 0 && notes_.First().quantum <= quantum) {
        engine_.HandleEvent(notes_.First().event);
        notes_.Pop();
    }
    if(count_ + kQuantum > static_cast<int>(buffer_left_.size()))
        Compact(pos_int_ - left_);
    engine_.Render(buffer_left_.data() + count_, buffer_right_.data() + count_);
    count_ += kQuantum;
    rendered_ += kQuantum;
}

void StreamAdapter::Process(float* left, float* right, int frames) {
    if(frames <= 0 || buffer_left_.empty())
        return;
    const DenormalsAreZero denormals_are_zero;
    const bool unity = ratio_.Unity();
    for(int frame = 0; frame < frames; ++frame) {
        // The engine runs just far enough for this output sample.
        while(rendered_ < pos_int_ + right_ + 1)
            RenderQuantum();
        const std::int64_t first = pos_int_ - left_ - base_; // index into the buffer
        if(unity) {
            left[frame] = buffer_left_[static_cast<std::size_t>(first)];
            right[frame] = buffer_right_[static_cast<std::size_t>(first)];
        } else {
            kernel_.Interpolate(buffer_left_.data() + first, buffer_right_.data() + first,
                                pos_rem_, left[frame], right[frame]);
        }
        // One host sample on, in both clocks.
        pos_rem_ += ratio_.num;
        pos_int_ += static_cast<std::int64_t>(pos_rem_ / ratio_.den);
        pos_rem_ %= ratio_.den;
        time_rem_ += ratio_.num;
        time_int_ += static_cast<std::int64_t>(time_rem_ / ratio_.den);
        time_rem_ %= ratio_.den;
        ++host_pos_;
    }
}

} // namespace torus_classic
