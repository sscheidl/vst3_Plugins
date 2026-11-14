// Checks the plug-in against the unchanged firmware engine. The plug-in's
// processor is driven in-process through the VST3 SDK's hosting classes, the
// way a host drives it.
#include "controller.h"
#include "editor.h"
#include "engine.h"
#include "keyboard_layout.h"
#include "parameters.h"
#include "processor.h"
#include "toy_piano.h"
#include "value_text.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/gui/iplugview.h"
#include "vstgui/lib/cframe.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace taureon::toy_piano_classic;

// In a plug-in the SDK's DLL entry point defines this handle and calls
// InitModule(). The test links the plug-in's classes into an executable, so it
// has to provide both itself (see TestEditorInWindow).
void* moduleHandle = nullptr;
bool InitModule();
bool DeinitModule();

#define CHECK(condition) \
    do { \
        if(!(condition)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            std::exit(1); \
        } \
    } while(false)

namespace {

struct Note {
    int sample;
    std::uint8_t note;
    std::uint8_t velocity;
};

// A fixed performance: 90 notes in 3 s (at 48 kHz), some of them together,
// some re-struck, and with a long decay more than 16 sounding at once.
std::vector<Note> Score(int total_samples) {
    std::vector<Note> score;
    std::uint32_t state = 12345;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    int sample = 0;
    for(int i = 0; i < 90; ++i) {
        if(i % 7 != 3) // every seventh note shares its sample with the one before
            sample += static_cast<int>(next() % (total_samples / 60));
        const std::uint8_t note = i % 11 == 5 ? 60 : static_cast<std::uint8_t>(30 + next() % 70);
        score.push_back({std::min(sample, total_samples - 1), note,
                         static_cast<std::uint8_t>(1 + next() % 127)});
    }
    return score;
}

// The firmware engine on its own: the reference.
std::vector<float> RenderCore(float sample_rate, const std::vector<Note>& score, int total_samples,
                              float attack, float decay) {
    toy_piano::ToyPiano piano;
    piano.Init(sample_rate);
    std::vector<float> out(total_samples);
    std::size_t next = 0;
    for(int i = 0; i < total_samples; ++i) {
        for(; next < score.size() && score[next].sample == i; ++next)
            piano.NoteOn(score[next].note, score[next].velocity, attack, decay);
        out[i] = piano.Process();
    }
    return out;
}

struct PluginRun {
    double sample_rate = 48000.0;
    int block_size = 64;
    // Sent as parameter changes in the first block.
    int master_pitch = 0; // semitones
    double attack = kDefaultAttack;
    double decay = kDefaultDecay;
    double volume = kDefaultVolume;
    double pitch_bend = kDefaultPitchBend;
    bool double_precision = false;
};

std::vector<float> RenderPlugin(const PluginRun& run, const std::vector<Note>& score,
                                int total_samples) {
    IPtr<Processor> processor = owned(new Processor());
    CHECK(processor->initialize(nullptr) == kResultOk);
    const int32 sample_size = run.double_precision ? kSample64 : kSample32;
    CHECK(processor->canProcessSampleSize(sample_size) == kResultTrue);
    ProcessSetup setup{kRealtime, sample_size, run.block_size, run.sample_rate};
    CHECK(processor->setupProcessing(setup) == kResultOk);
    CHECK(processor->setActive(true) == kResultOk);
    processor->setProcessing(true);

    HostProcessData data;
    CHECK(data.prepare(*processor.get(), run.block_size, sample_size));
    CHECK(data.numOutputs == 1 && data.outputs[0].numChannels == 2);
    EventList events(64);
    ParameterChanges changes(5);
    data.processMode = kRealtime;
    data.inputEvents = &events;
    data.inputParameterChanges = &changes;

    std::vector<float> out(total_samples);
    std::size_t next = 0;
    for(int start = 0; start < total_samples; start += run.block_size) {
        const int count = std::min(run.block_size, total_samples - start);
        events.clear();
        changes.clearQueue();
        if(start == 0) {
            const std::pair<ParamID, double> values[] = {
                {kParamMasterPitch, MasterPitchNormalized(run.master_pitch)},
                {kParamAttack, run.attack},
                {kParamDecay, run.decay},
                {kParamVolume, run.volume},
                {kParamPitchBend, run.pitch_bend}};
            for(const auto& [id, value] : values) {
                int32 index = 0;
                IParamValueQueue* queue = changes.addParameterData(id, index);
                CHECK(queue && queue->addPoint(0, value, index) == kResultOk);
            }
        }
        for(; next < score.size() && score[next].sample < start + count; ++next) {
            Event event{};
            event.type = Event::kNoteOnEvent;
            event.sampleOffset = score[next].sample - start;
            event.noteOn.pitch = score[next].note;
            event.noteOn.velocity = score[next].velocity / 127.0f;
            CHECK(events.addEvent(event) == kResultOk);
        }
        data.numSamples = count;
        data.outputs[0].silenceFlags = 0;
        CHECK(processor->process(data) == kResultOk);
        bool silent = true;
        for(int i = 0; i < count; ++i) {
            const double left = run.double_precision ? data.outputs[0].channelBuffers64[0][i]
                                                     : data.outputs[0].channelBuffers32[0][i];
            const double right = run.double_precision ? data.outputs[0].channelBuffers64[1][i]
                                                      : data.outputs[0].channelBuffers32[1][i];
            CHECK(left == right); // dual mono
            CHECK(std::isfinite(left) && std::fabs(left) <= toy_piano::kOutputCeiling);
            out[start + i] = static_cast<float>(left);
            silent = silent && left == 0.0;
        }
        CHECK((data.outputs[0].silenceFlags == 3) == silent); // the flags tell the truth
    }
    processor->setProcessing(false);
    CHECK(processor->setActive(false) == kResultOk);
    CHECK(processor->terminate() == kResultOk);
    return out;
}

bool Identical(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

float Peak(const std::vector<float>& x, std::size_t from = 0) {
    float peak = 0.0f;
    for(std::size_t i = from; i < x.size(); ++i)
        peak = std::max(peak, std::fabs(x[i]));
    return peak;
}

// Pitch from rising zero crossings, with the crossing time interpolated.
double Frequency(const std::vector<float>& x, double sample_rate, std::size_t from,
                 std::size_t to) {
    double first = -1.0, last = -1.0;
    int periods = -1;
    for(std::size_t i = from + 1; i < to; ++i) {
        if(x[i - 1] < 0.0f && x[i] >= 0.0f) {
            last = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if(first < 0.0)
                first = last;
            ++periods;
        }
    }
    return periods > 0 ? periods * sample_rate / (last - first) : 0.0;
}

// With master pitch 0 the plug-in must deliver the firmware engine's output
// bit for bit, whatever block size the host uses.
void TestBitExactAtPitchZero() {
    const int total = 3 * 48000;
    const auto score = Score(total);
    PluginRun run;
    run.attack = 0.15;
    run.decay = 0.9;
    const auto reference =
        RenderCore(48000.0f, score, total, AttackSeconds(run.attack), DecaySeconds(run.decay));
    CHECK(Peak(reference) > 0.1f); // many voices at once, not a lone note
    // 64-bit processing carries the same float samples.
    for(const bool double_precision : {false, true})
        for(const int block : {1, 16, 48, 64, 333, 1024, 2048}) {
            run.block_size = block;
            run.double_precision = double_precision;
            CHECK(Identical(RenderPlugin(run, score, total), reference));
        }
    std::printf("bit-exact against the firmware engine: 7 block sizes, each 32 and 64 bit\n");
}

// Other sample rates: same engine, initialised with the host's rate.
void TestSampleRates() {
    for(const double rate : {44100.0, 88200.0, 96000.0, 192000.0}) {
        const int total = static_cast<int>(rate);
        const std::vector<Note> score = {{0, 69, 127}};
        PluginRun run;
        run.sample_rate = rate;
        run.decay = 1.0;
        const auto out = RenderPlugin(run, score, total);
        CHECK(Identical(out, RenderCore(static_cast<float>(rate), score, total,
                                        AttackSeconds(run.attack), DecaySeconds(run.decay))));
        const double frequency = Frequency(out, rate, total / 100, total / 2);
        CHECK(std::fabs(frequency - 440.0) < 0.05);
    }
    std::printf("A4 = 440 Hz at 44.1, 88.2, 96 and 192 kHz\n");
}

// Master pitch transposes in semitones and does not depend on the block size.
void TestMasterPitch() {
    const int total = 48000;
    const std::vector<Note> score = {{0, 69, 127}};
    PluginRun run;
    run.decay = 1.0;
    for(const int semitones : {-24, -12, -1, 0, 1, 7, 12, 24}) {
        run.master_pitch = semitones;
        run.block_size = 64;
        const auto out = RenderPlugin(run, score, total);
        const double expected = 440.0 * std::pow(2.0, semitones / 12.0);
        // Measured after the glide has settled.
        const double frequency = Frequency(out, 48000.0, 9600, 33600);
        CHECK(std::fabs(frequency / expected - 1.0) < 1e-4);
        run.block_size = 333;
        CHECK(Identical(RenderPlugin(run, score, total), out));
    }
    std::printf("master pitch -24..+24: frequencies within 0.01 %%\n");
}

// A change while a note sounds glides without a jump in the waveform.
void TestMasterPitchGlide() {
    Engine engine;
    engine.Init(48000.0f);
    engine.NoteOn(57, 127, toy_piano::kAttackMin, toy_piano::kDecayMax);
    float previous = 0.0f, largest_step = 0.0f;
    for(int i = 0; i < 4800; ++i) {
        const float sample = engine.Process();
        largest_step = std::max(largest_step, std::fabs(sample - previous));
        previous = sample;
    }
    engine.SetPitch(12.0f);
    for(int i = 0; i < 4800; ++i) { // the glide and some time after it
        const float sample = engine.Process();
        // An octave up doubles the slope at most; a phase jump would be far larger.
        CHECK(std::fabs(sample - previous) < 2.2f * largest_step);
        previous = sample;
    }
    CHECK(engine.PitchRatio() == 2.0f);
    engine.SetPitch(0.0f);
    for(int i = 0; i < 4800; ++i)
        engine.Process();
    CHECK(engine.PitchRatio() == 1.0f); // back to the exact firmware pitch
    for(std::size_t voice = toy_piano::kNumKeys; voice < toy_piano::kNumVoices; ++voice) {
        CHECK(engine.Piano().VoiceIncrement(voice) == engine.Piano().BaseIncrement(voice));
        CHECK(engine.Piano().VoiceLevel(voice) == 1.0f);
    }
    std::printf("master pitch glide: no waveform jump, returns to the exact pitch\n");
}

// Pitches the firmware never produces: above Nyquist the voice is muted
// instead of aliasing, and the engine's sine table is never overrun. (The
// Debug build checks array bounds, so an overrun would abort this test.)
void TestAboveNyquist() {
    for(const double rate : {8000.0, 22050.0, 44100.0, 48000.0}) {
        const int total = static_cast<int>(rate) / 2;
        std::vector<Note> score;
        for(int i = 0; i < 20; ++i)
            score.push_back({i * 10, static_cast<std::uint8_t>(108 + i), 127});
        PluginRun run;
        run.sample_rate = rate;
        run.master_pitch = 24;
        run.decay = 1.0;
        const auto out = RenderPlugin(run, score, total); // checks finite and bounded
        // Notes 108..127 raised two octaves all lie above 16 kHz, hence above
        // Nyquist at 8 and 22.05 kHz. The pitch glides up from 0 in the first
        // 50 ms or so; after that and the mute ramps there is silence.
        if(rate <= 22050.0)
            CHECK(Peak(out, static_cast<std::size_t>(0.2 * rate)) == 0.0f);
        else
            CHECK(Peak(out, static_cast<std::size_t>(0.2 * rate)) > 0.0f); // 16.7 kHz still fits
    }
    std::printf("above Nyquist: muted, bounded, no table overrun\n");
}

// Attack and decay follow the parameters; the times are those of the firmware pots.
void TestEnvelopeParameters() {
    CHECK(std::fabs(AttackSeconds(0.0) - 0.002f) < 1e-7f && std::fabs(AttackSeconds(1.0) - 0.3f) < 1e-6f);
    CHECK(std::fabs(DecaySeconds(0.0) - 0.05f) < 1e-7f && std::fabs(DecaySeconds(1.0) - 3.0f) < 1e-5f);
    CHECK(std::fabs(LogTimePosition(DecaySeconds(0.37), 0.05, 3.0) - 0.37) < 1e-5);
    CHECK(MasterPitchSemitones(0.0) == -24 && MasterPitchSemitones(0.5) == 0
          && MasterPitchSemitones(1.0) == 24);
    for(int semitones = -24; semitones <= 24; ++semitones)
        CHECK(MasterPitchSemitones(MasterPitchNormalized(semitones)) == semitones);

    const int total = 4 * 48000;
    const std::vector<Note> score = {{0, 60, 127}};
    for(const double decay : {0.0, 0.5, 1.0}) {
        PluginRun run;
        run.attack = 1.0;
        run.decay = decay;
        const auto out = RenderPlugin(run, score, total);
        const int attack_end = static_cast<int>(0.3 * 48000);
        const int decay_end = attack_end + static_cast<int>(DecaySeconds(decay) * 48000.0f + 0.5f);
        // The note peaks at the end of the attack and is over with the decay.
        CHECK(Peak(out, attack_end - 400) > 0.9f * Peak(out));
        CHECK(Peak(out, decay_end + 1) == 0.0f);
        CHECK(Peak(out, decay_end - decay_end / 10) > 0.0f);
    }
    std::printf("attack and decay parameters set the firmware's envelope times\n");
}

// The state survives saving and loading; damaged states are rejected.
void TestState() {
    IPtr<Processor> saved = owned(new Processor());
    CHECK(saved->initialize(nullptr) == kResultOk);
    {
        HostProcessData data; // a parameter flush without audio
        ParameterChanges changes(5);
        const std::pair<ParamID, double> values[] = {
            {kParamMasterPitch, MasterPitchNormalized(-7)}, {kParamAttack, 0.25},
            {kParamDecay, 0.75}, {kParamVolume, 0.9}, {kParamPitchBend, 1.0}};
        for(const auto& [id, value] : values) {
            int32 index = 0;
            changes.addParameterData(id, index)->addPoint(0, value, index);
        }
        data.inputParameterChanges = &changes;
        data.numSamples = 0;
        CHECK(saved->process(data) == kResultOk);
    }
    MemoryStream stream;
    CHECK(saved->getState(&stream) == kResultOk);
    CHECK(stream.getSize() == 4 + 4 * 8); // the pitch wheel is not part of the state

    IPtr<Processor> loaded = owned(new Processor());
    CHECK(loaded->initialize(nullptr) == kResultOk);
    int64 position = 0;
    stream.seek(0, IBStream::kIBSeekSet, &position);
    CHECK(loaded->setState(&stream) == kResultOk);
    MemoryStream copy;
    CHECK(loaded->getState(&copy) == kResultOk);
    CHECK(copy.getSize() == stream.getSize()
          && std::memcmp(copy.getData(), stream.getData(), static_cast<std::size_t>(copy.getSize())) == 0);

    MemoryStream empty;
    CHECK(loaded->setState(&empty) != kResultOk);
    MemoryStream wrong_version;
    int32 version = 99, written = 0;
    wrong_version.write(&version, sizeof(version), &written);
    wrong_version.seek(0, IBStream::kIBSeekSet, &position);
    CHECK(loaded->setState(&wrong_version) != kResultOk);
    MemoryStream after_rejects; // a rejected state changes nothing
    CHECK(loaded->getState(&after_rejects) == kResultOk);
    CHECK(std::memcmp(after_rejects.getData(), stream.getData(),
                      static_cast<std::size_t>(stream.getSize())) == 0);
    saved->terminate();
    loaded->terminate();
    std::printf("state: round trip identical, damaged states rejected\n");
}

// The volume acts in front of the limiter: it scales quiet signals, mutes at
// the bottom and cannot push the output past the firmware's ceiling.
void TestVolume() {
    const int total = 48000;
    const std::vector<Note> single = {{0, 60, 127}};
    PluginRun run;
    run.decay = 1.0;
    // Measured from 0.2 s on: the volume arrives in the first block and takes
    // some milliseconds to settle.
    const std::size_t settled = 9600;
    const float reference = Peak(RenderPlugin(run, single, total), settled);
    for(const double db : {-24.0, -12.0, 6.0, 12.0}) {
        run.volume = VolumeNormalized(db);
        const float peak = Peak(RenderPlugin(run, single, total), settled);
        // One note stays far below the limiter's knee, so the gain is almost exact.
        CHECK(std::fabs(peak / reference / std::pow(10.0, db / 20.0) - 1.0) < 0.03);
    }
    run.volume = 0.0;
    CHECK(Peak(RenderPlugin(run, single, total), settled) == 0.0f);

    const auto score = Score(3 * 48000);
    run.volume = 1.0; // +24 dB on a dense performance: deep in the limiter
    const float loud = Peak(RenderPlugin(run, score, 3 * 48000)); // checked against the ceiling
    run.volume = kDefaultVolume;
    CHECK(loud > Peak(RenderPlugin(run, score, 3 * 48000)));
    CHECK(VolumeGain(kDefaultVolume) == 1.0f && VolumeGain(0.0) == 0.0f);
    CHECK(std::fabs(VolumeDb(VolumeNormalized(-17.5)) + 17.5) < 1e-9);

    // A change while a note sounds is smoothed: no step larger than the wave's own.
    Engine engine;
    engine.Init(48000.0f);
    engine.NoteOn(57, 127, toy_piano::kAttackMin, toy_piano::kDecayMax);
    float previous = 0.0f, largest_step = 0.0f;
    for(int i = 0; i < 4800; ++i) {
        const float sample = engine.Process();
        largest_step = std::max(largest_step, std::fabs(sample - previous));
        previous = sample;
    }
    engine.SetVolume(0.0f);
    for(int i = 0; i < 9600; ++i) {
        const float sample = engine.Process();
        CHECK(std::fabs(sample - previous) < 1.1f * largest_step);
        previous = sample;
    }
    CHECK(engine.Volume() == 0.0f && previous == 0.0f);
    std::printf("volume: gain in dB, silence at the bottom, bounded at +24 dB, smoothed\n");
}

// The pitch wheel bends by up to two semitones and adds to the master pitch.
void TestPitchBend() {
    const int total = 48000;
    const std::vector<Note> score = {{0, 69, 127}};
    PluginRun run;
    run.decay = 1.0;
    const struct {
        int master_pitch;
        double bend;
        double semitones;
    } cases[] = {{0, 1.0, 2.0}, {0, 0.0, -2.0}, {0, 0.75, 1.0}, {12, 1.0, 14.0}, {-24, 0.0, -26.0}};
    for(const auto& c : cases) {
        run.master_pitch = c.master_pitch;
        run.pitch_bend = c.bend;
        const double frequency = Frequency(RenderPlugin(run, score, total), 48000.0, 9600, 33600);
        CHECK(std::fabs(frequency / (440.0 * std::pow(2.0, c.semitones / 12.0)) - 1.0) < 1e-4);
    }
    std::printf("pitch bend: +/-2 semitones on top of the master pitch\n");
}

// The controller: parameters as hosts see them, and the MIDI controller map.
void TestController() {
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(nullptr) == kResultOk);
    CHECK(controller->getParameterCount() == 5);

    const auto text = [&](ParamID id, double normalized) {
        String128 string{};
        CHECK(controller->getParamStringByValue(id, normalized, string) == kResultOk);
        return StringConvert::convert(string);
    };
    CHECK(text(kParamMasterPitch, MasterPitchNormalized(-7)) == "-7");
    CHECK(text(kParamMasterPitch, kDefaultMasterPitch) == "0");
    CHECK(text(kParamMasterPitch, 1.0) == "24");
    CHECK(text(kParamAttack, 0.0) == "2.0" && text(kParamAttack, 1.0) == "300");
    CHECK(text(kParamDecay, 0.0) == "50.0" && text(kParamDecay, 1.0) == "3000");
    CHECK(text(kParamVolume, kDefaultVolume) == "+0.0" && text(kParamVolume, 0.0) == "-inf");
    CHECK(text(kParamVolume, 1.0) == "+24.0");
    for(int semitones = -24; semitones <= 24; ++semitones) // display and engine agree
        CHECK(text(kParamMasterPitch, MasterPitchNormalized(semitones))
              == std::to_string(semitones));
    for(int cc = 0; cc < 128; ++cc) // also for every value a MIDI controller can send
        CHECK(text(kParamMasterPitch, cc / 127.0)
              == std::to_string(MasterPitchSemitones(cc / 127.0)));

    ParamValue normalized = 0.0;
    String128 typed{};
    StringConvert::convert(std::string("500"), typed);
    CHECK(controller->getParamValueByString(kParamDecay, typed, normalized) == kResultOk);
    CHECK(std::fabs(DecaySeconds(normalized) - 0.5f) < 1e-4f);
    StringConvert::convert(std::string("-6"), typed);
    CHECK(controller->getParamValueByString(kParamVolume, typed, normalized) == kResultOk);
    CHECK(std::fabs(VolumeDb(normalized) + 6.0) < 1e-9);

    IMidiMapping* mapping = nullptr;
    CHECK(controller->queryInterface(IMidiMapping::iid, reinterpret_cast<void**>(&mapping))
          == kResultOk);
    for(int16 channel = 0; channel < 16; ++channel) {
        ParamID id = 0;
        CHECK(mapping->getMidiControllerAssignment(0, channel, kPitchBend, id) == kResultTrue
              && id == kParamPitchBend);
        CHECK(mapping->getMidiControllerAssignment(0, channel, 16, id) == kResultTrue
              && id == kParamMasterPitch);
        CHECK(mapping->getMidiControllerAssignment(0, channel, 73, id) == kResultTrue
              && id == kParamAttack);
        CHECK(mapping->getMidiControllerAssignment(0, channel, 75, id) == kResultTrue
              && id == kParamDecay);
        CHECK(mapping->getMidiControllerAssignment(0, channel, kCtrlModWheel, id) != kResultTrue);
        CHECK(mapping->getMidiControllerAssignment(1, channel, 73, id) != kResultTrue);
    }
    mapping->release();

    // The controller takes over what the processor saved.
    IPtr<Processor> processor = owned(new Processor());
    CHECK(processor->initialize(nullptr) == kResultOk);
    {
        HostProcessData data;
        ParameterChanges changes(5);
        const std::pair<ParamID, double> values[] = {
            {kParamMasterPitch, MasterPitchNormalized(5)}, {kParamAttack, 0.3},
            {kParamDecay, 0.4}, {kParamVolume, 0.8}};
        for(const auto& [id, value] : values) {
            int32 index = 0;
            changes.addParameterData(id, index)->addPoint(0, value, index);
        }
        data.inputParameterChanges = &changes;
        data.numSamples = 0;
        CHECK(processor->process(data) == kResultOk);
    }
    MemoryStream stream;
    CHECK(processor->getState(&stream) == kResultOk);
    int64 position = 0;
    stream.seek(0, IBStream::kIBSeekSet, &position);
    CHECK(controller->setComponentState(&stream) == kResultOk);
    CHECK(controller->getParamNormalized(kParamMasterPitch) == MasterPitchNormalized(5));
    CHECK(controller->getParamNormalized(kParamAttack) == 0.3);
    CHECK(controller->getParamNormalized(kParamDecay) == 0.4);
    CHECK(controller->getParamNormalized(kParamVolume) == 0.8);
    CHECK(controller->getParamNormalized(kParamPitchBend) == kDefaultPitchBend);
    processor->terminate();

    // The controller's own state: the window's zoom and whether the keyboard shows.
    const auto window_state = [](int32 version, double zoom, int keyboard) {
        auto stream = owned(new MemoryStream());
        int32 written = 0;
        stream->write(&version, sizeof(version), &written);
        stream->write(&zoom, sizeof(zoom), &written);
        if(keyboard >= 0) {
            const int8 flag = static_cast<int8>(keyboard);
            stream->write(const_cast<int8*>(&flag), sizeof(flag), &written);
        }
        int64 start = 0;
        stream->seek(0, IBStream::kIBSeekSet, &start);
        return stream;
    };
    const auto saved = [](Controller& source) {
        MemoryStream state;
        CHECK(source.getState(&state) == kResultOk);
        CHECK(state.getSize() == 4 + 8 + 1);
        int32 version = 0;
        std::memcpy(&version, state.getData(), sizeof(version));
        CHECK(version == kControllerStateVersion);
        EditorSettings settings;
        std::memcpy(&settings.zoom, state.getData() + 4, sizeof(settings.zoom));
        settings.keyboard = state.getData()[12] != 0;
        return settings;
    };
    const auto is = [&](double zoom, bool keyboard) {
        const EditorSettings settings = saved(*controller);
        return settings.zoom == zoom && settings.keyboard == keyboard;
    };
    CHECK(is(1.0, true)); // keyboard shown by default
    CHECK(controller->setState(window_state(2, 1.5, 0)) == kResultOk);
    CHECK(is(1.5, false));
    // Version 1, as saved by plug-in 0.2.0: no keyboard flag, the keyboard shows.
    CHECK(controller->setState(window_state(1, 1.25, -1)) == kResultOk);
    CHECK(is(1.25, true));
    CHECK(controller->setState(window_state(2, 10.0, 0)) == kResultOk);
    CHECK(is(kMaxEditorZoom, false)); // out of range: clamped
    CHECK(controller->setState(window_state(2, 0.01, 1)) == kResultOk);
    CHECK(is(kMinEditorZoom, true));
    CHECK(controller->setState(window_state(99, 1.5, 0)) != kResultOk);
    CHECK(controller->setState(window_state(0, 1.5, 0)) != kResultOk);
    CHECK(controller->setState(window_state(2, 1.5, -1)) != kResultOk); // flag missing
    MemoryStream empty;
    CHECK(controller->setState(&empty) != kResultOk);
    CHECK(is(kMinEditorZoom, true)); // a rejected state changes nothing
    // Loading the sound's state leaves the window settings alone.
    stream.seek(0, IBStream::kIBSeekSet, &position);
    CHECK(controller->setComponentState(&stream) == kResultOk);
    CHECK(is(kMinEditorZoom, true));

    controller->terminate();
    std::printf("controller: value texts, MIDI map (pitch wheel, CC 16, 73, 75), state, "
                "window settings\n");
}

// Resizing the window: the aspect ratio holds, the zoom stays within its
// limits, and the window follows the mouse without jumping.
void TestEditorResize() {
    using Clock = DragConstraint::Clock;
    const auto same = [](EditorSize a, EditorSize b) {
        return a.width == b.width && a.height == b.height;
    };
    // One request of a drag, as Windows reports it: it starts from the size
    // granted before and puts the dragged edge or corner where the mouse is.
    // The requests come 10 ms apart.
    Clock::time_point now{};
    const auto request = [&](DragConstraint& drag, EditorSize asked, EditorSize current,
                             double scale, bool keyboard) {
        now += std::chrono::milliseconds(10);
        return drag.Apply(asked, current, scale, keyboard, now);
    };
    const EditorSize base = EditorSizeFor(1.0, false);
    const EditorSize with_keys = EditorSizeFor(1.0, true);
    CHECK(same(base, {460, 190}));
    CHECK(same(with_keys, {460, 272}));
    {
        DragConstraint drag;
        // Right edge dragged: the edge stays under the mouse, the height follows.
        CHECK(same(request(drag, {600, 190}, base, 1.0, false), {600, 248}));
        CHECK(same(request(drag, {600, 272}, with_keys, 1.0, true), {600, 355}));
        // Bottom edge: the width follows.
        CHECK(same(request(drag, {460, 300}, base, 1.0, false), {726, 300}));
    }
    {
        DragConstraint drag;
        // Corner: the allowed size nearest to the mouse.
        CHECK(same(request(drag, {700, 200}, base, 1.0, false), {669, 276}));
    }
    // Limits: 50 % and 200 %, also on a scaled display.
    const EditorSize scaled = EditorSizeFor(1.2, false);
    for(const auto& [asked, current, scale, expected] :
        {std::tuple{EditorSize{10, 10}, base, 1.0, EditorSize{230, 95}},
         std::tuple{EditorSize{5000, 190}, base, 1.0, EditorSize{920, 380}},
         std::tuple{EditorSize{10, 10}, scaled, 1.2, EditorSize{276, 114}},
         std::tuple{EditorSize{5000, 228}, scaled, 1.2, EditorSize{1104, 456}}}) {
        DragConstraint drag;
        CHECK(same(request(drag, asked, current, scale, false), expected));
    }
    // Asking for the current size changes nothing.
    for(const bool keyboard : {false, true})
        for(const double total : {0.5, 0.8, 1.0, 1.2, 1.37, 2.0, 2.4}) {
            DragConstraint drag;
            const EditorSize size = EditorSizeFor(total, keyboard);
            CHECK(same(request(drag, size, size, total > 2.0 ? 1.2 : 1.0, keyboard), size));
        }
    // Every granted size has the layout's aspect ratio within a pixel. (Both
    // sides are rounded, so the height can be off by up to 0.5 + 0.5 * 272 / 460.)
    for(const bool keyboard : {false, true})
        for(const bool corner : {false, true})
            for(int width = 200; width <= 1000; width += 7)
                for(int height = 80; height <= 500; height += 31) {
                    const EditorSize size = ConstrainEditorSize(
                        {width, height}, EditorSizeFor(1.0, keyboard), 1.0, keyboard, corner);
                    CHECK(std::abs(size.height
                                   - size.width * EditorBaseHeight(keyboard) / kEditorWidth)
                          < 1.0);
                }
    for(const bool keyboard : {false, true}) {
        const EditorSize anchor = EditorSizeFor(1.0, keyboard);
        // Corner drag, the mouse 7 px right and 2 px down per request. Up to
        // 0.2.0 the window jumped back and forth here; now it grows steadily,
        // also when the mouse meets the window's height by chance.
        DragConstraint drag;
        EditorSize current = anchor;
        bool met_height = false;
        for(int step = 1; step <= 60; ++step) {
            const EditorSize mouse = {anchor.width + 7 * step, anchor.height + 2 * step};
            met_height = met_height || mouse.height == current.height;
            const EditorSize next = request(drag, mouse, current, 1.0, keyboard);
            CHECK(next.width >= current.width && next.height >= current.height);
            CHECK(next.width - current.width <= 8);
            current = next;
        }
        CHECK(met_height); // the case that used to jump did occur
        // After a pause, a new drag: a right-edge drag that follows the mouse.
        now += std::chrono::seconds(1);
        int mouse = current.width;
        for(int step = 1; step <= 30; ++step) {
            mouse -= 5;
            current = request(drag, {mouse, current.height}, current, 1.0, keyboard);
            CHECK(current.width == mouse);
        }
    }
    // A window size leads back to the zoom it was made for.
    for(const bool keyboard : {false, true})
        for(const double scale : {1.0, 1.2, 1.5, 2.0})
            for(const double zoom : {0.5, 0.75, 1.0, 1.3, 2.0}) {
                const EditorSize size = EditorSizeFor(scale * zoom, keyboard);
                CHECK(std::fabs(EditorZoomForSize(size.width, size.height, scale, keyboard) - zoom)
                      < 0.005);
            }
    // A window of another shape: the content fits into it.
    CHECK(std::fabs(EditorZoomForSize(700, 420, 1.0, false) - 700 / kEditorWidth) < 1e-12);
    CHECK(std::fabs(EditorZoomForSize(700, 200, 1.0, false) - 200 / kEditorTopHeight) < 1e-12);
    CHECK(ClampEditorZoom(std::nan("")) == kMinEditorZoom);
    std::printf("editor resize: aspect ratio kept, zoom 50..200 %%, drags follow the mouse\n");
}
// The keyboard's geometry: every key where it is drawn, black keys on top.
void TestKeyboardLayout() {
    constexpr double width = 420.0, height = 68.0;
    int white = 0, black = 0;
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        const KeyRect key = KeyRectFor(note, width, height);
        CHECK(key.left >= 0.0 && key.right <= width && key.bottom <= height);
        const double centre = (key.left + key.right) / 2.0;
        if(IsBlackKey(note)) {
            ++black;
            CHECK(NoteAt(centre, height * 0.3, width, height) == note);
            // Below a black key lie the two white keys beside it.
            const double boundary = (WhiteKeyIndex(note) + 1) * width / kKeyboardWhiteKeys;
            CHECK(NoteAt(boundary - 1.0, height * 0.9, width, height) == note - 1);
            CHECK(NoteAt(boundary + 1.0, height * 0.9, width, height) == note + 1);
        } else {
            ++white;
            CHECK(NoteAt(centre, height * 0.9, width, height) == note);
        }
    }
    CHECK(white == kKeyboardWhiteKeys && black == 10);
    CHECK(kKeyboardHighNote - kKeyboardLowNote == 24); // two octaves and the top C
    CHECK(WhiteKeyIndex(kKeyboardHighNote) == kKeyboardWhiteKeys - 1);
    for(const auto& [x, y] : {std::pair{-1.0, 10.0}, {width, 10.0}, {10.0, height}, {10.0, -1.0}})
        CHECK(NoteAt(x, y, width, height) == -1);
    // A scan over the whole keyboard finds every key and nothing else.
    bool found[128] = {};
    for(double x = 0.0; x < width; x += 0.5)
        for(double y = 0.0; y < height; y += 2.0) {
            const int note = NoteAt(x, y, width, height);
            CHECK(note >= kKeyboardLowNote && note <= kKeyboardHighNote);
            found[note] = true;
        }
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note)
        CHECK(found[note]);
    CHECK(kKeyboardVelocity == 100);
    std::printf("keyboard: 25 keys C4..C6, every key where it is drawn\n");
}

