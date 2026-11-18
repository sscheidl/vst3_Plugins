// The measuring plate (0.2.0; the map strip of 0.1.4 to 0.1.11 before it): across, the
// position in the file, on one time axis for everything in the view. From the top: the
// seconds axis on the metal; the plate of glass in its bezel with the drifting nebula, a
// grid of crosses, the loop, the spray range around the playhead and the sounding grains
// as shooting stars (down: their panorama, L at the top); the waveform of the file in
// the same scale with the loop's brackets; the START slider; and the row with START's
// value and the legend. The playhead runs from the axis through plate and waveform, with
// a snowflake while Freeze holds it.
//
// It draws what the controller last received. A click or drag anywhere on it sets START
// to that place in the file, the wheel widens or narrows SPRAY (since 0.1.8), and a WAV
// can be dropped on it.
#pragma once

#include "gui_skin.h"
#include "looper_engine.h"
#include "wave_overview.h"

#include "vstgui/lib/cview.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace messier {

class MapView : public VSTGUI::CView {
  public:
    // `time_inset`: how far the time axis stays inside the view's left and right edges
    // (the bezel's width in the window).
    explicit MapView(const VSTGUI::CRect& size, VSTGUI::CCoord time_inset = 0);

    // New data; redraws if it differs from what is shown. Never throws.
    void Show(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept;
    void SetSkin(std::shared_ptr<const Skin> skin) noexcept;
    // BLEND (normalized): the nebula fades and the grains brighten towards Granular.
    void SetBlend(double blend) noexcept;
    // START (normalized): where the slider's thumb stands.
    void SetStart(double start) noexcept;
    // The window's timer: the nebula drifts, so the plate is drawn again now and then
    // even when no new display comes.
    void Tick() noexcept;

    void draw(VSTGUI::CDrawContext* context) override;
    void DrawUnchecked(VSTGUI::CDrawContext* context);
    // A drag over the window's sample area (since 0.1.8): a usable WAV shows a frame
    // and "Drop WAV to load", a refused file a frame in the warning colour and why.
    // Never throws.
    enum class DropShown { kNone, kUsable, kRefused };
    void SetDropState(DropShown state, const std::string& reason) noexcept;
    DropShown DropState() const { return drop_state_; }

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
    // The speed (frames per second, signed) the view has measured for a sounding grain
    // from two displays in a row, 0 while it has seen the grain once. Its tail is drawn
    // from that: the grain's real motion through the file.
    double GrainSpeed(std::uint32_t index) const;

  private:
    // What the view keeps of a grain between two displays.
    struct Trail {
        float pan = 0, phase = 0;
        double position = 0;
        double speed = 0; // frames per second
        bool measured = false;
    };
    void FollowGrains(const EngineDisplay& before, const EngineDisplay& now, double seconds);
    double Share(double x) const;
    void DrawDropHighlight(VSTGUI::CDrawContext* context);

    EngineDisplay display_{};
    std::shared_ptr<const WaveOverview> wave_;
    std::shared_ptr<const Skin> skin_;
    std::array<Trail, EngineDisplay::kMaxGrains> trails_{};
    std::chrono::steady_clock::time_point shown_at_{};
    std::chrono::steady_clock::time_point born_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point drawn_at_{};
    VSTGUI::CCoord inset_ = 0;
    double blend_ = 0.5;
    double start_ = 0.0;
    DropShown drop_state_ = DropShown::kNone;
    std::string drop_reason_;
    Edits edits_;
    bool dragging_ = false;
};

} // namespace messier
