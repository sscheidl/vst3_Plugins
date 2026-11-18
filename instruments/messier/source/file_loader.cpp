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
        ++ticket_;
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
        ++ticket_; // a request from the window still running gives way
        has_wanted_ = false;
        if(status_.request_state == RequestState::kLoading)
            status_.request_state = RequestState::kNone;
        ++status_.serial;
    }
    WavResult result;
    if(!path.empty())
        result = ReadWavFile(path, max_pcm_bytes_);
    const std::lock_guard<std::mutex> lock(mutex_);
    ++status_.serial;
    std::unique_ptr<FileData> file;
    try {
        if(path.empty()) {
            file = TakeEmptyLocked(false); // frames 0: no file
            status_.path.clear();
            status_.info.clear();
            status_.sound = SoundState::kNone;
        } else if(result.error == WavError::kNone && result.file) {
            std::string info = DescribeFile(*result.file, result.source_bits, result.source_float);
            status_.path = path;
            status_.info = std::move(info);
            status_.sound = SoundState::kReady;
            file = std::move(result.file);
        } else {
            // The project names this file: keep its path, play nothing.
            std::string info = std::string(WavErrorText(result.error)) + ": " + result.detail;
            file = TakeEmptyLocked(false);
            status_.path = path;
            status_.sound = SoundFor(result.error);
            status_.info = std::move(info);
        }
    } catch(...) {
        // No memory left to describe the outcome (MS-R01): silence, from the spare if
        // need be. The status keeps its previous path then.
        file = TakeEmptyLocked(true);
        status_.sound = SoundState::kTooLarge;
        status_.info = kOutOfMemory;
    }
    if(file) {
        file->project = project;
        file->sequence = ++sequence_;
    }
    RefillSpareLocked();
    return file;
}

void FileLoader::Finish(std::uint64_t ticket, const std::string& path, WavResult result) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    try {
        FinishUnchecked(ticket, path, result);
    } catch(...) {
        // No memory left to describe the outcome (MS-R01): the request fails, the
        // sound stays.
        ++status_.serial;
        status_.request_state = RequestState::kFailed;
        status_.detail = kOutOfMemory;
    }
    RefillSpareLocked();
}

void FileLoader::FinishUnchecked(std::uint64_t ticket, const std::string& path, WavResult& result) {
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
        result.file->sequence = ++sequence_;
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
            Finish(ticket, path, ReadWavFile(path, max_pcm_bytes_));
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
