// Checks the plug-in: what it took over from Toy Piano Classic against the
// unchanged firmware engine, and the sequencer inside it against the notes
// its pattern must give. The plug-in's processor is driven in-process through
// the VST3 SDK's hosting classes, the way a host drives it.
#include "controller.h"
#include "editor.h"
#include "engine.h"
#include "keyboard_layout.h"
#include "parameters.h"
#include "processor.h"
#include "state_io.h"
#include "step_clock.h"
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
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace taureon::toy_piano_generative;
namespace generative = taureon::generative;

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
    double tuning = kDefaultTuning;
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
    ParameterChanges changes(8);
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
                {kParamPitchBend, run.pitch_bend},
                {kParamTuning, run.tuning},
                // MIDI notes play here; by default they would transpose.
                {kParamKeysTranspose, 0.0}};
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
    CHECK(stream.getSize() == kStateSize); // the pitch wheel is not part of the state

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
    CHECK(controller->getParameterCount() == 18);

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

    // The controller's own state: the window's zoom, whether the keyboard
    // shows, and which octaves it shows. `bytes` cuts the state short.
    const auto window_state = [](int32 version, double zoom, int keyboard, int shift,
                                 int bytes = 14) {
        auto stream = owned(new MemoryStream());
        char data[14];
        std::memcpy(data, &version, 4);
        std::memcpy(data + 4, &zoom, 8);
        data[12] = static_cast<char>(keyboard);
        data[13] = static_cast<char>(shift);
        int32 written = 0;
        stream->write(data, bytes, &written);
        int64 start = 0;
        stream->seek(0, IBStream::kIBSeekSet, &start);
        return stream;
    };
    const auto saved = [](Controller& source) {
        MemoryStream state;
        CHECK(source.getState(&state) == kResultOk);
        CHECK(state.getSize() == 4 + 8 + 1 + 1);
        int32 version = 0;
        std::memcpy(&version, state.getData(), sizeof(version));
        CHECK(version == kControllerStateVersion);
        EditorSettings settings;
        std::memcpy(&settings.zoom, state.getData() + 4, sizeof(settings.zoom));
        settings.keyboard = state.getData()[12] != 0;
        settings.keyboard_shift = static_cast<signed char>(state.getData()[13]);
        return settings;
    };
    const auto is = [&](double zoom, bool keyboard, int shift = kKeyboardDefaultShift) {
        const EditorSettings settings = saved(*controller);
        return settings.zoom == zoom && settings.keyboard == keyboard
               && settings.keyboard_shift == shift;
    };
    CHECK(is(1.0, true)); // keyboard shown by default, from C3
    CHECK(controller->setState(window_state(1, 1.5, 0, 12)) == kResultOk);
    CHECK(is(1.5, false, 12));
    CHECK(controller->setState(window_state(1, 10.0, 0, 100)) == kResultOk);
    CHECK(is(kMaxEditorZoom, false, kKeyboardShiftMax)); // out of range: clamped
    CHECK(controller->setState(window_state(1, 0.01, 1, -100)) == kResultOk);
    CHECK(is(kMinEditorZoom, true, kKeyboardShiftMin));
    CHECK(controller->setState(window_state(99, 1.5, 0, 0)) != kResultOk);
    CHECK(controller->setState(window_state(0, 1.5, 0, 0)) != kResultOk);
    CHECK(controller->setState(window_state(1, 1.5, 0, 0, 13)) != kResultOk); // cut short
    MemoryStream empty;
    CHECK(controller->setState(&empty) != kResultOk);
    CHECK(is(kMinEditorZoom, true, kKeyboardShiftMin)); // a rejected state changes nothing
    // Loading the sound's state leaves the window settings alone.
    stream.seek(0, IBStream::kIBSeekSet, &position);
    CHECK(controller->setComponentState(&stream) == kResultOk);
    CHECK(is(kMinEditorZoom, true, kKeyboardShiftMin));

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
    CHECK(same(base, {640, 316}));
    CHECK(same(with_keys, {640, 398}));
    {
        DragConstraint drag;
        // Right edge dragged: the edge stays under the mouse, the height follows.
        CHECK(same(request(drag, {800, 316}, base, 1.0, false), {800, 395}));
        CHECK(same(request(drag, {800, 398}, with_keys, 1.0, true), {800, 498}));
        // Bottom edge: the width follows.
        CHECK(same(request(drag, {640, 400}, base, 1.0, false), {810, 400}));
    }
    {
        DragConstraint drag;
        // Corner: the allowed size nearest to the mouse.
        CHECK(same(request(drag, {900, 330}, base, 1.0, false), {855, 422}));
    }
    // Limits: 50 % and 200 %, also on a scaled display.
    const EditorSize scaled = EditorSizeFor(1.2, false);
    CHECK(same(scaled, {768, 379}));
    for(const auto& [asked, current, scale, expected] :
        {std::tuple{EditorSize{10, 10}, base, 1.0, EditorSize{320, 158}},
         std::tuple{EditorSize{5000, 316}, base, 1.0, EditorSize{1280, 632}},
         std::tuple{EditorSize{10, 10}, scaled, 1.2, EditorSize{384, 190}},
         std::tuple{EditorSize{5000, 379}, scaled, 1.2, EditorSize{1536, 758}}}) {
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
    // sides are rounded, so the height can be off by up to 0.5 + 0.5 * 398 / 640.)
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
        {kParamMasterPitch, kParamAttack, kParamDecay, kParamVolume, kParamPitchBend,
         kParamAlgorithm, kParamMutation, kParamNoteProbability, kParamOctave, kParamLength,
         kParamTuning})
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
    CHECK(same(view_size(view), {640, 398}) && same(frame_size(editor), {640, 398}));

    // KEYS off, host agrees: the window changes only once the host has agreed.
    // (The SDK's attached() has already sent one request of its own.)
    int requests = host.requests;
    editor->SetKeyboardVisible(false);
    CHECK(host.requests == requests + 1);
    CHECK(host.size_at_request.getHeight() == 398 && host.frame_at_request.height == 398);
    CHECK(!editor->KeyboardVisible());
    CHECK(same(view_size(view), {640, 316}) && same(frame_size(editor), {640, 316}));

    // KEYS on, host refuses: nothing changes, nothing is cut off.
    host.accept = false;
    requests = host.requests;
    editor->SetKeyboardVisible(true);
    CHECK(host.requests == requests + 1 && host.frame_at_request.height == 316);
    CHECK(!editor->KeyboardVisible());
    CHECK(same(view_size(view), {640, 316}) && same(frame_size(editor), {640, 316}));

    // The same at the smallest zoom.
    host.accept = true;
    editor->ApplySettings({kMinEditorZoom, false});
    CHECK(same(view_size(view), {320, 158}) && same(frame_size(editor), {320, 158}));
    host.accept = false;
    editor->SetKeyboardVisible(true);
    CHECK(!editor->KeyboardVisible() && same(frame_size(editor), {320, 158}));

    // A loaded project brings the keyboard, the host keeps the window: the
    // content shrinks below 50 % to fit; the saved zoom stays as it was.
    editor->ApplySettings({kMinEditorZoom, true});
    CHECK(editor->KeyboardVisible() && same(frame_size(editor), {320, 158}));
    const double display = editor->getFrame()->getTransform().m11;
    CHECK(EditorBaseHeight(true) * display <= 158.0 + 1e-9
          && kEditorWidth * display <= 320.0 + 1e-9);
    CHECK(editor->Settings().zoom == kMinEditorZoom);

    // Display scale and a user drag, host agreeing.
    host.accept = true;
    editor->ApplySettings({1.0, true});
    CHECK(same(view_size(view), {640, 398}));
    CHECK(editor->setContentScaleFactor(1.5f) == kResultOk);
    CHECK(same(view_size(view), {960, 597}) && same(frame_size(editor), {960, 597}));
    ViewRect dragged(0, 0, 1280, 796);
    CHECK(view->onSize(&dragged) == kResultOk);
    CHECK(std::fabs(editor->Settings().zoom - 1280.0 / 960.0) < 1e-9);

    // The octave buttons: the visible keys move by an octave, as far as they
    // go, and the sequence moves along, as far as its own range goes.
    const auto octave = [&] {
        return StepIndex(controller->getParamNormalized(kParamOctave), kOctaveSteps);
    };
    CHECK(editor->Settings().keyboard_shift == kKeyboardDefaultShift && octave() == 2);
    editor->ShiftKeyboard(1);
    CHECK(editor->Settings().keyboard_shift == 0 && octave() == 3);
    editor->ShiftKeyboard(1);
    editor->ShiftKeyboard(1);
    CHECK(editor->Settings().keyboard_shift == kKeyboardShiftMax && octave() == 5);
    editor->ShiftKeyboard(1);
    CHECK(editor->Settings().keyboard_shift == kKeyboardShiftMax && octave() == 5);
    for(int i = 0; i < 7; ++i)
        editor->ShiftKeyboard(-1);
    CHECK(editor->Settings().keyboard_shift == kKeyboardShiftMin && octave() == 0);
    editor->ShiftKeyboard(1);
    editor->ShiftKeyboard(1);
    CHECK(editor->Settings().keyboard_shift == kKeyboardDefaultShift && octave() == 2);

    // The mutation lock: a click is an edit, and a change from elsewhere shows.
    CHECK(editor->ShownMutationLock() == 0);
    editor->SetMutationLock(true);
    CHECK(controller->getParamNormalized(kParamMutationLock) == 1.0
          && editor->ShownMutationLock() == 1);
    CHECK(controller->setParamNormalized(kParamMutationLock, 0.0) == kResultOk);
    CHECK(editor->ShownMutationLock() == 0);
    // The algorithm's tooltip follows the chosen algorithm.
    CHECK(editor->AlgorithmTooltip() == kAlgorithms[0].description);
    CHECK(controller->setParamNormalized(kParamAlgorithm, StepNormalized(2, kAlgorithmSteps))
          == kResultOk);
    CHECK(editor->AlgorithmTooltip() == kAlgorithms[2].description);
    // Ladder uses neither Mutation nor Notes: they dim, and come back.
    CHECK(editor->SequencerControlsDimmed());
    CHECK(controller->setParamNormalized(kParamAlgorithm, StepNormalized(4, kAlgorithmSteps))
          == kResultOk);
    CHECK(!editor->SequencerControlsDimmed());
    // The small pass knob shows its number in the middle.
    CHECK(editor->MutateEveryText() == "-");
    CHECK(controller->setParamNormalized(kParamMutateEvery, StepNormalized(4, kMutateEverySteps))
          == kResultOk);
    CHECK(editor->MutateEveryText() == "4");
    CHECK(controller->setParamNormalized(kParamMutateEvery, 1.0) == kResultOk);
    CHECK(editor->MutateEveryText() == "16");

    // Closed properly, then opened again with the settings it had.
    CHECK(view->removed() == kResultOk);
    CHECK(controller->OpenEditorCount() == 0);
    view = nullptr;
    view = open_view();
    CHECK(controller->OpenEditorCount() == 1);
    CHECK(same(view_size(view), EditorSizeFor(1280.0 / 960.0, true)));

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