// Notes from the screen keyboard: from the controller as a message to the
// processor, then through the firmware's queue into the engine.
void TestScreenKeyboardNotes() {
    IPtr<HostApplication> host = owned(new HostApplication());
    IPtr<Processor> processor = owned(new Processor());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(processor->initialize(host) == kResultOk);
    CHECK(controller->initialize(host) == kResultOk);
    // What a host does: connect the two halves.
    CHECK(processor->connect(controller) == kResultOk);
    CHECK(controller->connect(processor) == kResultOk);

    constexpr int block = 64;
    ProcessSetup setup{kRealtime, kSample32, block, 48000.0};
    CHECK(processor->setupProcessing(setup) == kResultOk);
    CHECK(processor->setActive(true) == kResultOk);
    HostProcessData data;
    CHECK(data.prepare(*processor.get(), block, kSample32));
    data.numSamples = block;
    const auto render = [&](int samples) {
        std::vector<float> out;
        for(int done = 0; done < samples; done += block) {
            CHECK(processor->process(data) == kResultOk);
            out.insert(out.end(), data.outputs[0].channelBuffers32[0],
                       data.outputs[0].channelBuffers32[0] + block);
        }
        return out;
    };

    // A struck key sounds from the next block on, exactly like a MIDI note-on
    // with the firmware's key velocity at that block's first sample.
    controller->PlayNote(64);
    const int total = 48000;
    const auto played = render(total);
    CHECK(Identical(played, RenderCore(48000.0f, {{0, 64, kKeyboardVelocity}}, total,
                                       AttackSeconds(kDefaultAttack),
                                       DecaySeconds(kDefaultDecay))));
    CHECK(Peak(played) > 0.0f);

    // Keys struck while the audio is off are not played later.
    CHECK(processor->setActive(false) == kResultOk);
    controller->PlayNote(70);
    CHECK(processor->setActive(true) == kResultOk);
    CHECK(Peak(render(4800)) == 0.0f);

    // Messages that are not keyboard notes, or carry nonsense, are refused.
    const auto message = [](const char* id, int64 note, int64 velocity) {
        auto result = owned(new HostMessage());
        result->setMessageID(id);
        result->getAttributes()->setInt(kNoteMessageNote, note);
        result->getAttributes()->setInt(kNoteMessageVelocity, velocity);
        return result;
    };
    CHECK(processor->notify(message(kNoteMessageId, 128, 100)) != kResultOk);
    CHECK(processor->notify(message(kNoteMessageId, 60, 0)) != kResultOk);
    CHECK(processor->notify(message("SomethingElse", 60, 100)) != kResultOk);
    CHECK(Peak(render(4800)) == 0.0f);
    CHECK(processor->notify(message(kNoteMessageId, 60, 100)) == kResultOk);
    CHECK(Peak(render(4800)) > 0.0f);

    processor->setActive(false);
    processor->disconnect(controller);
    controller->disconnect(processor);
    processor->terminate();
    controller->terminate();
    std::printf("screen keyboard: note message reaches the engine, bit-exact\n");
}

