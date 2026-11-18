// Tests of the file loader: the WAV reader (every accepted format and its
// conversion, damaged, cut and unsupported files, the size limit, missing files,
// a path outside ASCII), the loader's worker and status, the newest request
// winning, and the exchange that hands files to the audio thread and back.
// Writes its files into a fresh folder under the system's temporary directory and
// removes it at the end.
#include "test_support.h"

#include "file_loader.h"
#include "wav_reader.h"

#include <atomic>
#include <chrono>
#include <new>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <process.h>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace messier;
using namespace test_support;
namespace fs = std::filesystem;

// Fault injection for MS-R01: while g_fail_from is not 0, every allocation of at
// least that many bytes, in any thread, throws std::bad_alloc. The nothrow forms
// of operator new call this one and return null.
static std::atomic<std::size_t> g_fail_from{0};
static std::atomic<std::uint64_t> g_failed{0};

void* operator new(std::size_t size) {
    const std::size_t from = g_fail_from.load(std::memory_order_relaxed);
    if(from != 0 && size >= from) {
        g_failed.fetch_add(1, std::memory_order_relaxed);
        throw std::bad_alloc();
    }
    if(void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    return operator new(size);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

namespace {

fs::path g_dir;

struct Bytes {
    std::vector<std::uint8_t> data;
    void U8(unsigned v) { data.push_back(static_cast<std::uint8_t>(v)); }
    void U16(unsigned v) {
        U8(v & 0xff);
        U8((v >> 8) & 0xff);
    }
    void U32(std::uint32_t v) {
        U16(v & 0xffff);
        U16(v >> 16);
    }
    void Tag(const char* t) {
        for(int i = 0; i < 4; ++i)
            U8(static_cast<unsigned char>(t[i]));
    }
    void Append(const Bytes& b) { data.insert(data.end(), b.data.begin(), b.data.end()); }
};

struct Format {
    unsigned tag = 1, channels = 2, rate = 48000, bits = 16;
    bool extensible = false;
    unsigned valid_bits = 0; // 0: as bits
    unsigned sub_tag = 1;
};

Bytes FmtChunk(const Format& f) {
    Bytes b;
    b.Tag("fmt ");
    b.U32(f.extensible ? 40 : 16);
    b.U16(f.extensible ? 0xFFFE : f.tag);
    b.U16(f.channels);
    b.U32(f.rate);
    b.U32(f.rate * f.channels * f.bits / 8);
    b.U16(f.channels * f.bits / 8);
    b.U16(f.bits);
    if(f.extensible) {
        b.U16(22);
        b.U16(f.valid_bits ? f.valid_bits : f.bits);
        b.U32(f.channels == 1 ? 4 : 3);
        b.U16(f.sub_tag);
        const unsigned char tail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
        for(unsigned char c : tail)
            b.U8(c);
    }
    return b;
}

Bytes DataChunk(const std::vector<std::uint8_t>& payload, std::uint32_t declared = 0xFFFFFFFF) {
    Bytes b;
    b.Tag("data");
    b.U32(declared == 0xFFFFFFFF ? static_cast<std::uint32_t>(payload.size()) : declared);
    b.data.insert(b.data.end(), payload.begin(), payload.end());
    if(payload.size() & 1)
        b.U8(0);
    return b;
}

Bytes Riff(const std::vector<Bytes>& chunks) {
    Bytes body;
    body.Tag("WAVE");
    for(const Bytes& c : chunks)
        body.Append(c);
    Bytes b;
    b.Tag("RIFF");
    b.U32(static_cast<std::uint32_t>(body.data.size()));
    b.Append(body);
    return b;
}

std::string Write(const std::string& name, const Bytes& bytes) {
    const fs::path path = g_dir / fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()));
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data.data()), static_cast<std::streamsize>(bytes.data.size()));
    out.close();
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::vector<std::uint8_t> Pcm16(const std::vector<std::int16_t>& samples) {
    std::vector<std::uint8_t> out;
    for(std::int16_t s : samples) {
        out.push_back(static_cast<std::uint8_t>(s & 0xff));
        out.push_back(static_cast<std::uint8_t>((s >> 8) & 0xff));
    }
    return out;
}

std::vector<std::uint8_t> Pcm24(const std::vector<std::int32_t>& samples) {
    std::vector<std::uint8_t> out;
    for(std::int32_t s : samples)
        for(int k = 0; k < 3; ++k)
            out.push_back(static_cast<std::uint8_t>((static_cast<std::uint32_t>(s) >> (8 * k)) & 0xff));
    return out;
}