// ---------------------------------------------------------------------------
// The sequencer inside the plug-in
// ---------------------------------------------------------------------------

// A saved state as a stream, ready to load.
IPtr<MemoryStream> StateStream(const SavedState& state) {
    auto stream = owned(new MemoryStream());
    IBStreamer writer(stream, kLittleEndian);
    CHECK(WriteState(writer, state));
    CHECK(stream->getSize() == kStateSize);
    int64 start = 0;
    stream->seek(0, IBStream::kIBSeekSet, &start);
    return stream;
}

SavedState StateOf(Processor& processor) {
    MemoryStream stream;
    CHECK(processor.getState(&stream) == kResultOk);
    CHECK(stream.getSize() == kStateSize);
    int64 start = 0;
    stream.seek(0, IBStream::kIBSeekSet, &start);
    IBStreamer reader(&stream, kLittleEndian);
    SavedState state;
    CHECK(ReadState(reader, state));
    return state;
}

// What of a sequencer is saved: its pattern, its settings, its generator.
bool SameSequencer(const generative::State& a, const generative::State& b) {
    return a.steps == b.steps && a.random == b.random && a.tonic == b.tonic
           && a.scale_mode == b.scale_mode && a.algorithm == b.algorithm
           && a.mutation == b.mutation && a.note_probability == b.note_probability
           && a.octave == b.octave && a.length == b.length && a.clock_divide == b.clock_divide;
}

// A fixed pattern in A minor with notes of several pitches and rests between
// them, so that a shift by one step or one semitone shows. MIDI notes play.
SavedState TestPattern() {
    SavedState state;
    state.keys_transpose = false;
    generative::State& sequencer = state.sequencer;
    sequencer.steps.fill(generative::kRest);
    const int notes[][2] = {{0, 9},   {1, 12},  {3, 21},  {4, 9},  {6, 16},
                            {7, 19},  {10, 14}, {11, 31}, {13, 9}, {15, 17}};
    for(const auto& note : notes)
        sequencer.steps[static_cast<std::size_t>(note[0])] = static_cast<std::int8_t>(note[1]);
    sequencer.random = 0x1234567890ABCDEFULL;
    return state;
}

// The notes a pattern gives for the sixteenths `first_tick` to `last_tick - 1`
// after a start, one every `spacing` samples from `first_sample` on. For the
// plain case: algorithm 1 without mutation, every sixteenth a step.
std::vector<Note> PatternScore(const generative::State& sequencer, int first_tick, int last_tick,
                               int first_sample, int spacing) {
    std::vector<Note> score;
    for(int tick = first_tick; tick < last_tick; ++tick) {
        const std::int8_t step =
            sequencer.steps[static_cast<std::size_t>(tick % sequencer.length)];
        if(step != generative::kRest)
            score.push_back({first_sample + (tick - first_tick) * spacing,
                             SoundingNote(step + 12 * sequencer.octave), kSequencerVelocity});
    }
    return score;
}

void Append(std::vector<Note>& score, const std::vector<Note>& more) {
    score.insert(score.end(), more.begin(), more.end());
}

// A host in small: one processor, a transport and a count of samples.
class Session {
  public:
    struct Report {
        ParamID id;
        int sample; // counted from the start of the Render() that saw it
        double value;
    };

    explicit Session(double sample_rate = 48000.0, int block = 64, bool double_precision = false)
    : rate_(sample_rate), block_(block), double_(double_precision) {
        processor_ = owned(new Processor());
        CHECK(processor_->initialize(nullptr) == kResultOk);
        const int32 sample_size = double_ ? kSample64 : kSample32;
        ProcessSetup setup{kRealtime, sample_size, block_, rate_};
        CHECK(processor_->setupProcessing(setup) == kResultOk);
        CHECK(processor_->setActive(true) == kResultOk);
        processor_->setProcessing(true);
        CHECK(data_.prepare(*processor_.get(), block_, sample_size));
        data_.processMode = kRealtime;
        data_.inputEvents = &events_;
        data_.inputParameterChanges = &in_;
        data_.outputParameterChanges = &out_;
    }

