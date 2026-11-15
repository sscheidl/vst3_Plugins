// Tests of the DSP core (layer 1) against the original code.
//
//  1. At 32 kHz the port must be the original: same tables, and the same
//     output bit for bit over a set of patches and gate patterns. Every
//     change made for other sample rates has to reduce to the original
//     arithmetic there.
//  2. At 48 kHz and at 96 kHz the port is measured against the original at
//     32 kHz: pitch, decay time, level and spectrum. Those results are
//     printed, and written to the file named on the command line, for the
//     build report; the checks only catch gross errors, the numbers are the
//     result.
#include "test_support.h"

#include "reference_api.h"

#include "elements/dsp/part.h"
#include "elements/dsp/resonator.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <memory>
#include <string>

using elements_reference_api::OriginalPart;
using test_support::Lcg;

namespace {

// The port's part with its reverb buffer.
struct PortPart {
    std::unique_ptr<elements::Part> part = std::make_unique<elements::Part>();
    std::vector<std::uint16_t> reverb;
    float silence[elements::kMaxBlockSize] = {};

    bool Init(float rate) {
        const std::size_t size = elements::Part::ReverbBufferSize(rate);
        if(size == 0)
            return false;
        reverb.assign(size, 0);
        return part->Init(rate, reverb.data(), reverb.size());
    }
    void SetPatch(const elements_reference_api::Patch& p, float rate) {
        elements::Patch& q = *part->mutable_patch();
        q.exciter_envelope_shape = p.exciter_envelope_shape;
        q.exciter_bow_level = p.exciter_bow_level;
        q.exciter_bow_timbre = p.exciter_bow_timbre;
        q.exciter_blow_level = p.exciter_blow_level;
        q.exciter_blow_meta = p.exciter_blow_meta;
        q.exciter_blow_timbre = p.exciter_blow_timbre;
        q.exciter_strike_level = p.exciter_strike_level;
        q.exciter_strike_meta = p.exciter_strike_meta;
        q.exciter_strike_timbre = p.exciter_strike_timbre;
        q.exciter_signature = p.exciter_signature;
        q.resonator_geometry = p.resonator_geometry;
        q.resonator_brightness = p.resonator_brightness;
        q.resonator_damping = p.resonator_damping;
        q.resonator_position = p.resonator_position;
        // Given in cycles per sample at 32 kHz.
        q.resonator_modulation_frequency = p.resonator_modulation_frequency * (32000.0f / rate);
        q.resonator_modulation_offset = p.resonator_modulation_offset;
        q.reverb_diffusion = p.reverb_diffusion;
        q.reverb_lp = p.reverb_lp;
        q.space = p.space;
    }
    void Process(bool gate, float note, float modulation, float strength, float* main,
                 float* aux) {
        elements::PerformanceState state;
        state.gate = gate;
        state.note = note;
        state.modulation = modulation;
        state.strength = strength;
        part->Process(state, silence, silence, main, aux, part->block_size());
    }
};

// --- 1. The port at 32 kHz is the original ---------------------------------

void TestTables() {
    elements::SampleRate rate;
    CHECK(!rate.Init(44100.0f));
    CHECK(!rate.Init(0.0f));
    CHECK(rate.Init(32000.0f));
    CHECK(rate.reference() && rate.block_size() == 16 && rate.ratio() == 1.0f);
    const float* g = elements_reference_api::OriginalSvfG();
    const float* h = elements_reference_api::OriginalSvfH();
    const float* gain = elements_reference_api::OriginalSvfGain();
    CHECK(std::memcmp(rate.svf_g(), g, 257 * sizeof(float)) == 0);
    CHECK(std::memcmp(rate.svf_h(), h, 257 * sizeof(float)) == 0);
    CHECK(std::memcmp(rate.svf_gain(), gain, 257 * sizeof(float)) == 0);
    CHECK(rate.Samples(static_cast<size_t>(6312)) == 6312 && rate.Samples(10.0f) == 10.0f);
    CHECK(rate.Decay(0.997f) == 0.997f && rate.OnePole(0.7f) == 0.7f);
    CHECK(rate.Probability(0.01f) == 0.01f && rate.noise_gain() == 1.0f);

    // At the other rates: the same cutoff in Hz, the same pulse area.
    for(const float hz : {48000.0f, 96000.0f}) {
        const auto start = std::chrono::steady_clock::now();
        CHECK(rate.Init(hz));
        const double first = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto again = std::chrono::steady_clock::now();
        CHECK(rate.Init(hz)); // the same rate again: nothing to compute
        const double second = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - again).count();
        std::printf("tables for %.0f kHz computed in %.1f ms; a second Init() takes %.3f ms", hz / 1000.0, first, second);
        std::puts("");
        CHECK(second < 1.0);
        CHECK(!rate.reference());
        CHECK(rate.block_size() == static_cast<size_t>(hz / 2000.0f));
        for(int i = 0; i < 257; ++i) {
            const double cutoff = std::atan(static_cast<double>(g[i])) / test_support::kPi * 32000.0;
            const double expected = std::tan(test_support::kPi * cutoff / hz);
            CHECK(std::fabs(rate.svf_g()[i] - expected) <= 1e-6 * expected);
            const double eh = 1.0 / (1.0 + 2.0 * expected + expected * expected);
            CHECK(std::fabs(rate.svf_h()[i] - eh) <= 1e-6 * eh);
            CHECK(std::fabs(rate.svf_gain()[i] / hz - gain[i] / 32000.0) <= 1e-6 * gain[i] / 32000.0);
        }
        // The exciters' low-pass at this rate (matched_low_pass.h): stable,
        // unity gain at 0 Hz, and for every entry of the cutoff table close
        // to the original's response wherever that is above -40 dB. Its
        // coefficients come from a numerical fit per entry: neighbouring
        // entries must not jump apart, or turning Timbre would click.
        const auto section = [&](int index, double f) {
            const double w = 2.0 * test_support::kPi * f / hz;
            const double numerator[4] = {1.0, rate.matched_n1()[index], rate.matched_n2()[index],
                                         rate.matched_n3()[index]};
            double nr = 0.0, ni = 0.0;
            for(int k = 0; k < 4; ++k) {
                nr += numerator[k] * std::cos(k * w);
                ni -= numerator[k] * std::sin(k * w);
            }
            const double a1 = rate.matched_a1()[index], a2 = rate.matched_a2()[index];
            const double dr = 1.0 + a1 * std::cos(w) + a2 * std::cos(2.0 * w);
            const double di = -a1 * std::sin(w) - a2 * std::sin(2.0 * w);
            return rate.matched_b0()[index] * std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
        };
        const auto original = [&](int index, double f) {
            const double w = 2.0 * test_support::kPi * f / 32000.0;
            const double p = (1.0 - g[index]) / (1.0 + g[index]);
            return (1.0 - p) * std::cos(0.5 * w)
                   / std::sqrt(1.0 - 2.0 * p * std::cos(w) + p * p);
        };
        double worst = 0.0, worst_step = 0.0;
        for(int i = 0; i < 257; ++i) {
            const double a1 = rate.matched_a1()[i], a2 = rate.matched_a2()[i];
            CHECK(std::fabs(a2) < 1.0 && std::fabs(a1) < 1.0 + a2); // poles inside the unit circle
            CHECK(std::fabs(section(i, 0.0) - 1.0) < 1e-4);
            for(double f = 100.0; f <= 14000.0; f += 100.0) {
                const double target = 40.0 * std::log10(original(i, f)); // both sections
                if(target < -40.0)
                    continue;
                const double deviation = std::fabs(40.0 * std::log10(section(i, f)) - target);
                worst = std::max(worst, deviation);
                if(i > 0) {
                    const double step = std::fabs(40.0 * std::log10(section(i, f) / section(i - 1, f))
                                                  - 40.0 * std::log10(original(i, f) / original(i - 1, f)));
                    worst_step = std::max(worst_step, step);
                }
            }
        }
        std::printf("exciter low-pass at %.0f kHz: within %.2f dB of the original's response over "
                    "all 257 cutoffs (100 Hz to 14 kHz, where above -40 dB); neighbouring cutoffs "
                    "within %.2f dB of the original's step\n",
                    hz / 1000.0, worst, worst_step);
        CHECK(worst < 1.5);
        CHECK(worst_step < 1.0);
        // Half a millisecond per block; time constants kept.
        CHECK(std::fabs(std::pow(static_cast<double>(rate.Decay(0.997f)), hz / 32000.0) - 0.997)
              < 1e-6);
        CHECK(std::fabs(std::pow(1.0 - rate.OnePole(0.7f), hz / 32000.0) - 0.3) < 1e-6);
    }
    std::printf("tables: identical at 32 kHz, same cutoffs and pulse areas at 48 and 96 kHz\n");
}

