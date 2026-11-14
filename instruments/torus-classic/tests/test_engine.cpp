// Tests of the dry engine (layer 2) on its own: comparison with the pinned
// firmware engine, the six models, note handling, instance isolation and
// freedom from allocation while rendering.

#define TEST_SUPPORT_COUNT_ALLOCATIONS
#include "test_support.h"

#include "audio_output.h"
#include "dry_engine.h"
#include "reference_api.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace torus_classic;
using namespace test_support;
using torus_classic_reference::FirmwareSynth;

namespace {

constexpr int kQ = DryEngine::kQuantum;
constexpr std::uint32_t kSeed = 0x12345678;
const char* const kModelNames[6] = {"Modal Resonator",    "Sympathetic Strings", "Inharmonic Strings",
                                    "FM Voice",           "Western Chords",      "Stereo Strings"};

// --- Scores: what is played, in engine quanta -------------------------------

struct Action {
    enum Kind { kNote, kControl, kModel, kBend };
    std::uint32_t quantum;
    Kind kind;
    unsigned a;  // note, control index, model or raw bend
    float value; // velocity or control value
};
using Score = std::vector<Action>;

void Note(Score& score, std::uint32_t quantum, unsigned note, unsigned velocity) {
    score.push_back({quantum, Action::kNote, note, static_cast<float>(velocity)});
}
void Set(Score& score, std::uint32_t quantum, Control control, float value) {
    score.push_back({quantum, Action::kControl, static_cast<unsigned>(control), value});
}
void Model(Score& score, std::uint32_t quantum, unsigned model) {
    score.push_back({quantum, Action::kModel, model, 0.0f});
}
void Bend(Score& score, std::uint32_t quantum, unsigned raw) {
    score.push_back({quantum, Action::kBend, raw, 0.0f});
}

void Apply(DryEngine& engine, const Action& action) {
    switch(action.kind) {
        case Action::kNote:
            engine.NoteOn(action.a, static_cast<unsigned>(action.value));
            break;
        case Action::kControl:
            engine.SetControl(static_cast<Control>(action.a), action.value);
            break;
        case Action::kModel: engine.SetModel(action.a); break;
        case Action::kBend: engine.SetPitchBend(action.a); break;
    }
}

void Apply(FirmwareSynth& reference, const Action& action) {
    using namespace torus_classic_reference;
    static constexpr unsigned kFirmwareParameter[kControlCount] = {
        kFrequency, kStructure, kBrightness, kDamping, kPosition, kExcitation, kMaster};
    switch(action.kind) {
        case Action::kNote:
            reference.NoteOn(action.a, static_cast<unsigned>(action.value));
            break;
        case Action::kControl: reference.Set(kFirmwareParameter[action.a], action.value); break;
        case Action::kModel: reference.SelectModel(action.a); break;
        case Action::kBend: reference.SetBend(action.a); break;
    }
}

// Interleaved stereo.
std::vector<float> Play(DryEngine& engine, const Score& score, std::uint32_t quanta) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(quanta) * kQ * 2);
    float left[kQ], right[kQ];
    std::size_t next = 0;
    for(std::uint32_t q = 0; q < quanta; ++q) {
        for(; next < score.size() && score[next].quantum <= q; ++next)
            Apply(engine, score[next]);
        engine.Render(left, right);
        for(int i = 0; i < kQ; ++i) {
            out.push_back(left[i]);
            out.push_back(right[i]);
        }
    }
    return out;
}

std::vector<float> PlayFresh(const Score& score, std::uint32_t quanta, std::uint32_t seed = kSeed) {
    DryEngine engine;
    engine.Reset(seed);
    return Play(engine, score, quanta);
}

bool Same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// One model played through the things a keyboard does.
Score NotesScore(unsigned model) {
    Score s;
    Model(s, 0, model);
    Note(s, 60, 60, 100);                       // one note
    for(unsigned note : {60u, 64u, 67u, 72u})   // a four-voice chord in one quantum
        Note(s, 400, note, 110);
    for(unsigned i = 0; i < 6; ++i)             // more notes than voices: stealing
        Note(s, 800 + 20 * i, 50 + 3 * i, 90);
    for(unsigned i = 0; i < 8; ++i)             // the same note again and again
        Note(s, 1100 + 30 * i, 67, 40 + 10 * i);
    Note(s, 1500, 0, 127);                      // the ends of the MIDI range
    Note(s, 1560, 127, 127);
    Note(s, 1620, 1, 1);
    Note(s, 1680, 126, 1);
    for(unsigned i = 0; i < 12; ++i)            // twelve at once: three quanta of triggers
        Note(s, 1800, 36 + 5 * i, 100);
    for(unsigned i = 0; i < 5; ++i)             // strikes on neighbouring quanta
        Note(s, 2100 + i, 72 - i, 80);
    return s;
}