    ~Session() {
        processor_->setProcessing(false);
        processor_->setActive(false);
        processor_->terminate();
    }

    Processor& processor() { return *processor_; }
    void Load(const SavedState& state) {
        CHECK(processor_->setState(StateStream(state)) == kResultOk);
    }
    // Sent with the next block.
    void Set(ParamID id, double value) { pending_.push_back({id, value}); }
    // The transport, from the next block on.
    void Play(double tempo, double from_quarters = 0.0) {
        playing_ = true;
        tempo_ = tempo;
        anchor_quarters_ = from_quarters;
        song_samples_ = 0;
    }
    void Stop() { playing_ = false; }
    // What the host tells: without these the plug-in has to do without.
    bool tells_tempo = true;
    bool tells_position = true;

    // Renders `samples`. The score's sample positions count from here.
    std::vector<float> Render(int samples, const std::vector<Note>& score = {}) {
        std::vector<float> out(static_cast<std::size_t>(samples));
        reports.clear();
        std::size_t next = 0;
        for(int start = 0; start < samples; start += block_) {
            const int count = std::min(block_, samples - start);
            events_.clear();
            in_.clearQueue();
            out_.clearQueue();
            for(const auto& [id, value] : pending_) {
                int32 index = 0;
                IParamValueQueue* queue = in_.addParameterData(id, index);
                CHECK(queue && queue->addPoint(0, value, index) == kResultOk);
            }
            pending_.clear();
            for(; next < score.size() && score[next].sample < start + count; ++next) {
                Event event{};
                event.type = Event::kNoteOnEvent;
                event.sampleOffset = score[next].sample - start;
                event.noteOn.pitch = score[next].note;
                event.noteOn.velocity = score[next].velocity / 127.0f;
                CHECK(events_.addEvent(event) == kResultOk);
            }
            context_ = {};
            context_.sampleRate = rate_;
            if(playing_)
                context_.state |= ProcessContext::kPlaying;
            if(tells_tempo) {
                context_.state |= ProcessContext::kTempoValid;
                context_.tempo = tempo_;
            }
            if(tells_position) {
                context_.state |= ProcessContext::kProjectTimeMusicValid;
                context_.projectTimeMusic =
                    anchor_quarters_ + static_cast<double>(song_samples_) * tempo_ / 60.0 / rate_;
            }
            data_.processContext = &context_;
            data_.numSamples = count;
            data_.outputs[0].silenceFlags = 0;
            CHECK(processor_->process(data_) == kResultOk);
            if(playing_)
                song_samples_ += count;
            for(int i = 0; i < count; ++i) {
                const double left = double_ ? data_.outputs[0].channelBuffers64[0][i]
                                            : data_.outputs[0].channelBuffers32[0][i];
                const double right = double_ ? data_.outputs[0].channelBuffers64[1][i]
                                             : data_.outputs[0].channelBuffers32[1][i];
                CHECK(left == right && std::isfinite(left)
                      && std::fabs(left) <= toy_piano::kOutputCeiling);
                out[static_cast<std::size_t>(start + i)] = static_cast<float>(left);
            }
            for(int32 i = 0; i < out_.getParameterCount(); ++i) {
                IParamValueQueue* queue = out_.getParameterData(i);
                for(int32 point = 0; queue && point < queue->getPointCount(); ++point) {
                    int32 offset = 0;
                    ParamValue value = 0.0;
                    CHECK(queue->getPoint(point, offset, value) == kResultOk);
                    CHECK(offset >= 0 && offset < count);
                    reports.push_back({queue->getParameterId(), start + offset, value});
                }
            }
        }
        return out;
    }

    // The parameter changes the plug-in reported during the last Render().
    std::vector<Report> reports;

  private:
    double rate_;
    int block_;
    bool double_;
    IPtr<Processor> processor_;
    HostProcessData data_;
    EventList events_{64};
    ParameterChanges in_{16};
    ParameterChanges out_{16};
    ProcessContext context_{};
    std::vector<std::pair<ParamID, double>> pending_;
    bool playing_ = false;
    double tempo_ = 120.0;
    double anchor_quarters_ = 0.0;
    long long song_samples_ = 0;
};

// The samples at which the clock finds sixteenths while a song plays from
// `from_quarters` for `total` samples, handed over in blocks of `block`.
std::vector<long long> Sixteenths(double tempo, double rate, int block, long long total,
                                  double from_quarters = 0.0, bool with_position = true,
                                  bool with_tempo = true) {
    StepClock clock;
    std::vector<long long> found;
    for(long long start = 0; start < total; start += block) {
        const int count = static_cast<int>(std::min<long long>(block, total - start));
        Transport transport;
        transport.playing = true;
        transport.has_tempo = with_tempo;
        transport.tempo = tempo;
        transport.has_position = with_position;
        transport.quarters = from_quarters + static_cast<double>(start) * tempo / 60.0 / rate;
        CHECK(clock.Begin(transport, rate, count) == (start == 0));
        int offset = 0, previous = -1;
        while(clock.Next(offset)) {
            CHECK(offset > previous && offset < count);
            previous = offset;
            found.push_back(start + offset);
        }
    }
    return found;
}

// The clock: every sixteenth once, on its sample, whatever the block size.
void TestStepClock() {
    const int blocks[] = {1, 16, 64, 333, 1024, 2048};
    // 120 BPM at 48 kHz: a sixteenth every 6000 samples.
    std::vector<long long> even;
    for(long long sample = 0; sample < 96000; sample += 6000)
        even.push_back(sample);
    for(const int block : blocks) {
        CHECK(Sixteenths(120.0, 48000.0, block, 96000) == even);
        // A host that tells no song position: counted from the start.
        CHECK(Sixteenths(120.0, 48000.0, block, 96000, 0.0, false) == even);
    }
    // From the middle of a sixteenth: the first one comes when it is due.
    const auto late = Sixteenths(120.0, 48000.0, 64, 30000, 0.3);
    CHECK(late.size() == 5 && late[0] == 4800 && late[4] == 28800);
    // No tempo from the host: 130 BPM.
    const auto fallback = Sixteenths(0.0, 48000.0, 64, 96000, 0.0, false, false);
    const double fallback_spacing = 48000.0 * 60.0 / StepClock::kFallbackTempo / 4.0;
    CHECK(fallback.size() == 18);
    for(std::size_t i = 0; i < fallback.size(); ++i)
        CHECK(fallback[i]
              == static_cast<long long>(std::ceil(static_cast<double>(i) * fallback_spacing - 1e-4)));
    // An uneven tempo at 44.1 kHz: each sixteenth on the first sample at or
    // after its time, and the same samples at every block size.
    const double rate = 44100.0, tempo = 97.3;
    const double spacing = rate * 60.0 / tempo / 4.0;
    const auto reference = Sixteenths(tempo, rate, 1, 5 * 44100);
    CHECK(reference.size() == static_cast<std::size_t>(std::ceil(5 * 44100 / spacing)));
    for(std::size_t i = 0; i < reference.size(); ++i) {
        const double exact = static_cast<double>(i) * spacing;
        CHECK(static_cast<double>(reference[i]) >= exact - 1e-3
              && static_cast<double>(reference[i]) < exact + 1.0);
    }
    for(const int block : blocks)
        CHECK(Sixteenths(tempo, rate, block, 5 * 44100) == reference);

    // Stop and start: no sixteenths while the host stands, then a new start.
    StepClock clock;
    Transport transport;
    transport.playing = true;
    transport.has_tempo = transport.has_position = true;
    transport.tempo = 120.0;
    int offset = -1;
    CHECK(clock.Begin(transport, 48000.0, 512) && clock.Next(offset) && offset == 0);
    CHECK(!clock.Next(offset));
    transport.playing = false;
    CHECK(!clock.Begin(transport, 48000.0, 512) && !clock.Playing() && !clock.Next(offset));
    transport.playing = true;
    transport.quarters = 1.0;
    CHECK(clock.Begin(transport, 48000.0, 512) && clock.Next(offset) && offset == 0);
    // The host jumps back, as at the end of a loop: the sixteenth there comes,
    // but that is no new start.
    transport.quarters = 1.0 + 512.0 / 24000.0;
    CHECK(!clock.Begin(transport, 48000.0, 512) && !clock.Next(offset));
    transport.quarters = 0.5;
    CHECK(!clock.Begin(transport, 48000.0, 512) && clock.Next(offset) && offset == 0);

    // A tempo change between two blocks: the next sixteenth comes when the
    // song reaches it at the new tempo, once.
    StepClock changing;
    std::vector<long long> found;
    double quarters = 0.0;
    for(long long start = 0; start < 96000; start += 512) {
        Transport now;
        now.playing = now.has_tempo = now.has_position = true;
        now.tempo = start < 47616 ? 120.0 : 60.0;
        now.quarters = quarters;
        changing.Begin(now, 48000.0, 512);
        while(changing.Next(offset))
            found.push_back(start + offset);
        quarters += 512.0 * now.tempo / 60.0 / 48000.0;
    }
    std::vector<long long> expected;
    for(long long sample = 0; sample < 47616; sample += 6000)
        expected.push_back(sample);
    for(long long sample = 48384; sample < 96000; sample += 12000) // 0.064 sixteenths later
        expected.push_back(sample);
    CHECK(found == expected);
    std::printf("step clock: sixteenths on their sample at 6 block sizes, start, stop, jump, "
                "tempo change, no tempo, no position\n");
}