elements_reference_api::Patch RandomPatch(Lcg& random, const elements_reference_api::Patch& base) {
    elements_reference_api::Patch p = base;
    p.exciter_envelope_shape = random.Unit();
    p.exciter_bow_level = random.Below(3) == 0 ? random.Unit() : 0.0f;
    p.exciter_bow_timbre = random.Unit() * 0.9995f;
    p.exciter_blow_level = random.Below(3) == 0 ? random.Unit() : 0.0f;
    p.exciter_blow_meta = random.Unit() * 0.9995f;
    p.exciter_blow_timbre = random.Unit() * 0.9995f;
    p.exciter_strike_level = random.Below(4) != 0 ? random.Unit() : 0.0f;
    p.exciter_strike_meta = random.Unit() * 0.9995f;
    p.exciter_strike_timbre = random.Unit() * 0.995f;
    p.exciter_signature = random.Unit() * 0.875f;
    p.resonator_geometry = random.Unit() * 0.9995f;
    p.resonator_brightness = random.Unit() * 0.9995f;
    p.resonator_damping = random.Unit() * 0.9995f;
    p.resonator_position = random.Unit() * 0.9995f;
    p.resonator_modulation_frequency = (0.4f + 0.8f * random.Unit()) / 32000.0f;
    p.resonator_modulation_offset = 0.05f + 0.1f * random.Unit();
    p.reverb_diffusion = 0.55f + 0.15f * random.Unit();
    p.reverb_lp = 0.7f + 0.2f * random.Unit();
    // Now and then beyond 1.75, where the reverb freezes.
    p.space = random.Below(8) == 0 ? 1.8f + 0.2f * random.Unit() : random.Unit();
    return p;
}

