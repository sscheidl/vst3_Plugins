// The waveform overview the window draws (since 0.1.4): per column the lowest and
// the highest sample of the PCM16 buffer the engine plays, over all channels.
// Made by the loader off the audio thread, once per file, and tagged with the
// file's sequence number, so that the window shows the overview of exactly the
// file the core reads (EngineDisplay::sequence). Free of VST3 types.
#pragma once

#include "looper_engine.h"

#include <cstdint>
#include <memory>

namespace messier {

struct WaveOverview {
    static constexpr std::uint32_t kMaxColumns = 2048;
    std::uint64_t sequence = 0; // FileData::sequence of the file it shows
    std::uint32_t frames = 0;   // that file's length
    std::uint32_t columns = 0;  // kMaxColumns, or frames for a shorter file
    // Since 0.2.0: the file's sample rate, for the window's seconds axis; 0 unknown.
    std::uint32_t rate = 0;
    std::uint32_t reserved = 0; // no padding: every byte that travels is set
    // Column c covers the frames [c * frames / columns, (c + 1) * frames / columns).
    std::int16_t low[kMaxColumns] = {};
    std::int16_t high[kMaxColumns] = {};
};

// The overview of `file`, or null for an empty file or when there is no memory for
// it (the file plays all the same; the window then shows no waveform). One
// allocation; never throws.
std::shared_ptr<WaveOverview> MakeOverview(const FileData& file) noexcept;

} // namespace messier
