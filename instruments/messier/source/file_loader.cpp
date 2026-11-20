#include "file_loader.h"

#include <cstdio>

namespace messier {

FileExchange::~FileExchange() {
    delete pending_.exchange(nullptr);
    Collect();
}

void FileExchange::Offer(std::unique_ptr<FileData> file) {
    delete pending_.exchange(file.release(), std::memory_order_acq_rel);
}

void FileExchange::Collect() {
    const std::lock_guard<std::mutex> lock(collect_mutex_);
    const FileData* file = nullptr;
    while(retired_.Pop(file))
        delete file;
}

std::string FileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string DescribeFile(const FileData& file, std::uint16_t bits, bool is_float) {
    char text[96];
    const double seconds = file.rate ? static_cast<double>(file.frames) / file.rate : 0.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    std::snprintf(text, sizeof(text), "%g kHz %s, %u bit%s, %d:%04.1f", file.rate / 1000.0,
                  file.channels == 1 ? "mono" : "stereo", static_cast<unsigned>(bits),
                  is_float ? " float" : "", minutes, seconds - 60.0 * minutes);
    return text;
}

static SoundState SoundFor(WavError error) {
    switch(error) {
        case WavError::kNone: return SoundState::kReady;
        case WavError::kMissing: return SoundState::kMissing;
        case WavError::kInvalid: return SoundState::kInvalid;
        case WavError::kTooLarge: return SoundState::kTooLarge;
        case WavError::kReadError: return SoundState::kReadError;
        case WavError::kOutOfMemory: return SoundState::kReadError; // never a project's sound (MS-R16)
        case WavError::kCancelled: return SoundState::kReadError;   // only window reads give up
    }
    return SoundState::kInvalid;
}

FileLoader::FileLoader(FileExchange& exchange, std::uint64_t max_pcm_bytes)
: exchange_(exchange), max_pcm_bytes_(max_pcm_bytes) {
    status_.serial = 1; // 0 is "not heard from the processor yet" in the controller
    spare_empty_ = std::make_unique<FileData>();
    worker_ = std::thread([this] { Run(); });
}

FileLoader::~FileLoader() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    stopping_.store(true, std::memory_order_relaxed); // a read under way gives up (F6)
    wake_.notify_all();
    worker_.join();
}

// Texts of at most 15 characters fit std::string's own buffer: assigning them
// allocates nothing, so the failure paths below cannot fail again.
constexpr const char* kOutOfMemory = "out of memory";

std::unique_ptr<FileData> FileLoader::TakeEmptyLocked(bool out_of_memory) noexcept {
    // Caller holds mutex_ (MS-R10). When memory has just run out, the spare first,
    // without another attempt to allocate.
    std::unique_ptr<FileData> empty;
    if(out_of_memory && spare_empty_)
        return std::move(spare_empty_);
    try {
        empty = std::make_unique<FileData>();
    } catch(...) {
        empty = std::move(spare_empty_);
    }
    return empty;
}

void FileLoader::RefillSpareLocked() noexcept {
    if(spare_empty_)
        return;
    try {
        spare_empty_ = std::make_unique<FileData>();
    } catch(...) {
    }
}

void FileLoader::Request(const std::string& path) noexcept {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        newest_ticket_.store(++ticket_, std::memory_order_relaxed);
        ++status_.serial;
        status_.detail.clear();
        try {
            wanted_ = path;
            status_.request = path;
            has_wanted_ = true;
            status_.request_state = RequestState::kLoading;
        } catch(...) {
            has_wanted_ = false;
            status_.request_state = RequestState::kFailed;
            status_.detail = kOutOfMemory;
        }
    }
    wake_.notify_all();
}

std::unique_ptr<FileData> FileLoader::LoadProject(const std::string& path,
                                                  std::uint64_t project) noexcept {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        newest_ticket_.store(++ticket_, std::memory_order_relaxed); // a window request gives way
        has_wanted_ = false;
        if(status_.request_state == RequestState::kLoading)
            status_.request_state = RequestState::kNone;
        ++status_.serial;
    }
    WavResult result;
    if(!path.empty())
        result = ReadWavFile(path, max_pcm_bytes_);
    std::shared_ptr<WaveOverview> overview = result.file ? MakeOverview(*result.file) : nullptr;
    const std::lock_guard<std::mutex> lock(mutex_);
    ++status_.serial;
    // MS-R14: the new texts first, then the file object; the status changes only once
    // the file is there. Without one, setState() refuses the project, and the status
    // and the path that getState() saves stay those of the project still playing.
    std::unique_ptr<FileData> file;
    try {
        std::string info;
        SoundState sound = SoundState::kNone;
        std::string new_path = path;
        if(path.empty()) {
            file = TakeEmptyLocked(false); // frames 0: no file
        } else if(result.error == WavError::kNone && result.file) {
            info = DescribeFile(*result.file, result.source_bits, result.source_float);
            sound = SoundState::kReady;
            file = std::move(result.file);
        } else if(result.error == WavError::kOutOfMemory) {
            // MS-R16: the file may be fine; there was no memory to decode it. Not a
            // silent project with this path: refused as a whole, like any other
            // failure for want of memory (below).
        } else {
            // The project names this file: keep its path, play nothing.
            info = std::string(WavErrorText(result.error)) + ": " + result.detail;
            sound = SoundFor(result.error);
            file = TakeEmptyLocked(false);
        }
        if(file) {
            status_.path = std::move(new_path);
            status_.info = std::move(info);
            status_.sound = sound;
            // E3 (0.2.9): the project's file is what the line shows now; a window load
            // that failed before is no longer news.
            if(status_.request_state == RequestState::kFailed) {
                status_.request_state = RequestState::kNone;
                status_.detail.clear();
            }
        }
    } catch(...) {
        // No memory to take the project over completely (MS-R01, MS-R14): refused as
        // a whole; the old project plays on and stays what is saved. An empty file
        // object goes back to the spare.
        if(file && file->frames == 0 && !spare_empty_)
            spare_empty_ = std::move(file);
        file.reset();
    }
    if(!file) {
        // Refused (MS-R14): reported apart from the sound, which is unchanged, as a failed
        // request for the project's file (F1, 0.2.9: with its name, where memory allows).
        status_.request_state = RequestState::kFailed;
        status_.detail = kOutOfMemory;
        try {
            status_.request = path;
        } catch(...) {
        }
    }
    if(file) {
        file->project = project;
        NumberLocked(*file, file->frames ? std::move(overview) : nullptr);
    }
    RefillSpareLocked();
    return file;
}