// How sequencer notes and keys map to MIDI notes.
void TestPitchMapping() {
    CHECK(kSequencerTranspose == 24 && kSequencerVelocity == 100);
    CHECK(SoundingNote(33) == 57);  // the default tonic at the default octave: A3
    CHECK(SoundingNote(103) == 127);
    CHECK(SoundingNote(104) == 116); // folded down an octave
    CHECK(SoundingNote(130) == 118);
    CHECK(SoundingNote(-24) == 0 && SoundingNote(-26) == 10);
    for(int tonic = 0; tonic < generative::kNumTonics; ++tonic)
        for(int octave = 0; octave <= generative::kMaxOctave; ++octave) {
            const int key = KeyForTransposition(tonic, octave);
            CHECK(key == SoundingNote(tonic + 12 * octave)); // where the tonic sounds
            const KeyTransposition back = TranspositionForKey(key);
            CHECK(back.tonic == tonic && back.octave == octave);
        }
    CHECK(KeyForTransposition(0, 0) == 24 && KeyForTransposition(11, 5) == 95); // C1 to B6
    // Keys beyond that range take the nearest octave.
    CHECK(TranspositionForKey(0).tonic == 0 && TranspositionForKey(0).octave == 0);
    CHECK(TranspositionForKey(23).tonic == 11 && TranspositionForKey(23).octave == 0);
    CHECK(TranspositionForKey(96).tonic == 0 && TranspositionForKey(96).octave == 5);
    CHECK(TranspositionForKey(127).tonic == 7 && TranspositionForKey(127).octave == 5);
    // The keyboard's range: whole octaves, C1..C3 up to C5..C7.
    CHECK(ClampKeyboardShift(kKeyboardDefaultShift) == -12 && ClampKeyboardShift(0) == 0);
    CHECK(ClampKeyboardShift(-48) == -36 && ClampKeyboardShift(24) == 12
          && ClampKeyboardShift(7) == 0 && ClampKeyboardShift(-13) == -12);
    CHECK(kKeyboardLowNote + kKeyboardShiftMin == 24 && kKeyboardHighNote + kKeyboardShiftMax == 96);
    std::printf("pitch mapping: sequencer notes two octaves up, keys C1..B6 set tonic and octave\n");
}

// The sequencer plays the engine: every step of the pattern on the sample of
// its sixteenth, bit for bit like the same notes sent to the bare engine.
void TestSequencerSteps() {
    const SavedState pattern = TestPattern();
    const int spacing = 6000; // 120 BPM at 48 kHz
    const int total = 3 * 48000;
    const float attack = AttackSeconds(kDefaultAttack), decay = DecaySeconds(kDefaultDecay);
    const auto reference =
        RenderCore(48000.0f, PatternScore(pattern.sequencer, 0, 24, 0, spacing), total, attack, decay);
    CHECK(Peak(reference) > 0.02f);
    for(const bool double_precision : {false, true})
        for(const int block : {1, 16, 64, 333, 1024, 2048}) {
            Session session(48000.0, block, double_precision);
            session.Load(pattern);
            session.Play(120.0);
            CHECK(Identical(session.Render(total), reference));
        }
    {
        // Started in the middle of a sixteenth: the first step waits for the next one.
        Session session;
        session.Load(pattern);
        session.Play(120.0, 0.3);
        CHECK(Identical(session.Render(total),
                        RenderCore(48000.0f, PatternScore(pattern.sequencer, 0, 24, 4800, spacing),
                                   total, attack, decay)));
    }
    {
        // Length, clock divide and octave: five steps, one every third sixteenth.
        SavedState shaped = pattern;
        shaped.sequencer.length = 5;
        shaped.sequencer.clock_divide = 2; // every third sixteenth
        shaped.sequencer.octave = 4;
        Session session;
        session.Load(shaped);
        session.Play(120.0);
        CHECK(Identical(session.Render(total),
                        RenderCore(48000.0f,
                                   PatternScore(shaped.sequencer, 0, 8, 0, 3 * spacing), total,
                                   attack, decay)));
    }
    {
        // A pattern that mutates, at an uneven tempo and rate: the same audio
        // and the same pattern afterwards at every block size.
        SavedState mutating = pattern;
        mutating.sequencer.algorithm = 2;
        mutating.sequencer.mutation = 60;
        std::vector<float> first;
        SavedState first_state;
        for(const int block : {1, 64, 333, 2048}) {
            Session session(44100.0, block);
            session.Load(mutating);
            session.Play(97.3);
            const auto out = session.Render(4 * 44100);
            const SavedState after = StateOf(session.processor());
            if(first.empty()) {
                first = out;
                first_state = after;
                CHECK(Peak(out) > 0.02f);
                CHECK(after.sequencer.steps != mutating.sequencer.steps
                      && after.sequencer.random != mutating.sequencer.random);
            }
            CHECK(Identical(out, first) && SameSequencer(after.sequencer, first_state.sequencer));
        }
    }
    std::printf("sequencer: steps on their exact sample, bit-exact against the engine, "
                "6 block sizes, 32 and 64 bit\n");
}