// Controls moving under sounding notes, in the pattern of the firmware's
// performance corpus (src/performance_render.h).
Score SweepScore(unsigned model) {
    Score s;
    Model(s, 0, model);
    for(std::uint32_t q = 50; q < 3000; q += 250)
        for(unsigned offset : {0u, 4u, 7u, 12u})
            Note(s, q, 48 + model * 2 + offset, 90);
    for(std::uint32_t q = 300; q < 3000; q += 25) {
        const float x = static_cast<float>((q / 25) % 11) / 10.0f;
        Set(s, q, Control::Structure, x);
        Set(s, q, Control::Damping, 1.0f - x);
        if(q >= 1200) {
            Set(s, q, Control::Tune, 0.35f + 0.3f * x);
            Set(s, q, Control::Brightness, x);
            Set(s, q, Control::Position, 1.0f - x);
            Set(s, q, Control::Excitation, 0.3f + 0.7f * x);
        }
        if(q >= 2000)
            Set(s, q, Control::Volume, 1.0f - 0.8f * x);
        if(q >= 2400 && (q / 25) % 4 == 0)
            Bend(s, q, static_cast<unsigned>(16383.0f * x));
    }
    return s;
}

// Every model change, with notes before, during and after the fades.
Score SwitchScore() {
    Score s;
    const unsigned order[] = {1, 2, 3, 4, 5, 0, 3, 1, 5, 2, 4, 0, 5, 5, 0};
    std::uint32_t q = 20;
    for(unsigned model : order) {
        Model(s, q, model);
        q += 170;
    }
    for(std::uint32_t at = 5; at < q + 200; at += 37)
        Note(s, at, 40 + (at * 7) % 50, 60 + at % 60);
    for(std::uint32_t at = 100; at < q; at += 113)
        Set(s, at, Control::Structure, static_cast<float>(at % 9) / 8.0f);
    std::stable_sort(s.begin(), s.end(),
                     [](const Action& x, const Action& y) { return x.quantum < y.quantum; });
    return s;
}

// The ends of every range.
Score LimitsScore(unsigned model) {
    Score s;
    Model(s, 0, model);
    std::uint32_t q = 40;
    for(float tune : {0.0f, 1.0f})
        for(unsigned bend : {0u, 16383u}) {
            Set(s, q, Control::Tune, tune);
            Bend(s, q, bend);
            Note(s, q + 30, 0, 127);
            Note(s, q + 30, 127, 127);
            Note(s, q + 60, 60, 127);
            q += 160;
        }
    Set(s, q, Control::Tune, 0.5f);
    Bend(s, q, 8192);
    for(unsigned bits = 0; bits < 32; ++bits) {
        Set(s, q, Control::Structure, static_cast<float>(bits & 1));
        Set(s, q, Control::Brightness, static_cast<float>((bits >> 1) & 1));
        Set(s, q, Control::Damping, static_cast<float>((bits >> 2) & 1));
        Set(s, q, Control::Position, static_cast<float>((bits >> 3) & 1));
        Set(s, q, Control::Excitation, static_cast<float>((bits >> 4) & 1));
        for(unsigned note : {36u, 60u, 84u, 108u})
            Note(s, q + 30, note, 127);
        q += 70;
    }
    Set(s, q, Control::Volume, 0.0f);
    Note(s, q + 30, 60, 127);
    Set(s, q + 100, Control::Volume, 1.0f);
    return s;
}

// A long pseudo-random performance.
Score RandomScore(std::uint32_t seed, std::uint32_t quanta) {
    Score s;
    Lcg random(seed);
    for(std::uint32_t q = 0; q < quanta; ++q) {
        if(random.Below(18) == 0) {
            const unsigned count = 1 + random.Below(random.Below(6) == 0 ? 9 : 3);
            for(unsigned i = 0; i < count; ++i)
                Note(s, q, random.Below(128), 1 + random.Below(127));
        }
        if(random.Below(14) == 0)
            Set(s, q, static_cast<Control>(random.Below(kControlCount)), random.Unit());
        if(random.Below(70) == 0)
            Bend(s, q, random.Below(16384));
        if(random.Below(450) == 0)
            Model(s, q, random.Below(6));
    }
    return s;
}

std::uint32_t LastQuantum(const Score& score) {
    return score.empty() ? 0 : score.back().quantum;
}

// --- The dry reference comparison -------------------------------------------
//
// Reference: the pinned firmware engine with Dry/Wet at exactly 0. Its dry/wet
// starts at 0.25 and reaches 0 only after smoothing, so both engines first
// render kWarmUp silent quanta from the same seed; the check below proves the
// reference's mix is exactly 0 from then on. The reverb keeps running in the
// reference, as in the firmware; its output is multiplied by that 0.
//
// Tolerance, fixed before the first run: none. Both engines are compiled by
// the same compiler with /fp:precise and receive the same calls in the same
// quanta, and the port's dry path performs the firmware's operations in the
// firmware's order, so every sample must compare equal (==). The one
// difference the firmware's mix can leave is the sign of a zero: it computes
// dry + (wet - dry) * 0, which turns -0 into +0. Those compare equal and are
// counted separately.