std::vector<std::uint8_t> Float32(const std::vector<float>& samples) {
    std::vector<std::uint8_t> out(samples.size() * 4);
    std::memcpy(out.data(), samples.data(), out.size());
    return out;
}

void TestFormats() {
    // PCM16 stereo: unchanged.
    std::vector<std::int16_t> s16;
    for(int i = 0; i < 2000; ++i)
        s16.push_back(static_cast<std::int16_t>((i * 37) % 65536 - 32768));
    WavResult r = ReadWavFile(Write("pcm16.wav", Riff({FmtChunk({}), DataChunk(Pcm16(s16))})));
    CHECK(r.error == WavError::kNone && r.file->frames == 1000 && r.file->channels == 2
          && r.file->rate == 48000);
    CHECK(std::memcmp(r.file->pcm.get(), s16.data(), s16.size() * 2) == 0);

    // PCM16 mono 44.1 kHz with an unknown odd-sized chunk before and fmt after data.
    Bytes odd;
    odd.Tag("LIST");
    odd.U32(3);
    odd.U8(1);
    odd.U8(2);
    odd.U8(3);
    odd.U8(0); // pad
    Format mono;
    mono.channels = 1;
    mono.rate = 44100;
    r = ReadWavFile(Write("mono.wav", Riff({odd, DataChunk(Pcm16({1, -2, 3, 32767, -32768})), FmtChunk(mono)})));
    CHECK(r.error == WavError::kNone && r.file->frames == 5 && r.file->channels == 1
          && r.file->rate == 44100 && r.file->pcm[3] == 32767 && r.file->pcm[4] == -32768);

    // PCM24: rounded to the nearest 16-bit value, full scale clamped.
    const std::vector<std::int32_t> s24 = {0, 127, 128, 129, -128, -129, 0x7FFFFF, -0x800000, 0x7FFF7F,
                                           0x7FFF80, 256, -256};
    const std::vector<std::int16_t> e24 = {0, 0, 1, 1, 0, -1, 32767, -32768, 32767, 32767, 1, -1};
    Format f24;
    f24.bits = 24;
    f24.channels = 1;
    r = ReadWavFile(Write("pcm24.wav", Riff({FmtChunk(f24), DataChunk(Pcm24(s24))})));
    CHECK(r.error == WavError::kNone && r.file->frames == s24.size() && r.source_bits == 24);
    for(std::size_t i = 0; i < e24.size(); ++i)
        CHECK(r.file->pcm[i] == e24[i]);
    // The same as WAVE_FORMAT_EXTENSIBLE, stereo, 20 valid bits.
    f24.extensible = true;
    f24.channels = 2;
    f24.valid_bits = 20;
    r = ReadWavFile(Write("ext24.wav", Riff({FmtChunk(f24), DataChunk(Pcm24(s24))})));
    CHECK(r.error == WavError::kNone && r.file->frames == s24.size() / 2);
    for(std::size_t i = 0; i < e24.size(); ++i)
        CHECK(r.file->pcm[i] == e24[i]);

    // Float: rounded, clamped, NaN is silence.
    const std::vector<float> sf = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -3.0f, std::nanf(""),
                                   1.0f / 65536.0f, 3.0f / 65536.0f};
    const std::vector<std::int16_t> ef = {0, 16384, -16384, 32767, -32768, 32767, -32768, 0, 1, 2};
    Format ff;
    ff.tag = 3;
    ff.bits = 32;
    ff.channels = 1;
    ff.rate = 8000;
    r = ReadWavFile(Write("float.wav", Riff({FmtChunk(ff), DataChunk(Float32(sf))})));
    CHECK(r.error == WavError::kNone && r.source_float && r.file->rate == 8000);
    for(std::size_t i = 0; i < ef.size(); ++i)
        CHECK(r.file->pcm[i] == ef[i]);
    ff.extensible = true;
    ff.sub_tag = 3;
    ff.rate = 192000;
    r = ReadWavFile(Write("extfloat.wav", Riff({FmtChunk(ff), DataChunk(Float32(sf))})));
    CHECK(r.error == WavError::kNone && r.file->rate == 192000 && r.file->pcm[1] == 16384);

    // A path outside ASCII.
    r = ReadWavFile(Write("Kl\xC3\xA4nge \xC3\xBC \xE2\x82\xAC.wav", Riff({FmtChunk({}), DataChunk(Pcm16(s16))})));
    CHECK(r.error == WavError::kNone && r.file->frames == 1000);
    std::printf("formats: PCM16, PCM24 (rounded), float32 (rounded, clamped, NaN as 0), "
                "EXTENSIBLE, mono and stereo, 8 to 192 kHz, chunks in any order, a UTF-8 path\n");
}