// The sequencer follows the transport and the Run switch.
void TestSequencerTransport() {
    const SavedState pattern = TestPattern();
    const int spacing = 6000;
    const float attack = AttackSeconds(kDefaultAttack), decay = DecaySeconds(kDefaultDecay);
    {
        // The host stands, or says nothing about its transport: no steps.
        Session session;
        session.Load(pattern);
        CHECK(Peak(session.Render(48000)) == 0.0f);
        session.tells_tempo = session.tells_position = false;
        CHECK(Peak(session.Render(48000)) == 0.0f);
        // Playing without tempo and position: steps at 130 BPM from the start.
        session.Play(0.0);
        const auto out = session.Render(48000);
        const double fallback = 48000.0 * 60.0 / StepClock::kFallbackTempo / 4.0;
        std::vector<Note> expected;
        for(int tick = 0; tick * fallback < 48000.0; ++tick) {
            const std::int8_t step = pattern.sequencer.steps[static_cast<std::size_t>(tick)];
            if(step != generative::kRest)
                expected.push_back({static_cast<int>(std::ceil(tick * fallback - 1e-4)),
                                    SoundingNote(step + 12 * pattern.sequencer.octave),
                                    kSequencerVelocity});
        }
        CHECK(expected.size() >= 5);
        CHECK(Identical(out, RenderCore(48000.0f, expected, 48000, attack, decay)));
    }
    Session session;
    session.Load(pattern);
    std::vector<Note> score;
    std::vector<float> out;
    const auto render = [&](int samples) {
        const auto part = session.Render(samples);
        out.insert(out.end(), part.begin(), part.end());
    };
    // Ten sixteenths, then the host stops: the notes ring out, none follow.
    session.Play(120.0);
    render(10 * spacing);
    Append(score, PatternScore(pattern.sequencer, 0, 10, 0, spacing));
    session.Stop();
    render(5 * spacing);
    // A new start, somewhere in the song: the pattern begins at its first step.
    int now = 15 * spacing;
    session.Play(120.0, 7.0);
    render(14 * spacing);
    Append(score, PatternScore(pattern.sequencer, 0, 14, now, spacing));
    now += 14 * spacing;
    // Run off while the host plays on: silence from the sequencer.
    session.Set(kParamRun, 0.0);
    render(4 * spacing);
    now += 4 * spacing;
    // Run on again: the pattern begins at its first step, on the next sixteenth.
    session.Set(kParamRun, 1.0);
    render(5 * spacing);
    Append(score, PatternScore(pattern.sequencer, 0, 5, now, spacing));
    now += 5 * spacing;
    CHECK(static_cast<int>(out.size()) == now);
    CHECK(Identical(out, RenderCore(48000.0f, score, now, attack, decay)));
    std::printf("transport: steps only while the host plays and Run is on; every start begins "
                "at step 1\n");
}

