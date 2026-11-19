// Layer 2 of Messier: the looper engine. Drives the firmware's neb::Engine
// (core/engine.h) through its public interface only: controls as values 0..1,
// Freeze and Reset as actions, notes and the pitch wheel as MIDI events, and the
// file through the hand-over of the firmware's FileBridge (mute, acknowledge,
// swap). The firmware's panel code (Ui::Process with pots, keys and switches) is
// never called. Free of neb and VST3 types, so the layers above do not see the
// DSP sources or their macros.
//
// Component id "messier.engine", version kEngineVersion.
//
// Settings of the core, fixed here: 48 kHz, quanta of kQuantum samples, the
// vocoder on the common grid (kScheduleCommon, both channels in the same block,
// as the original instrument), no modulation matrix, no profiling. The random
// generator is the one inside neb::Engine, one per instance; Reset() constructs
// the firmware engine anew, so it starts from the same value every time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace messier {

constexpr const char* kEngineVersion = "0.1.5";

// A sample file in the core's format: interleaved PCM16, mono or stereo, at the
// file's own rate (the core converts the rate when it reads). Immutable once handed
// to the engine. frames == 0 stands for "no file": the engine is silent.
struct FileData {
    std::unique_ptr<std::int16_t[]> pcm;
    std::uint32_t frames = 0;
    std::uint32_t rate = 48000;
    std::uint16_t channels = 2;
    // Which project load brought the file: the processor's number for it, 0 for a
    // file chosen in the window. The engine does not read it.
    std::uint64_t project = 0;
    // The loader's count of finished files, window and project alike: which of two
    // files is the newer choice. The engine does not read it.
    std::uint64_t sequence = 0;
};

// Continuous controls, each 0..1 as the pot of the Daisy kit. The first eight are
// the pots' primary functions, the next eight their secondary functions (the
// firmware's SOURCE page) in the same order; two of those (InputGain, GrainFm) act
// on the audio input only and stay at 0 in 0.1.0. Volume is the plug-in's own,
// behind the core.
enum class Control : unsigned {
    Start, Speed, Size, Density, Pitch, Overlap, Blend, Window,
    Spray, InputGain, PanRandom, Displacement, PitchRandom, SizeRandom, GrainFm, Mask,
    Volume
};
constexpr unsigned kPrimaryCount = 8;
constexpr unsigned kControlCount = 17;

// What the window shows of the engine (since 0.1.4): read after a quantum from the
// state the core already holds, without changing it. Positions are in frames of the
// file the core reads, at the file's own rate. The positions are floats: above 2^24
// frames they may round to slightly beyond the file's length.
struct GrainDot {
    float position = 0; // read position
    float pan = 0;      // 0 left .. 1 right, as the core spreads it
    float phase = 0;    // of the grain's window, 0 at its start .. 1 at its end
    // Since 0.2.2 (MS-R43, MS-R45): how the grain moves, from its own step and window
    // increment, so the window need not guess it from two displays. Frames of the file
    // per second (signed; without the input's grain FM, 0.3.0) and window phase per
    // second.
    float speed = 0;
    float phase_rate = 0;
};
struct EngineDisplay {
    static constexpr unsigned kMaxGrains = 20; // both grain channels of the core
    std::uint64_t sequence = 0; // FileData::sequence of the file the core reads, 0 for none
    std::uint32_t frames = 0;   // its length; 0: no file, nothing else is meaningful
    std::uint32_t grain_count = 0;
    double loop_start = 0, loop_length = 0; // START and SIZE as the core applies them
    double playhead = 0;                    // inside [loop_start, loop_start + loop_length)
    float spray = 0;   // grains start up to this many frames either side of the playhead
    // Flags as bytes, 0 or 1: the struct travels to the controller as bytes.
    std::uint8_t frozen = 0;
    std::uint8_t swapping = 0; // a new file waits for the old one to fade out
    std::uint8_t unused[2] = {}; // no padding: every byte that travels is set
    // Since 0.2.2 (MS-R45): the playhead's speed, frames of the file per second
    // (signed, 0 while frozen), and the quanta rendered so far (wraps): a display with
    // the same count is one without audio in between.
    float head_speed = 0;
    std::uint32_t rendered = 0;
    GrainDot grains[kMaxGrains]{}; // the sounding grains, first grain_count
};

