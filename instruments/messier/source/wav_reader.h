// Reads a WAV file into the core's format (interleaved PCM16 at the file's own
// rate, mono or stereo). Free of VST3 types. Not for the audio thread: it
// allocates and reads the disk.
//
// Accepted: RIFF/WAVE with PCM 16 or 24 bit, IEEE float 32 bit, also as
// WAVE_FORMAT_EXTENSIBLE; one or two channels; 8 to 192 kHz; chunks in any order,
// unknown chunks skipped. 24-bit samples are rounded to the nearest PCM16 value as
// Daisy-Seed's Nebulae 0.3.0 does (Pcm24To16), float samples rounded after clamping
// to -1..1; no dither. Refused: anything else, a data chunk cut short, a second fmt
// or data chunk, and a file whose PCM16 would be larger than the limit.
#pragma once

#include "looper_engine.h"

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>

namespace messier {

// 256 MiB of PCM16: about 23 minutes of stereo at 48 kHz.
constexpr std::uint64_t kMaxPcmBytes = 256ull * 1024 * 1024;

// kOutOfMemory (since 0.1.5, MS-R16): a valid file that could not be decoded for
// want of memory, apart from kTooLarge, a file beyond the limit. A project load
// refuses the project on it instead of falling silent.
enum class WavError : int { kNone = 0, kMissing, kInvalid, kTooLarge, kReadError, kOutOfMemory };

struct WavResult {
    WavError error = WavError::kNone;
    // Why, in English, for the window. A fixed buffer, not a std::string: a result
    // must be possible without any allocation (MS-R01; in Debug builds even an empty
    // std::string allocates).
    char detail[96] = {};
    std::unique_ptr<FileData> file;   // set exactly when error is kNone
    std::uint16_t source_bits = 0;    // 16, 24 or 32 (float)
    bool source_float = false;
};

// One 24-bit little-endian sample rounded to the nearest PCM16 value; full scale
// clamps. Daisy-Seed instruments/granular/src/wav.h (Nebulae 0.3.0, MIT).
inline std::int16_t Pcm24To16(const std::uint8_t* p) {
    std::int32_t x = static_cast<std::int32_t>(std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8
                                               | std::uint32_t(p[2]) << 16);
    if(x & 0x800000)
        x -= 0x1000000;
    const std::int32_t y = ((x + 0x800080) >> 8) - 0x8000; // x + 0x800080 is never negative
    return static_cast<std::int16_t>(y > 32767 ? 32767 : y);
}

// Copies `text` into result.detail, cut to its size.
void SetDetail(WavResult& result, const char* text) noexcept;

// A float sample rounded to PCM16: NaN is 0, beyond full scale clamps.
std::int16_t FloatTo16(float value);

// Decodes a WAV of `size` bytes from `in`.
// Never throws: a failed allocation is the result kOutOfMemory (MS-R01, MS-R16).
WavResult DecodeWav(std::istream& in, std::uint64_t size, std::uint64_t max_pcm_bytes = kMaxPcmBytes) noexcept;

// Opens and decodes the file at a UTF-8 path.
WavResult ReadWavFile(const std::string& utf8_path, std::uint64_t max_pcm_bytes = kMaxPcmBytes) noexcept;
// Since 0.1.8: checks the file at a UTF-8 path as ReadWavFile does (headers, format,
// channels, rate, alignment, length, size limit) without reading the audio data, for
// a drag over the window. kNone means it would load as far as its headers tell; file
// stays null. A read error inside the audio data shows only when it is loaded.
WavResult ProbeWavFile(const std::string& utf8_path, std::uint64_t max_pcm_bytes = kMaxPcmBytes) noexcept;

const char* WavErrorText(WavError error);

} // namespace messier