// The pattern and the generator are part of the state.
void TestSequencerState() {
    const SavedState pattern = TestPattern();
    const auto bytes_of = [](Processor& processor) {
        MemoryStream stream;
        CHECK(processor.getState(&stream) == kResultOk);
        return std::vector<char>(stream.getData(), stream.getData() + stream.getSize());
    };
    const auto wanted = [](const SavedState& state) {
        const auto stream = StateStream(state);
        return std::vector<char>(stream->getData(), stream->getData() + stream->getSize());
    };
    {
        // A fresh instance: the start-up pattern, the tonic or rests, and the
        // defaults. Two instances differ.
        Session one, two;
        const SavedState fresh = StateOf(one.processor());
        for(const std::int8_t step : fresh.sequencer.steps)
            CHECK(step == generative::kRest || step == generative::kDefaultTonic);
        CHECK(fresh.run && fresh.keys_transpose && fresh.tuning == 0.0
              && fresh.sequencer.algorithm == 1 && fresh.sequencer.mutation == 0
              && fresh.sequencer.note_probability == 65 && fresh.sequencer.scale_mode == 1
              && fresh.sequencer.octave == 2 && fresh.sequencer.length == 16);
        CHECK(fresh.sequencer.random != StateOf(two.processor()).sequencer.random);
    }
    {
        // Loaded and saved again at once, before any audio: the same bytes.
        // Loading does not draw a new pattern, and neither does standing still.
        Session session;
        session.Load(pattern);
        CHECK(bytes_of(session.processor()) == wanted(pattern));
        session.Render(4800);
        CHECK(bytes_of(session.processor()) == wanted(pattern));
        // Playing without mutation keeps the pattern too.
        session.Play(120.0);
        session.Render(48000);
        CHECK(StateOf(session.processor()).sequencer.steps == pattern.sequencer.steps);
        // Setting the scale it already has changes nothing; another one draws anew.
        session.Set(kParamScale, StepNormalized(1, kScaleSteps));
        session.Render(64);
        CHECK(StateOf(session.processor()).sequencer.steps == pattern.sequencer.steps);
        session.Set(kParamScale, StepNormalized(0, kScaleSteps));
        session.Render(64);
        const SavedState drawn = StateOf(session.processor());
        CHECK(drawn.sequencer.scale_mode == 0 && drawn.sequencer.steps != pattern.sequencer.steps);
        // A new tonic transposes the stored notes; rests stay.
        session.Set(kParamTonic, StepNormalized(0, kTonicSteps));
        session.Render(64);
        const SavedState moved = StateOf(session.processor());
        CHECK(moved.sequencer.tonic == 0);
        for(std::size_t i = 0; i < moved.sequencer.steps.size(); ++i)
            CHECK(drawn.sequencer.steps[i] == generative::kRest
                      ? moved.sequencer.steps[i] == generative::kRest
                      : moved.sequencer.steps[i] == drawn.sequencer.steps[i] - 9);
        // The other settings arrive and are saved.
        session.Set(kParamAlgorithm, StepNormalized(3, kAlgorithmSteps));
        session.Set(kParamMutation, generative::MutationPosition(35));
        session.Set(kParamNoteProbability, StepNormalized(80, kNoteProbabilitySteps));
        session.Set(kParamOctave, StepNormalized(4, kOctaveSteps));
        session.Set(kParamLength, StepNormalized(6, kLengthSteps));
        session.Set(kParamClockDivide, StepNormalized(5, kClockDivideSteps));
        session.Set(kParamTuning, 0.25);
        session.Set(kParamRun, 0.0);
        session.Set(kParamKeysTranspose, 1.0);
        session.Set(kParamMutationLock, 1.0);
        session.Set(kParamMutateEvery, StepNormalized(4, kMutateEverySteps));
        session.Render(64);
        const SavedState set = StateOf(session.processor());
        CHECK(set.sequencer.algorithm == 4 && set.sequencer.mutation == 35
              && set.sequencer.note_probability == 80 && set.sequencer.octave == 4
              && set.sequencer.length == 7 && set.sequencer.clock_divide == 5
              && set.tuning == 0.25 && !set.run && set.keys_transpose
              && set.sequencer.mutation_lock == 1 && set.sequencer.mutate_every == 4);
    }
    {
        // Mutate every 2 passes, in the plug-in: two passes sound exactly as
        // stored, the third starts changed.
        SavedState every = pattern;
        every.sequencer.algorithm = 1;
        every.sequencer.mutation = 100;
        every.sequencer.mutate_every = 2;
        every.sequencer.length = 8;
        Session session;
        session.Load(every);
        session.Play(120.0);
        const int pass = 8 * 6000;
        CHECK(Identical(session.Render(2 * pass - 100),
                        RenderCore(48000.0f, PatternScore(every.sequencer, 0, 16, 0, 6000),
                                   2 * pass - 100, AttackSeconds(kDefaultAttack),
                                   DecaySeconds(kDefaultDecay))));
        CHECK(StateOf(session.processor()).sequencer.steps == pattern.sequencer.steps);
        session.Render(200);
        CHECK(StateOf(session.processor()).sequencer.steps != pattern.sequencer.steps);
    }
    {
        // The lock freezes a pattern that would mutate on every step, while
        // it plays; opened, it mutates again.
        SavedState locked = pattern;
        locked.sequencer.algorithm = 2;
        locked.sequencer.mutation = 100;
        locked.sequencer.mutation_lock = 1;
        Session session;
        session.Load(locked);
        session.Play(120.0);
        CHECK(Identical(session.Render(2 * 48000),
                        RenderCore(48000.0f, PatternScore(pattern.sequencer, 0, 16, 0, 6000),
                                   2 * 48000, AttackSeconds(kDefaultAttack),
                                   DecaySeconds(kDefaultDecay))));
        const SavedState frozen = StateOf(session.processor());
        CHECK(frozen.sequencer.steps == pattern.sequencer.steps
              && frozen.sequencer.random == pattern.sequencer.random);
        session.Set(kParamMutationLock, 0.0);
        session.Render(48000);
        const SavedState opened = StateOf(session.processor());
        CHECK(opened.sequencer.mutation_lock == 0 && opened.sequencer.steps != pattern.sequencer.steps);
    }
    {
        // A state of version 1, as plug-in 0.1.0 saved it: no lock, and the
        // scale modes numbered as they were then.
        const auto version_1 = [&](int scale) {
            SavedState old = pattern;
            old.sequencer.scale_mode = static_cast<std::uint8_t>(scale);
            const auto current = StateStream(old);
            std::vector<char> bytes(current->getData(), current->getData() + current->getSize());
            const std::int32_t one = 1;
            std::memcpy(bytes.data(), &one, sizeof(one));
            bytes.erase(bytes.begin() + 4 + 40 + 2 + 8, bytes.begin() + 4 + 40 + 2 + 10); // lock, every
            CHECK(static_cast<int>(bytes.size()) == kStateSizeVersion1);
            auto stream = owned(new MemoryStream());
            int32 written = 0;
            stream->write(bytes.data(), static_cast<int32>(bytes.size()), &written);
            int64 start = 0;
            stream->seek(0, IBStream::kIBSeekSet, &start);
            return stream;
        };
        const int moved[7] = {0, 1, 9, generative::kOddMode, 14, 15, generative::kTonicOnlyMode};
        for(int scale = 0; scale < 7; ++scale) {
            Session session;
            CHECK(session.processor().setState(version_1(scale)) == kResultOk);
            const SavedState loaded = StateOf(session.processor());
            CHECK(loaded.sequencer.scale_mode == moved[scale] && loaded.sequencer.mutation_lock == 0
                  && loaded.sequencer.mutate_every == 0
                  && loaded.sequencer.steps == pattern.sequencer.steps
                  && loaded.sequencer.random == pattern.sequencer.random);
            IPtr<Controller> controller = owned(new Controller());
            CHECK(controller->initialize(nullptr) == kResultOk);
            CHECK(controller->setComponentState(version_1(scale)) == kResultOk);
            CHECK(controller->getParamNormalized(kParamScale) == StepNormalized(moved[scale], kScaleSteps)
                  && controller->getParamNormalized(kParamMutationLock) == 0.0);
            controller->terminate();
        }
        Session session;
        CHECK(session.processor().setState(version_1(7)) != kResultOk); // version 1 had seven
    }
    {
        // A project saved while its pattern mutates and loaded again goes on
        // exactly as the original did.
        SavedState mutating = pattern;
        mutating.sequencer.algorithm = 2;
        mutating.sequencer.mutation = 60;
        Session original;
        original.Load(mutating);
        original.Play(120.0);
        original.Render(10 * 6000);
        const SavedState saved = StateOf(original.processor());
        CHECK(saved.sequencer.random != mutating.sequencer.random);
        original.Render(20 * 6000);
        Session reloaded;
        reloaded.Load(saved);
        reloaded.Play(120.0);
        reloaded.Render(20 * 6000);
        const SavedState a = StateOf(original.processor()), b = StateOf(reloaded.processor());
        CHECK(SameSequencer(a.sequencer, b.sequencer));
        CHECK(a.sequencer.steps != saved.sequencer.steps);
    }
    {
        // Damaged states are refused whole.
        Session session;
        session.Load(pattern);
        const auto good = wanted(pattern);
        const auto refused = [&](std::vector<char> bytes) {
            MemoryStream stream;
            int32 written = 0;
            stream.write(bytes.data(), static_cast<int32>(bytes.size()), &written);
            int64 start = 0;
            stream.seek(0, IBStream::kIBSeekSet, &start);
            return session.processor().setState(&stream) != kResultOk
                   && bytes_of(session.processor()) == good;
        };
        const auto with = [&](std::size_t at, char value) {
            std::vector<char> bytes = good;
            bytes[at] = value;
            return bytes;
        };
        CHECK(refused({}));
        CHECK(refused(std::vector<char>(good.begin(), good.end() - 1))); // cut short
        CHECK(refused(with(0, 3)));                // another version
        CHECK(refused(with(0, 0)));
        CHECK(refused(with(4 + 42 + 8, 2)));       // the lock neither 0 nor 1
        CHECK(refused(with(4 + 42 + 9, 17)));      // mutate-every beyond 16
        CHECK(refused(with(4 + 42 + 1, 17)));      // a scale mode beyond the last
        CHECK(refused(with(4 + 7, 0x7F)));         // master pitch far out of range
        CHECK(refused(with(4 + 40, 2)));           // run neither 0 nor 1
        CHECK(refused(with(4 + 42, 12)));          // tonic
        CHECK(refused(with(4 + 42 + 2, 0)));       // algorithm 0
        CHECK(refused(with(4 + 42 + 6, 17)));      // length
        CHECK(refused(with(4 + 50 + 3, 97)));      // a step out of range
        // The controller reads the same state and refuses the same ones.
        IPtr<Controller> controller = owned(new Controller());
        CHECK(controller->initialize(nullptr) == kResultOk);
        MemoryStream cut;
        int32 written = 0;
        cut.write(const_cast<char*>(good.data()), static_cast<int32>(good.size()) - 1, &written);
        int64 start = 0;
        cut.seek(0, IBStream::kIBSeekSet, &start);
        CHECK(controller->setComponentState(&cut) != kResultOk);
        CHECK(controller->getParamNormalized(kParamKeysTranspose) == kDefaultKeysTranspose);
        controller->terminate();
    }
    {
        // getState() and setState() while the audio thread steps and mutates:
        // every state read is a whole one.
        SavedState busy = pattern;
        busy.sequencer.algorithm = 2;
        busy.sequencer.mutation = 100;
        busy.sequencer.note_probability = 100;
        Session session(48000.0, 32);
        session.Load(busy);
        session.Play(900.0);
        std::atomic<bool> stop{false};
        std::atomic<int> rendered{0};
        std::thread audio([&] {
            while(!stop.load()) {
                session.Render(32 * 64);
                ++rendered;
            }
        });
        int loads = 0;
        // At least 4000 reads, and until the audio thread has done its share.
        for(int i = 0; i < 4000 || rendered.load() < 200; ++i) {
            const SavedState read = StateOf(session.processor()); // checks it as a whole
            CHECK(read.sequencer.algorithm == 2 && read.sequencer.mutation == 100);
            if(i % 200 == 199) {
                CHECK(session.processor().setState(StateStream(busy)) == kResultOk);
                // Whether the audio thread has taken it yet or not, the state
                // is the loaded one or what followed from it, never the old one.
                ++loads;
            }
        }
        stop.store(true);
        audio.join();
        CHECK(loads >= 20 && rendered.load() >= 200);
        // A state loaded while the audio stands is the state at once.
        session.Load(pattern);
        CHECK(bytes_of(session.processor()) == wanted(pattern));
    }
    std::printf("state: pattern and generator saved, a reloaded project continues identically, "
                "loading draws nothing, damaged states refused, safe beside running audio\n");
}

