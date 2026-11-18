// The measuring plate (0.2.0; the map strip of 0.1.4 to 0.1.11 before it): across, the
// position in the file, on one time axis for everything in the view. From the top: the
// seconds axis on the metal; the plate of glass in its bezel with the drifting nebula, the
// waveform of the file (on the glass since 0.2.1), a grid of crosses, the loop, the spray
// range around the playhead and the sounding grains as shooting stars (down: their
// panorama, L at the top); the START slider; and the row with START's value and the
// legend. The playhead runs across the plate, with a snowflake while Freeze holds it.
//
// It draws what the controller last received. Between two displays (about 30 ms apart,
// later while the host is busy) it moves playhead and grains on at their measured speeds
// (0.2.1), and the loop follows START and SIZE at once, so the plate stays fluid. A click
// or drag sets START to that place in the file, a click on the snowflake ends Freeze
// (0.2.1), the wheel widens or narrows SPRAY (since 0.1.8), and a WAV can be dropped on it.
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
    using Clock = std::chrono::steady_clock;

    // `time_inset`: how far the time axis stays inside the view's left and right edges
    // (the bezel's width in the window).
    explicit MapView(const VSTGUI::CRect& size, VSTGUI::CCoord time_inset = 0);

    // New data; redraws if it differs from what is shown. Never throws.
    void Show(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept;
    // The same, received at `now` (tests).
    void ShowAt(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave,
                Clock::time_point now) noexcept;
    void SetSkin(std::shared_ptr<const Skin> skin) noexcept;
    // BLEND (normalized): the nebula fades and the grains brighten towards Granular.
    void SetBlend(double blend) noexcept;
    // START and SIZE (normalized): the slider's thumb and the loop as the core will set
    // it (START x length; SIZE squared of the rest), ahead of the next display.
    void SetStart(double start) noexcept;
    void SetSize(double size) noexcept;
    // The window's timer and the user's edits: the plate is drawn again, about as often
    // as displays come, so that nebula, playhead and grains move on.
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
        std::function<void()> freeze_off;         // the snowflake was clicked (0.2.1)
    };
    void SetEdits(Edits edits) { edits_ = std::move(edits); }
    // What the mouse events do (public for tests: VSTGUI's events need its platform).
    // x and y are relative to the view's top left; false if nothing was done. Without y
    // (negative) only START is placed.
    bool PressAt(double x, double y = -1.0);
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
    // The speed (frames per second, signed) of a sounding grain once two moves in a row
    // agree, else 0: the grain's real motion through the file, which its tail shows. The
    // display carries no grain numbers; a grain is followed by its panorama, its growing
    // window phase and its position (MS-R43: implausible or ambiguous matches start anew).
    double GrainSpeed(std::uint32_t index) const;
    // Where the snowflake is while Freeze holds, relative to the view; empty otherwise.
    VSTGUI::CRect SnowflakeBox() const;

  private:
    // What the view keeps of a grain between two displays.
    struct Trail {
        float pan = 0, phase = 0;
        double position = 0;
        double speed = 0;      // frames per second, from the last match
        double phase_rate = 0; // window phase per second
        int matches = 0;       // moves in a row that agree
    };
    void FollowGrains(const EngineDisplay& before, const EngineDisplay& now, double seconds);
    void FollowPlayhead(const EngineDisplay& before, const EngineDisplay& now, double seconds);
    double Share(double x) const;
    double LoopStart() const;
    double LoopLength() const;
    void DrawDropHighlight(VSTGUI::CDrawContext* context);
    void DrawWave(VSTGUI::CDrawContext* context, const VSTGUI::CRect& plate, double loop_left,
                  double loop_right);

    EngineDisplay display_{};
    std::shared_ptr<const WaveOverview> wave_;
    std::shared_ptr<const Skin> skin_;
    std::array<Trail, EngineDisplay::kMaxGrains> trails_{};
    double head_speed_ = 0;   // the playhead's, frames per second
    bool head_measured_ = false;
    bool resting_ = false; // the same display for a while: nothing moves on
    Clock::time_point shown_at_{};
    Clock::time_point born_ = Clock::now();
    Clock::time_point drawn_at_{};
    VSTGUI::CCoord inset_ = 0;
    double blend_ = 0.5;
    double start_ = 0.0, size_ = 1.0;
    bool start_known_ = false, size_known_ = false;
    DropShown drop_state_ = DropShown::kNone;
    std::string drop_reason_;
    Edits edits_;
    bool dragging_ = false;
    // The waveform drawn once into a bitmap (0.2.1), for the overview, the skin and the
    // device scale it was made for.
    VSTGUI::SharedPointer<VSTGUI::CBitmap> wave_cache_;
    std::shared_ptr<const WaveOverview> cached_wave_; // held, so no other can take its address
    std::shared_ptr<const Skin> cached_skin_;
    double cached_scale_ = 0;
    VSTGUI::CPoint cached_size_;
};

} // namespace messier
