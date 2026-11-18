// Drag and drop of WAV files onto the window (since 0.1.8, user's wish of
// 2026-11-18). The drop target sits on the views that show the sample: the map with
// the waveform and the file line. A dropped WAV goes the same way as one chosen with
// LOAD...: the loader reads and checks it, and a failure leaves the old file playing.
#pragma once

#include "vstgui/lib/dragging.h"
#include "vstgui/lib/idatapackage.h"
#include "vstgui/lib/vstguibase.h"

#include <functional>
#include <string>

namespace messier {

// The first file path in the package that ends in ".wav" (any case), as UTF-8, or an
// empty string if there is none. Never throws.
std::string PickDroppedWav(VSTGUI::IDataPackage* package) noexcept;

// What a drag carries (since 0.1.8, checked while it hovers): the first .wav that the
// reader would load as far as its headers tell (ProbeWavFile), or, if there is none,
// why not. Never throws.
struct DropVerdict {
    bool usable = false;
    std::string path;   // the WAV to load, if usable
    std::string reason; // why not, if not: for the window
};
DropVerdict JudgeDrop(VSTGUI::IDataPackage* package) noexcept;

class FileDropTarget : public VSTGUI::DropTargetAdapter, public VSTGUI::NonAtomicReferenceCounted {
  public:
    enum class Hover { kNone, kUsable, kRefused };
    // hover: what is over the target now (kNone when it left), with the reason for a
    // refusal. drop: a usable WAV was dropped. Both called on the UI thread; exceptions
    // in them are caught here.
    FileDropTarget(std::function<void(Hover, const std::string&)> hover,
                   std::function<void(const std::string&)> drop)
    : hover_(std::move(hover)), drop_(std::move(drop)) {}

    VSTGUI::DragOperation onDragEnter(VSTGUI::DragEventData data) override;
    VSTGUI::DragOperation onDragMove(VSTGUI::DragEventData data) override;
    void onDragLeave(VSTGUI::DragEventData data) override;
    bool onDrop(VSTGUI::DragEventData data) override;

  private:
    void Show(Hover state, const std::string& reason) noexcept;
    std::function<void(Hover, const std::string&)> hover_;
    std::function<void(const std::string&)> drop_;
    DropVerdict verdict_;
    bool over_ = false;
};

} // namespace messier
