#include "wav_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <system_error>
#include <vector>

namespace messier {

namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;
// The last 14 bytes of KSDATAFORMAT_SUBTYPE_PCM and _IEEE_FLOAT.
constexpr std::uint8_t kSubformatTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                             0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

std::uint16_t U16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}
std::uint32_t U32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16
           | std::uint32_t(p[3]) << 24;
}

WavResult Fail(WavError error, const char* detail) noexcept {
    WavResult result;
    result.error = error;
    SetDetail(result, detail);
    return result;
}

// MS-R25: while decoding, an exception inside the stream (std::bad_alloc in its
// buffer, say) is passed on instead of becoming a silent short read: badbit joins the
// stream's exception mask, and the caller's mask comes back afterwards.
// MS-R29: the guard is complete before anything that can throw, and gives the mask
// back in any case (exceptions() sets the mask before it may throw).
class RethrowStreamErrors {
  public:
    explicit RethrowStreamErrors(std::istream& in) noexcept : in_(in), saved_(in.exceptions()) {}
    // May throw (a stream without a buffer, a mask of the caller's); the destructor
    // restores the mask then too.
    void Arm() {
        in_.clear();
        in_.exceptions(saved_ | std::ios::badbit);
    }
    ~RethrowStreamErrors() {
        try {
            in_.exceptions(saved_);
        } catch(...) {
            // The mask is set; only the check of the stream's state threw.
        }
    }
    RethrowStreamErrors(const RethrowStreamErrors&) = delete;
    RethrowStreamErrors& operator=(const RethrowStreamErrors&) = delete;

  private:
    std::istream& in_;
    std::ios::iostate saved_;
};

bool ReadAt(std::istream& in, std::uint64_t offset, void* target, std::size_t count) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    if(!in)
        return false;
    in.read(static_cast<char*>(target), static_cast<std::streamsize>(count));
    return static_cast<std::size_t>(in.gcount()) == count;
}

} // namespace

void SetDetail(WavResult& result, const char* text) noexcept {
    std::snprintf(result.detail, sizeof(result.detail), "%s", text ? text : "");
}

std::int16_t FloatTo16(float value) {
    if(!(value == value))
        return 0; // NaN
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    const long scaled = std::lround(static_cast<double>(clamped) * 32768.0);
    return static_cast<std::int16_t>(std::clamp(scaled, -32768L, 32767L));
}

const char* WavErrorText(WavError error) {
    switch(error) {
        case WavError::kNone: return "";
        case WavError::kMissing: return "file not found";
        case WavError::kInvalid: return "not a valid WAV file";
        case WavError::kTooLarge: return "too large";
        case WavError::kReadError: return "could not be read";
        case WavError::kOutOfMemory: return "out of memory";
        case WavError::kCancelled: return "cancelled";
    }
    return "";
}

