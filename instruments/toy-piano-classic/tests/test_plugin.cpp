// Checks the plug-in against the unchanged firmware engine. The plug-in's
// processor is driven in-process through the VST3 SDK's hosting classes, the
// way a host drives it.
#include "controller.h"
#include "editor.h"
#include "engine.h"
#include "parameters.h"
#include "processor.h"
#include "toy_piano.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
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

    // The controller's own state is the zoom of the editor window.
    const auto zoom_state = [](int32 version, double zoom) {
        auto stream = owned(new MemoryStream());
        int32 written = 0;
        stream->write(&version, sizeof(version), &written);
        stream->write(&zoom, sizeof(zoom), &written);
        int64 start = 0;
        stream->seek(0, IBStream::kIBSeekSet, &start);
        return stream;
    };
    const auto saved_zoom = [](Controller& source) {
        MemoryStream saved;
        CHECK(source.getState(&saved) == kResultOk);
        CHECK(saved.getSize() == 4 + 8);
        double zoom = 0.0;
        std::memcpy(&zoom, saved.getData() + 4, sizeof(zoom));
        return zoom;
    };
    CHECK(saved_zoom(*controller) == 1.0);
    CHECK(controller->setState(zoom_state(kControllerStateVersion, 1.5)) == kResultOk);
    CHECK(saved_zoom(*controller) == 1.5);
    CHECK(controller->setState(zoom_state(kControllerStateVersion, 10.0)) == kResultOk);
    CHECK(saved_zoom(*controller) == kMaxEditorZoom); // out of range: clamped
    CHECK(controller->setState(zoom_state(kControllerStateVersion, 0.01)) == kResultOk);
    CHECK(saved_zoom(*controller) == kMinEditorZoom);
    CHECK(controller->setState(zoom_state(99, 1.5)) != kResultOk);
    MemoryStream empty;
    CHECK(controller->setState(&empty) != kResultOk);
    CHECK(saved_zoom(*controller) == kMinEditorZoom); // a rejected state changes nothing
    // Loading the sound's state leaves the window zoom alone.
    stream.seek(0, IBStream::kIBSeekSet, &position);
    CHECK(controller->setComponentState(&stream) == kResultOk);
    CHECK(saved_zoom(*controller) == kMinEditorZoom);

    controller->terminate();
    std::printf("controller: value texts, MIDI map (pitch wheel, CC 16, 73, 75), state, zoom\n");
}

// Resizing the window: the aspect ratio holds, the zoom stays within its
// limits, and the edge being dragged decides.
void TestEditorResize() {
    const auto same = [](EditorSize a, EditorSize b) {
        return a.width == b.width && a.height == b.height;
    };
    const EditorSize base = EditorSizeFor(1.0);
    CHECK(same(base, {460, 190}));
    // Right edge dragged: the height follows the width.
    CHECK(same(ConstrainEditorSize({600, 190}, base, 1.0), {600, 248}));
    // Bottom edge dragged: the width follows the height.
    CHECK(same(ConstrainEditorSize({460, 300}, base, 1.0), {726, 300}));
    // Corner dragged: the larger move decides.
    CHECK(same(ConstrainEditorSize({700, 200}, base, 1.0), {700, 289}));
    // Limits: 50 % and 200 %, also on a scaled display.
    CHECK(same(ConstrainEditorSize({10, 10}, base, 1.0), {230, 95}));
    CHECK(same(ConstrainEditorSize({5000, 190}, base, 1.0), {920, 380}));
    CHECK(same(ConstrainEditorSize({10, 10}, EditorSizeFor(1.2), 1.2), {276, 114}));
    CHECK(same(ConstrainEditorSize({5000, 228}, EditorSizeFor(1.2), 1.2), {1104, 456}));
    // Asking for the current size changes nothing.
    for(const double total : {0.5, 0.8, 1.0, 1.2, 1.37, 2.0, 2.4}) {
        const EditorSize size = EditorSizeFor(total);
        CHECK(same(ConstrainEditorSize(size, size, 1.0 + (total > 2.0 ? 0.2 : 0.0)), size));
    }
    // Every allowed size has the layout's aspect ratio, to the pixel.
    for(int width = 200; width <= 1000; width += 7) {
        const EditorSize size = ConstrainEditorSize({width, 190}, base, 1.0);
        CHECK(std::abs(size.height - size.width * kEditorHeight / kEditorWidth) <= 0.5);
    }
    // A window size leads back to the zoom it was made for.
    for(const double scale : {1.0, 1.2, 1.5, 2.0})
        for(const double zoom : {0.5, 0.75, 1.0, 1.3, 2.0}) {
            const EditorSize size = EditorSizeFor(scale * zoom);
            CHECK(std::fabs(EditorZoomForSize(size.width, size.height, scale) - zoom) < 0.005);
        }
    // A window of another shape: the content fits into it.
    CHECK(std::fabs(EditorZoomForSize(700, 420, 1.0) - 700 / kEditorWidth) < 1e-12);
    CHECK(std::fabs(EditorZoomForSize(700, 200, 1.0) - 200 / kEditorHeight) < 1e-12);
    CHECK(ClampEditorZoom(std::nan("")) == kMinEditorZoom);
    std::printf("editor resize: aspect ratio kept, zoom 50..200 %%, dragged edge decides\n");
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
    std::printf("PASS\n");
    return 0;
}
