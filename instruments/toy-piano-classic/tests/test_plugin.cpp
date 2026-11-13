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

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

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

// In a plug-in the SDK's DLL entry point defines this handle. The test links
// the plug-in's classes into an executable, so it has to provide it.
void* moduleHandle = nullptr;

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
    for(const int block : {1, 16, 48, 64, 333, 1024, 2048}) {
        run.block_size = block;
        CHECK(Identical(RenderPlugin(run, score, total), reference));
    }
    run.block_size = 256;
    run.double_precision = true; // 64-bit processing carries the same float samples
    CHECK(Identical(RenderPlugin(run, score, total), reference));
    std::printf("bit-exact against the firmware engine: 7 block sizes, 32 and 64 bit\n");
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

} // namespace

int main() {
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
    std::printf("PASS\n");
    return 0;
}