// With Keys Transpose on, a MIDI note moves the sequence instead of sounding.
void TestTransposeByMidi() {
    SavedState pattern = TestPattern();
    pattern.keys_transpose = true;
    const float attack = AttackSeconds(kDefaultAttack), decay = DecaySeconds(kDefaultDecay);
    {
        Session session;
        session.Load(pattern);
        // C4 while the host stands: no sound, tonic C and octave 3 from now on.
        CHECK(Peak(session.Render(4800, {{100, 60, 100}})) == 0.0f);
        CHECK(session.reports.size() == 2);
        for(const Session::Report& report : session.reports) {
            CHECK(report.sample == 100);
            CHECK(report.id == kParamTonic ? report.value == StepNormalized(0, kTonicSteps)
                                           : (report.id == kParamOctave
                                              && report.value == StepNormalized(3, kOctaveSteps)));
        }
        const SavedState moved = StateOf(session.processor());
        CHECK(moved.sequencer.tonic == 0 && moved.sequencer.octave == 3);
        for(std::size_t i = 0; i < moved.sequencer.steps.size(); ++i)
            CHECK(pattern.sequencer.steps[i] == generative::kRest
                      ? moved.sequencer.steps[i] == generative::kRest
                      : moved.sequencer.steps[i] == pattern.sequencer.steps[i] - 9);
        // Velocity 0 and notes outside the MIDI range move nothing.
        session.Render(4800, {{10, 62, 0}});
        CHECK(session.reports.empty() && StateOf(session.processor()).sequencer.tonic == 0);
    }
    {
        // While it plays: D4 between the second and the third sixteenth. The
        // first two steps sound as before, all later ones from D at octave 3.
        const int spacing = 6000, total = 2 * 48000;
        SavedState moved = pattern;
        moved.sequencer.tonic = 2;
        moved.sequencer.octave = 3;
        for(std::int8_t& step : moved.sequencer.steps)
            if(step != generative::kRest)
                step = static_cast<std::int8_t>(step - 7);
        std::vector<Note> score = PatternScore(pattern.sequencer, 0, 2, 0, spacing);
        Append(score, PatternScore(moved.sequencer, 2, 16, 2 * spacing, spacing));
        const auto reference = RenderCore(48000.0f, score, total, attack, decay);
        for(const int block : {1, 64, 333, 2048}) {
            Session session(48000.0, block);
            session.Load(pattern);
            session.Play(120.0);
            CHECK(Identical(session.Render(total, {{9000, 62, 100}}), reference));
            CHECK(session.reports.size() == 2 && session.reports[0].sample == 9000);
        }
    }
    {
        // Switched off, the same note plays, and the sequence stays.
        Session session;
        SavedState playing = pattern;
        playing.keys_transpose = false;
        session.Load(playing);
        const auto out = session.Render(48000, {{100, 60, 100}});
        CHECK(Identical(out, RenderCore(48000.0f, {{100, 60, 100}}, 48000, attack, decay)));
        CHECK(session.reports.empty());
        CHECK(SameSequencer(StateOf(session.processor()).sequencer, playing.sequencer));
    }
    std::printf("transposing: a MIDI note sets tonic and octave at its sample, is reported to the "
                "host and does not sound; switched off it plays\n");
}

// The tuning: exact by default, down to whole hertz at 100 %.
void TestTuning() {
    for(int note = 0; note <= 127; ++note) {
        CHECK(TuningRatio(note, 0.0) == 1.0);
        const double exact = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const double tuned = exact * TuningRatio(note, 1.0);
        CHECK(std::fabs(tuned - std::round(tuned)) < 1e-9); // a whole number of hertz
        CHECK(tuned < exact && exact - tuned <= 1.0 + 1e-9);
        // Half way in between at 50 %.
        CHECK(std::fabs(exact * TuningRatio(note, 0.5) - (exact + tuned) / 2.0) < 1e-9);
    }
    CHECK(std::fabs(440.0 * TuningRatio(69, 1.0) - 439.0) < 1e-9);
    CHECK(std::fabs(55.0 * TuningRatio(33, 1.0) - 54.0) < 1e-9);
    CHECK(TuningRatio(69, -1.0) == 1.0 && TuningRatio(69, 5.0) == TuningRatio(69, 1.0));

    // Measured on the plug-in's output.
    const int total = 3 * 48000;
    const struct {
        std::uint8_t note;
        double tuning, hertz;
    } cases[] = {{33, 0.0, 55.0}, {33, 1.0, 54.0}, {33, 0.5, 54.5},
                 {69, 0.0, 440.0}, {69, 1.0, 439.0}, {60, 1.0, 261.0}};
    for(const auto& c : cases) {
        PluginRun run;
        run.decay = 1.0;
        run.tuning = c.tuning;
        const auto out = RenderPlugin(run, {{0, c.note, 127}}, total);
        const double measured = Frequency(out, 48000.0, 9600, 105600);
        std::printf("  note %d at tuning %.0f %%: %.3f Hz\n", c.note, 100.0 * c.tuning, measured);
        CHECK(std::fabs(measured - c.hertz) < 0.02);
    }
    // The tuning is taken at the strike: a note that sounds keeps its own.
    Engine engine;
    engine.Init(48000.0f);
    engine.NoteOn(69, 127, toy_piano::kAttackMin, toy_piano::kDecayMax);
    const float before = engine.Piano().VoiceIncrement(toy_piano::kNumKeys);
    engine.SetTuning(1.0);
    for(int i = 0; i < 480; ++i)
        engine.Process();
    CHECK(engine.Piano().VoiceIncrement(toy_piano::kNumKeys) == before);
    engine.NoteOn(69, 127, toy_piano::kAttackMin, toy_piano::kDecayMax);
    CHECK(std::fabs(engine.Piano().VoiceIncrement(toy_piano::kNumKeys) / before - 439.0f / 440.0f)
          < 1e-6f);
    std::printf("tuning: exact at 0 %%, whole hertz at 100 %% (A4 439 Hz, A1 54 Hz), taken at "
                "the strike\n");
}