void TestBitExactAt32k() {
    OriginalPart original;
    PortPart port;
    CHECK(port.Init(32000.0f));
    CHECK(port.part->block_size() == 16);

    // The defaults of Init().
    const elements_reference_api::Patch defaults = original.GetPatch();
    {
        const elements::Patch& q = *port.part->mutable_patch();
        CHECK(q.exciter_strike_level == defaults.exciter_strike_level);
        CHECK(q.resonator_modulation_frequency == defaults.resonator_modulation_frequency);
        CHECK(q.space == defaults.space && q.reverb_lp == defaults.reverb_lp);
    }

    Lcg random(20261114u);
    // An unoptimized build is some twenty times slower: it compares less.
#ifdef _DEBUG
    const int kScenarios = 8;
#else
    const int kScenarios = 48;
#endif
    const int kBlocks = 4000; // 2 s each
    std::uint64_t compared = 0;
    double loudest = 0.0;
    for(int scenario = 0; scenario < kScenarios; ++scenario) {
        original.Reset();
        CHECK(port.Init(32000.0f));
        // Scenario 0 plays the defaults; the others a random patch that
        // changes to another one half way, as a player would turn the pots.
        elements_reference_api::Patch patch = scenario == 0 ? defaults : RandomPatch(random, defaults);
        const elements_reference_api::Patch second = RandomPatch(random, defaults);
        const int period = 200 + static_cast<int>(random.Below(1800));
        const int open = 1 + static_cast<int>(random.Below(static_cast<std::uint32_t>(period)));
        float note = 24.0f + static_cast<float>(random.Below(72));
        float strength = random.Unit();
        for(int block = 0; block < kBlocks; ++block) {
            if(block == kBlocks / 2)
                patch = second;
            if(block % period == 0 && block > 0) {
                note = 12.0f + static_cast<float>(random.Below(96)) + random.Unit();
                strength = random.Unit();
            }
            const bool gate = block % period < open;
            const float modulation =
                scenario % 3 == 0 ? 2.0f * std::sin(0.01f * static_cast<float>(block)) : 0.0f;
            original.SetPatch(patch);
            port.SetPatch(patch, 32000.0f);
            float main_a[16], aux_a[16], main_b[16], aux_b[16];
            original.Process(gate, note, modulation, strength, main_a, aux_a);
            port.Process(gate, note, modulation, strength, main_b, aux_b);
            if(std::memcmp(main_a, main_b, sizeof(main_a)) != 0
               || std::memcmp(aux_a, aux_b, sizeof(aux_a)) != 0) {
                for(int i = 0; i < 16; ++i)
                    if(main_a[i] != main_b[i] || aux_a[i] != aux_b[i]) {
                        std::printf("scenario %d block %d sample %d: original %.9g / %.9g, "
                                    "port %.9g / %.9g\n",
                                    scenario, block, i, main_a[i], aux_a[i], main_b[i], aux_b[i]);
                        break;
                    }
                CHECK(!"the port at 32 kHz differs from the original");
            }
            for(int i = 0; i < 16; ++i) {
                CHECK(std::isfinite(main_a[i]) && std::isfinite(aux_a[i]));
                loudest = std::max(loudest, static_cast<double>(std::fabs(main_a[i])));
            }
            compared += 32;
        }
    }
    CHECK(loudest > 0.01); // the scenarios made sound
    std::printf("bit-exact at 32 kHz: %d scenarios, %llu samples identical (peak %.3f)\n",
                kScenarios, static_cast<unsigned long long>(compared), loudest);
}

// --- 2. The port at 48 and 96 kHz against the original at 32 kHz -----------

FILE* g_report = nullptr;

void Report(const char* format, ...) {
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    std::printf("%s\n", line);
    if(g_report)
        std::fprintf(g_report, "%s\n", line);
}

// The answer of stmlib's Svf band-pass to one pulse, computed in double.
std::vector<double> SvfPulse(double g, double resonance, double height, int length) {
    const double r = 1.0 / resonance, h = 1.0 / (1.0 + r * g + g * g);
    double s1 = 0.0, s2 = 0.0;
    std::vector<double> out(static_cast<std::size_t>(length));
    for(int n = 0; n < length; ++n) {
        const double in = n == 0 ? height : 0.0;
        const double hp = (in - r * s1 - g * s1 - s2) * h;
        const double bp = g * hp + s1;
        s1 = g * hp + bp;
        const double lp = g * bp + s2;
        s2 = g * bp + lp;
        out[static_cast<std::size_t>(n)] = bp;
    }
    return out;
}

// The ringing of that band-pass after a pulse of `height`: the pole
// rho e^(j phi) as frequency in Hz and decay rate per second, from the roots
// of its denominator (1 + r g + g^2) z^2 - 2 (1 - g^2) z + (1 - r g + g^2), and
// the amplitude A of its answer A rho^n sin(n phi + phase), read from the
// computed answer.
struct Ringing {
    double hz = 0.0, decay = 0.0, amplitude = 0.0;
};

Ringing SvfRinging(double g, double resonance, double height, double rate) {
    const double r = 1.0 / resonance, a0 = 1.0 + r * g + g * g;
    const double phi = std::atan2(g * std::sqrt(4.0 - r * r), 1.0 - g * g);
    const double log_rho2 = std::log1p(-2.0 * r * g / a0);
    const double rho = std::exp(0.5 * log_rho2);
    Ringing ringing;
    ringing.hz = phi * rate / (2.0 * test_support::kPi);
    ringing.decay = -0.5 * log_rho2 * rate;
    const int from = 5;
    const std::vector<double> x = SvfPulse(g, resonance, height, from + 2);
    const double h0 = x[from], h1 = x[from + 1];
    const double quadrature = (h1 / rho - h0 * std::cos(phi)) / std::sin(phi);
    ringing.amplitude = std::sqrt(h0 * h0 + quadrature * quadrature) / std::pow(rho, from);
    return ringing;
}