// Typed values: one number, optionally the unit, nothing else (review R03).
void TestTypedValues() {
    double value = 7.0;
    for(const auto& [text, unit, expected] :
        {std::tuple{"500", "ms", 500.0}, std::tuple{" 500 ms ", "ms", 500.0},
         std::tuple{"500MS", "ms", 500.0}, std::tuple{"+2.5", "", 2.5},
         std::tuple{"2,5", "", 2.5}, std::tuple{"-6dB", "dB", -6.0},
         std::tuple{"1e2", "", 100.0}}) {
        CHECK(ParseTypedNumber(text, unit, value) && value == expected);
    }
    for(const char* text : {"", "   ", "foo", "12junk", "nan", "inf", "-inf", "1,2,3", "ms",
                            "5 s", "--5", "+", "+-5", "++5", "-+5", "+-5 ms", "+ 5"}) {
        value = 7.0;
        CHECK(!ParseTypedNumber(text, "ms", value));
        CHECK(value == 7.0); // untouched on failure
    }

    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(nullptr) == kResultOk);
    const auto typed = [&](ParamID id, const char* text, ParamValue& normalized) {
        String128 string{};
        StringConvert::convert(std::string(text), string);
        return controller->getParamValueByString(id, string, normalized) == kResultOk;
    };
    const auto refused = [&](ParamID id, const char* text) {
        ParamValue normalized = 0.123;
        return !typed(id, text, normalized) && normalized == 0.123;
    };
    for(const ParamID id :
        {kParamMasterPitch, kParamAttack, kParamDecay, kParamVolume, kParamPitchBend})
        for(const char* text : {"", "foo", "12junk", "nan", "+inf", "+-5", "+-5 dB", "++5"})
            CHECK(refused(id, text)); // "+-5": Codex re-check, R03-N1
    ParamValue normalized = 0.0;
    CHECK(typed(kParamDecay, "500", normalized) && std::fabs(DecaySeconds(normalized) - 0.5f) < 1e-4f);
    CHECK(typed(kParamAttack, "10 ms", normalized)
          && std::fabs(AttackSeconds(normalized) - 0.01f) < 1e-5f);
    CHECK(typed(kParamVolume, "-inf", normalized) && normalized == 0.0);
    CHECK(typed(kParamVolume, "-INF dB", normalized) && normalized == 0.0);
    CHECK(typed(kParamVolume, "-6 dB", normalized) && std::fabs(VolumeDb(normalized) + 6.0) < 1e-9);
    CHECK(typed(kParamMasterPitch, "-7 st", normalized) && MasterPitchSemitones(normalized) == -7);
    CHECK(typed(kParamMasterPitch, "7.4", normalized) && MasterPitchSemitones(normalized) == 7);
    CHECK(typed(kParamMasterPitch, "+30", normalized) && MasterPitchSemitones(normalized) == 24);
    CHECK(typed(kParamPitchBend, "1", normalized) && std::fabs(normalized - 0.75) < 1e-12);
    controller->terminate();
    std::printf("typed values: strict, unit optional, -inf for silence, junk refused\n");
}