constexpr std::uint32_t kWarmUp = 400;

struct Comparison {
    std::size_t samples = 0, different = 0, zero_signs = 0;
    double max_difference = 0.0, energy = 0.0;
    float peak = 0.0f;
};

Comparison CompareWithFirmware(const Score& score, std::uint32_t quanta, std::uint32_t seed) {
    DryEngine port;
    FirmwareSynth firmware;
    port.Reset(seed);
    firmware.Reset(seed);
    firmware.Set(torus_classic_reference::kWet, 0.0f);

    float pl[kQ], pr[kQ], fl[kQ], fr[kQ];
    for(std::uint32_t q = 0; q < kWarmUp; ++q) {
        port.Render(pl, pr);
        firmware.Render(fl, fr);
        for(int i = 0; i < kQ; ++i)
            CHECK(pl[i] == 0.0f && pr[i] == 0.0f && fl[i] == 0.0f && fr[i] == 0.0f);
    }
    CHECK(firmware.Current(torus_classic_reference::kWet) == 0.0f); // exactly dry from here
    CHECK(port.NoiseState() == FirmwareSynth::RandomState());

    Comparison result;
    std::size_t next = 0;
    for(std::uint32_t q = 0; q < quanta; ++q) {
        for(; next < score.size() && score[next].quantum <= q; ++next) {
            Apply(port, score[next]);
            Apply(firmware, score[next]);
        }
        port.Render(pl, pr);
        firmware.Render(fl, fr);
        for(int i = 0; i < kQ; ++i) {
            for(const auto& [p, f] : {std::pair{pl[i], fl[i]}, std::pair{pr[i], fr[i]}}) {
                ++result.samples;
                CHECK(std::isfinite(p) && std::isfinite(f));
                if(p != f) {
                    ++result.different;
                    result.max_difference =
                        std::max(result.max_difference, std::fabs(double(p) - double(f)));
                } else if(std::signbit(p) != std::signbit(f)) {
                    ++result.zero_signs;
                }
                result.energy += double(p) * p;
                result.peak = std::max(result.peak, std::fabs(p));
            }
        }
        CHECK(port.Model() == firmware.Model());
        CHECK(port.TriggerCount() == firmware.TriggerCount());
    }
    CHECK(port.NoiseState() == FirmwareSynth::RandomState()); // same noise drawn
    return result;
}

void TestDryReference() {
    std::size_t total = 0, zero_signs = 0;
    const auto run = [&](const char* name, const Score& score, std::uint32_t quanta,
                         std::uint32_t seed) {
        const Comparison c = CompareWithFirmware(score, quanta, seed);
        std::printf("  %-34s %8zu samples, %zu differ (max %.3g), %zu zero signs, RMS %.4f, "
                    "peak %.3f\n",
                    name, c.samples, c.different, c.max_difference, c.zero_signs,
                    std::sqrt(c.energy / double(c.samples)), c.peak);
        CHECK(c.different == 0);
        CHECK(c.energy > 1.0); // the comparison is not one of silence
        CHECK(c.peak < 0.8f);
        total += c.samples;
        zero_signs += c.zero_signs;
    };
    char name[64];
    for(unsigned model = 0; model < 6; ++model) {
        std::snprintf(name, sizeof(name), "notes, %s", kModelNames[model]);
        run(name, NotesScore(model), 2400, kSeed);
        std::snprintf(name, sizeof(name), "sweeps, %s", kModelNames[model]);
        run(name, SweepScore(model), 3000, kSeed);
        std::snprintf(name, sizeof(name), "limits, %s", kModelNames[model]);
        const Score limits = LimitsScore(model);
        run(name, limits, LastQuantum(limits) + 300, kSeed);
    }
    const Score switches = SwitchScore();
    run("model changes", switches, LastQuantum(switches) + 300, kSeed);
    run("random performance, seed 1", RandomScore(1, 8000), 8000, kSeed);
    run("random performance, seed 2", RandomScore(2, 8000), 8000, DryEngine::kDefaultSeed);
    std::printf("dry reference: %zu samples equal to the firmware engine at Dry/Wet 0 "
                "(%zu zeros of the other sign)\n",
                total, zero_signs);
}

// --- Models and notes --------------------------------------------------------