// A resonator mode at 48 and 96 kHz (Resonator::ConvertMode) against the
// original's at 32 kHz: the same frequency of its ringing, the same decay per
// second and, for a pulse of the same area, the same amplitude, at every
// resonance the resonator makes, down to 1. Also shows how far the first
// port's conversion was off at low resonance.
void TestModeConversion() {
    double worst_hz = 0.0, worst_decay = 0.0, worst_amplitude = 0.0, old_decay = 0.0;
    for(const double ratio : {1.5, 3.0}) {
        for(const float f : {0.0005f, 0.002f, 0.01f, 0.05f, 0.1f, 0.2f, 0.25f, 0.3f, 0.4f, 0.49f}) {
            for(const float q : {1.0f, 1.25f, 2.0f, 5.0f, 20.0f, 100.0f, 1000.0f, 20000.0f}) {
                const float g32 = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f);
                const elements::Resonator::Mode mode =
                    elements::Resonator::ConvertMode(g32, q, static_cast<float>(ratio));
                CHECK(mode.g > 0.0f && mode.resonance >= 0.5f && mode.gain > 0.0f);
                CHECK(std::isfinite(mode.g) && std::isfinite(mode.resonance) && std::isfinite(mode.gain));
                const Ringing original = SvfRinging(g32, q, 1.0, 32000.0);
                const Ringing port = SvfRinging(mode.g, mode.resonance, mode.gain, 32000.0 * ratio);
                worst_hz = std::max(worst_hz, std::fabs(port.hz / original.hz - 1.0));
                worst_decay = std::max(worst_decay, std::fabs(port.decay / original.decay - 1.0));
                // A pulse of 1 at the higher rate has 1 / ratio of the area.
                worst_amplitude = std::max(
                    worst_amplitude, std::fabs(port.amplitude * ratio / original.amplitude - 1.0));
                // The first port: angle kept, Q scaled by k.
                const double g_old = std::tan(std::atan(static_cast<double>(g32)) / ratio);
                const double k = ratio * (g_old * (1.0 + g32 * g32)) / (g32 * (1.0 + g_old * g_old));
                const Ringing old = SvfRinging(g_old, q * k, 1.0 / k, 32000.0 * ratio);
                old_decay = std::max(old_decay, std::fabs(old.decay / original.decay - 1.0));
            }
        }
    }
    // The case the review found (EL-R04): T60 0.392982 ms at 32 kHz.
    const float g32 = 1.00083756f;
    const elements::Resonator::Mode mode = elements::Resonator::ConvertMode(g32, 1.0f, 1.5f);
    const Ringing original = SvfRinging(g32, 1.0, 1.0, 32000.0);
    const Ringing port = SvfRinging(mode.g, mode.resonance, mode.gain, 48000.0);
    const double t60_original = 1000.0 * std::log(1000.0) / original.decay;
    const double t60_port = 1000.0 * std::log(1000.0) / port.decay;
    CHECK(std::fabs(t60_original - 0.392982) < 1e-4);
    CHECK(std::fabs(t60_port / t60_original - 1.0) < 1e-4);

    Report("");
    Report("### A single resonator mode (Resonator::ConvertMode)");
    Report("");
    Report("Frequency and decay of the ringing and its amplitude after a pulse of the");
    Report("same area, port at 48 and 96 kHz against the original at 32 kHz; 10");
    Report("frequencies from 16 Hz to 15.7 kHz, resonance 1 to 20000.");
    Report("");
    Report("| | largest relative deviation |");
    Report("| --- | --- |");
    Report("| Frequency | %.1e |", worst_hz);
    Report("| Decay rate | %.1e |", worst_decay);
    Report("| Amplitude | %.1e |", worst_amplitude);
    Report("| Decay rate, conversion of the first commit | %.1e |", old_decay);
    Report("");
    Report("The mode of review finding EL-R04 (resonance 1 at 8 kHz): T60 %.6f ms at", t60_original);
    Report("32 kHz, %.6f ms at 48 kHz.", t60_port);
    // Float precision: the conversion itself is exact.
    CHECK(worst_hz < 1e-5 && worst_decay < 1e-5 && worst_amplitude < 1e-5);
}

// The filters a resonator starts with (Svf::Init(): 0.01 of the rate,
// resonance 100) sound in the first block for the modes above the 25th that
// are not computed until the second (EL-R05). At other rates they must be the
// 32 kHz start filter converted like any other mode, which TestModeConversion
// shows to ring as at 32 kHz. Two resonators that differ only by mode 27
// (index 26) leave that mode alone in the difference of their outputs.
void TestStartFilters() {
    const float g_start = stmlib::OnePole::tan<stmlib::FREQUENCY_DIRTY>(0.01f);
    double scale = 0.0; // the position's weight of that mode, read at 32 kHz
    double worst = 0.0, unconverted = 0.0;
    for(const float hz : {32000.0f, 48000.0f, 96000.0f}) {
        elements::SampleRate rate;
        CHECK(rate.Init(hz));
        const std::size_t block = rate.block_size();
        std::vector<float> center[2];
        for(int which = 0; which < 2; ++which) {
            auto resonator = std::make_unique<elements::Resonator>();
            resonator->Init(&rate);
            resonator->set_frequency(0.002f);
            resonator->set_geometry(0.0f);
            resonator->set_position(0.0f);
            resonator->set_resolution(26 + static_cast<std::size_t>(which));
            std::vector<float> bow(block, 0.0f), in(block, 0.0f), sides(block);
            in[0] = 1.0f;
            center[which].assign(block, 0.0f);
            resonator->Process(bow.data(), in.data(), center[which].data(), sides.data(), block);
        }
        const elements::Resonator::Mode mode =
            hz == 32000.0f ? elements::Resonator::Mode{g_start, 100.0f, 1.0f}
                           : elements::Resonator::ConvertMode(g_start, 100.0f, hz / 32000.0f);
        // The resonator scales its input by 0.125.
        const std::vector<double> expected =
            SvfPulse(mode.g, mode.resonance, 0.125 * mode.gain, static_cast<int>(block));
        const std::vector<double> as_init = SvfPulse(g_start, 100.0, 0.125, static_cast<int>(block));
        if(hz == 32000.0f)
            scale = (static_cast<double>(center[1][0]) - center[0][0]) / expected[0];
        double peak = 0.0, deviation = 0.0, from_init = 0.0;
        for(std::size_t i = 0; i < block; ++i) {
            const double difference = static_cast<double>(center[1][i]) - center[0][i];
            peak = std::max(peak, std::fabs(scale * expected[i]));
            deviation = std::max(deviation, std::fabs(difference - scale * expected[i]));
            from_init = std::max(from_init, std::fabs(difference - scale * as_init[i]));
        }
        worst = std::max(worst, deviation / peak);
        if(hz != 32000.0f)
            unconverted = std::max(unconverted, from_init / peak);
    }
    Report("");
    Report("The start filter of a mode not yet computed in the first block (EL-R05): within");
    Report("%.1e of the converted 32 kHz start filter at 48 and 96 kHz; the unconverted", worst);
    Report("filter of the first commit would be %.0f %% off.", 100.0 * unconverted);
    CHECK(worst < 1e-3);
    CHECK(unconverted > 0.1); // the comparison can tell the two apart
}

struct Take {
    double rate = 0.0;
    std::vector<float> main, aux; // the hardware's right and left output
};

