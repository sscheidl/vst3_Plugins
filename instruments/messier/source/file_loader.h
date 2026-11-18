// Getting sample files to the audio thread and back out of memory. Free of VST3
// types.
//
// FileExchange hands a finished file to the audio thread and takes the files the
// audio thread no longer reads, without locks or allocation on the audio side:
//   other threads  --Offer-->  one pending slot  --Take-->      audio thread
//   other threads  <-Collect-  retire queue      <-Retire--     audio thread
// A file offered while an older one is still pending replaces it; the older one,
// which the audio thread never saw, is freed at once. Retired files are freed by
// Collect(), on the loader's thread or any other non-audio thread.
//
// FileLoader reads files on its own worker thread when the window asks for one
// (Request), and in the calling thread when a project is loaded (LoadProject). It
// keeps the status the window shows. The newest request wins: a result that a
// later request or a loaded project has overtaken is thrown away. Every failure
// leaves the playing file as it is, unlike the firmware, which has to overwrite
// its one buffer.
#pragma once

#include "looper_engine.h"
#include "spsc_queue.h"
#include "wav_reader.h"
#include "wave_overview.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace messier {

class FileExchange {
  public:
    static constexpr std::size_t kRetireCapacity = 16;

    FileExchange() = default;
    ~FileExchange();
    FileExchange(const FileExchange&) = delete;
    FileExchange& operator=(const FileExchange&) = delete;

    // Not the audio thread; callers of Offer serialise among themselves.
    void Offer(std::unique_ptr<FileData> file);
    void Collect();
    // True while a file waits for the audio thread.
    bool Pending() const { return pending_.load(std::memory_order_acquire) != nullptr; }

    // Audio thread. Take passes ownership to the caller.
    FileData* Take() { return pending_.exchange(nullptr, std::memory_order_acq_rel); }
    // False if the queue is full; then the caller keeps the file and tries later.
    bool Retire(const FileData* file) { return retired_.Push(file); }

  private:
    std::atomic<FileData*> pending_{nullptr};
    SpscQueue<const FileData*, kRetireCapacity> retired_;
    std::mutex collect_mutex_; // the retire queue has one consumer at a time
};

// What the window shows about the sample.
enum class SoundState : int {
    kNone = 0,     // no file
    kReady = 1,    // the file plays
    kMissing = 2,  // a project's file that is not there: silence
    kInvalid = 3,  // a project's file that is no usable WAV: silence
    kTooLarge = 4,
    kReadError = 5,
};
enum class RequestState : int {
    kNone = 0,    // nothing asked, or the last request succeeded
    kLoading = 1, // a file from the window is being read
    kFailed = 2,  // the last request from the window failed; detail says why
};

struct FileStatus {
    std::string path;    // the file the sound refers to, as the project saves it
    SoundState sound = SoundState::kNone;
    std::string info;    // format and length of the playing file, or why it is silent
    std::string request; // the path of the last request from the window
    RequestState request_state = RequestState::kNone;
    std::string detail;  // why that request failed
    std::uint32_t serial = 0; // changes with every change
    // Counts the files from the window that became the sound (MS-R04): the
    // controller tells the host that the project changed when it grows. Project
    // loads do not count.
    std::uint32_t user_loads = 0;

    // The two states in one number, for the message to the controller.
    int Code() const { return static_cast<int>(sound) | static_cast<int>(request_state) << 4; }
    void SetCode(int code) {
        sound = static_cast<SoundState>(code & 15);
        request_state = static_cast<RequestState>((code >> 4) & 15);
    }
};

class FileLoader {
  public:
    // The limit for one file; tests set a small one.
    explicit FileLoader(FileExchange& exchange, std::uint64_t max_pcm_bytes = kMaxPcmBytes);
    ~FileLoader();
    FileLoader(const FileLoader&) = delete;
    FileLoader& operator=(const FileLoader&) = delete;

    // Reads the file on the worker thread. Returns at once. Never throws: if even
    // the request cannot be recorded, it is reported as failed (MS-R01).
    void Request(const std::string& utf8_path) noexcept;
    // Reads a project's file in this thread and returns it, tagged with `project`
    // (the processor's number of this project load) and the next sequence number, or
    // an empty file (frames 0) if it cannot be read: a project whose file is missing
    // stays silent and keeps the path. An empty path is no file. Does not offer the
    // file: the processor hands it to the audio thread together with the project's
    // values (MS-R07). Overtakes every request from the window still running. Null
    // if not even an empty file can be made, or if memory ran out while decoding the
    // file (MS-R16): then the project is refused and the status stays. Never throws.
    std::unique_ptr<FileData> LoadProject(const std::string& utf8_path, std::uint64_t project) noexcept;

    // Files from the window that became the sound so far (FileStatus::user_loads),
    // readable without the lock, also by the audio thread.
    std::uint32_t UserLoads() const noexcept { return user_loads_.load(std::memory_order_acquire); }

    FileStatus Status() const;
    std::string SoundPath() const;
    // The waveform overview of the finished file with this sequence number, or null
    // (no such file kept, an empty file, or no memory was left for the overview).
    // The newest kKeptOverviews files keep theirs. Never throws; not the audio thread.
    static constexpr std::size_t kKeptOverviews = 4;
    std::shared_ptr<const WaveOverview> Overview(std::uint64_t sequence) const noexcept;
    // For tests: true once no request is waiting or being read.
    bool Idle() const;
    // For tests: whether the spare empty file is there.
    bool SpareReadyForTest() const;
    // Frees the files the audio thread has let go. The worker does it every
    // kCollectInterval as well.
    void Collect() { exchange_.Collect(); }
    static constexpr std::chrono::milliseconds kCollectInterval{50};

  private:
    void Run();
    // Offers a window request's result if the request is still the newest. Takes the
    // mutex; never throws.
    void Finish(std::uint64_t ticket, const std::string& path, WavResult result,
                std::shared_ptr<WaveOverview> overview) noexcept;
    void FinishUnchecked(std::uint64_t ticket, const std::string& path, WavResult& result,
                         std::shared_ptr<WaveOverview>& overview);
    // Numbers the file and keeps its overview. Only with mutex_ held; never throws.
    void NumberLocked(FileData& file, std::shared_ptr<WaveOverview> overview) noexcept;
    // An empty file; the spare one if memory has run out. Only with mutex_ held,
    // like every use of spare_empty_ (MS-R10).
    std::unique_ptr<FileData> TakeEmptyLocked(bool out_of_memory) noexcept;
    void RefillSpareLocked() noexcept;

    FileExchange& exchange_;
    const std::uint64_t max_pcm_bytes_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    FileStatus status_;
    std::string wanted_;           // the request waiting for the worker
    bool has_wanted_ = false, busy_ = false, stop_ = false;
    std::uint64_t ticket_ = 0;     // of the newest request or project load
    std::uint64_t sequence_ = 0;   // numbers the finished files
    std::atomic<std::uint32_t> user_loads_{0};
    // An empty file, kept ready so that a project can fall silent even when no memory
    // is left; refilled after every result.
    std::unique_ptr<FileData> spare_empty_;
    std::shared_ptr<const WaveOverview> overviews_[kKeptOverviews];
    std::size_t next_overview_ = 0;
    std::thread worker_;
};

// "44.1 kHz stereo, 16 bit, 0:12.3", for the window.
std::string DescribeFile(const FileData& file, std::uint16_t bits, bool is_float);
// The last part of a path, for the window.
std::string FileName(const std::string& utf8_path);

} // namespace messier
