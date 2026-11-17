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

void FileLoader::Request(const std::string& path) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        wanted_ = path;
        has_wanted_ = true;
        ++ticket_;
        status_.request = path;
        status_.request_state = RequestState::kLoading;
        status_.detail.clear();
        ++status_.serial;
    }
    wake_.notify_all();
}

void FileLoader::LoadNow(const std::string& path) {
    std::uint64_t ticket;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ticket = ++ticket_;
        has_wanted_ = false; // a project overrides what the window asked for
        if(status_.request_state == RequestState::kLoading)
            status_.request_state = RequestState::kNone;
        ++status_.serial;
    }
    if(path.empty()) {
        WavResult none;
        none.file = std::make_unique<FileData>(); // frames 0: no file
        Finish(ticket, path, std::move(none), true);
        return;
    }
    Finish(ticket, path, ReadWavFile(path, max_pcm_bytes_), true);
}

void FileLoader::Finish(std::uint64_t ticket, const std::string& path, WavResult result,
                        bool project) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if(ticket != ticket_)
        return; // overtaken: the newer request decides
    ++status_.serial;
    if(result.error == WavError::kNone) {
        status_.path = path;
        status_.sound = result.file->frames ? SoundState::kReady : SoundState::kNone;
        status_.info = result.file->frames
                           ? DescribeFile(*result.file, result.source_bits, result.source_float)
                           : std::string();
        if(!project)
            status_.request_state = RequestState::kNone;
        exchange_.Offer(std::move(result.file));
        return;
    }
    if(project) {
        // The project names this file: keep its path, play nothing.
        status_.path = path;
        status_.sound = SoundFor(result.error);
        status_.info = std::string(WavErrorText(result.error)) + ": " + result.detail;
        exchange_.Offer(std::make_unique<FileData>());
        return;
    }
    status_.request_state = RequestState::kFailed;
    status_.detail = std::string(WavErrorText(result.error)) + " (" + result.detail + ")";
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
        const std::string path = wanted_;
        const std::uint64_t ticket = ticket_;
        has_wanted_ = false;
        busy_ = true;
        lock.unlock();
        WavResult result = ReadWavFile(path, max_pcm_bytes_);
        Finish(ticket, path, std::move(result), false);
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

bool FileLoader::Idle() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return !has_wanted_ && !busy_;
}

} // namespace messier