// A patch without reverb and stereo spread: `main` is the resonator alone,
// `aux` the exciter signal alone.
elements_reference_api::Patch DryPatch() {
    OriginalPart original;
    elements_reference_api::Patch p = original.GetPatch();
    p.space = 0.0f;
    return p;
}

// Plays one note: the gate opens at 0 and closes after `gate_seconds`.
// rate 0 renders the original code, at its 32 kHz. Before the note the part
// runs for 10 ms with the gate closed and is not recorded: the DSP glides to
// its strength and position over its first block after Init(), and a note
// that began in that very block would be measured on the way there.
constexpr int kPreRollBlocks = 20;

Take Play(double rate, const elements_reference_api::Patch& patch, float note, float strength,
          double gate_seconds, double seconds, std::uint32_t seed = 0x21) {
    Take take;
    take.rate = rate == 0.0 ? 32000.0 : rate;
    const int block = static_cast<int>(take.rate / 2000.0);
    const int blocks = static_cast<int>(seconds * 2000.0);
    const int gate_blocks = static_cast<int>(gate_seconds * 2000.0);
    take.main.resize(static_cast<std::size_t>(blocks) * block);
    take.aux.resize(take.main.size());
    if(rate == 0.0) {
        OriginalPart original;
        original.Reset(seed);
        original.SetPatch(patch);
        float skipped_main[16], skipped_aux[16];
        for(int b = 0; b < kPreRollBlocks; ++b)
            original.Process(false, note, 0.0f, strength, skipped_main, skipped_aux);
        for(int b = 0; b < blocks; ++b)
            original.Process(b < gate_blocks, note, 0.0f, strength, take.main.data() + b * 16,
                             take.aux.data() + b * 16);
    } else {
        PortPart port;
        CHECK(port.Init(static_cast<float>(rate)));
        port.part->mutable_rng()->Seed(seed);
        port.SetPatch(patch, static_cast<float>(rate));
        float skipped_main[elements::kMaxBlockSize], skipped_aux[elements::kMaxBlockSize];
        for(int b = 0; b < kPreRollBlocks; ++b)
            port.Process(false, note, 0.0f, strength, skipped_main, skipped_aux);
        for(int b = 0; b < blocks; ++b)
            port.Process(b < gate_blocks, note, 0.0f, strength, take.main.data() + b * block,
                         take.aux.data() + b * block);
    }
    for(std::size_t i = 0; i < take.main.size(); ++i)
        CHECK(std::isfinite(take.main[i]) && std::isfinite(take.aux[i]));
    return take;
}

