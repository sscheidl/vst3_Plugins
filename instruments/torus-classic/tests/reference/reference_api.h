// A plain interface to the pinned Daisy firmware engine in this directory,
// reverb included, exactly as copied (see ../../UPSTREAM.md and
// ../../SHA256SUMS.upstream). Test-only; the plug-in does not contain it.
//
// The reference sources are compiled into their own library with the
// namespaces torus, stmlib and physical_modeling renamed, so they can live in
// one test program with the port without the two sharing a single symbol.
#pragma once

#include <cstddef>
#include <cstdint>

namespace torus_classic_reference {

// Indices of physical_modeling::Parameter in the firmware.
enum FirmwareParameter : unsigned {
    kFrequency = 0, kStructure, kBrightness, kDamping, kPosition, kExcitation,
    kDecay, kWet, kModel, kPolyphony, kReverbDamping, kDiffusion, kInputGain, kMaster
};

class FirmwareSynth {
  public:
    FirmwareSynth();
    ~FirmwareSynth();
    FirmwareSynth(const FirmwareSynth&) = delete;
    FirmwareSynth& operator=(const FirmwareSynth&) = delete;

    // Clears the memory, constructs and initialises the firmware Synth the way
    // the firmware's startup does, then sets the process-wide stmlib::Random
    // state of the reference library to `seed`.
    void Reset(std::uint32_t seed);
    bool NoteOn(unsigned note, unsigned velocity);
    void Set(unsigned parameter, float value);
    void SelectModel(unsigned model);
    void SetBend(unsigned raw14);
    // One 12-sample quantum through physical_modeling::Synth::Process.
    void Render(float* left, float* right);

    unsigned Model() const;
    float Current(unsigned parameter) const; // smoothed value
    std::uint32_t TriggerCount() const;
    static std::uint32_t RandomState(); // of the reference's global generator
    static std::size_t StateBytes();

  private:
    struct Impl;
    Impl* impl_;
};

} // namespace torus_classic_reference
