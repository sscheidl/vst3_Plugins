// The map of the sample (since 0.1.4, provisional until the Atlas/Orion window):
// across, the position in the file; in the upper part the panorama of the sounding
// grains (left at the top), the loop, the spray range around the playhead and the
// playhead itself; below, in the same scale, the waveform overview of the file the
// engine reads. It draws what the controller last received; since 0.1.8 a click or
// drag sets START, the wheel SPRAY, and a WAV can be dropped on it.
#pragma once

#include "looper_engine.h"
#include "wave_overview.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <functional>
#include <memory>
#include <string>

namespace messier {

class MapView : public VSTGUI::CView {
  public:
    explicit MapView(const VSTGUI::CRect& size);

    // New data; redraws if it differs from what is shown. Never throws.
    void Show(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept;
    void draw(VSTGUI::CDrawContext* context) override;
    // A drag over the window's sample area (since 0.1.8): a usable WAV shows a frame
    // and "Drop WAV to load", a refused file a frame in the warning colour and why.
    // Never throws.
    enum class DropShown { kNone, kUsable, kRefused };
    void SetDropState(DropShown state, const std::string& reason) noexcept;
    DropShown DropState() const { return drop_state_; }

    // Since 0.1.8 the map takes the mouse (the user's wish): a click or drag sets START
    // to that place in the file, the wheel widens or narrows SPRAY. The editor turns
    // these into parameter edits.
    struct Edits {
        std::function<void()> start_begin;
        std::function<void(double share)> start; // 0..1 of the file
        std::function<void()> start_end;
        std::function<void(double step)> spray;   // a change of the normalized value
    };
    void SetEdits(Edits edits) { edits_ = std::move(edits); }
    // What the mouse events do (public for tests: VSTGUI's events need its platform).
    // x is relative to the view's left edge; false if nothing was done.
    bool PressAt(double x);
    void DragTo(double x);
    void Release();
    bool Wheel(double delta, bool fine);
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override;

    // For tests: what the view would draw.
    const EngineDisplay& Shown() const { return display_; }
    bool ShowsWave() const { return wave_ != nullptr; }

  private:
    EngineDisplay display_{};
    std::shared_ptr<const WaveOverview> wave_;
    DropShown drop_state_ = DropShown::kNone;
    std::string drop_reason_;
    Edits edits_;
    bool dragging_ = false;
    double Share(double x) const;
    void DrawDropHighlight(VSTGUI::CDrawContext* context);
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
};

} // namespace messier
