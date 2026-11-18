#include "file_drop.h"

#include "wav_reader.h"

#include <cctype>

namespace messier {

using namespace VSTGUI;

namespace {

bool EndsWithWav(const std::string& path) {
    if(path.size() < 4)
        return false;
    const char* tail = path.c_str() + path.size() - 4;
    return tail[0] == '.' && std::tolower(static_cast<unsigned char>(tail[1])) == 'w'
           && std::tolower(static_cast<unsigned char>(tail[2])) == 'a'
           && std::tolower(static_cast<unsigned char>(tail[3])) == 'v';
}

} // namespace

std::string PickDroppedWav(IDataPackage* package) noexcept {
    try {
        if(!package)
            return {};
        const uint32_t count = package->getCount();
        for(uint32_t i = 0; i < count; ++i) {
            const void* buffer = nullptr;
            IDataPackage::Type type = IDataPackage::kError;
            const uint32_t size = package->getData(i, buffer, type);
            if(type != IDataPackage::kFilePath || !buffer || size == 0)
                continue;
            std::string path(static_cast<const char*>(buffer), size);
            while(!path.empty() && path.back() == '\0') // the C string's terminator, if counted
                path.pop_back();
            if(EndsWithWav(path))
                return path;
        }
    } catch(...) {
        // No memory for the path: as if nothing usable were dropped.
    }
    return {};
}

DropVerdict JudgeDrop(IDataPackage* package) noexcept {
    DropVerdict verdict;
    try {
        if(!package)
            return verdict;
        bool any_wav = false;
        const uint32_t count = package->getCount();
        for(uint32_t i = 0; i < count; ++i) {
            const void* buffer = nullptr;
            IDataPackage::Type type = IDataPackage::kError;
            const uint32_t size = package->getData(i, buffer, type);
            if(type != IDataPackage::kFilePath || !buffer || size == 0)
                continue;
            std::string path(static_cast<const char*>(buffer), size);
            while(!path.empty() && path.back() == '\0')
                path.pop_back();
            if(!EndsWithWav(path))
                continue;
            const WavResult probe = ProbeWavFile(path);
            if(probe.error == WavError::kNone) {
                verdict.usable = true;
                verdict.path = std::move(path);
                verdict.reason.clear();
                return verdict;
            }
            if(!any_wav) // the first refused WAV says why
                verdict.reason = std::string(WavErrorText(probe.error)) + ": " + probe.detail;
            any_wav = true;
        }
        if(!any_wav)
            verdict.reason = "not a WAV file";
    } catch(...) {
        verdict = DropVerdict{};
        verdict.reason = "out of memory";
    }
    return verdict;
}

void FileDropTarget::Show(Hover state, const std::string& reason) noexcept {
    try {
        if(hover_)
            hover_(state, reason);
    } catch(...) {
    }
}

DragOperation FileDropTarget::onDragEnter(DragEventData data) {
    // Judged once, when the drag enters: the files do not change while it hovers.
    verdict_ = JudgeDrop(data.drag);
    over_ = true;
    Show(verdict_.usable ? Hover::kUsable : Hover::kRefused, verdict_.reason);
    return verdict_.usable ? DragOperation::Copy : DragOperation::None;
}

DragOperation FileDropTarget::onDragMove(DragEventData) {
    return verdict_.usable ? DragOperation::Copy : DragOperation::None;
}

void FileDropTarget::onDragLeave(DragEventData) {
    over_ = false;
    Show(Hover::kNone, {});
}

bool FileDropTarget::onDrop(DragEventData data) {
    Show(Hover::kNone, {});
    // Judged again if the drag did not enter here first (some hosts send no enter).
    const DropVerdict verdict = over_ ? verdict_ : JudgeDrop(data.drag);
    over_ = false;
    verdict_ = DropVerdict{};
    if(!verdict.usable)
        return false;
    try {
        if(drop_)
            drop_(verdict.path);
    } catch(...) {
        return false;
    }
    return true;
}

} // namespace messier