std::vector<float> Mono(const std::vector<float>& stereo) {
    std::vector<float> mono(stereo.size() / 2);
    for(std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = stereo[2 * i] + stereo[2 * i + 1];
    return mono;
}

std::vector<float> Channel(const std::vector<float>& stereo, int channel) {
    std::vector<float> out(stereo.size() / 2);
    for(std::size_t i = 0; i < out.size(); ++i)
        out[i] = stereo[2 * i + channel];
    return out;
}

void CheckBounded(const std::vector<float>& x) {
    for(const float v : x)
        CHECK(std::isfinite(v) && std::fabs(v) < 0.8f);
}

void TestModels() {
    std::array<double, 6> energies{};
    for(unsigned model = 0; model < 6; ++model) {
        DryEngine engine;
        engine.Reset(kSeed);
        engine.JumpModel(model);
        CHECK(engine.Model() == model);
        // Silent before the first strike, the FM voice included.
        CHECK(Energy(Play(engine, {}, 200)) == 0.0);
        // One note.
        Score one;
        Note(one, 0, 69, 127);
        const auto single = Play(engine, one, 400);
        CheckBounded(single);
        CHECK(Energy(single) > 1e-4);
        // A chord of four, then a fifth and sixth note steal the oldest voices.
        engine.Reset(kSeed);
        engine.JumpModel(model);
        Score chord;
        for(unsigned note : {60u, 64u, 67u, 72u})
            Note(chord, 0, note, 127);
        const auto played = Play(engine, chord, 400);
        CheckBounded(played);
        energies[model] = Energy(played);
        CHECK(energies[model] > 1e-4);
        CHECK(engine.TriggerCount() == 4);
        for(unsigned voice = 0; voice < 4; ++voice)
            CHECK(engine.VoiceNote(voice) == static_cast<float>(chord[voice].a));
        CHECK(engine.NoteOn(76, 100) && engine.NoteOn(79, 100));
        Play(engine, {}, 1);
        CHECK(engine.VoiceNote(0) == 76.0f && engine.VoiceNote(1) == 79.0f);
        CHECK(engine.VoiceNote(2) == 67.0f && engine.VoiceNote(3) == 72.0f);
        // Velocity: a harder strike is louder; excitation 0 adds no energy.
        double level[2];
        for(int pass = 0; pass < 2; ++pass) {
            engine.Reset(kSeed);
            engine.JumpModel(model);
            Score strike;
            Note(strike, 0, 69, pass ? 127 : 32);
            level[pass] = Energy(Play(engine, strike, 300));
        }
        CHECK(level[0] > 0.0 && level[0] < level[1]);
        engine.Reset(kSeed);
        engine.JumpModel(model);
        engine.JumpControl(Control::Excitation, 0.0f);
        Score quiet;
        Note(quiet, 0, 69, 127);
        CHECK(Energy(Play(engine, quiet, 300)) < 1e-10);
        // The same note struck repeatedly, and the ends of the MIDI range.
        const auto notes = PlayFresh(NotesScore(model), 2400);
        CheckBounded(notes);
        const Score limits = LimitsScore(model);
        CheckBounded(PlayFresh(limits, LastQuantum(limits) + 300));
        std::printf("  %-20s chord energy %.4f, velocity 32/127 energy %.5f/%.5f\n",
                    kModelNames[model], energies[model], level[0], level[1]);
    }
    for(unsigned a = 0; a < 6; ++a) // six different sounds
        for(unsigned b = a + 1; b < 6; ++b)
            CHECK(std::fabs(energies[a] - energies[b]) > 1e-7);

    // Invalid strikes are refused and trigger nothing.
    DryEngine engine;
    CHECK(!engine.NoteOn(60, 0) && !engine.NoteOn(128, 100) && !engine.NoteOn(60, 128));
    Play(engine, {}, 4);
    CHECK(engine.TriggerCount() == 0 && engine.PendingNotes() == 0);
    std::printf("models: six distinct sounds, silence before the first strike, chords, voice "
                "stealing, velocity, range ends\n");
}

void TestStereoStrings() {
    // Model 5 is the dry branch of the firmware's "String & Reverb": the two
    // channels are cross-mixed by Position and the right one is inverted. At
    // Position 0.5 the cross-mix makes both channels equal, so the inversion
    // leaves exact opposites.
    DryEngine engine;
    engine.Reset(kSeed);
    engine.JumpModel(5);
    engine.JumpControl(Control::Position, 0.5f);
    Score chord;
    for(unsigned note : {48u, 55u, 60u, 64u})
        Note(chord, 0, note, 127);
    const auto centred = Play(engine, chord, 600);
    double energy = 0.0;
    for(std::size_t i = 0; i < centred.size(); i += 2) {
        CHECK(centred[i] == -centred[i + 1]);
        energy += double(centred[i]) * centred[i];
    }
    CHECK(energy > 1e-3);
    // Away from the centre the channels differ in more than sign.
    engine.Reset(kSeed);
    engine.JumpModel(5);
    engine.JumpControl(Control::Position, 0.9f);
    const auto wide = Play(engine, chord, 600);
    std::size_t unequal = 0;
    for(std::size_t i = 0; i < wide.size(); i += 2)
        unequal += wide[i] != -wide[i + 1];
    CHECK(unequal > wide.size() / 8);
    // The other string model with dispersion does not invert: same strikes,
    // different result.
    engine.Reset(kSeed);
    engine.JumpModel(2);
    engine.JumpControl(Control::Position, 0.5f);
    const auto inharmonic = Play(engine, chord, 600);
    CHECK(!Same(centred, inharmonic));
    std::printf("stereo strings: right channel inverted, exact opposites at Position 0.5\n");
}

double ModalPitch(float tune, unsigned bend, unsigned note) {
    DryEngine engine;
    engine.Reset(kSeed);
    engine.JumpControl(Control::Structure, 0.25f);
    engine.JumpControl(Control::Damping, 0.7f);
    engine.JumpControl(Control::Tune, tune);
    engine.JumpPitchBend(bend);
    Score strike;
    Note(strike, 0, note, 127);
    Play(engine, strike, 200);
    const auto mono = Mono(Play(engine, {}, 800));
    const double expected =
        440.0 * std::pow(2.0, (double(note) - 69.0 + engine.PitchOffset()) / 12.0);
    double amplitude = 0.0;
    const double found = PeakFrequency(mono, expected * 0.94, expected * 1.06, expected * 0.0005,
                                       48000.0, 0, mono.size(), &amplitude);
    CHECK(amplitude > 1e-4);
    return found;
}

void TestPitch() {
    // The modal model's fundamental follows note, tune and pitch bend. (The
    // string models keep Torus' approximate tuning and are compared with the
    // reference instead, see TestDryReference.)
    const double a4 = ModalPitch(0.5f, 8192, 69);
    const double octave = ModalPitch(0.5f, 8192, 81);
    const double tuned_up = ModalPitch(0.75f, 8192, 69);   // +12 semitones
    const double tuned_down = ModalPitch(0.25f, 8192, 69); // -12 semitones
    const double bent_up = ModalPitch(0.5f, 16383, 69);    // +2 semitones
    const double bent_down = ModalPitch(0.5f, 0, 69);      // -2 semitones
    std::printf("  A4 %.2f Hz, A5 %.2f Hz, tune +12 %.2f Hz, tune -12 %.2f Hz, bend +2 %.2f Hz, "
                "bend -2 %.2f Hz\n",
                a4, octave, tuned_up, tuned_down, bent_up, bent_down);
    CHECK(std::fabs(a4 - 440.0) < 2.0);
    CHECK(std::fabs(octave / a4 - 2.0) < 0.01);
    CHECK(std::fabs(tuned_up / a4 - 2.0) < 0.01 && std::fabs(tuned_down / a4 - 0.5) < 0.01);
    CHECK(std::fabs(bent_up / a4 - std::pow(2.0, 2.0 / 12.0)) < 0.005);
    CHECK(std::fabs(bent_down / a4 - std::pow(2.0, -2.0 / 12.0)) < 0.005);

    // The limits of tune and bend: +/-24 and +/-2 semitones, continuous.
    DryEngine engine;
    engine.JumpControl(Control::Tune, 0.0f);
    engine.JumpPitchBend(0);
    CHECK(engine.PitchOffset() == -26.0f);
    engine.JumpControl(Control::Tune, 1.0f);
    engine.JumpPitchBend(16383);
    CHECK(engine.PitchOffset() == 26.0f);
    engine.JumpControl(Control::Tune, 0.5f);
    engine.JumpPitchBend(DryEngine::kBendCentre);
    CHECK(engine.PitchOffset() == 0.0f);
    engine.JumpControl(Control::Tune, 0.5f + 0.37f / 48.0f); // not snapped to semitones
    CHECK(std::fabs(engine.PitchOffset() - 0.37f) < 1e-5f);
    std::printf("pitch: modal fundamental follows note, tune (continuous, +/-24) and bend "
                "(+/-2)\n");
}

void TestTails() {
    // There is no note-off: a strike rings until the resonator has decayed.
    // With Damping at 1 the string and FM models hold their sound.
    for(unsigned model = 0; model < 6; ++model) {
        DryEngine engine;
        engine.Reset(kSeed);
        engine.JumpModel(model);
        engine.JumpControl(Control::Damping, 1.0f);
        Score strike;
        Note(strike, 0, 57, 127);
        Play(engine, strike, 4000);                        // one second
        const double early = Rms(Play(engine, {}, 4000));  // second 1..2
        Play(engine, {}, 4000 * 18);
        const auto late_samples = Play(engine, {}, 4000);  // second 20..21
        CheckBounded(late_samples);
        const double late = Rms(late_samples);

        engine.Reset(kSeed);
        engine.JumpModel(model);
        engine.JumpControl(Control::Damping, 0.0f);
        Play(engine, strike, 4000 * 3);
        const double damped = Rms(Play(engine, {}, 4000)); // second 3..4
        std::printf("  %-20s Damping 1: RMS %.5f after 1 s, %.5f after 20 s; Damping 0: %.2g "
                    "after 3 s\n",
                    kModelNames[model], early, late, damped);
        CHECK(early > 1e-4);
        CHECK(damped < early * 0.05);
        if(model != 0) // strings and FM sustain; the modal filters decay in the end
            CHECK(late > early * 0.1);
    }
    std::printf("tails: strikes ring on without note-off; Damping decides how long\n");
}

void TestNoteQueue() {
    DryEngine engine;
    float left[kQ], right[kQ];
    // Four strikes start per quantum, in the order they arrived.
    for(unsigned i = 0; i < 10; ++i)
        CHECK(engine.NoteOn(40 + i, 100));
    CHECK(engine.PendingNotes() == 10);
    engine.Render(left, right);
    CHECK(engine.TriggerCount() == 4 && engine.PendingNotes() == 6);
    for(unsigned voice = 0; voice < 4; ++voice)
        CHECK(engine.VoiceNote(voice) == 40.0f + voice);
    engine.Render(left, right);
    CHECK(engine.TriggerCount() == 8 && engine.PendingNotes() == 2);
    engine.Render(left, right);
    CHECK(engine.TriggerCount() == 10 && engine.PendingNotes() == 0);
    CHECK(engine.VoiceNote(0) == 48.0f && engine.VoiceNote(1) == 49.0f);
    CHECK(engine.VoiceNote(2) == 46.0f && engine.VoiceNote(3) == 47.0f);

    // The queue holds 256 strikes; the next one is refused and counted.
    engine.Reset();
    for(int i = 0; i < DryEngine::kNoteCapacity; ++i)
        CHECK(engine.NoteOn(i % 128, 100));
    CHECK(!engine.NoteOn(60, 100) && engine.OverflowCount() == 1);
    for(int q = 0; q < DryEngine::kNoteCapacity / DryEngine::kVoices; ++q)
        engine.Render(left, right);
    CHECK(engine.TriggerCount() == 256u && engine.PendingNotes() == 0);
    CHECK(engine.NoteOn(60, 100));

    // A model change fades out over 192 samples, switches, fades in. Strikes
    // that arrive while it fades out wait for the new model.
    engine.Reset();
    engine.SetModel(3);
    engine.Render(left, right);
    CHECK(engine.Model() == 0 && engine.DesiredModel() == 3);
    CHECK(engine.NoteOn(60, 100));
    int quanta = 1;
    while(engine.Model() != 3 && quanta < 100) {
        CHECK(engine.TriggerCount() == 0 && engine.PendingNotes() == 1);
        engine.Render(left, right);
        ++quanta;
    }
    // 192 samples of fade-out are 16 quanta; the float fade may need one more to
    // reach exactly 0, and the model changes in the quantum after that.
    std::printf("  model change: new model active in quantum %d after the request\n", quanta);
    CHECK(quanta == 17 || quanta == 18);
    CHECK(engine.TriggerCount() == 1 && engine.PendingNotes() == 0);
    std::printf("note queue: four strikes per quantum in order, 256 waiting at most, strikes "
                "wait out a model change\n");
}

void TestSmoothing() {
    // Controls approach their target and land on it exactly.
    DryEngine engine;
    float left[kQ], right[kQ];
    for(unsigned c = 0; c < kControlCount; ++c) {
        const auto control = static_cast<Control>(c);
        for(const float target : {0.0f, 1.0f, 0.3f}) {
            const float before = engine.ControlValue(control);
            engine.SetControl(control, target);
            engine.Render(left, right);
            const float after = engine.ControlValue(control); // moving, not jumping
            CHECK(after != before && after != target);
            CHECK((after > before) == (target > before));
            for(int q = 0; q < 400; ++q)
                engine.Render(left, right);
            CHECK(engine.ControlValue(control) == target);
        }
        // Out of range and not-a-number are clamped.
        engine.JumpControl(control, std::nanf(""));
        CHECK(engine.ControlValue(control) == 0.0f);
        engine.JumpControl(control, -3.0f);
        CHECK(engine.ControlValue(control) == 0.0f);
        engine.JumpControl(control, 3.0f);
        CHECK(engine.ControlValue(control) == 1.0f);
    }
    for(unsigned model = 0; model < 9; ++model) {
        engine.JumpModel(model);
        CHECK(engine.Model() == std::min(model, 5u));
    }
    std::printf("controls: 5 ms smoothing reaches the target exactly; out-of-range values "
                "clamped\n");
}

// --- The output stage, under the float options actually used ----------------

#if !defined(_M_FP_PRECISE)
#error "this test must be compiled with /fp:precise, like the engine it tests"
#endif

void TestOutputStage() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // Volume in front of the limiter: 0.8 times the master, linear up to 0.6.
    CHECK(Output(0.5f, 1.0f) == 0.4f && Output(0.5f, 0.0f) == 0.0f);
    CHECK(Output(0.75f, 1.0f) == 0.6f && Output(-0.75f, 1.0f) == -0.6f);
    CHECK(Output(0.5f, 0.5f) == 0.2f);
    // Above it a knee that never reaches 0.8, whatever comes in.
    float previous = 0.6f;
    for(float x = 0.76f; x < 1000.0f; x *= 1.07f) {
        const float y = Output(x, 1.0f);
        CHECK(y > previous && y < 0.8f && Output(-x, 1.0f) == -y);
        previous = y;
    }
    // Not-a-number and infinity are silenced, also as the master value.
    CHECK(Output(nan, 1.0f) == 0.0f && Output(inf, 1.0f) == 0.0f && Output(-inf, 1.0f) == 0.0f);
    for(const float x : {0.0f, 1.0f, -1.0f, 1e30f, -1e30f, 3e38f, inf, -inf, nan})
        for(const float master : {0.0f, 0.5f, 1.0f, 2.0f, -1.0f, inf, nan}) {
            const float y = Output(x, master);
            CHECK(std::isfinite(y) && std::fabs(y) < 0.8f);
        }
    CHECK(Unit(nan) == 0.0f && Unit(inf) == 1.0f && Unit(-inf) == 0.0f);
    std::printf("output stage (compiled with /fp:precise): linear to 0.6, below 0.8 always, "
                "not-a-number and infinity silenced\n");
}