// Volume and pitch glides land exactly on their target, at every sample rate
// and from both sides (review R02). Back at 0 dB the output is the engine's
// own again, bit for bit.
void TestGlidesReachTarget() {
    for(const float rate : {8000.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
        Engine engine;
        engine.Init(rate);
        const int second = static_cast<int>(rate);
        const auto run = [&](int samples) {
            for(int i = 0; i < samples; ++i)
                engine.Process();
        };
        for(const float target : {0.25f, 1.0f, VolumeGain(1.0), 1.0f, 0.0f, 1.0f}) {
            engine.SetVolume(target);
            run(second);
            CHECK(engine.Volume() == target);
        }
        for(const float semitones : {7.0f, 0.0f, -26.0f, 0.0f, 0.37f, 0.0f}) {
            engine.SetPitch(semitones);
            run(second);
            CHECK(engine.PitchRatio() == std::pow(2.0f, semitones / 12.0f));
        }
        CHECK(engine.PitchRatio() == 1.0f);
    }
    // A note played through a volume round trip sounds like one that never had it.
    Engine moved, still;
    moved.Init(48000.0f);
    still.Init(48000.0f);
    moved.NoteOn(60, 127, toy_piano::kAttackMin, toy_piano::kDecayMax);
    still.NoteOn(60, 127, toy_piano::kAttackMin, toy_piano::kDecayMax);
    moved.SetVolume(0.25f);
    for(int i = 0; i < 24000; ++i) {
        moved.Process();
        still.Process();
    }
    moved.SetVolume(1.0f);
    for(int i = 0; i < 48000; ++i) {
        moved.Process();
        still.Process();
    }
    for(int i = 0; i < 4800; ++i)
        CHECK(moved.Process() == still.Process());
    std::printf("glides: volume and pitch land exactly on target at 8..192 kHz\n");
}

// The second harmonic fades out below its cut-off instead of switching off
// (review R07, fixed in the plug-in only).
void TestHarmonicFade() {
    using toy_piano::kSecondHarmonic;
    CHECK(HarmonicWeight(0.01f) == kSecondHarmonic && HarmonicWeight(0.2f) == kSecondHarmonic);
    CHECK(HarmonicWeight(0.25f) == 0.0f && HarmonicWeight(0.49f) == 0.0f);
    CHECK(std::fabs(HarmonicWeight(0.225f) - kSecondHarmonic / 2.0f) < 1e-6f);
    float previous = kSecondHarmonic;
    for(float increment = 0.0f; increment < 0.5f; increment += 0.0005f) {
        const float weight = HarmonicWeight(increment);
        CHECK(weight <= previous && weight >= 0.0f);
        previous = weight;
    }

    // Codex's case: note 115, then Master Pitch to +24 and back. The voice
    // crosses 12 kHz on the way. In 0.3.1 its harmonic jumped from 12 % to 0
    // in one step; now it passes through the band in steps. A jump of two
    // octaves crosses the band within a few glide updates (every 16 samples),
    // so the steps stay at up to a third of the old jump there.
    constexpr std::size_t voice = toy_piano::kNumKeys; // the first MIDI voice
    Engine engine;
    engine.Init(48000.0f);
    engine.NoteOn(115, 127, toy_piano::kAttackMin, toy_piano::kDecayMax);
    for(int i = 0; i < 4800; ++i)
        engine.Process();
    for(const float target : {24.0f, 0.0f}) {
        engine.SetPitch(target);
        float last = engine.Piano().VoiceHarmonic(voice), largest_step = 0.0f;
        int between = 0;
        for(int i = 0; i < 9600; ++i) {
            engine.Process();
            const float weight = engine.Piano().VoiceHarmonic(voice);
            const float increment = engine.Piano().VoiceIncrement(voice);
            CHECK(weight == HarmonicWeight(increment)); // follows the pitch exactly
            largest_step = std::max(largest_step, std::fabs(weight - last));
            between += weight > 0.0f && weight < kSecondHarmonic;
            last = weight;
        }
        CHECK(largest_step <= kSecondHarmonic / 3.0f); // was the full 0.12
        CHECK(between >= 3);                            // it did pass through the band
    }
    CHECK(engine.Piano().VoiceHarmonic(voice) == kSecondHarmonic);

    // The pitch wheel moves slowly through the band: very small steps. Note 125,
    // bent from -2 to +2, crosses the whole band (notes 123 to 127).
    Engine bent;
    bent.SetPitch(-2.0f);
    bent.Init(48000.0f);
    bent.NoteOn(125, 127, toy_piano::kAttackMin, toy_piano::kDecayMax); // 123 -> 127
    bent.SetPitch(2.0f);
    float last = bent.Piano().VoiceHarmonic(voice), largest_step = 0.0f;
    for(int i = 0; i < 9600; ++i) {
        bent.Process();
        largest_step = std::max(largest_step, std::fabs(bent.Piano().VoiceHarmonic(voice) - last));
        last = bent.Piano().VoiceHarmonic(voice);
    }
    std::printf("  pitch wheel through the band: largest step %.4f\n", largest_step);
    CHECK(last == 0.0f && largest_step <= kSecondHarmonic / 15.0f);

    // Where the plug-in and the firmware still agree: at Master Pitch 0 every
    // note below the band sounds bit for bit like the firmware. Only notes in
    // the band (123 to 126 at 48 kHz) have a quieter harmonic.
    for(int note = 0; note <= 127; ++note) {
        Engine plugin;
        toy_piano::ToyPiano firmware;
        plugin.Init(48000.0f);
        firmware.Init(48000.0f);
        plugin.NoteOn(static_cast<std::uint8_t>(note), 127, toy_piano::kAttackMin, 1.0f);
        firmware.NoteOn(static_cast<std::uint8_t>(note), 127, toy_piano::kAttackMin, 1.0f);
        bool same = true;
        for(int i = 0; i < 512; ++i)
            same = same && plugin.Process() == firmware.Process();
        const bool in_band = note >= 123 && note <= 126;
        CHECK(same == !in_band);
    }
    std::printf("harmonic: fades out between 0.20 and 0.25 fs, glides cross it in small steps, "
                "firmware sound kept below the band\n");
}

// A full keyboard queue refuses further notes and says so; one block empties it.
void TestKeyboardQueueLimit() {
    IPtr<HostApplication> host = owned(new HostApplication());
    IPtr<Processor> processor = owned(new Processor());
    CHECK(processor->initialize(host) == kResultOk);
    ProcessSetup setup{kRealtime, kSample32, 64, 48000.0};
    CHECK(processor->setupProcessing(setup) == kResultOk);
    CHECK(processor->setActive(true) == kResultOk);
    const auto note = [&](int64 pitch) {
        auto message = owned(new HostMessage());
        message->setMessageID(kNoteMessageId);
        message->getAttributes()->setInt(kNoteMessageNote, pitch);
        message->getAttributes()->setInt(kNoteMessageVelocity, 100);
        return processor->notify(message);
    };
    for(int i = 0; i < 64; ++i)
        CHECK(note(40 + i) == kResultOk);
    CHECK(note(110) != kResultOk); // the 65th
    HostProcessData data;
    CHECK(data.prepare(*processor.get(), 64, kSample32));
    data.numSamples = 64;
    CHECK(processor->process(data) == kResultOk);
    CHECK(note(110) == kResultOk); // room again
    processor->setActive(false);
    processor->terminate();
    std::printf("keyboard queue: 64 notes, a full queue reports it, one block empties it\n");
}

// A host stand-in: answers the window's size requests. Accepting, it does what
// a host does: resizes its window, then calls the view's onSize().
class FakeHost : public IPlugFrame {
  public:
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) override {
        ++requests;
        view->getSize(&size_at_request);
        const VSTGUI::CRect frame = editor->getFrame()->getViewSize();
        frame_at_request = {static_cast<int>(frame.getWidth()), static_cast<int>(frame.getHeight())};
        if(!accept)
            return kResultFalse;
        view->onSize(size);
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface(const TUID, void** obj) override {
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    Editor* editor = nullptr;
    bool accept = true;
    int requests = 0;
    ViewRect size_at_request{};
    EditorSize frame_at_request{};
};

// The real window in a hidden parent window, with a host that accepts or
// refuses sizes (review R01, R04, R05).
void TestEditorInWindow() {
    using VSTGUI::CRect;
    moduleHandle = GetModuleHandleW(nullptr);
    CHECK(InitModule()); // what a plug-in's DLL entry does: sets up VSTGUI
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1600, 1000, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    CHECK(parent != nullptr);

    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(nullptr) == kResultOk);
    FakeHost host;
    const auto open_view = [&]() {
        IPtr<IPlugView> view = owned(controller->createView(ViewType::kEditor));
        CHECK(view != nullptr);
        host.editor = dynamic_cast<Editor*>(view.get());
        CHECK(host.editor != nullptr);
        CHECK(view->setFrame(&host) == kResultOk);
        CHECK(view->attached(parent, kPlatformTypeHWND) == kResultOk);
        return view;
    };
    const auto same = [](EditorSize a, EditorSize b) {
        return a.width == b.width && a.height == b.height;
    };
    const auto view_size = [](IPlugView* view) {
        ViewRect size{};
        CHECK(view->getSize(&size) == kResultOk);
        return EditorSize{size.getWidth(), size.getHeight()};
    };
    const auto frame_size = [](Editor* editor) {
        const CRect size = editor->getFrame()->getViewSize();
        return EditorSize{static_cast<int>(size.getWidth()), static_cast<int>(size.getHeight())};
    };

    IPtr<IPlugView> view = open_view();
    Editor* editor = host.editor;
    CHECK(controller->OpenEditorCount() == 1);
    CHECK(editor->KeyboardVisible());
    CHECK(same(view_size(view), {460, 272}) && same(frame_size(editor), {460, 272}));

    // KEYS off, host agrees: the window changes only once the host has agreed.
    // (The SDK's attached() has already sent one request of its own.)
    int requests = host.requests;
    editor->SetKeyboardVisible(false);
    CHECK(host.requests == requests + 1);
    CHECK(host.size_at_request.getHeight() == 272 && host.frame_at_request.height == 272);
    CHECK(!editor->KeyboardVisible());
    CHECK(same(view_size(view), {460, 190}) && same(frame_size(editor), {460, 190}));

    // KEYS on, host refuses: nothing changes, nothing is cut off.
    host.accept = false;
    requests = host.requests;
    editor->SetKeyboardVisible(true);
    CHECK(host.requests == requests + 1 && host.frame_at_request.height == 190);
    CHECK(!editor->KeyboardVisible());
    CHECK(same(view_size(view), {460, 190}) && same(frame_size(editor), {460, 190}));

    // The same at the smallest zoom (the case in R04).
    host.accept = true;
    editor->ApplySettings({kMinEditorZoom, false});
    CHECK(same(view_size(view), {230, 95}) && same(frame_size(editor), {230, 95}));
    host.accept = false;
    editor->SetKeyboardVisible(true);
    CHECK(!editor->KeyboardVisible() && same(frame_size(editor), {230, 95}));

    // A loaded project brings the keyboard, the host keeps the window: the
    // content shrinks below 50 % to fit; the saved zoom stays as it was.
    editor->ApplySettings({kMinEditorZoom, true});
    CHECK(editor->KeyboardVisible() && same(frame_size(editor), {230, 95}));
    const double display = editor->getFrame()->getTransform().m11;
    CHECK(EditorBaseHeight(true) * display <= 95.0 + 1e-9 && kEditorWidth * display <= 230.0 + 1e-9);
    CHECK(editor->Settings().zoom == kMinEditorZoom);

    // Display scale and a user drag, host agreeing.
    host.accept = true;
    editor->ApplySettings({1.0, true});
    CHECK(same(view_size(view), {460, 272}));
    CHECK(editor->setContentScaleFactor(1.5f) == kResultOk);
    CHECK(same(view_size(view), {690, 408}) && same(frame_size(editor), {690, 408}));
    ViewRect dragged(0, 0, 920, 544);
    CHECK(view->onSize(&dragged) == kResultOk);
    CHECK(std::fabs(editor->Settings().zoom - 920.0 / 690.0) < 1e-9);

    // Closed properly, then opened again with the settings it had.
    CHECK(view->removed() == kResultOk);
    CHECK(controller->OpenEditorCount() == 0);
    view = nullptr;
    view = open_view();
    CHECK(controller->OpenEditorCount() == 1);
    CHECK(same(view_size(view), EditorSizeFor(920.0 / 690.0, true)));

    // A host that drops the window without removed() (R01): the controller
    // forgets it, and the next parameter change does not reach it.
    view = nullptr;
    CHECK(controller->OpenEditorCount() == 0);
    CHECK(controller->setParamNormalized(kParamVolume, 0.3) == kResultOk);

    controller->terminate();
    DestroyWindow(parent);
    DeinitModule();
    std::printf("editor in a window: asks before resizing, refusals change nothing, "
                "dropped windows forgotten\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // progress survives a crash
    TestBitExactAtPitchZero();
    TestSampleRates();
    TestMasterPitch();
    TestMasterPitchGlide();
    TestAboveNyquist();
    TestEnvelopeParameters();
    TestState();
    TestVolume();
    TestPitchBend();
    TestController();
    TestEditorResize();
    TestKeyboardLayout();
    TestScreenKeyboardNotes();
    TestTypedValues();
    TestHarmonicFade();
    TestGlidesReachTarget();
    TestKeyboardQueueLimit();
    TestEditorInWindow();
    std::printf("PASS\n");
    return 0;
}