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
// (Request), and in the calling thread when a project is loaded (LoadNow). It
// keeps the status the window shows. The newest request wins: a result that a
// later request or a loaded project has overtaken is thrown away. Every failure
// leaves the playing file as it is, unlike the firmware, which has to overwrite
// its one buffer.
#pragma once

#include "looper_engine.h"
#include "spsc_queue.h"
#include "wav_reader.h"

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

    // Reads the file on the worker thread. Returns at once.
    void Request(const std::string& utf8_path);
    // Reads the file in this thread and offers it, or an empty file if it cannot be
    // read: a project whose file is missing stays silent and keeps the path. An
    // empty path is no file. Overtakes every request still running.
    void LoadNow(const std::string& utf8_path);

    FileStatus Status() const;
    std::string SoundPath() const;
    // For tests: true once no request is waiting or being read.
    bool Idle() const;
    // Frees the files the audio thread has let go. The worker does it every
    // kCollectInterval as well.
    void Collect() { exchange_.Collect(); }
    static constexpr std::chrono::milliseconds kCollectInterval{50};

  private:
    void Run();
    // Offers the result if `ticket` is still the newest. For a project's file
    // (`project`) a failure still replaces the sound with silence.
    void Finish(std::uint64_t ticket, const std::string& path, WavResult result, bool project);

    FileExchange& exchange_;
    const std::uint64_t max_pcm_bytes_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    FileStatus status_;
    std::string wanted_;           // the request waiting for the worker
    bool has_wanted_ = false, busy_ = false, stop_ = false;
    std::uint64_t ticket_ = 0;     // of the newest request or project load
    std::thread worker_;
};

// "44.1 kHz stereo, 16 bit, 0:12.3", for the window.
std::string DescribeFile(const FileData& file, std::uint16_t bits, bool is_float);
// The last part of a path, for the window.
std::string FileName(const std::string& utf8_path);

} // namespace messier