void TestRefused() {
    const std::vector<std::uint8_t> payload = Pcm16(std::vector<std::int16_t>(64, 100));
    struct Case {
        const char* name;
        Bytes bytes;
        WavError error;
    };
    std::vector<Case> cases;
    auto with = [&](const Format& f) { return Riff({FmtChunk(f), DataChunk(payload)}); };
    Format f;
    f.rate = 7999;
    cases.push_back({"rate7999", with(f), WavError::kInvalid});
    f.rate = 192001;
    cases.push_back({"rate192001", with(f), WavError::kInvalid});
    f = {};
    f.channels = 3;
    cases.push_back({"3ch", Riff({FmtChunk(f), DataChunk(std::vector<std::uint8_t>(60, 0))}), WavError::kInvalid});
    f = {};
    f.bits = 8;
    cases.push_back({"8bit", with(f), WavError::kInvalid});
    f = {};
    f.bits = 32;
    cases.push_back({"int32", with(f), WavError::kInvalid});
    f = {};
    f.tag = 3;
    f.bits = 64;
    cases.push_back({"double", with(f), WavError::kInvalid});
    f = {};
    f.tag = 2; // ADPCM
    cases.push_back({"adpcm", with(f), WavError::kInvalid});
    f = {};
    f.extensible = true;
    f.sub_tag = 2;
    cases.push_back({"ext-adpcm", with(f), WavError::kInvalid});
    cases.push_back({"two fmt", Riff({FmtChunk({}), FmtChunk({}), DataChunk(payload)}), WavError::kInvalid});
    cases.push_back({"two data", Riff({FmtChunk({}), DataChunk(payload), DataChunk(payload)}), WavError::kInvalid});
    cases.push_back({"no data", Riff({FmtChunk({})}), WavError::kInvalid});
    cases.push_back({"no fmt", Riff({DataChunk(payload)}), WavError::kInvalid});
    cases.push_back({"empty data", Riff({FmtChunk({}), DataChunk({})}), WavError::kInvalid});
    // Cut short: the data chunk says more than the file has.
    Bytes cut = with({});
    cut.data.resize(cut.data.size() - 10);
    cases.push_back({"cut", cut, WavError::kInvalid});
    Bytes header_only = with({});
    header_only.data.resize(30);
    cases.push_back({"cut header", header_only, WavError::kInvalid});
    Bytes garbage;
    for(int i = 0; i < 500; ++i)
        garbage.U8(static_cast<unsigned>(i * 7919 % 251));
    cases.push_back({"garbage", garbage, WavError::kInvalid});
    Bytes rf64 = with({});
    std::memcpy(rf64.data.data(), "RF64", 4);
    cases.push_back({"rf64", rf64, WavError::kInvalid});
    Bytes tiny;
    tiny.Tag("RIFF");
    cases.push_back({"tiny", tiny, WavError::kInvalid});
    // Damaged block alignment.
    Bytes bad_align = with({});
    bad_align.data[32] = 3;
    cases.push_back({"alignment", bad_align, WavError::kInvalid});
    for(const Case& c : cases) {
        const WavResult r = ReadWavFile(Write(std::string("refused-") + c.name + ".wav", c.bytes));
        if(r.error != c.error)
            std::printf("  %s: error %d (%s)\n", c.name, static_cast<int>(r.error), r.detail);
        CHECK(r.error == c.error && !r.file && r.detail[0] != 0);
    }
    // Missing, and a folder instead of a file.
    CHECK(ReadWavFile((g_dir / "nothing.wav").string()).error == WavError::kMissing);
    CHECK(ReadWavFile(g_dir.string()).error == WavError::kMissing);
    CHECK(ReadWavFile("").error == WavError::kMissing);
    // Too large: checked from the header, before anything is allocated.
    const std::string big = Write("big.wav", with({}));
    CHECK(ReadWavFile(big, 127).error == WavError::kTooLarge);
    CHECK(ReadWavFile(big, 128).error == WavError::kNone);
    static_assert(kMaxPcmBytes == 256ull * 1024 * 1024);
    std::printf("refused: %zu damaged, cut or unsupported files as invalid, missing files, and "
                "files above the limit (256 MiB of PCM16 in the product)\n",
                cases.size());
}