// --- Reset and instance isolation -------------------------------------------

void TestReset() {
    const Score score = RandomScore(7, 1500);
    const auto first = PlayFresh(score, 1500);
    DryEngine engine;
    engine.Reset(kSeed);
    CHECK(Same(Play(engine, score, 1500), first));
    // After any history, a reset gives the same start again.
    Play(engine, SwitchScore(), 900);
    for(int i = 0; i < 300; ++i)
        engine.NoteOn(60, 100);
    engine.Reset(kSeed);
    CHECK(engine.PendingNotes() == 0 && engine.TriggerCount() == 0 && engine.OverflowCount() == 0);
    CHECK(engine.Model() == 0);
    CHECK(Same(Play(engine, score, 1500), first));
    // The seed is the instance's own: another seed, other noise.
    Score strings;
    Model(strings, 0, 2);
    Note(strings, 40, 60, 127);
    CHECK(!Same(PlayFresh(strings, 600, 1), PlayFresh(strings, 600, 2)));
    CHECK(Same(PlayFresh(strings, 600, 1), PlayFresh(strings, 600, 1)));
    std::printf("reset: reproducible start from a seed, nothing left of earlier notes\n");
}

void TestInstanceIsolation() {
    // A's score uses the string models, whose noise the firmware draws from
    // one generator for the whole program.
    const std::uint32_t quanta = 2500;
    Score a_score = RandomScore(11, quanta);
    a_score.insert(a_score.begin(), {0, Action::kModel, 2, 0.0f});
    const Score b_score = RandomScore(12, quanta);
    const auto alone = PlayFresh(a_score, quanta);
    CHECK(Energy(alone) > 1.0);

    // The check can fail: two firmware engines in one program do disturb each
    // other, because they share stmlib::Random.
    {
        const auto firmware_run = [&](bool with_b) {
            FirmwareSynth a, b;
            b.Reset(99);
            a.Reset(kSeed); // sets the shared generator
            std::vector<float> out;
            float l[kQ], r[kQ], bl[kQ], br[kQ];
            std::size_t next = 0, next_b = 0;
            b.SelectModel(2);
            for(std::uint32_t q = 0; q < 600; ++q) {
                for(; next < a_score.size() && a_score[next].quantum <= q; ++next)
                    Apply(a, a_score[next]);
                a.Render(l, r);
                if(with_b) {
                    for(; next_b < b_score.size() && b_score[next_b].quantum <= q; ++next_b)
                        Apply(b, b_score[next_b]);
                    b.Render(bl, br);
                }
                out.insert(out.end(), l, l + kQ);
            }
            return out;
        };
        CHECK(!Same(firmware_run(false), firmware_run(true)));
    }

    // Interleaved on one thread: B plays, is reset, destroyed and created
    // anew between A's quanta.
    {
        DryEngine a;
        a.Reset(kSeed);
        auto b = std::make_unique<DryEngine>();
        b->Reset(99);
        std::vector<float> out;
        float l[kQ], r[kQ], bl[kQ], br[kQ];
        std::size_t next = 0, next_b = 0;
        for(std::uint32_t q = 0; q < quanta; ++q) {
            for(; next < a_score.size() && a_score[next].quantum <= q; ++next)
                Apply(a, a_score[next]);
            a.Render(l, r);
            for(int i = 0; i < kQ; ++i) {
                out.push_back(l[i]);
                out.push_back(r[i]);
            }
            for(; next_b < b_score.size() && b_score[next_b].quantum <= q; ++next_b)
                Apply(*b, b_score[next_b]);
            b->Render(bl, br);
            if(q % 97 == 50)
                b->Reset(q);
            if(q % 211 == 100) {
                b.reset();
                b = std::make_unique<DryEngine>();
                b->JumpModel(q % 6);
            }
        }
        CHECK(Same(out, alone));
    }

    // In parallel: other threads create, play, reset and destroy instances
    // while A renders.
    {
        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> other_quanta{0};
        const auto churn = [&](std::uint32_t seed) {
            float l[kQ], r[kQ];
            Lcg random(seed);
            while(!stop.load()) {
                DryEngine b;
                b.Reset(random.Next());
                b.JumpModel(random.Below(6));
                for(int q = 0; q < 40 && !stop.load(); ++q) {
                    if(q % 5 == 0)
                        b.NoteOn(30 + random.Below(70), 1 + random.Below(127));
                    b.Render(l, r);
                    other_quanta.fetch_add(1);
                }
                b.Reset(random.Next());
            }
        };
        std::thread t1(churn, 1u), t2(churn, 2u), t3(churn, 3u);
        const auto parallel = PlayFresh(a_score, quanta);
        // Keep the others busy for a second run, so that on a fast machine the
        // overlap is not over before it began.
        const auto parallel_again = PlayFresh(a_score, quanta);
        stop.store(true);
        t1.join();
        t2.join();
        t3.join();
        CHECK(other_quanta.load() > 0);
        CHECK(Same(parallel, alone) && Same(parallel_again, alone));
        std::printf("  parallel: %llu quanta rendered by three other threads meanwhile\n",
                    static_cast<unsigned long long>(other_quanta.load()));
    }

    // Changing threads: A's quanta are rendered by two threads in turn, as a
    // host's worker pool or an offline render may do.
    {
        DryEngine a;
        a.Reset(kSeed);
        std::vector<float> out;
        std::size_t next = 0;
        const auto part = [&](std::uint32_t from, std::uint32_t to) {
            float l[kQ], r[kQ];
            for(std::uint32_t q = from; q < to; ++q) {
                for(; next < a_score.size() && a_score[next].quantum <= q; ++next)
                    Apply(a, a_score[next]);
                a.Render(l, r);
                for(int i = 0; i < kQ; ++i) {
                    out.push_back(l[i]);
                    out.push_back(r[i]);
                }
            }
        };
        for(std::uint32_t q = 0; q < quanta; q += 125) {
            std::thread worker(part, q, std::min(q + 125, quanta));
            worker.join();
        }
        CHECK(Same(out, alone));
    }
    std::printf("instances: A's output is the same alone, interleaved with B, beside threads "
                "that create and destroy instances, and across threads\n");
}

