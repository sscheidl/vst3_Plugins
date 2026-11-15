// A plain interface to the original Elements DSP in eurorack/, exactly as
// copied from pichenettes/eurorack (see ../../UPSTREAM.md and
// ../../SHA256SUMS.upstream): 32 kHz, blocks of 16 samples. Test-only; the
// plug-in does not contain it.
//
// The reference sources are compiled into their own library with the
// namespaces elements and stmlib renamed, so they can live in one test
// program with the port without the two sharing a single symbol.
#pragma once

#include <cstddef>
#include <cstdint>

namespace elements_reference_api {

constexpr double kSampleRate = 32000.0;
constexpr int kBlockSize = 16;

// elements::Patch of the original, field for field.
struct Patch {
    float exciter_envelope_shape;
    float exciter_bow_level;
    float exciter_bow_timbre;
    float exciter_blow_level;
    float exciter_blow_meta;
    float exciter_blow_timbre;
    float exciter_strike_level;
    float exciter_strike_meta;
    float exciter_strike_timbre;
    float exciter_signature;
    float resonator_geometry;
    float resonator_brightness;
    float resonator_damping;
    float resonator_position;
    float resonator_modulation_frequency;
    float resonator_modulation_offset;
    float reverb_diffusion;
    float reverb_lp;
    float space;
};

class OriginalPart {
  public:
    OriginalPart();
    ~OriginalPart();
    OriginalPart(const OriginalPart&) = delete;
    OriginalPart& operator=(const OriginalPart&) = delete;

    // Clears the memory of the part and of its reverb buffer, as the
    // hardware's start-up does, calls Part::Init() and sets the process-wide
    // stmlib::Random of the reference library to `seed`.
    void Reset(std::uint32_t seed = 0x21);
    // The patch as Part::Init() left it, or as set last.
    Patch GetPatch() const;
    void SetPatch(const Patch& patch);
    // One block of 16 samples through Part::Process(), the external inputs
    // silent. `main` is the hardware's right output, `aux` its left one.
    void Process(bool gate, float note, float modulation, float strength, float* main,
                 float* aux);

    static std::uint32_t RandomState();
    static std::size_t StateBytes();

  private:
    struct Impl;
    Impl* impl_;
};

// The original tables that the port derives its own from, for comparison.
const float* OriginalSvfG();    // 257 values
const float* OriginalSvfH();
const float* OriginalSvfGain();

} // namespace elements_reference_api
