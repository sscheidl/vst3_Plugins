// A plain interface to the pinned firmware engine in tests/reference/, compiled
// on its own with the namespace neb renamed to neb_reference, so that it lives in
// one test program with the plug-in's core without sharing a symbol. Those copies
// differ from the firmware sources only by the F42 line (GRAIN_AMP 0.90, see
// UPSTREAM.md); they have neither of the plug-in's other patches. Test-only; the
// plug-in does not contain it.
//
// This driver is written independently of the plug-in's looper engine
// (engine/looper_engine.cpp) and does what the firmware does: one fixed file
// buffer, Init() with it, and a file change through gate, acknowledgement, writing
// the buffer, publishing the new generation. Controls go in as the pot registers
// (Parameters::CC for the primaries, the secondary register as it is), notes and
// the wheel as MIDI events, Freeze and Reset as actions.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace messier_reference {

struct FileImage {
    std::vector<std::int16_t> pcm; // interleaved
    std::uint32_t frames = 0, rate = 48000;
    std::uint16_t channels = 2;
};

class FirmwareLooper {
  public:
    // `capacity` samples in the one file buffer.
    explicit FirmwareLooper(std::size_t capacity);
    ~FirmwareLooper();
    FirmwareLooper(const FirmwareLooper&) = delete;
    FirmwareLooper& operator=(const FirmwareLooper&) = delete;

    // Constructs and initialises the firmware engine with the pots at these values
    // (8 primaries, 8 secondaries), the vocoder schedule set to the common grid
    // (kScheduleCommon) at run time, and `file` (or none) as its first generation.
    void Reset(const FileImage* file, const float* primary, const float* secondary);
    void SetPrimary(unsigned index, float value);
    void SetSecondary(unsigned index, float value);
    void Freeze(bool on);
    void ResetPlayhead();
    // A MIDI message as the firmware's parser delivers it (type = status & 0xf0).
    void Midi(std::uint8_t type, std::uint8_t channel, std::uint8_t a, std::uint8_t b);
    // Starts a file change: the firmware sets the gate and waits for the engine.
    void ChangeFile(const FileImage* file);
    bool Changing() const;
    // `n` samples through Engine::Process with silent inputs, after the loader's
    // step: once the engine has acknowledged, the buffer is written and published.
    void Render(float* left, float* right, unsigned n);

    double Position() const;
    static unsigned ScheduleOfReset(); // the schedule a fresh engine has (to show the setting matters)
    static std::size_t StateBytes();

  private:
    struct Impl;
    Impl* impl_;
};

} // namespace messier_reference
