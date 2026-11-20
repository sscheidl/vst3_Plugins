#include "looper_engine.h"

#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace messier {

static_assert(LooperEngine::kQuantum <= static_cast<int>(neb::kBlock));
static_assert(LooperEngine::kSampleRate == neb::kRate);
static_assert(neb::FFT_N == 2048, "fft_desktop.cpp is written for 2048 points");
static_assert(EngineDisplay::kMaxGrains == 2 * neb::MAX_GRAINS);

struct LooperEngine::Impl {
    // The firmware engine lives in this storage and is constructed anew by Reset():
    // placement new does not allocate.
    alignas(neb::Engine) unsigned char storage[sizeof(neb::Engine)];
    neb::Engine* engine = nullptr;
    neb::FileBridge bridge;
    // 0.2.5 (MS-R55): a number for each grain start, for the display only. A slot whose
    // grain starts to sound, or whose window began again, gets the next number.
    std::uint32_t grain_id[2][neb::MAX_GRAINS]{};
    float grain_envelope[2][neb::MAX_GRAINS]{};
    bool grain_sounding[2][neb::MAX_GRAINS]{};
    std::uint32_t next_grain_id = 0;

    void TrackGrains() {
        for(unsigned c = 0; c < 2; ++c) {
            const neb::Grain* grains = engine->DisplayGrains(c);
            for(int i = 0; i < neb::MAX_GRAINS; ++i) {
                const neb::Grain& g = grains[i];
                const bool sounding = g.active && g.delay == 0;
                if(sounding && (!grain_sounding[c][i] || g.envelope < grain_envelope[c][i])) {
                    if(++next_grain_id == 0)
                        next_grain_id = 1; // 0 means none
                    grain_id[c][i] = next_grain_id;
                }
                grain_sounding[c][i] = sounding;
                grain_envelope[c][i] = g.envelope;
            }
        }
    }

    void Destroy() {
        if(engine)
            engine->~Engine();
        engine = nullptr;
    }
    ~Impl() { Destroy(); }
};

LooperEngine::LooperEngine() : impl_(new Impl) {
    Reset(nullptr, EngineSettings{});
}

LooperEngine::~LooperEngine() = default;

std::size_t LooperEngine::StateBytes() {
    return sizeof(neb::Engine);
}

void LooperEngine::Release(const FileData* file) {
    if(!file)
        return;
    for(int i = 0; i < released_count_; ++i)
        if(released_[i] == file)
            return;
    // Reset releases at most two files, a swap one; TakeReleased empties the list.
    if(released_count_ < 4)
        released_[released_count_++] = file;
}

const FileData* LooperEngine::TakeReleased() {
    if(released_count_ == 0)
        return nullptr;
    return released_[--released_count_];
}

static std::int16_t* Buffer(const FileData* file) {
    // The core reads the file and never writes it (it writes only its live banks,
    // which Messier 0.1.0 does not give it); its interface takes a plain pointer.
    return file && file->frames ? const_cast<std::int16_t*>(file->pcm.get()) : nullptr;
}

static void Publish(neb::FileBridge& bridge, const FileData* file) {
    neb::WavInfo info;
    if(file && file->frames) {
        info.frames = file->frames;
        info.rate = file->rate;
        info.channels = file->channels;
        info.bytes = file->frames * file->channels * 2u;
    }
    bridge.published = info;
    bridge.generation.store(bridge.generation.load(std::memory_order_relaxed) + 1,
                            std::memory_order_release);
}