template <typename Predicate>
bool WaitFor(Predicate predicate) {
    for(int i = 0; i < 400; ++i) {
        if(predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void TestLoader() {
    std::vector<std::int16_t> samples(4800 * 2, 1000);
    const std::string good = Write("good.wav", Riff({FmtChunk({}), DataChunk(Pcm16(samples))}));
    samples.assign(441 * 2, -7);
    Format f;
    f.rate = 44100;
    const std::string other = Write("other.wav", Riff({FmtChunk(f), DataChunk(Pcm16(samples))}));
    const std::string bad = Write("bad.wav", Bytes{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}});

    FileExchange exchange;
    FileLoader loader(exchange, 1u << 20);
    CHECK(loader.Status().sound == SoundState::kNone && loader.Status().serial > 0);
    loader.Request(good);
    CHECK(loader.Status().request_state == RequestState::kLoading);
    CHECK(WaitFor([&] { return loader.Idle(); }));
    FileStatus status = loader.Status();
    CHECK(status.sound == SoundState::kReady && status.path == good
          && status.request_state == RequestState::kNone);
    CHECK(status.info == "48 kHz stereo, 16 bit, 0:00.1");
    CHECK(exchange.Pending());
    FileData* taken = exchange.Take(); // the audio thread
    CHECK(taken && taken->frames == 4800 && !exchange.Pending());

    // A failure keeps the sound and says why.
    loader.Request(bad);
    CHECK(WaitFor([&] { return loader.Idle(); }));
    status = loader.Status();
    CHECK(status.sound == SoundState::kReady && status.path == good
          && status.request_state == RequestState::kFailed && !status.detail.empty()
          && status.request == bad && !exchange.Pending());

    // Requests in a row: the newest wins.
    for(int i = 0; i < 20; ++i)
        loader.Request(i % 2 ? good : other);
    loader.Request(other);
    CHECK(WaitFor([&] { return loader.Idle(); }));
    CHECK(loader.SoundPath() == other);
    FileData* second = exchange.Take();
    CHECK(second && second->rate == 44100);

    // A project's file that is missing: the path stays, the sound is silent.
    const std::string gone = (g_dir / "gone.wav").string();
    // A project's file is returned, not offered (MS-R07): the processor hands it over
    // together with the project's values.
    FileData* empty = loader.LoadProject(gone, 1).release();
    status = loader.Status();
    CHECK(status.path == gone && status.sound == SoundState::kMissing);
    CHECK(empty && empty->frames == 0 && empty->project == 1 && !exchange.Pending());
    CHECK(empty->sequence > second->sequence);
    std::unique_ptr<FileData> project = loader.LoadProject(good, 2);
    CHECK(project && project->frames == 4800 && project->sequence > empty->sequence);
    CHECK(loader.Status().sound == SoundState::kReady && !exchange.Pending());
    project.reset();
    FileData* last = loader.LoadProject("", 3).release();
    CHECK(loader.Status().sound == SoundState::kNone && loader.SoundPath().empty());
    CHECK(last && last->frames == 0 && last->project == 3);
    // A project load does not count as a file from the window.
    CHECK(loader.UserLoads() == loader.Status().user_loads && loader.UserLoads() > 0);

    // Retired files are freed by the worker.
    CHECK(exchange.Retire(taken) && exchange.Retire(second) && exchange.Retire(empty)
          && exchange.Retire(last));
    std::this_thread::sleep_for(FileLoader::kCollectInterval * 3);
    for(std::size_t i = 0; i < FileExchange::kRetireCapacity; ++i)
        CHECK(exchange.Retire(new FileData)); // room again: the four were collected
    CHECK(!exchange.Retire(nullptr));        // full now
    exchange.Collect();
    std::printf("loader: worker thread, status, a failure keeps the old file, the newest request "
                "wins, a missing project file stays named and silent, retired files are freed\n");
}


// MS-R01: allocations that fail while loading, in the worker thread and in the
// thread that loads a project, become a failure result; the process lives on, the
// playing file stays for a request from the window, a project falls silent.
void TestOutOfMemory() {
    std::vector<std::int16_t> samples(9600 * 2, 500);
    const std::string good = Write("oom-good.wav", Riff({FmtChunk({}), DataChunk(Pcm16(samples))}));
    samples.assign(4800 * 2, -500);
    const std::string other = Write("oom-other-with-a-rather-long-name-for-the-path.wav",
                                    Riff({FmtChunk({}), DataChunk(Pcm16(samples))}));
    samples.assign(300000 * 2, 250); // 1.2 MB of PCM16: more than the conversion buffer
    const std::string big = Write("oom-big.wav", Riff({FmtChunk({}), DataChunk(Pcm16(samples))}));
    FileExchange exchange;
    FileLoader loader(exchange);
    FileData* playing = loader.LoadProject(good, 1).release();
    CHECK(playing && playing->frames == 9600 && playing->project == 1);
    struct Case {
        const char* what;
        std::size_t from;
        const std::string* path;
    };
    // The conversion buffer (exactly 1 MiB) of a small file; the PCM array of a
    // 1.2 MB file, whose buffer still fits; nearly everything (strings, the file
    // object, the status texts).
    const Case cases[] = {{"conversion buffer", 1u << 20, &other},
                          {"PCM array", 1100000, &big},
                          {"almost all", 40, &other}};
    for(const Case& c : cases) {
        const std::uint64_t failed_before = g_failed.load();
        g_fail_from = c.from;
        loader.Request(*c.path);
        const bool idle = WaitFor([&] { return loader.Idle(); });
        g_fail_from = 0;
        CHECK(idle);
        const FileStatus status = loader.Status();
        std::printf("  %s: %llu allocations refused; request %d, detail '%s'\n", c.what,
                    static_cast<unsigned long long>(g_failed.load() - failed_before),
                    static_cast<int>(status.request_state), status.detail.c_str());
        CHECK(g_failed.load() > failed_before);
        CHECK(status.request_state == RequestState::kFailed && !status.detail.empty());
        CHECK(status.path == good && status.sound == SoundState::kReady && !exchange.Pending());
        }
    // A project loaded with almost no memory: silent, from the spare empty file.
    // FileData has 40 bytes, so not even the empty file object can be made here: the
    // spare is used, and cannot be made again while memory is short. This runs in
    // Debug builds too (no allocation below 40 bytes fails).
    static_assert(sizeof(FileData) >= 40);
    CHECK(loader.SpareReadyForTest());
    g_fail_from = 40;
    FileData* silent = loader.LoadProject(other, 2).release();
    const bool spare_after_failure = loader.SpareReadyForTest();
    g_fail_from = 0;
    CHECK(!spare_after_failure);
    FileStatus status = loader.Status();
    CHECK(status.sound == SoundState::kTooLarge || status.sound == SoundState::kReadError);
    CHECK(silent && silent->frames == 0 && silent->project == 2);
    // Memory back: everything works again, the spare is refilled.
    FileData* back = loader.LoadProject(other, 3).release();
    CHECK(loader.SpareReadyForTest()); // made again
    CHECK(back && back->frames == 4800 && back->project == 3);
    g_fail_from = 40;
    FileData* again = loader.LoadProject(other, 4).release();
    g_fail_from = 0;
    CHECK(again && again->frames == 0 && again->project == 4);
    // So little memory that not even an empty file object can be made: the spare
    // kept for this case is used, and made again once memory is back. Not in Debug
    // builds: there the standard library allocates a 16-byte container proxy even in
    // noexcept constructors (std::filesystem::path) and terminates by design when that
    // fails. The plug-in ships as a Release build, which has no such proxies.
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    // Memory back once more, so that the spare is there again (the case above may
    // have used it: FileData has 40 bytes since 0.1.2).
    loader.LoadProject(other, 50).reset();
    g_fail_from = 16;
    FileData* spare = loader.LoadProject(other, 5).release();
    g_fail_from = 0;
    CHECK(spare && spare->frames == 0 && spare->project == 5);
    delete spare;
    loader.LoadProject(other, 6).reset();
    g_fail_from = 16;
    spare = loader.LoadProject(other, 7).release();
    g_fail_from = 0;
    CHECK(spare && spare->frames == 0 && spare->project == 7);
    delete spare;
#endif
    loader.Request(other);
    CHECK(WaitFor([&] { return loader.Idle(); }));
    CHECK(loader.Status().request_state == RequestState::kNone && loader.SoundPath() == other);
    delete exchange.Take();
    for(FileData* file : {playing, silent, back, again})
        delete file;
    std::printf("out of memory: the conversion buffer, the PCM array and the status texts refused "
                "in the worker, and a project load in the calling thread: the process lives on, "
                "the old file stays, a project falls silent\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_dir = fs::temp_directory_path() / ("messier-test-" + std::to_string(_getpid()));
    fs::remove_all(g_dir);
    fs::create_directories(g_dir);
    TestFormats();
    TestRefused();
    TestLoader();
    TestOutOfMemory();
    fs::remove_all(g_dir);
    std::printf("PASS messier.loader\n");
    return 0;
}
