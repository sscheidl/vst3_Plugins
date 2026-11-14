// Tests of the instrument engine (layer 2) and of the streaming adapter
// (layer 3), without VST3: the voice, the controls, the three exciters, the
// resonator's parameters, the reverb, stability, and time in the adapter.
#define TEST_SUPPORT_COUNT_ALLOCATIONS
#include "test_support.h"

#include "engine.h"
#include "engine_bridge.h"
#include "stream_adapter.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <vector>

using namespace elements_synth;
using namespace test_support;

namespace {

constexpr double kRate = 48000.0;
constexpr int kQ = 24;

struct Take {
    std::vector<float> left, right;
};

// Renders `seconds` and appends to the take.
void Render(Engine& engine, Take& take, double seconds) {
    const int quanta = static_cast<int>(seconds * 2000.0 + 0.5);
    float l[Engine::kMaxQuantum], r[Engine::kMaxQuantum];
    for(int q = 0; q < quanta; ++q) {
        engine.Render(l, r);
        for(int i = 0; i < engine.Quantum(); ++i) {
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
            CHECK(std::fabs(l[i]) <= 1.0f && std::fabs(r[i]) <= 1.0f);
            take.left.push_back(l[i]);
            take.right.push_back(r[i]);
        }
    }
}

// A dry setting: no reverb, no spread; the right channel is the resonator.
void SetDry(Engine& engine) {
    engine.JumpControl(Control::Space, 0.0f);
    engine.JumpControl(Control::Velocity, 0.0f); // every note at the nominal level
}

std::size_t At(double seconds, double rate = kRate) {
    return static_cast<std::size_t>(seconds * rate);
}

double Cents(double hz, double reference) {
    return 1200.0 * std::log2(hz / reference);
}

double Fundamental(const std::vector<float>& x, double expected, double rate, double from,
                   double to) {
    double hz = PeakFrequency(x, expected * 0.97, expected * 1.03, expected * 0.001, rate,
                              At(from, rate), At(to, rate));
    return PeakFrequency(x, hz - expected * 0.001, hz + expected * 0.001, expected * 0.00002, rate,
                         At(from, rate), At(to, rate));
}

void TestPrepare() {
    Engine engine;
    CHECK(!engine.Prepared());
    CHECK(!engine.Prepare(44100.0) && !engine.Prepare(0.0) && !engine.Prepared());
    CHECK(engine.Prepare(32000.0) && engine.Quantum() == 16);
    CHECK(engine.Prepare(96000.0) && engine.Quantum() == 48);
    CHECK(engine.Prepare(48000.0) && engine.Quantum() == 24 && engine.SampleRate() == 48000.0);
    CHECK(!engine.Prepare(22050.0) && engine.SampleRate() == 48000.0); // stays as it was
    CHECK(Engine::SupportedRate(48000.0) && !Engine::SupportedRate(44100.0));
    for(unsigned i = 0; i < kControlCount; ++i)
        CHECK(engine.ControlTarget(static_cast<Control>(i)) == static_cast<float>(kControlDefaults[i]));
    // Silence until a key goes down, with every control at its default.
    Take take;
    Render(engine, take, 0.5);
    CHECK(Energy(take.left) == 0.0 && Energy(take.right) == 0.0);
    std::printf("prepare: 48 kHz in quanta of 24, 96 kHz of 48, 32 kHz of 16; other rates refused\n");
}

void TestVoice() {
    Engine engine;
    CHECK(engine.Prepare(kRate));
    float l[Engine::kMaxQuantum], r[Engine::kMaxQuantum];
    const auto step = [&] { engine.Render(l, r); return engine.Gate(); };
    CHECK(!step() && engine.SoundingNote() == -1);

    engine.NoteOn(60, 100);
    CHECK(step() && engine.SoundingNote() == 60 && engine.HeldCount() == 1);
    CHECK(std::fabs(engine.Pitch() - 60.0f) < 1e-6f);
    // A second key while the first is held: the gate closes for one quantum,
    // so that the new note strikes.
    engine.NoteOn(64, 100);
    CHECK(!step() && step() && engine.SoundingNote() == 64 && engine.HeldCount() == 2);
    // Its release hands the pitch back to the key still held, without a strike.
    engine.NoteOff(64);
    CHECK(step() && engine.SoundingNote() == 60 && step());
    // The release of a key that is not the sounding one changes nothing.
    engine.NoteOn(67, 100);
    step();
    step();
    engine.NoteOff(60);
    CHECK(step() && engine.SoundingNote() == 67 && engine.HeldCount() == 1);
    engine.NoteOff(67);
    CHECK(!step() && engine.HeldCount() == 0);
    CHECK(engine.SoundingNote() == 67); // the pitch stays for the release

    // Down and up within one quantum still strikes: one quantum of gate.
    engine.NoteOn(72, 100);
    engine.NoteOff(72);
    CHECK(step() && !step());
    // Two keys within one quantum: one strike, of the later one.
    engine.NoteOn(50, 100);
    engine.NoteOn(52, 100);
    CHECK(step() && engine.SoundingNote() == 52 && step());
    engine.AllNotesOff();
    CHECK(!step() && engine.HeldCount() == 0);

    // Nonsense is ignored.
    engine.NoteOn(128, 100);
    engine.NoteOn(60, 0);
    engine.NoteOn(60, 128);
    engine.NoteOff(200);
    CHECK(!step() && engine.SoundingNote() == 52);

    // More keys than the engine remembers: the oldest are forgotten, none hangs.
    for(unsigned note = 30; note < 30 + Engine::kHeldNotes + 4; ++note)
        engine.NoteOn(note, 90);
    CHECK(engine.HeldCount() == Engine::kHeldNotes);
    for(unsigned note = 30; note < 30 + Engine::kHeldNotes + 4; ++note)
        engine.NoteOff(note);
    step();
    CHECK(!step() && engine.HeldCount() == 0);

    // Velocity as the hardware's STRENGTH.
    CHECK(StrengthFor(127, 0.0) == 0.5 && StrengthFor(1, 0.0) == 0.5);
    CHECK(StrengthFor(127, 1.0) == 1.0 && StrengthFor(64, 1.0) > 0.5 && StrengthFor(63, 1.0) < 0.5);
    CHECK(std::fabs(StrengthFor(127, 0.5) - 0.75) < 1e-12);
    engine.JumpControl(Control::Velocity, 1.0f);
    engine.NoteOn(60, 127);
    step();
    CHECK(engine.Strength() == 1.0f);
    engine.NoteOn(60, 1);
    step();
    step();
    CHECK(std::fabs(engine.Strength() - static_cast<float>(0.5 + (1.0 / 127.0 - 0.5))) < 1e-6f);
    std::printf("voice: last-note priority, a strike per key, fall-back without one, no hanging gate\n");
}

void TestPitch() {
    Engine engine;
    CHECK(engine.Prepare(kRate));
    SetDry(engine);
    engine.JumpControl(Control::Damping, 0.6f);
    const auto played = [&](unsigned note, double expected) {
        engine.Reset();
        SetDry(engine);
        engine.JumpControl(Control::Damping, 0.6f);
        engine.NoteOn(note, 100);
        Take take;
        Render(engine, take, 1.0);
        engine.NoteOff(note);
        return Cents(Fundamental(take.right, expected, kRate, 0.05, 1.0), expected);
    };
    CHECK(std::fabs(played(69, 440.0)) < 1.0);
    CHECK(std::fabs(played(57, 220.0)) < 1.0);
    CHECK(std::fabs(played(81, 880.0)) < 1.0);
    // Coarse and Fine.
    engine.JumpControl(Control::Coarse, static_cast<float>(CoarseNormalized(12)));
    CHECK(std::fabs(played(69, 880.0)) < 1.0);
    engine.JumpControl(Control::Coarse, static_cast<float>(CoarseNormalized(-7)));
    CHECK(std::fabs(played(69, 440.0 * std::pow(2.0, -7.0 / 12.0))) < 1.0);
    engine.JumpControl(Control::Coarse, static_cast<float>(CoarseNormalized(0)));
    engine.JumpControl(Control::Fine, static_cast<float>(FineNormalized(0.5)));
    CHECK(std::fabs(played(69, 440.0 * std::pow(2.0, 0.5 / 12.0))) < 1.0);
    engine.JumpControl(Control::Fine, static_cast<float>(FineNormalized(0.0)));
    CHECK(CoarseSemitones(0.0) == -30 && CoarseSemitones(1.0) == 30 && CoarseSemitones(0.5) == 0);
    for(int semitones = -kCoarseRange; semitones <= kCoarseRange; ++semitones)
        CHECK(CoarseSemitones(CoarseNormalized(semitones)) == semitones);

    // The pitch wheel: a held note moves by the semitones given.
    engine.Reset();
    SetDry(engine);
    engine.JumpControl(Control::Damping, 0.8f);
    engine.NoteOn(69, 100);
    Take take;
    Render(engine, take, 0.5);
    engine.SetPitchBend(2.0f);
    Render(engine, take, 1.0);
    engine.SetPitchBend(-2.0f);
    Render(engine, take, 1.0);
    CHECK(std::fabs(Cents(Fundamental(take.right, 440.0, kRate, 0.05, 0.5), 440.0)) < 1.0);
    const double up = 440.0 * std::pow(2.0, 2.0 / 12.0), down = 440.0 * std::pow(2.0, -2.0 / 12.0);
    CHECK(std::fabs(Cents(Fundamental(take.right, up, kRate, 0.6, 1.5), up)) < 2.0);
    CHECK(std::fabs(Cents(Fundamental(take.right, down, kRate, 1.6, 2.5), down)) < 2.0);
    std::printf("pitch: A4 = 440 Hz within a cent; Coarse, Fine and the wheel move it as stated\n");
}

// The level of a stretch of a take, in dB re full scale.
double Level(const std::vector<float>& x, double from, double to) {
    return Decibel(Rms(x, At(from), At(to)));
}

void TestExciters() {
    Engine engine;
    CHECK(engine.Prepare(kRate));
    const auto play = [&](Control level, bool sustained) {
        engine.Reset();
        SetDry(engine);
        engine.JumpControl(Control::StrikeLevel, 0.0f);
        engine.JumpControl(Control::BowLevel, 0.0f);
        engine.JumpControl(Control::BlowLevel, 0.0f);
        engine.JumpControl(level, 0.8f);
        // 0.9: an envelope that holds while the key is down and lets go
        // quickly; lower settings release over seconds.
        engine.JumpControl(Control::Contour, sustained ? 0.9f : 1.0f);
        engine.JumpControl(Control::Damping, 0.3f);
        Take take;
        engine.NoteOn(57, 100);
        Render(engine, take, 2.0);
        engine.NoteOff(57);
        Render(engine, take, 3.0);
        return take;
    };
    // With every exciter off a key makes no sound at all.
    engine.Reset();
    SetDry(engine);
    engine.JumpControl(Control::StrikeLevel, 0.0f);
    {
        Take take;
        engine.NoteOn(57, 100);
        Render(engine, take, 1.0);
        CHECK(Level(take.right, 0.0, 1.0) < -100.0);
    }
    // Strike: sound at once, decaying while the key is still held.
    const Take strike = play(Control::StrikeLevel, false);
    CHECK(Level(strike.right, 0.0, 0.2) > -45.0);
    CHECK(Level(strike.right, 1.5, 2.0) < Level(strike.right, 0.0, 0.2) - 30.0);
    // Bow and blow: sound as long as the key is held, gone after it.
    const Take bow = play(Control::BowLevel, true);
    CHECK(Level(bow.right, 1.0, 2.0) > -40.0);
    CHECK(Level(bow.right, 4.0, 5.0) < Level(bow.right, 1.0, 2.0) - 40.0);
    const Take blow = play(Control::BlowLevel, true);
    CHECK(Level(blow.right, 1.0, 2.0) > -45.0);
    CHECK(Level(blow.right, 4.0, 5.0) < Level(blow.right, 1.0, 2.0) - 40.0);
    // Three different sounds: the bow drives the low modes into a steady
    // tone, the blown noise is spread over the spectrum.
    const double a = ToneAmplitude(bow.right, 220.0, kRate, At(1.0), At(2.0)) / Rms(bow.right, At(1.0), At(2.0));
    const double b = ToneAmplitude(blow.right, 220.0, kRate, At(1.0), At(2.0)) / Rms(blow.right, At(1.0), At(2.0));
    CHECK(a > 0.0 && b > 0.0 && std::fabs(Decibel(a) - Decibel(b)) > 1.0);
    // The exciter signal itself is on the left output at SPACE 0.
    CHECK(Level(blow.left, 1.0, 2.0) > -50.0 && Level(strike.left, 0.0, 0.05) > -50.0);
    std::printf("exciters: strike %.1f dB at the attack, bow %.1f dB and blow %.1f dB while held; "
                "each silent again after the release\n",
                Level(strike.right, 0.0, 0.2), Level(bow.right, 1.0, 2.0), Level(blow.right, 1.0, 2.0));
}

// Share of the energy above `hz`.
double HighShare(const std::vector<float>& x, double hz) {
    // A one-pole high-pass is enough to tell bright from dull.
    const double k = std::exp(-2.0 * kPi * hz / kRate);
    double low = 0.0, high_energy = 0.0, total = 0.0;
    for(const float sample : x) {
        low = k * low + (1.0 - k) * sample;
        const double high = sample - low;
        high_energy += high * high;
        total += static_cast<double>(sample) * sample;
    }
    return high_energy / total;
}

void TestResonatorParameters() {
    Engine engine;
    CHECK(engine.Prepare(kRate));
    const auto strike = [&](const std::function<void()>& set, double seconds = 2.0) {
        engine.Reset();
        SetDry(engine);
        set();
        Take take;
        engine.NoteOn(57, 100);
        Render(engine, take, 0.02);
        engine.NoteOff(57);
        Render(engine, take, seconds);
        return take.right;
    };
    // Brightness: more of the sound above 3 kHz.
    const auto dull = strike([&] { engine.JumpControl(Control::Brightness, 0.1f); });
    const auto bright = strike([&] { engine.JumpControl(Control::Brightness, 0.9f); });
    CHECK(HighShare(bright, 3000.0) > 4.0 * HighShare(dull, 3000.0));
    // Damping: a higher value rings longer (the hardware's sense).
    const auto short_one = strike([&] { engine.JumpControl(Control::Damping, 0.2f); }, 6.0);
    const auto long_one = strike([&] { engine.JumpControl(Control::Damping, 0.7f); }, 6.0);
    const double late_short = Decibel(Rms(short_one, At(1.5), At(2.0)));
    const double late_long = Decibel(Rms(long_one, At(1.5), At(2.0)));
    CHECK(late_long > late_short + 30.0);
    // Geometry: the partials move. At 0.3 they are harmonic, above stretched.
    const auto harmonic = strike([&] {
        engine.JumpControl(Control::Geometry, 0.3f);
        engine.JumpControl(Control::Damping, 0.6f);
    });
    const auto stretched = strike([&] {
        engine.JumpControl(Control::Geometry, 0.6f);
        engine.JumpControl(Control::Damping, 0.6f);
    });
    const double second_harmonic = PeakFrequency(harmonic, 400.0, 520.0, 0.5, kRate, At(0.05), At(1.0));
    const double second_stretched = PeakFrequency(stretched, 400.0, 520.0, 0.5, kRate, At(0.05), At(1.0));
    CHECK(std::fabs(Cents(second_harmonic, 440.0)) < 5.0);
    CHECK(Cents(second_stretched, 440.0) > 20.0);
    // Position: where the structure is excited decides which partials sound.
    // In the middle the even ones are missing.
    const auto edge = strike([&] {
        engine.JumpControl(Control::Geometry, 0.3f);
        engine.JumpControl(Control::Position, 0.1f);
        engine.JumpControl(Control::Damping, 0.6f);
    });
    const auto middle = strike([&] {
        engine.JumpControl(Control::Geometry, 0.3f);
        engine.JumpControl(Control::Position, 0.5f);
        engine.JumpControl(Control::Damping, 0.6f);
    });
    const auto ratio = [&](const std::vector<float>& x) {
        return Decibel(ToneAmplitude(x, 440.0, kRate, At(0.05), At(0.5))
                       / ToneAmplitude(x, 220.0, kRate, At(0.05), At(0.5)));
    };
    CHECK(ratio(edge) > ratio(middle) + 15.0);
    std::printf("resonator: Brightness, Damping, Geometry and Position change the sound as the "
                "hardware's manual says (second partial at Position 0.5: %.1f dB against %.1f dB "
                "at 0.1)\n",
                ratio(middle), ratio(edge));
}

double Correlation(const std::vector<float>& a, const std::vector<float>& b, std::size_t from,
                   std::size_t to) {
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for(std::size_t i = from; i < to; ++i) {
        ab += static_cast<double>(a[i]) * b[i];
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return ab / std::sqrt(aa * bb);
}

void TestSpace() {
    Engine engine;
    CHECK(engine.Prepare(kRate));
    const auto strike = [&](float space, bool freeze, double seconds) {
        engine.Reset();
        engine.JumpControl(Control::Velocity, 0.0f);
        engine.JumpControl(Control::Space, space);
        engine.JumpControl(Control::Damping, 0.1f); // a short resonator: what lasts is the room
        Take take;
        engine.NoteOn(60, 100);
        Render(engine, take, 0.05);
        engine.NoteOff(60);
        if(freeze)
            engine.SetControl(Control::Freeze, 1.0f);
        Render(engine, take, seconds);
        return take;
    };
    const Take dry = strike(0.0f, false, 3.0);
    const Take wet = strike(0.9f, false, 3.0);
    // Without reverb the sound is gone after a second; with it a tail remains.
    CHECK(Level(dry.right, 1.0, 1.5) < -90.0);
    CHECK(Level(wet.right, 1.0, 1.5) > Level(dry.right, 1.0, 1.5) + 30.0);
    CHECK(Level(wet.left, 1.0, 1.5) > Level(dry.left, 1.0, 1.5) + 30.0);
    // The tail decays, and the two channels are two different signals.
    CHECK(Level(wet.right, 2.5, 3.0) < Level(wet.right, 1.0, 1.5) - 10.0);
    CHECK(std::fabs(Correlation(wet.left, wet.right, At(0.5), At(1.5))) < 0.9);
    // Frozen, the reverb keeps what it holds.
    const Take frozen = strike(0.9f, true, 8.0);
    CHECK(Level(frozen.right, 7.0, 8.0) > Level(frozen.right, 2.0, 3.0) - 3.0);
    CHECK(Level(frozen.right, 7.0, 8.0) > -70.0);
    CHECK(Peak(frozen.right) <= 1.0f && Peak(frozen.left) <= 1.0f);
    std::printf("space: tail at 1 s %.1f dB with reverb, %.1f dB without; channels correlate "
                "%.2f; frozen it holds (%.1f dB after 7 s)\n",
                Level(wet.right, 1.0, 1.5), Level(dry.right, 1.0, 1.5),
                Correlation(wet.left, wet.right, At(0.5), At(1.5)), Level(frozen.right, 7.0, 8.0));
}

void TestSmoothingAndVolume() {
    Engine engine;
    CHECK(engine.Prepare(kRate));
    // A sustained sound; then the volume goes to nothing: a ramp, no step.
    engine.JumpControl(Control::StrikeLevel, 0.0f);
    engine.JumpControl(Control::BlowLevel, 0.8f);
    engine.JumpControl(Control::Contour, 0.5f);
    engine.JumpControl(Control::Space, 0.0f);
    engine.NoteOn(57, 100);
    Take take;
    Render(engine, take, 1.0);
    const double before = Level(take.left, 0.5, 1.0);
    engine.SetControl(Control::Volume, 0.0f);
    Render(engine, take, 0.004);
    CHECK(Rms(take.left, At(1.0), At(1.004)) > 0.0); // still fading
    Render(engine, take, 0.5);
    CHECK(Energy(take.left, At(1.01)) == 0.0 && Energy(take.right, At(1.01)) == 0.0);
    CHECK(before > -50.0);
    // Jumping sets it at once.
    engine.JumpControl(Control::Volume, 1.0f);
    Render(engine, take, 0.1);
    CHECK(Level(take.left, 1.52, 1.6) > before - 6.0);

    // A pot glides to its target as the hardware's do: 1 % of the way per
    // quantum, so about half way after 69 quanta.
    engine.Reset();
    engine.JumpControl(Control::Brightness, 0.0f);
    engine.SetControl(Control::Brightness, 1.0f);
    CHECK(engine.ControlTarget(Control::Brightness) == 1.0f);
    CHECK(std::fabs(1.0 - std::pow(1.0 - Engine::kPotSmoothing, 69.0) - 0.5) < 0.01);
    // Values outside 0..1 are pulled in; NaN becomes 0.
    engine.SetControl(Control::Space, 7.0f);
    CHECK(engine.ControlTarget(Control::Space) == 1.0f);
    engine.SetControl(Control::Space, std::nanf(""));
    CHECK(engine.ControlTarget(Control::Space) == 0.0f);
    std::printf("controls: the volume ramps over %d quanta, the pots glide like the hardware's\n",
                Engine::kVolumeRampQuanta);
}

void TestStability(double rate) {
    Engine engine;
    CHECK(engine.Prepare(rate));
    Lcg random(rate == 48000.0 ? 4801u : 9601u);
    float l[Engine::kMaxQuantum], r[Engine::kMaxQuantum];
    float peak = 0.0f;
    // An unoptimized build is some twenty times slower: it plays for less long.
#ifdef _DEBUG
    const int seconds = 6;
#else
    const int seconds = 90;
#endif
    const int quanta = 2000 * seconds; // everything at once
    for(int q = 0; q < quanta; ++q) {
        const std::uint32_t what = random.Below(400);
        if(what < 6)
            engine.NoteOn(random.Below(128), 1 + random.Below(127));
        else if(what < 10)
            engine.NoteOff(random.Below(128));
        else if(what < 11)
            engine.AllNotesOff();
        else if(what < 20)
            engine.SetControl(static_cast<Control>(random.Below(kControlCount)), random.Unit());
        else if(what < 22)
            engine.JumpControl(static_cast<Control>(random.Below(kControlCount)), random.Unit());
        else if(what < 24)
            engine.SetPitchBend(4.0f * random.Unit() - 2.0f);
        engine.Render(l, r);
        for(int i = 0; i < engine.Quantum(); ++i) {
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
            peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
        }
    }
    CHECK(peak <= 1.0f && peak > 0.05f);
    CHECK(engine.RestartCount() == 0); // the guard never had to act
    // Everything let go, the reverb not frozen: the sound dies away.
    engine.AllNotesOff();
    engine.SetControl(Control::Freeze, 0.0f);
    engine.SetControl(Control::Space, 0.3f);
    engine.SetControl(Control::Damping, 0.3f);
    engine.SetControl(Control::Volume, 1.0f);
    double last = 0.0;
    for(int q = 0; q < 2000 * 30; ++q) {
        engine.Render(l, r);
        if(q >= 2000 * 29) // the last second
            for(int i = 0; i < engine.Quantum(); ++i)
                last = std::fmax(last, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
    }
    CHECK(!engine.Gate() && engine.HeldCount() == 0);
    CHECK(last < 1e-4); // below -80 dB: no note hangs
    std::printf("stability at %.0f kHz: %d s of random notes and controls, peak %.3f, no "
                "non-finite sample, silent 30 s after the last release (%.1e)\n",
                rate / 1000.0, seconds, peak, last);
}

void TestGuardAndAllocations() {
    Engine engine;
    CHECK(engine.Prepare(kRate));
    float l[Engine::kMaxQuantum], r[Engine::kMaxQuantum];
    engine.NoteOn(60, 100);
    engine.Render(l, r);
    engine.InjectFaultForTest();
    engine.Render(l, r);
    for(int i = 0; i < kQ; ++i)
        CHECK(l[i] == 0.0f && r[i] == 0.0f);
    CHECK(engine.RestartCount() == 1 && engine.HeldCount() == 0 && !engine.Gate());
    // And it plays again.
    engine.NoteOn(60, 100);
    Take take;
    Render(engine, take, 0.2);
    CHECK(Energy(take.right) > 1e-4 && engine.RestartCount() == 1);

    // Nothing but Prepare() allocates.
    const AllocationProbe probe;
    engine.Reset();
    engine.NoteOn(64, 90);
    engine.SetControl(Control::Space, 0.8f);
    engine.JumpControl(Control::Geometry, 0.7f);
    for(int q = 0; q < 400; ++q)
        engine.Render(l, r);
    engine.InjectFaultForTest();
    engine.Render(l, r);
    CHECK(probe.Count() == 0);

    // Two engines are two instruments: same input, same output, no shared state.
    Engine a, b;
    CHECK(a.Prepare(kRate) && b.Prepare(kRate));
    a.JumpControl(Control::BowLevel, 0.8f);
    b.JumpControl(Control::BowLevel, 0.8f);
    a.NoteOn(50, 100);
    b.NoteOn(50, 100);
    Take ta, tb;
    for(int i = 0; i < 400; ++i) {
        Render(a, ta, 0.0005);
        if(i % 3 == 0)
            Render(b, tb, 0.0005); // b falls behind and catches up below
    }
    while(tb.left.size() < ta.left.size())
        Render(b, tb, 0.0005);
    CHECK(ta.left == tb.left && ta.right == tb.right && Energy(ta.right) > 1e-6);
    std::printf("guard: a non-finite sample restarts the DSP in silence, keys up; Render() and "
                "Reset() allocate nothing; instances do not share a random generator\n");
}

// --- The adapter --------------------------------------------------------------

struct Scored {
    std::int64_t sample;
    EngineEvent event;
};

// Plays a score through engine and adapter in blocks of the given sizes.
Take Play(double host_rate, const std::vector<Scored>& score, std::int64_t total,
          const std::vector<int>& blocks, int* latency = nullptr, std::uint64_t* allocations = nullptr) {
    Engine engine;
    CHECK(engine.Prepare(StreamAdapter::kInternalRate));
    EngineBridge bridge(engine);
    StreamAdapter adapter(bridge);
    CHECK(adapter.Prepare(host_rate));
    if(latency)
        *latency = adapter.LatencySamples();
    Take take;
    take.left.resize(static_cast<std::size_t>(total));
    take.right.resize(static_cast<std::size_t>(total));
    std::size_t next = 0, which = 0;
    std::int64_t position = 0;
    const AllocationProbe probe;
    while(position < total) {
        const int frames = static_cast<int>(
            std::min<std::int64_t>(blocks[which++ % blocks.size()], total - position));
        for(; next < score.size() && score[next].sample < position + frames; ++next)
            adapter.AddEvent(static_cast<std::int32_t>(score[next].sample - position), score[next].event);
        adapter.Process(take.left.data() + position, take.right.data() + position, frames);
        position += frames;
    }
    if(allocations)
        *allocations = probe.Count();
    CHECK(adapter.PendingEvents() == 0 || next < score.size() || total < 100);
    return take;
}

std::vector<Scored> Performance(double rate, std::int64_t total, std::uint32_t seed) {
    std::vector<Scored> score;
    Lcg random(seed);
    std::int64_t t = 0;
    while(true) {
        t += 1 + static_cast<std::int64_t>(random.Below(2400) * rate / 48000.0);
        if(t >= total)
            break;
        const std::uint32_t kind = random.Below(10);
        if(kind < 4)
            score.push_back({t, NoteOnEvent(36 + random.Below(48), 1 + random.Below(127))});
        else if(kind < 6)
            score.push_back({t, NoteOffEvent(36 + random.Below(48))});
        else if(kind < 9)
            score.push_back({t, ControlEvent(static_cast<Control>(random.Below(kControlCount)), random.Unit())});
        else
            score.push_back({t, BendEvent(4.0f * random.Unit() - 2.0f)});
    }
    return score;
}

void TestAdapter() {
    // At 48 kHz nothing is converted: the adapter's output is the engine's,
    // each event applied at the first quantum at or after its sample.
    {
        const std::int64_t total = 48000;
        const std::vector<Scored> score = {{0, NoteOnEvent(60, 100)},
                                           {25, ControlEvent(Control::Brightness, 0.9f)},
                                           {4800, NoteOffEvent(60)},
                                           {9601, NoteOnEvent(67, 80)},
                                           {20000, BendEvent(1.0f)}};
        int latency = -1;
        const Take through = Play(48000.0, score, total, {64}, &latency);
        CHECK(latency == 0);
        Engine engine;
        CHECK(engine.Prepare(48000.0));
        Take direct;
        float l[Engine::kMaxQuantum], r[Engine::kMaxQuantum];
        for(std::int64_t quantum = 0; quantum < total / kQ; ++quantum) {
            const std::int64_t begin = quantum * kQ;
            for(const Scored& scored : score) {
                // First quantum boundary at or after the event's sample.
                const std::int64_t at = (scored.sample + kQ - 1) / kQ * kQ;
                if(at != begin)
                    continue;
                EngineBridge(engine).HandleEvent(scored.event);
            }
            engine.Render(l, r);
            direct.left.insert(direct.left.end(), l, l + kQ);
            direct.right.insert(direct.right.end(), r, r + kQ);
        }
        CHECK(through.left == direct.left && through.right == direct.right);
        CHECK(Energy(through.right) > 1e-3);
    }
    // The result does not depend on how the host cuts its blocks, at any rate.
    std::uint64_t allocations = 0;
    for(const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        const std::int64_t total = static_cast<std::int64_t>(rate * 3);
        const std::vector<Scored> score = Performance(rate, total, 1234u);
        int latency = 0;
        std::uint64_t counted = 0;
        const Take reference = Play(rate, score, total, {64}, &latency, &counted);
        allocations += counted;
        CHECK((latency == 0) == (rate == 48000.0));
        CHECK(Energy(reference.left) > 1e-3 && Energy(reference.right) > 1e-3);
        for(const std::vector<int>& blocks :
            {std::vector<int>{1}, std::vector<int>{7, 333, 24, 1, 1024, 13}, std::vector<int>{2048}}) {
            if(blocks.size() == 1 && blocks[0] == 1 && rate > 48000.0)
                continue; // sample by sample is slow; twice is enough
            const Take other = Play(rate, score, total, blocks, nullptr, &counted);
            allocations += counted;
            CHECK(other.left == reference.left && other.right == reference.right);
        }
    }
    CHECK(allocations == 0);

    // Converted to another rate the pitch stays where it is.
    for(const double rate : {44100.0, 96000.0}) {
        const std::int64_t total = static_cast<std::int64_t>(rate);
        const Take take = Play(rate, {{0, ControlEvent(Control::Space, 0.0f)},
                                      {0, ControlEvent(Control::Damping, 0.6f)},
                                      {static_cast<std::int64_t>(rate * 0.3), NoteOnEvent(69, 100)}},
                               total, {64});
        CHECK(std::fabs(Cents(Fundamental(take.right, 440.0, rate, 0.4, 1.0), 440.0)) < 1.0);
    }

    // Overload: more note events than can wait. Note-ons beyond the capacity
    // are dropped; a release is never lost, so nothing hangs.
    {
        Engine engine;
        CHECK(engine.Prepare(48000.0));
        EngineBridge bridge(engine);
        StreamAdapter adapter(bridge);
        CHECK(adapter.Prepare(48000.0));
        for(int i = 0; i < StreamAdapter::kNoteCapacity; ++i)
            CHECK(adapter.AddEvent(0, NoteOnEvent(40 + i % 40, 100)));
        CHECK(!adapter.AddEvent(0, NoteOnEvent(90, 100)) && adapter.DroppedNotes() == 1);
        CHECK(!adapter.AddEvent(0, NoteOffEvent(41))); // taken as "all notes off"
        float l[256], r[256];
        adapter.Process(l, r, 256);
        CHECK(adapter.PendingEvents() == 0 && engine.HeldCount() == 0 && !engine.Gate());
        // A control index the engine does not have is refused.
        EngineEvent bad = ControlEvent(Control::Volume, 0.5f);
        bad.index = StreamAdapter::kControlKinds;
        CHECK(!adapter.AddEvent(0, bad));
    }
    std::printf("adapter: at 48 kHz the engine's own output, bit for bit; block size does not "
                "matter at 44.1, 48, 88.2, 96 and 192 kHz; pitch kept; a full queue cannot leave "
                "a note hanging\n");
}

// How much of one processor core the engine takes: a measurement for the
// build report, not a check.
void MeasureLoad() {
    for(const double rate : {48000.0, 96000.0}) {
        Engine engine;
        CHECK(engine.Prepare(rate));
        engine.JumpControl(Control::BowLevel, 0.7f);
        engine.JumpControl(Control::BlowLevel, 0.9f);
        engine.JumpControl(Control::StrikeLevel, 0.8f);
        engine.JumpControl(Control::Contour, 0.5f);
        engine.JumpControl(Control::Space, 0.8f);
        engine.JumpControl(Control::Brightness, 0.8f);
        float l[Engine::kMaxQuantum], r[Engine::kMaxQuantum];
#ifdef _DEBUG
        const double seconds = 1.0;
#else
        const double seconds = 20.0;
#endif
        const int quanta = static_cast<int>(seconds * 2000.0);
        const auto start = std::chrono::steady_clock::now();
        for(int q = 0; q < quanta; ++q) {
            if(q % 1000 == 0)
                engine.NoteOn(36 + static_cast<unsigned>(q / 1000) % 36, 100);
            engine.Render(l, r);
        }
        const double spent =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("load at %.0f kHz: %.2f %% of one core (all three exciters, reverb, 52 modes; "
                    "denormals as the host leaves them)\n",
                    rate / 1000.0, 100.0 * spent / seconds);
    }
}

} // namespace

int main() {
    MeasureLoad();
    TestPrepare();
    TestVoice();
    TestPitch();
    TestExciters();
    TestResonatorParameters();
    TestSpace();
    TestSmoothingAndVolume();
    TestGuardAndAllocations();
    TestAdapter();
    TestStability(48000.0);
    TestStability(96000.0);
    std::printf("PASS\n");
    return 0;
}