void LooperEngine::Reset(const FileData* file, const EngineSettings& settings) {
    Impl& m = *impl_;
    m.Destroy();
    // A fresh bridge: no mute pending, no acknowledgement, generation 0.
    m.bridge.gate.store(false);
    m.bridge.ack.store(false);
    m.bridge.generation.store(0);
    m.bridge.published = {};
    m.engine = new(m.storage) neb::Engine();
    for(unsigned c = 0; c < 2; ++c)
        for(int i = 0; i < neb::MAX_GRAINS; ++i) {
            m.grain_id[c][i] = 0;
            m.grain_envelope[c][i] = 0;
            m.grain_sounding[c][i] = false;
        }
    neb::Engine& e = *m.engine;
    // The pots before Init(), which starts the smoothing from them.
    for(unsigned i = 0; i < kPrimaryCount; ++i) {
        e.ui.params.CC(i, settings.control[i]);
        e.ui.params.secondary[i] = neb::Unit(settings.control[kPrimaryCount + i]);
    }
    e.Init(nullptr, nullptr, 0, Buffer(file), m.bridge);
    e.SetSchedule(neb::kScheduleCommon);
    // The file is the first generation the engine sees: it starts from START with it.
    Publish(m.bridge, file);
    if(settings.freeze)
        e.Act(neb::Action::Freeze);
    SetPitchBend(settings.bend);

    if(current_ != file)
        Release(current_);
    if(swapping_ && next_ != file)
        Release(next_);
    current_ = file;
    next_ = nullptr;
    swapping_ = false;
    volume_ = volume_target_ = neb::Unit(settings.control[static_cast<unsigned>(Control::Volume)]);
}

void LooperEngine::SetControl(Control control, float value) {
    neb::Engine& e = *impl_->engine;
    const auto index = static_cast<unsigned>(control);
    if(index < kPrimaryCount)
        e.ui.params.CC(index, value);
    else if(index < 2 * kPrimaryCount)
        e.ui.params.secondary[index - kPrimaryCount] = neb::Unit(value);
    else if(control == Control::Volume)
        volume_target_ = neb::Unit(value);
}

void LooperEngine::SetFreeze(bool on) {
    neb::Engine& e = *impl_->engine;
    if(e.frozen != on)
        e.Act(neb::Action::Freeze);
}

void LooperEngine::TriggerReset() {
    impl_->engine->Act(neb::Action::Reset);
}

void LooperEngine::NoteOn(unsigned channel, unsigned note, unsigned velocity) {
    if(channel > 15 || note > 127 || velocity == 0 || velocity > 127)
        return;
    impl_->engine->MidiEvent({0x90, static_cast<std::uint8_t>(channel),
                              static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity)});
}

void LooperEngine::NoteOff(unsigned channel, unsigned note) {
    if(channel > 15 || note > 127)
        return;
    impl_->engine->MidiEvent(
        {0x80, static_cast<std::uint8_t>(channel), static_cast<std::uint8_t>(note), 0});
}

void LooperEngine::AllNotesOff() {
    for(std::uint8_t channel = 0; channel < 16; ++channel)
        impl_->engine->MidiEvent({0xb0, channel, 123, 0}); // the firmware's All Notes Off
}

void LooperEngine::SetPitchBend(unsigned raw14) {
    if(raw14 > 16383)
        raw14 = 16383;
    // The firmware follows the bend of the channel of the newest note; the same value
    // on every channel makes it one wheel.
    const auto low = static_cast<std::uint8_t>(raw14 & 0x7f);
    const auto high = static_cast<std::uint8_t>(raw14 >> 7);
    for(std::uint8_t channel = 0; channel < 16; ++channel)
        impl_->engine->MidiEvent({0xe0, channel, low, high});
}

void LooperEngine::SwapFile(const FileData* file) {
    if(swapping_) {
        if(next_ != file && next_ != current_)
            Release(next_);
        next_ = file;
        return;
    }
    if(file == current_)
        return;
    next_ = file;
    swapping_ = true;
    // Mute: the core fades the file out and acknowledges once it reads nothing.
    impl_->bridge.gate.store(true, std::memory_order_release);
}

void LooperEngine::FinishSwapSilently() {
    float silence[kQuantum] = {};
    float discard_left[kQuantum], discard_right[kQuantum];
    // The core fades over 256 samples (4 quanta) and acknowledges; the swap follows at
    // the start of the next quantum. The bound only guards against a core that never
    // acknowledges.
    for(int quantum = 0; swapping_ && quantum < 16; ++quantum)
        Render(silence, silence, discard_left, discard_right);
}