// The sequencer's parameters as a host sees them, and the keys that transpose.
void TestSequencerController() {
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(nullptr) == kResultOk);
    const auto text = [&](ParamID id, double normalized) {
        String128 string{};
        CHECK(controller->getParamStringByValue(id, normalized, string) == kResultOk);
        return StringConvert::convert(string);
    };
    const auto shown = [&](ParamID id) { return text(id, controller->getParamNormalized(id)); };
    // The defaults.
    CHECK(shown(kParamRun) == "On" && shown(kParamAlgorithm) == "Drift"
          && shown(kParamMutation) == "0" && shown(kParamMutationLock) == "Off"
          && shown(kParamMutateEvery) == "Step"
          && shown(kParamNoteProbability) == "65" && shown(kParamScale) == "Minor"
          && shown(kParamTonic) == "A" && shown(kParamOctave) == "2" && shown(kParamLength) == "16"
          && shown(kParamClockDivide) == "1" && shown(kParamTuning) == "0"
          && shown(kParamKeysTranspose) == "On");
    // Every value of the stepped ones, as the processor reads it.
    const char* scales[] = {"Major",       "Minor",       "Dorian",     "Phrygian",
                            "Lydian",      "Mixolydian",  "Harm. Minor", "Mel. Minor",
                            "Major Pent.", "Minor Pent.", "Blues",      "Whole Tone",
                            "Hirajoshi",   "Odd",         "Octave",     "Fifths",
                            "Tonic"};
    CHECK(std::size(scales) == generative::kNumScaleModes);
    for(int i = 0; i <= kScaleSteps; ++i)
        CHECK(text(kParamScale, StepNormalized(i, kScaleSteps)) == scales[i]);
    const char* tonics[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for(int i = 0; i <= kTonicSteps; ++i)
        CHECK(text(kParamTonic, StepNormalized(i, kTonicSteps)) == tonics[i]);
    for(int i = 0; i <= kClockDivideSteps; ++i)
        CHECK(text(kParamClockDivide, StepNormalized(i, kClockDivideSteps))
              == std::to_string(generative::kClockDivides[static_cast<std::size_t>(i)]));
    for(int i = 0; i <= kAlgorithmSteps; ++i)
        CHECK(text(kParamAlgorithm, StepNormalized(i, kAlgorithmSteps)) == kAlgorithms[i].name);
    // Each description starts with its name.
    for(const AlgorithmInfo& algorithm : kAlgorithms)
        CHECK(std::string(algorithm.description).rfind(std::string(algorithm.name) + ": ", 0) == 0);
    for(int i = 0; i <= kLengthSteps; ++i)
        CHECK(text(kParamLength, StepNormalized(i, kLengthSteps)) == std::to_string(i + 1));
    for(int i = 0; i <= kOctaveSteps; ++i)
        CHECK(text(kParamOctave, StepNormalized(i, kOctaveSteps)) == std::to_string(i));
    for(int i = 0; i <= kNoteProbabilitySteps; ++i)
        CHECK(text(kParamNoteProbability, StepNormalized(i, kNoteProbabilitySteps))
              == std::to_string(i));
    for(int cc = 0; cc < 128; ++cc) { // and for any value in between
        const double value = cc / 127.0;
        CHECK(text(kParamOctave, value) == std::to_string(StepIndex(value, kOctaveSteps)));
        CHECK(text(kParamTonic, value) == tonics[StepIndex(value, kTonicSteps)]);
        CHECK(text(kParamMutation, value) == std::to_string(generative::MutationPercent(value)));
    }
    // The mutation knob's law, and what can be typed.
    CHECK(text(kParamMutation, 0.25) == "10" && text(kParamMutation, 0.5) == "20"
          && text(kParamMutation, 0.75) == "60" && text(kParamMutation, 1.0) == "100");
    CHECK(text(kParamTuning, 0.5) == "50");
    const auto typed = [&](ParamID id, const char* string) {
        String128 wide{};
        StringConvert::convert(std::string(string), wide);
        ParamValue normalized = -1.0;
        CHECK(controller->getParamValueByString(id, wide, normalized) == kResultOk);
        return normalized;
    };
    CHECK(generative::MutationPercent(typed(kParamMutation, "35 %")) == 35);
    CHECK(generative::MutationPercent(typed(kParamMutation, "250")) == 100);
    CHECK(StepIndex(typed(kParamNoteProbability, "80"), kNoteProbabilitySteps) == 80);
    CHECK(StepIndex(typed(kParamAlgorithm, "4"), kAlgorithmSteps) == 3);
    CHECK(StepIndex(typed(kParamAlgorithm, "ladder"), kAlgorithmSteps) == 2);
    CHECK(StepIndex(typed(kParamAlgorithm, " Focus "), kAlgorithmSteps) == 4);
    CHECK(StepIndex(typed(kParamAlgorithm, "9"), kAlgorithmSteps) == 4);
    {
        String128 wide{};
        StringConvert::convert(std::string("Nonsense"), wide);
        ParamValue normalized = 0.5;
        CHECK(controller->getParamValueByString(kParamAlgorithm, wide, normalized) != kResultOk
              && normalized == 0.5);
    }
    CHECK(StepIndex(typed(kParamLength, "7"), kLengthSteps) == 6);
    CHECK(StepIndex(typed(kParamLength, "99"), kLengthSteps) == kLengthSteps);
    CHECK(StepIndex(typed(kParamOctave, "5"), kOctaveSteps) == 5);
    CHECK(std::fabs(typed(kParamTuning, "25 %") - 0.25) < 1e-12);
    CHECK(StepIndex(typed(kParamScale, "Fifths"), kScaleSteps) == 15);
    CHECK(StepIndex(typed(kParamScale, "Dorian"), kScaleSteps) == 2);
    CHECK(StepIndex(typed(kParamTonic, "F#"), kTonicSteps) == 6);
    // No sequencer parameter is tied to a MIDI controller.
    for(int cc = 0; cc < 128; ++cc) {
        ParamID id = 0;
        if(controller->getMidiControllerAssignment(0, 0, static_cast<CtrlNumber>(cc), id)
           == kResultTrue)
            CHECK(id < kParamRun);
    }

    // A key of the screen keyboard, keys transposing: tonic and octave follow.
    controller->KeyStruck(62); // D4
    CHECK(shown(kParamTonic) == "D" && shown(kParamOctave) == "3");
    controller->KeyStruck(24); // C1, the lowest
    CHECK(shown(kParamTonic) == "C" && shown(kParamOctave) == "0");
    controller->KeyStruck(94); // A#6
    CHECK(shown(kParamTonic) == "A#" && shown(kParamOctave) == "5");
    // The octave buttons move the sequence within its range.
    controller->ShiftOctave(1);
    CHECK(shown(kParamOctave) == "5");
    controller->ShiftOctave(-2);
    CHECK(shown(kParamOctave) == "3" && shown(kParamTonic) == "A#");
    controller->ShiftOctave(-9);
    CHECK(shown(kParamOctave) == "0");
    // Keys playing: a key leaves the sequence alone.
    CHECK(controller->setParamNormalized(kParamKeysTranspose, 0.0) == kResultOk);
    controller->KeyStruck(62);
    CHECK(shown(kParamTonic) == "A#" && shown(kParamOctave) == "0");

    // The controller takes over what a processor saved.
    SavedState state = TestPattern();
    state.master_pitch = MasterPitchNormalized(5);
    state.attack = 0.3;
    state.decay = 0.4;
    state.volume = 0.8;
    state.tuning = 0.6;
    state.run = false;
    state.keys_transpose = true;
    state.sequencer.tonic = 4;
    state.sequencer.scale_mode = 15;
    state.sequencer.mutation_lock = 1;
    state.sequencer.mutate_every = 12;
    state.sequencer.algorithm = 3;
    state.sequencer.mutation = 45;
    state.sequencer.note_probability = 12;
    state.sequencer.octave = 1;
    state.sequencer.length = 9;
    state.sequencer.clock_divide = 6;
    CHECK(controller->setComponentState(StateStream(state)) == kResultOk);
    CHECK(shown(kParamMasterPitch) == "5" && controller->getParamNormalized(kParamAttack) == 0.3
          && controller->getParamNormalized(kParamDecay) == 0.4
          && controller->getParamNormalized(kParamVolume) == 0.8 && shown(kParamTuning) == "60");
    CHECK(shown(kParamRun) == "Off" && shown(kParamKeysTranspose) == "On"
          && shown(kParamTonic) == "E" && shown(kParamScale) == "Fifths"
          && shown(kParamAlgorithm) == "Ladder" && shown(kParamMutation) == "45"
          && shown(kParamMutationLock) == "On" && shown(kParamMutateEvery) == "12"
          && shown(kParamNoteProbability) == "12" && shown(kParamOctave) == "1"
          && shown(kParamLength) == "9" && shown(kParamClockDivide) == "16");
    controller->terminate();
    std::printf("sequencer parameters: texts, typed values, state; keys and octave buttons "
                "transpose\n");
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
    TestStepClock();
    TestPitchMapping();
    TestSequencerSteps();
    TestSequencerTransport();
    TestSequencerState();
    TestTransposeByMidi();
    TestTuning();
    TestSequencerController();
    std::printf("PASS\n");
    return 0;
}