// --- No allocation while rendering ------------------------------------------

void TestNoAllocation() {
    DryEngine engine; // allocates here, once
    const Score score = RandomScore(5, 3000);
    float left[kQ], right[kQ];
    std::size_t next = 0;
    {
        const AllocationProbe probe;
        engine.Reset(kSeed);
        for(std::uint32_t q = 0; q < 3000; ++q) {
            for(; next < score.size() && score[next].quantum <= q; ++next)
                Apply(engine, score[next]);
            if(q % 250 == 0)
                engine.SetModel((q / 250) % 6); // every model's (re)configuration
            if(q == 2900)
                engine.Reset(1);
            engine.Render(left, right);
        }
        CHECK(probe.Count() == 0);
    }
    {
        const AllocationProbe probe; // the probe itself works
        const auto p = std::make_unique<int>(1);
        CHECK(probe.Count() == 1 && *p == 1);
    }
    std::printf("allocation: none in Reset, NoteOn, controls, model changes and Render (operator "
                "new counted); state %zu bytes per instance, firmware Synth with reverb %zu\n",
                DryEngine::StateBytes(), FirmwareSynth::StateBytes());
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // progress survives a crash
    TestDryReference();
    TestModels();
    TestStereoStrings();
    TestPitch();
    TestTails();
    TestNoteQueue();
    TestSmoothing();
    TestOutputStage();
    TestReset();
    TestInstanceIsolation();
    TestNoAllocation();
    std::printf("PASS\n");
    return 0;
}