void LooperEngine::Render(const float* in_left, const float* in_right, float* left, float* right) {
    Impl& m = *impl_;
    if(swapping_ && m.bridge.ack.load(std::memory_order_acquire)) {
        // The core reads no file now: swap, publish, unmute. The engine takes the new
        // generation at the start of this quantum and fades in from START.
        m.engine->SetFileBuffer(Buffer(next_));
        Publish(m.bridge, next_);
        m.bridge.gate.store(false, std::memory_order_release);
        if(current_ != next_)
            Release(current_);
        current_ = next_;
        next_ = nullptr;
        swapping_ = false;
    }
    m.engine->Process(in_left, in_right, left, right, kQuantum);
    ++rendered_;
    m.TrackGrains(); // reads only (the sound is the core's alone)
    // Volume behind the core. At a steady value the gain is that value exactly, so
    // at 100 % the core's samples pass unchanged.
    for(int i = 0; i < kQuantum; ++i) {
        if(volume_ != volume_target_) {
            const float step = volume_target_ - volume_;
            volume_ = step > kVolumeStep ? volume_ + kVolumeStep
                                         : (step < -kVolumeStep ? volume_ - kVolumeStep : volume_target_);
        }
        if(volume_ != 1.0f) {
            left[i] *= volume_;
            right[i] *= volume_;
        }
    }
}

void LooperEngine::Display(EngineDisplay& display) const {
    const neb::Engine& e = *impl_->engine;
    display = EngineDisplay{};
    display.frozen = e.frozen ? 1 : 0;
    display.swapping = swapping_ ? 1 : 0;
    display.rendered = rendered_;
    // The file the core reads: the current one, also while it fades out for a swap
    // (current_ changes when the core acknowledges the mute).
    if(!current_ || current_->frames == 0)
        return;
    const double frames = current_->frames;
    display.sequence = current_->sequence;
    display.frames = current_->frames;
    // START and SIZE as Engine::Process applies them to the block just rendered
    // (core/engine.cpp, the two lines before the playhead loop), from the same
    // smoothed values.
    const double start = std::min(double(e.DisplaySmoothed(neb::Start)) * frames, frames - 1);
    const float size = e.DisplaySmoothed(neb::Size);
    const double length = std::max(1.0, double(size * size) * (frames - start));
    display.loop_start = start;
    display.loop_length = length;
    // The playhead the next sample starts from, inside the loop as the core wraps it.
    double position = e.Position();
    if(position < 0 || position >= length) {
        position = std::fmod(position, length);
        if(position < 0)
            position += length;
    }
    display.playhead = start + position;
    // The playhead moves EffectiveSpeed() x the file's rate / the core's rate frames a
    // sample (core/engine.cpp, `advance`), the core renders kSampleRate samples a second.
    display.head_speed = e.frozen ? 0.0f : static_cast<float>(double(e.EffectiveSpeed()) * current_->rate);
    const float spray = e.DisplaySecondary(neb::Start);
    display.spray = static_cast<float>(std::min(frames, double(spray) * spray * frames));
    unsigned count = 0;
    for(unsigned c = 0; c < 2; ++c) {
        const neb::Grain* grains = e.DisplayGrains(c);
        for(int i = 0; i < neb::MAX_GRAINS; ++i) {
            const neb::Grain& g = grains[i];
            if(!g.active || g.delay != 0) // waiting grains do not sound yet
                continue;
            GrainDot& dot = display.grains[count++];
            dot.position = static_cast<float>(g.position);
            dot.pan = g.pan;
            dot.phase = std::min(1.0f, std::max(0.0f, g.envelope));
            // A grain moves `step` frames and its window `increment` a sample.
            dot.speed = static_cast<float>(double(g.step) * kSampleRate);
            dot.phase_rate = static_cast<float>(double(g.increment) * kSampleRate);
            dot.id = impl_->grain_id[c][i];
        }
    }
    display.grain_count = count;
}

double LooperEngine::Position() const {
    return impl_->engine->Position();
}

float LooperEngine::EffectivePitch() const {
    return impl_->engine->EffectivePitch();
}

float LooperEngine::EffectiveSpeed() const {
    return impl_->engine->EffectiveSpeed();
}

bool LooperEngine::Frozen() const {
    return impl_->engine->frozen;
}

std::uint32_t LooperEngine::Resets() const {
    return impl_->engine->resets;
}

unsigned LooperEngine::Schedule() const {
    return impl_->engine->ScheduleMode();
}

} // namespace messier