void FileLoader::NumberLocked(FileData& file, std::shared_ptr<WaveOverview> overview) noexcept {
    file.sequence = ++sequence_;
    // The overview goes with the file's number, so the window can tell which file
    // it belongs to (EngineDisplay::sequence). Without one the oldest kept one stays.
    if(!overview)
        return;
    overview->sequence = file.sequence;
    overviews_[next_overview_] = std::move(overview);
    next_overview_ = (next_overview_ + 1) % kKeptOverviews;
}

std::shared_ptr<const WaveOverview> FileLoader::Overview(std::uint64_t sequence) const noexcept {
    if(sequence == 0)
        return nullptr;
    const std::lock_guard<std::mutex> lock(mutex_);
    for(const auto& overview : overviews_)
        if(overview && overview->sequence == sequence)
            return overview;
    return nullptr;
}

void FileLoader::Finish(std::uint64_t ticket, const std::string& path, WavResult result,
                        std::shared_ptr<WaveOverview> overview) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    try {
        FinishUnchecked(ticket, path, result, overview);
    } catch(...) {
        // No memory left to describe the outcome (MS-R01): the request fails, the
        // sound stays.
        ++status_.serial;
        status_.request_state = RequestState::kFailed;
        status_.detail = kOutOfMemory;
    }
    RefillSpareLocked();
}

void FileLoader::FinishUnchecked(std::uint64_t ticket, const std::string& path, WavResult& result,
                                 std::shared_ptr<WaveOverview>& overview) {
    // A request from the window gives way to any newer request or project load.
    if(ticket != ticket_)
        return;
    ++status_.serial;
    if(result.error == WavError::kNone && result.file) {
        std::string info = DescribeFile(*result.file, result.source_bits, result.source_float);
        status_.path = path;
        status_.info = std::move(info);
        status_.sound = SoundState::kReady;
        status_.request_state = RequestState::kNone;
        NumberLocked(*result.file, std::move(overview));
        exchange_.Offer(std::move(result.file));
        ++status_.user_loads;
        user_loads_.store(status_.user_loads, std::memory_order_release);
        return;
    }
    std::string detail = std::string(WavErrorText(result.error)) + " (" + result.detail + ")";
    status_.request_state = RequestState::kFailed;
    status_.detail = std::move(detail);
}

void FileLoader::Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for(;;) {
        wake_.wait_for(lock, kCollectInterval, [this] { return stop_ || has_wanted_; });
        lock.unlock();
        exchange_.Collect();
        lock.lock();
        if(stop_)
            return;
        if(!has_wanted_)
            continue;
        // Everything of one request inside try: in a Debug build even an empty
        // std::string allocates (MS-R01).
        std::uint64_t ticket = 0;
        try {
            std::string path = wanted_;
            ticket = ticket_;
            has_wanted_ = false;
            busy_ = true;
            lock.unlock();
            // F6 (0.2.9): the read gives up when it is overtaken or the loader ends.
            struct Mine {
                const FileLoader* loader;
                std::uint64_t ticket;
            } const mine{this, ticket};
            const WavCancel cancel{[](const void* context) {
                                       const auto& m = *static_cast<const Mine*>(context);
                                       return m.loader->stopping_.load(std::memory_order_relaxed)
                                              || m.loader->newest_ticket_.load(std::memory_order_relaxed)
                                                     != m.ticket;
                                   },
                                   &mine};
            WavResult result = ReadWavFile(path, max_pcm_bytes_, cancel);
            // The overview off the lock and off the audio thread, from the very buffer
            // the engine will play.
            std::shared_ptr<WaveOverview> overview = result.file ? MakeOverview(*result.file) : nullptr;
            Finish(ticket, path, std::move(result), std::move(overview));
        } catch(...) {
            if(!lock.owns_lock())
                lock.lock();
            has_wanted_ = false;
            ++status_.serial;
            status_.request_state = RequestState::kFailed;
            status_.detail = kOutOfMemory;
            busy_ = false;
            continue;
        }
        lock.lock();
        busy_ = false;
    }
}

FileStatus FileLoader::Status() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::string FileLoader::SoundPath() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return status_.path;
}

bool FileLoader::SpareReadyForTest() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return spare_empty_ != nullptr;
}

bool FileLoader::Idle() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return !has_wanted_ && !busy_;
}

} // namespace messier
