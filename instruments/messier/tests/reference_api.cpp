#include "reference_api.h"

#include "engine.h" // tests/reference/engine.h, namespace neb renamed

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

namespace messier_reference {

struct FirmwareLooper::Impl {
    alignas(neb::Engine) unsigned char storage[sizeof(neb::Engine)];
    neb::Engine* engine = nullptr;
    neb::FileBridge bridge;
    std::vector<std::int16_t> buffer; // the firmware's one file buffer
    const FileImage* waiting = nullptr;
    bool changing = false;

    void Write(const FileImage* file) {
        neb::WavInfo info;
        if(file && file->frames) {
            std::copy(file->pcm.begin(), file->pcm.end(), buffer.begin());
            info.frames = file->frames;
            info.rate = file->rate;
            info.channels = file->channels;
        }
        bridge.published = info;
        bridge.generation.store(bridge.generation.load() + 1, std::memory_order_release);
    }
};

FirmwareLooper::FirmwareLooper(std::size_t capacity) : impl_(new Impl) {
    impl_->buffer.assign(std::max<std::size_t>(capacity, 1), 0);
    const float primary[8] = {0, .5f, 1, .4f, .5f, .5f, 0, 0};
    const float secondary[8] = {};
    Reset(nullptr, primary, secondary);
}

FirmwareLooper::~FirmwareLooper() {
    if(impl_->engine)
        impl_->engine->~Engine();
    delete impl_;
}

void FirmwareLooper::Reset(const FileImage* file, const float* primary, const float* secondary) {
    Impl& m = *impl_;
    if(m.engine)
        m.engine->~Engine();
    m.bridge.gate.store(false);
    m.bridge.ack.store(false);
    m.bridge.generation.store(0);
    m.engine = new(m.storage) neb::Engine();
    for(unsigned i = 0; i < 8; ++i) {
        m.engine->ui.params.CC(i, primary[i]);
        m.engine->ui.params.secondary[i] = secondary[i];
    }
    m.engine->Init(nullptr, nullptr, 0, m.buffer.data(), m.bridge);
    m.engine->SetSchedule(neb::kScheduleCommon);
    m.Write(file);
    m.waiting = nullptr;
    m.changing = false;
}

void FirmwareLooper::SetPrimary(unsigned index, float value) {
    impl_->engine->ui.params.CC(index, value);
}

void FirmwareLooper::SetSecondary(unsigned index, float value) {
    impl_->engine->ui.params.secondary[index] = std::clamp(value, 0.0f, 1.0f);
}

void FirmwareLooper::Freeze(bool on) {
    if(impl_->engine->frozen != on)
        impl_->engine->Act(neb::Action::Freeze);
}

void FirmwareLooper::ResetPlayhead() {
    impl_->engine->Act(neb::Action::Reset);
}

void FirmwareLooper::Midi(std::uint8_t type, std::uint8_t channel, std::uint8_t a, std::uint8_t b) {
    impl_->engine->MidiEvent({type, channel, a, b});
}

void FirmwareLooper::ChangeFile(const FileImage* file) {
    impl_->waiting = file;
    if(!impl_->changing) {
        impl_->changing = true;
        impl_->bridge.gate.store(true, std::memory_order_release);
    }
}

bool FirmwareLooper::Changing() const {
    return impl_->changing;
}

void FirmwareLooper::Render(float* left, float* right, unsigned n) {
    Impl& m = *impl_;
    // The loader's step: once the audio side has let go, overwrite and publish.
    if(m.changing && m.bridge.ack.load(std::memory_order_acquire)) {
        m.Write(m.waiting);
        m.bridge.gate.store(false, std::memory_order_release);
        m.changing = false;
        m.waiting = nullptr;
    }
    static const float silence[neb::kBlock] = {};
    m.engine->Process(silence, silence, left, right, n);
}

double FirmwareLooper::Position() const {
    return impl_->engine->Position();
}

unsigned FirmwareLooper::ScheduleOfReset() {
    auto engine = std::make_unique<neb::Engine>();
    return engine->ScheduleMode();
}

std::size_t FirmwareLooper::StateBytes() {
    return sizeof(neb::Engine);
}

} // namespace messier_reference