namespace {

// MS-R01: an allocation that fails while reading becomes a result, never an
// exception that leaves the loader. Building the result allocates nothing. Its own
// code, not kTooLarge (MS-R16): the file may be fine, there was no memory for it.
WavResult OutOfMemory() noexcept {
    return Fail(WavError::kOutOfMemory, "while decoding");
}

WavResult DecodeUnchecked(std::istream& in, std::uint64_t size, std::uint64_t max_pcm_bytes, bool probe = false,
                          WavCancel cancel = {}) {
    RethrowStreamErrors rethrow(in);
    rethrow.Arm();
    std::uint8_t header[12];
    if(size < 12 || !ReadAt(in, 0, header, sizeof(header)))
        return Fail(WavError::kInvalid, "too short for a WAV file");
    if(std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
        return Fail(WavError::kInvalid, "no RIFF/WAVE header");

    bool have_format = false, have_data = false;
    std::uint16_t tag = 0, channels = 0, block = 0, bits = 0;
    std::uint32_t rate = 0;
    std::uint64_t data_offset = 0, data_bytes = 0;
    std::uint64_t position = 12;
    while(position + 8 <= size) {
        if(cancel())
            return Fail(WavError::kCancelled, "cancelled");
        std::uint8_t chunk[8];
        if(!ReadAt(in, position, chunk, sizeof(chunk)))
            return Fail(WavError::kReadError, "read error in the chunk list");
        const std::uint64_t length = U32(chunk + 4);
        const std::uint64_t body = position + 8;
        if(std::memcmp(chunk, "fmt ", 4) == 0) {
            if(have_format)
                return Fail(WavError::kInvalid, "two fmt chunks");
            if(length < 16 || body + length > size)
                return Fail(WavError::kInvalid, "fmt chunk too short");
            std::uint8_t fmt[40] = {};
            if(!ReadAt(in, body, fmt, static_cast<std::size_t>(std::min<std::uint64_t>(length, 40))))
                return Fail(WavError::kReadError, "read error in the fmt chunk");
            tag = U16(fmt);
            channels = U16(fmt + 2);
            rate = U32(fmt + 4);
            block = U16(fmt + 12);
            bits = U16(fmt + 14);
            if(tag == kFormatExtensible) {
                if(length < 40 || U16(fmt + 16) < 22)
                    return Fail(WavError::kInvalid, "WAVE_FORMAT_EXTENSIBLE without its extension");
                const std::uint16_t valid = U16(fmt + 18);
                if(std::memcmp(fmt + 26, kSubformatTail, sizeof(kSubformatTail)) != 0)
                    return Fail(WavError::kInvalid, "unknown sub-format");
                tag = U16(fmt + 24);
                if(valid == 0 || valid > bits)
                    return Fail(WavError::kInvalid, "bad valid-bits field");
            }
            have_format = true;
        } else if(std::memcmp(chunk, "data", 4) == 0) {
            if(have_data)
                return Fail(WavError::kInvalid, "two data chunks");
            if(body + length > size)
                return Fail(WavError::kInvalid, "the file is cut short");
            data_offset = body;
            data_bytes = length;
            have_data = true;
        }
        position = body + length + (length & 1); // chunks are padded to even length
    }
    if(!have_format || !have_data)
        return Fail(WavError::kInvalid, have_format ? "no data chunk" : "no fmt chunk");

    const bool pcm16 = tag == kFormatPcm && bits == 16;
    const bool pcm24 = tag == kFormatPcm && bits == 24;
    const bool float32 = tag == kFormatFloat && bits == 32;
    if(!pcm16 && !pcm24 && !float32)
        return Fail(WavError::kInvalid, "unsupported sample format (16 or 24 bit PCM, 32 bit float)");
    if(channels != 1 && channels != 2)
        return Fail(WavError::kInvalid, "only mono and stereo files");
    if(rate < 8000 || rate > 192000)
        return Fail(WavError::kInvalid, "sample rate outside 8 to 192 kHz");
    const unsigned bytes_per_sample = bits / 8u;
    if(block != channels * bytes_per_sample)
        return Fail(WavError::kInvalid, "bad block alignment");
    const std::uint64_t frames = data_bytes / block;
    if(frames == 0)
        return Fail(WavError::kInvalid, "no audio in the data chunk");
    const std::uint64_t pcm_bytes = frames * channels * 2u;
    if(pcm_bytes > max_pcm_bytes)
        {
        char text[64];
        std::snprintf(text, sizeof(text), "more than %llu MiB as 16-bit audio",
                      static_cast<unsigned long long>(max_pcm_bytes >> 20));
        return Fail(WavError::kTooLarge, text);
    }
    if(probe) { // ProbeWavFile: everything checked but the audio data itself
        WavResult result;
        result.source_bits = bits;
        result.source_float = float32;
        return result;
    }

    auto file = std::make_unique<FileData>();
    const std::size_t samples = static_cast<std::size_t>(frames * channels);
    file->pcm.reset(new(std::nothrow) std::int16_t[samples]);
    if(!file->pcm)
        return Fail(WavError::kOutOfMemory, "for the audio data");
    file->frames = static_cast<std::uint32_t>(frames);
    file->rate = rate;
    file->channels = channels;

    // In pieces of whole frames.
    constexpr std::size_t kPiece = 1u << 20;
    const std::size_t piece_frames = kPiece / block;
    std::vector<std::uint8_t> buffer(piece_frames * block);
    std::size_t done = 0; // samples
    in.clear();
    in.seekg(static_cast<std::streamoff>(data_offset));
    for(std::uint64_t left = frames; left > 0;) {
        if(cancel())
            return Fail(WavError::kCancelled, "cancelled");
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(left, piece_frames));
        const std::size_t bytes = count * block;
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(bytes));
        if(static_cast<std::size_t>(in.gcount()) != bytes)
            return Fail(WavError::kReadError, "read error in the audio data");
        const std::size_t n = count * channels;
        std::int16_t* target = file->pcm.get() + done;
        const std::uint8_t* p = buffer.data();
        if(pcm16) {
            for(std::size_t i = 0; i < n; ++i, p += 2)
                target[i] = static_cast<std::int16_t>(U16(p));
        } else if(pcm24) {
            for(std::size_t i = 0; i < n; ++i, p += 3)
                target[i] = Pcm24To16(p);
        } else {
            for(std::size_t i = 0; i < n; ++i, p += 4) {
                const std::uint32_t word = U32(p);
                float value;
                std::memcpy(&value, &word, sizeof(value));
                target[i] = FloatTo16(value);
            }
        }
        done += n;
        left -= count;
    }