void Fft(std::vector<double>& re, std::vector<double>& im) {
    const std::size_t n = re.size();
    for(std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for(; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if(i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for(std::size_t length = 2; length <= n; length <<= 1) {
        const double angle = -2.0 * test_support::kPi / static_cast<double>(length);
        const double wr = std::cos(angle), wi = std::sin(angle);
        for(std::size_t start = 0; start < n; start += length) {
            double cr = 1.0, ci = 0.0;
            for(std::size_t k = 0; k < length / 2; ++k) {
                const std::size_t a = start + k, b = a + length / 2;
                const double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
                const double next = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = next;
            }
        }
    }
}

// Mean square of the part of x[from, to) that lies in each of the bands
// between consecutive `edges` (Hz), from a windowed spectrum. The same
// stretch of time at two sample rates gives comparable numbers.
std::vector<double> BandPower(const std::vector<float>& x, double rate, double from_seconds,
                              double to_seconds, const std::vector<double>& edges) {
    const std::size_t from = static_cast<std::size_t>(from_seconds * rate);
    const std::size_t to = std::min(x.size(), static_cast<std::size_t>(to_seconds * rate));
    const std::size_t length = to - from;
    std::size_t n = 1;
    while(n < length)
        n <<= 1;
    std::vector<double> re(n, 0.0), im(n, 0.0);
    double window_power = 0.0;
    for(std::size_t i = 0; i < length; ++i) {
        // Flat with raised-cosine ends of a tenth each; a stretch that starts
        // at 0 starts with the note and keeps its attack.
        const double position = (i + 0.5) / static_cast<double>(length);
        double w = 1.0;
        if(position > 0.9)
            w = 0.5 + 0.5 * std::cos(test_support::kPi * (position - 0.9) / 0.1);
        else if(position < 0.1 && from > 0)
            w = 0.5 - 0.5 * std::cos(test_support::kPi * position / 0.1);
        re[i] = x[from + i] * w;
        window_power += w * w;
    }
    Fft(re, im);
    std::vector<double> bands(edges.size() - 1, 0.0);
    for(std::size_t k = 1; k < n / 2; ++k) {
        const double hz = static_cast<double>(k) * rate / static_cast<double>(n);
        for(std::size_t b = 0; b + 1 < edges.size(); ++b)
            if(hz >= edges[b] && hz < edges[b + 1])
                bands[b] += 2.0 * (re[k] * re[k] + im[k] * im[k]) / (static_cast<double>(n) * window_power);
    }
    return bands;
}

double Db(double power) {
    return 10.0 * std::log10(power > 1e-30 ? power : 1e-30);
}

// The frequency of the strongest component near `expected`, in Hz.
double Partial(const Take& take, double expected, double from_seconds, double to_seconds) {
    const std::size_t from = static_cast<std::size_t>(from_seconds * take.rate);
    const std::size_t to = static_cast<std::size_t>(to_seconds * take.rate);
    double hz = test_support::PeakFrequency(take.main, expected * 0.97, expected * 1.03,
                                            expected * 0.001, take.rate, from, to);
    hz = test_support::PeakFrequency(take.main, hz - expected * 0.001, hz + expected * 0.001,
                                     expected * 0.00002, take.rate, from, to);
    return hz;
}

double Cents(double hz, double reference) {
    return 1200.0 * std::log2(hz / reference);
}

// Seconds per 60 dB of decay, from a line fitted to the level (dB over time,
// windows of 20 ms) between 5 and 35 dB below its maximum.
double DecayTime(const std::vector<float>& x, double rate) {
    const std::size_t window = static_cast<std::size_t>(0.02 * rate);
    std::vector<double> level;
    for(std::size_t start = 0; start + window <= x.size(); start += window)
        level.push_back(Db(test_support::Energy(x, start, start + window) / window));
    std::size_t peak = 0;
    for(std::size_t i = 0; i < level.size(); ++i)
        if(level[i] > level[peak])
            peak = i;
    double n = 0, st = 0, sl = 0, stt = 0, stl = 0;
    for(std::size_t i = peak; i < level.size(); ++i) {
        const double below = level[peak] - level[i];
        if(below > 35.0)
            break;
        if(below < 5.0)
            continue;
        const double t = 0.02 * static_cast<double>(i);
        n += 1;
        st += t;
        sl += level[i];
        stt += t * t;
        stl += t * level[i];
    }
    CHECK(n >= 4); // the take is long enough to show 35 dB of decay
    const double slope = (n * stl - st * sl) / (n * stt - st * st); // dB per second
    CHECK(slope < 0.0);
    return -60.0 / slope;
}

const double kRates[] = {48000.0, 96000.0};

void MeasurePitch() {
    Report("");
    Report("### Pitch of the fundamental (struck, cents from equal temperament)");
    Report("");
    Report("| MIDI note | Hz | original 32 kHz | port 48 kHz | port 96 kHz |");
    Report("| --- | --- | --- | --- | --- |");
    elements_reference_api::Patch patch = DryPatch();
    patch.resonator_damping = 0.6f;
    for(const int note : {36, 48, 60, 69, 72, 84, 96, 108}) {
        const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        // Long enough to resolve a cent at this pitch.
        const double seconds = note < 60 ? 2.0 : 1.0;
        const double c0 = Cents(Partial(Play(0.0, patch, static_cast<float>(note), 0.5f, 0.01, seconds),
                                        hz, 0.05, seconds), hz);
        double c[2];
        for(int r = 0; r < 2; ++r) {
            c[r] = Cents(Partial(Play(kRates[r], patch, static_cast<float>(note), 0.5f, 0.01, seconds),
                                 hz, 0.05, seconds), hz);
            // In tune, and no further from the ideal than the original is.
            CHECK(std::fabs(c[r]) < 3.0);
            CHECK(std::fabs(c[r]) < std::fabs(c0) + 1.0);
        }
        Report("| %d | %.2f | %+.2f | %+.2f | %+.2f |", note, hz, c0, c[0], c[1]);
    }
}

// Seconds per 60 dB of decay of the component at `hz`, from a line fitted to
// its level in overlapping windows, between 5 and 35 dB below its maximum.
// The window must be long enough to tell the component from its neighbours.
// Negative if the component is gone too quickly to measure.
double PartialDecayTime(const Take& take, double hz, double window_seconds) {
    const std::size_t window = static_cast<std::size_t>(window_seconds * take.rate);
    const std::size_t hop = window / 2;
    std::vector<double> level;
    for(std::size_t start = 0; start + window <= take.main.size(); start += hop)
        level.push_back(test_support::Decibel(
            test_support::ToneAmplitude(take.main, hz, take.rate, start, start + window)));
    std::size_t peak = 0;
    for(std::size_t i = 0; i < level.size(); ++i)
        if(level[i] > level[peak])
            peak = i;
    double n = 0, st = 0, sl = 0, stt = 0, stl = 0;
    for(std::size_t i = peak; i < level.size(); ++i) {
        const double below = level[peak] - level[i];
        if(below > 35.0)
            break;
        if(below < 5.0)
            continue;
        const double t = 0.5 * window_seconds * static_cast<double>(i);
        n += 1;
        st += t;
        sl += level[i];
        stt += t * t;
        stl += t * level[i];
    }
    if(n < 4)
        return -1.0;
    const double slope = (n * stl - st * sl) / (n * stt - st * st);
    CHECK(slope < 0.0);
    return -60.0 / slope;
}

// The strongest component of the original between two frequencies.
double StrongestPartial(const Take& take, double low, double high) {
    const std::size_t to = static_cast<std::size_t>(0.3 * take.rate);
    double hz = test_support::PeakFrequency(take.main, low, high, 2.0, take.rate, 0, to);
    return test_support::PeakFrequency(take.main, hz - 3.0, hz + 3.0, 0.05, take.rate, 0, to);
}

void MeasureDecay() {
    Report("");
    Report("### Decay time of a struck note (seconds per 60 dB)");
    Report("");
    Report("Of the whole sound, of its fundamental, and of one high partial: the strongest");
    Report("one the original has between 5 and 7 kHz. \"Cents\" is how far that partial of");
    Report("the port lies from the original's. The patch is a bright one (Brightness and");
    Report("strike Timbre 0.8), so that the high partial can be followed.");
    Report("");
    Report("| MIDI note | Damping | Rate | Whole sound | ratio | Fundamental | ratio | High partial | Hz | cents | ratio |");
    Report("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |");
    for(const int note : {57, 69, 84}) {
        for(const float damping : {0.25f, 0.5f, 0.75f}) {
            elements_reference_api::Patch patch = DryPatch();
            patch.resonator_damping = damping;
            patch.resonator_brightness = 0.8f; // so that the high partial lasts
            patch.exciter_strike_timbre = 0.8f;
            const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const double seconds = damping > 0.7f ? 14.0 : (damping > 0.4f ? 4.0 : 1.5);
            const Take original = Play(0.0, patch, static_cast<float>(note), 0.5f, 0.01, seconds);
            const double whole0 = DecayTime(original.main, 32000.0);
            // Windows of six periods of the fundamental separate the partials.
            const double window = 6.0 / f0;
            const double fundamental0 = PartialDecayTime(original, f0, window);
            CHECK(fundamental0 > 0.0);
            const double high_hz = StrongestPartial(original, 5000.0, 7000.0);
            const double high0 = PartialDecayTime(original, high_hz, window);
            char high_text[32] = "too short";
            if(high0 > 0.0)
                std::snprintf(high_text, sizeof(high_text), "%.3f", high0);
            Report("| %d | %.2f | original 32 kHz | %.3f | | %.3f | | %s | %.1f | | |", note, damping,
                   whole0, fundamental0, high_text, high_hz);
            for(int r = 0; r < 2; ++r) {
                const Take port = Play(kRates[r], patch, static_cast<float>(note), 0.5f, 0.01, seconds);
                const double whole = DecayTime(port.main, kRates[r]);
                const double fundamental = PartialDecayTime(port, f0, window);
                const double port_hz = StrongestPartial(port, high_hz - 60.0, high_hz + 60.0);
                const double high = PartialDecayTime(port, port_hz, window);
                const double cents = Cents(port_hz, high_hz);
                char ratio_text[32] = ".";
                std::snprintf(high_text, sizeof(high_text), "too short");
                if(high0 > 0.0 && high > 0.0) {
                    std::snprintf(high_text, sizeof(high_text), "%.3f", high);
                    std::snprintf(ratio_text, sizeof(ratio_text), "%.3f", high / high0);
                }
                Report("| | | port %.0f kHz | %.3f | %.3f | %.3f | %.3f | %s | %.1f | %+.1f | %s |",
                       kRates[r] / 1000.0, whole, whole / whole0, fundamental,
                       fundamental / fundamental0, high_text, port_hz, cents, ratio_text);
                // Every mode rings as long as at 32 kHz, and where it rang.
                CHECK(std::fabs(fundamental / fundamental0 - 1.0) < 0.05);
                CHECK((high0 > 0.0) == (high > 0.0));
                if(high0 > 0.0)
                    CHECK(std::fabs(high / high0 - 1.0) < 0.05);
                CHECK(std::fabs(cents) < 3.0);
                // The whole sound is a mix of partials; a small difference in
                // their levels moves the fitted decay more than any of them.
                CHECK(std::fabs(whole / whole0 - 1.0) < 0.10);
            }
        }
    }
}

const std::vector<double> kBandEdges = {20,   125,  250,  500,   1000,  2000,
                                        4000, 8000, 12000, 15500, 24000, 48000};
const char* const kBandNames[] = {"20-125", "125-250", "250-500", "0.5-1k", "1-2k", "2-4k",
                                  "4-8k",   "8-12k",   "12-15.5k", "15.5-24k", "24-48k"};
constexpr int kBands = 11;
constexpr int kSharedBands = 9; // the bands below the original's 16 kHz

struct Case {
    const char* name;
    elements_reference_api::Patch patch;
    float note;
    double gate_seconds; // how long the key is held
    double from, to;     // the stretch that is analysed
    bool random;         // driven by the random generator: statistics, not waveforms
    bool aliased = false; // the original's sample playback aliases heavily here
    // At 96 kHz a very bright pulse is three times as high and as narrow as at
    // 32 kHz, and the exciter's filter leaves it content far above 16 kHz.
    // The resonator does not respond to that; the soft limiter in front of
    // the left output, where the exciter signal is heard at SPACE 0, does.
    // Known, reported, and one reason why 96 kHz is on hold.
    bool limited_at_96k = false;
};

std::vector<Case> Cases() {
    std::vector<Case> cases;
    const elements_reference_api::Patch dry = DryPatch();
    auto add = [&cases](const char* name, elements_reference_api::Patch patch, float note,
                        double gate, double from, double to, bool random) {
        cases.push_back({name, patch, note, gate, from, to, random});
    };
    elements_reference_api::Patch p = dry; // Init(): mallet, timbre 0.5
    add("strike: mallet (default patch)", p, 57.0f, 0.01, 0.0, 1.0, false);
    p = dry;
    p.exciter_strike_timbre = 0.9f;
    p.resonator_brightness = 0.9f;
    add("strike: mallet, bright", p, 69.0f, 0.01, 0.0, 1.0, false);
    cases.back().limited_at_96k = true;
    p = dry;
    p.exciter_strike_timbre = 0.2f;
    add("strike: mallet, dark, low note", p, 36.0f, 0.01, 0.0, 1.0, false);
    p = dry;
    p.exciter_strike_meta = 0.2f; // sample player
    add("strike: sample", p, 57.0f, 0.01, 0.0, 1.0, false);
    p = dry;
    p.exciter_strike_meta = 0.2f;
    p.exciter_strike_timbre = 0.8f;
    p.exciter_strike_level = 1.0f; // with the raw sample bleeding into the output
    add("strike: sample, bright, with bleed", p, 69.0f, 0.01, 0.0, 1.0, false);
    cases.back().aliased = true;
    p = dry;
    p.exciter_strike_meta = 0.7f; // plectrum
    add("strike: plectrum", p, 57.0f, 0.3, 0.0, 1.0, false);
    p = dry;
    p.exciter_strike_meta = 0.9f; // particles
    p.resonator_damping = 0.4f;
    add("strike: particles", p, 57.0f, 3.0, 0.2, 3.0, true);
    p = dry;
    p.exciter_strike_level = 0.0f;
    p.exciter_bow_level = 1.0f;
    p.exciter_envelope_shape = 0.5f;
    add("bow", p, 48.0f, 3.0, 1.0, 3.0, true);
    p.exciter_bow_timbre = 0.85f;
    add("bow, bright", p, 60.0f, 3.0, 1.0, 3.0, true);
    p = dry;
    p.exciter_strike_level = 0.0f;
    p.exciter_blow_level = 0.6f; // below the level where the tube joins in
    p.exciter_envelope_shape = 0.5f;
    add("blow", p, 57.0f, 3.0, 1.0, 3.0, true);
    p.exciter_blow_level = 1.0f; // with the tube
    p.exciter_blow_timbre = 0.7f;
    add("blow, with the tube", p, 57.0f, 3.0, 1.0, 3.0, true);
    p.exciter_blow_meta = 0.9f;
    p.exciter_blow_timbre = 0.3f;
    add("blow, with the tube, low timbre", p, 45.0f, 3.0, 1.0, 3.0, true);
    return cases;
}

constexpr int kSeeds = 8;

void MeasureSpectra(bool exciter) {
    int failures = 0;
    Report("");
    Report("### %s: level by frequency band (port minus original, dB)",
           exciter ? "Exciter signal" : "Resonator output");
    Report("");
    Report("%s without reverb. \"Level\" is the original's level over the",
           exciter ? "The summed exciters (the hardware's left output at SPACE 0)"
                   : "The resonator (the hardware's right output at SPACE 0)");
    Report("analysed stretch in dB re full scale of the DSP. A band is listed where the");
    Report("original has more than -70 dB in it; the two bands above 15.5 kHz, which the");
    Report("original cannot produce, are given as absolute levels of the port.");
    for(int r = 0; r < 2; ++r) {
        Report("");
        Report("Port at %.0f kHz:", kRates[r] / 1000.0);
        Report("");
        std::string head = "| Case | Level | Total";
        std::string rule = "| --- | --- | ---";
        for(int b = 0; b < kSharedBands; ++b) {
            head += std::string(" | ") + kBandNames[b];
            rule += " | ---";
        }
        head += " | abs. 15.5-24k | abs. 24-48k |";
        rule += " | --- | --- |";
        Report("%s", head.c_str());
        Report("%s", rule.c_str());
        for(const Case& c : Cases()) {
            // A sound driven by the random generator is a different one for
            // every start value: compared are the averages over several, and
            // the original's own scatter is given with the total.
            const int takes = c.random ? kSeeds : 1;
            std::vector<double> a(kBands, 0.0), b(kBands, 0.0);
            double sum = 0.0, squares = 0.0;
            for(int take = 0; take < takes; ++take) {
                const std::uint32_t seed = take == 0 ? 0x21u : 1000u + 77u * static_cast<std::uint32_t>(take);
                const Take original = Play(0.0, c.patch, c.note, 0.5f, c.gate_seconds, c.to, seed);
                const Take port = Play(kRates[r], c.patch, c.note, 0.5f, c.gate_seconds, c.to, seed);
                const std::vector<double> one_a =
                    BandPower(exciter ? original.aux : original.main, 32000.0, c.from, c.to, kBandEdges);
                const std::vector<double> one_b =
                    BandPower(exciter ? port.aux : port.main, kRates[r], c.from, c.to, kBandEdges);
                double level = 0.0;
                for(int band = 0; band < kBands; ++band) {
                    a[band] += one_a[band] / takes;
                    b[band] += one_b[band] / takes;
                    if(band < kSharedBands)
                        level += one_a[band];
                }
                sum += Db(level);
                squares += Db(level) * Db(level);
            }
            const double scatter =
                takes > 1 ? std::sqrt(std::max(0.0, (squares - sum * sum / takes) / (takes - 1))) : 0.0;
            double total_a = 0.0, total_b = 0.0;
            for(int band = 0; band < kSharedBands; ++band) {
                total_a += a[band];
                total_b += b[band];
            }
            CHECK(Db(total_a) > -80.0); // the case makes sound
            const double total = Db(total_b) - Db(total_a);
            // Gross errors only: a wrong gain or a detuned filter shows as
            // several dB.
            const double total_limit = c.random ? 3.0 : (c.aliased ? 4.0 : 1.0);
            const bool known = exciter && c.limited_at_96k && kRates[r] == 96000.0;
            const bool total_off = !(std::fabs(total) < total_limit);
            failures += total_off && !known;
            char text[64];
            std::string line = std::string("| ") + c.name;
            if(c.random)
                std::snprintf(text, sizeof(text), " | %.1f +/-%.1f | %+.2f%s", Db(total_a), scatter, total,
                              total_off ? "(!)" : "");
            else
                std::snprintf(text, sizeof(text), " | %.1f | %+.2f%s", Db(total_a), total,
                              total_off ? "(!)" : "");
            line += text;
            for(int band = 0; band < kSharedBands; ++band) {
                if(Db(a[band]) > -70.0) {
                    const double difference = Db(b[band]) - Db(a[band]);
                    std::snprintf(text, sizeof(text), " | %+.1f", difference);
                    // Where the original has substantial level, and below
                    // 8 kHz: above, the original's filters bend towards its
                    // 16 kHz and the two part by design.
                    if(!c.random && !c.aliased && Db(a[band]) > Db(total_a) - 20.0 && band < 7
                       && !(std::fabs(difference) < 1.5)) {
                        std::strcat(text, "(!)");
                        failures += !known;
                    }
                } else {
                    std::snprintf(text, sizeof(text), " | .");
                }
                line += text;
            }
            std::snprintf(text, sizeof(text), " | %.1f | %.1f |", Db(b[9]), Db(b[10]));
            line += text;
            Report("%s", line.c_str());
        }
    }
    CHECK(failures == 0);
}

} // namespace

int main(int argc, char** argv) {
    if(argc > 1)
        g_report = std::fopen(argv[1], "w");
    Report("## DSP core: the port against the original");
    Report("");
    Report("Written by test_elements_dsp. \"Original\" is the unchanged code of");
    Report("pichenettes/eurorack at its 32 kHz; \"port\" is core/ at the rate named.");
    Report("");
    TestTables();
    TestBitExactAt32k();
    TestModeConversion();
    TestStartFilters();
#ifdef _DEBUG
    // The measurements render some twenty minutes of sound; an unoptimized
    // build leaves them to the optimized one.
    Report("");
    Report("(Debug build: tables and bit-exact comparison only.)");
#else
    MeasurePitch();
    MeasureDecay();
    MeasureSpectra(true);
    MeasureSpectra(false);
#endif
    if(g_report)
        std::fclose(g_report);
    std::printf("PASS\n");
    return 0;
}