// What a restart begins with.
struct EngineSettings {
    float control[kControlCount] = {0.0f, 0.5f, 1.0f, 0.4f, 0.5f, 0.5f, 0.0f, 0.0f, // pots
                                    0, 0, 0, 0, 0, 0, 0, 0,                         // secondaries
                                    1.0f};                                          // Volume
    bool freeze = false;
    unsigned bend = 8192; // 14-bit pitch wheel, centred
};

class LooperEngine {
  public:
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kQuantum = 64; // samples per Render(): the original's control period
    static constexpr unsigned kBendCentre = 8192;
    // Volume moves at most this much per sample (from 0 to 1 in 5 ms).
    static constexpr float kVolumeStep = 1.0f / 240.0f;

    // Allocates the firmware engine. Not for the audio thread.
    LooperEngine();
    ~LooperEngine();
    LooperEngine(const LooperEngine&) = delete;
    LooperEngine& operator=(const LooperEngine&) = delete;

    // The firmware engine from scratch, as after power-on, with `file` (may be null
    // for none) installed at once, without the swap fade, and the settings applied:
    // smoothed controls start at their values. Files the engine held before are
    // released (TakeReleased). Does not allocate or lock.
    void Reset(const FileData* file, const EngineSettings& settings);

    // Targets; the core smooths the pots over 20 ms (Window over 100 ms) per quantum.
    void SetControl(Control control, float value);
    void SetFreeze(bool on);
    void TriggerReset(); // playback back to START, as the RESET button
    // MIDI: channel 0..15, note 0..127, velocity 1..127.
    void NoteOn(unsigned channel, unsigned note, unsigned velocity);
    void NoteOff(unsigned channel, unsigned note);
    void AllNotesOff();
    // One wheel for all channels: VST3 delivers the bend as a parameter.
    void SetPitchBend(unsigned raw14);

    // Starts the swap to another file (null for none): the core fades the old one out
    // (256 samples), acknowledges, the engine swaps at the next quantum and the core
    // fades the new one in, from START. A swap requested while one runs replaces the
    // file waiting for it, which is released at once.
    void SwapFile(const FileData* file);
    bool Swapping() const { return swapping_; }
    // The file playing (or fading out), and the one a running swap brings.
    const FileData* CurrentFile() const { return current_; }
    const FileData* NextFile() const { return swapping_ ? next_ : current_; }
    // A file the engine no longer reads, or null. Call until null.
    const FileData* TakeReleased();

    // One quantum. The inputs are the sidechain, for 0.2.0; 0.1.0 passes silence.
    void Render(const float* in_left, const float* in_right, float* left, float* right);

    // The display state after the last quantum. Reads only; does not allocate or lock.
    void Display(EngineDisplay& display) const;

    // For tests and diagnostics.
    double Position() const;            // playhead inside the loop, in file frames
    float EffectivePitch() const;       // grain read rate, before the file rate
    float EffectiveSpeed() const;
    bool Frozen() const;
    std::uint32_t Resets() const;       // wraps and resets of the playhead
    float Volume() const { return volume_; }
    unsigned Schedule() const;
    static std::size_t StateBytes();

  private:
    struct Impl;
    void Release(const FileData* file);

    std::unique_ptr<Impl> impl_;
    const FileData* current_ = nullptr;
    const FileData* next_ = nullptr;
    bool swapping_ = false;
    std::uint32_t rendered_ = 0; // quanta, for the display (0.2.2)
    float volume_ = 1.0f, volume_target_ = 1.0f;
    const FileData* released_[4] = {};
    int released_count_ = 0;
};

} // namespace messier