    WavResult result;
    result.file = std::move(file);
    result.source_bits = bits;
    result.source_float = float32;
    return result;
}

WavResult ReadUnchecked(const std::string& utf8_path, std::uint64_t max_pcm_bytes, bool probe = false,
                        WavCancel cancel = {}) {
    if(utf8_path.empty())
        return Fail(WavError::kMissing, "no path");
    std::filesystem::path path;
    try {
        path = std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(utf8_path.data()), utf8_path.size()));
    } catch(const std::bad_alloc&) {
        return OutOfMemory(); // MS-R16: no memory for the path is no missing file
    } catch(...) {
        return Fail(WavError::kMissing, "not a usable path");
    }
    // MS-R25: what the file system reports is told apart: not there, no memory, any
    // other error (no access, a drive gone).
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if(status.type() == std::filesystem::file_type::not_found)
        return Fail(WavError::kMissing, "file not found");
    if(error == std::errc::not_enough_memory)
        return OutOfMemory();
    if(error)
        return Fail(WavError::kReadError, "cannot check the file");
    if(status.type() != std::filesystem::file_type::regular)
        return Fail(WavError::kMissing, "not a file");
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if(error == std::errc::not_enough_memory)
        return OutOfMemory();
    if(error)
        return Fail(WavError::kReadError, "size unknown");
    // The standard library gives no reason when a stream cannot be opened; that stays
    // "cannot open the file" (documented in the README).
    std::ifstream in(path, std::ios::binary);
    if(!in)
        return Fail(WavError::kReadError, "cannot open the file");
    return DecodeUnchecked(in, static_cast<std::uint64_t>(size), max_pcm_bytes, probe, cancel);
}

} // namespace

WavResult DecodeWav(std::istream& in, std::uint64_t size, std::uint64_t max_pcm_bytes, WavCancel cancel) noexcept {
    try {
        return DecodeUnchecked(in, size, max_pcm_bytes, false, cancel);
    } catch(const std::bad_alloc&) {
        return OutOfMemory();
    } catch(...) {
        return Fail(WavError::kReadError, "could not be read");
    }
}

WavResult ProbeWavFile(const std::string& utf8_path, std::uint64_t max_pcm_bytes) noexcept {
    try {
        return ReadUnchecked(utf8_path, max_pcm_bytes, true);
    } catch(const std::bad_alloc&) {
        return OutOfMemory();
    } catch(...) {
        return Fail(WavError::kReadError, "could not be read");
    }
}

WavResult ReadWavFile(const std::string& utf8_path, std::uint64_t max_pcm_bytes, WavCancel cancel) noexcept {
    try {
        return ReadUnchecked(utf8_path, max_pcm_bytes, false, cancel);
    } catch(const std::bad_alloc&) {
        return OutOfMemory();
    } catch(...) {
        return Fail(WavError::kReadError, "could not be read");
    }
}

} // namespace messier
