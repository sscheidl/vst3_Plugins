// The measuring plate (0.2.0; the map strip of 0.1.4 to 0.1.11 before it): across, the
// position in the file, on one time axis for everything in the view. From the top: the
// seconds axis on the metal; the plate of glass in its bezel with the drifting nebula, the
// waveform of the file (on the glass since 0.2.1), a grid of crosses, the loop, the spray
// range around the playhead and the sounding grains as shooting stars (down: their
// panorama, L at the top); the START slider; and the row with START's value and the
// legend. The playhead runs across the plate, with a snowflake while Freeze holds it.
//
// It draws what the controller last received. Between two displays (about 30 ms apart,
// later while the host is busy) it moves playhead and grains on at the speeds the display
// carries (0.2.1; since 0.2.2 the core's own, not measured), and the loop follows START
// and SIZE at once for a moment (then the core's loop again), so the plate stays fluid. A click
// or drag sets START to that place in the file, a click on the snowflake ends Freeze
// (0.2.1), the wheel widens or narrows SPRAY (since 0.1.8), and a WAV can be dropped on it.
//
// 0.2.2 (the user's design round): the grains glow on along their path after
// they passed (also after a grain ended; a new file clears that), the nebula drifts with
// the effective signed tempo the display carries (never with the read position's jumps),
// and the position triangle has a lane of its own between the axis labels and the plate.
#pragma once

#include "gui_skin.h"
#include "looper_engine.h"
#include "parameters.h"
#include "wave_overview.h"

#include "vstgui/lib/cview.h"

#include <array>
#include <chrono>
#include <cstddef>
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
    // PITCH in octaves (0.2.3): it tints the grains' halo, rays and tails, warm at 0.
    void SetPitch(double octaves) noexcept;
    static VSTGUI::CColor PitchTint(const VSTGUI::CColor& warm, double octaves);
    // START and SIZE (normalized): the slider's thumb, and for kPreview after a change
    // the loop as the core will set it (START x length; SIZE squared of the rest), ahead
    // of the displays. MS-R46: after that the loop the core reports, which also follows
    // automation the processor gets and the controller does not.
    void SetStart(double start) noexcept;
    void SetSize(double size) noexcept;
    static constexpr std::chrono::milliseconds kPreview{300};
    // STOP (0.2.2): the nebula's drift comes to rest at once, not only once the displays
    // stand still.
    void SetStopped(bool stopped) noexcept { stopped_ = stopped; }
    // How long a grain's path glows on after the grain passed (display only).
    void SetTrail(std::int32_t milliseconds) noexcept;
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
    // The speed (frames per second, signed) of a sounding grain, as the display carries
    // it: the grain's real motion through the file, which its tail shows (since 0.2.2;
    // MS-R43: 0.2.1 guessed it from two displays).
    double GrainSpeed(std::uint32_t index) const;
    // Where the playhead and a grain are drawn at `now`: the display's place moved on at
    // its speed for at most a short while, the playhead inside the display's own loop
    // (MS-R45, MS-R46), nothing moved while frozen or resting.
    double HeadAt(Clock::time_point now) const;
    double GrainAt(std::uint32_t index, Clock::time_point now) const;
    // How long ago a grain of the display ended at `now`, on the wall clock (MS-R54); its
    // glow is gone after the trail time. Negative while it still sounds.
    double EndedFor(std::uint32_t index, Clock::time_point now) const;
    // Where the START thumb and a frame of the file stand across the view (0.2.4: the same
    // mapping, so the thumb stands on the left loop bracket).
    double StartX() const;
    double FrameX(double frame) const;
    // The loop as drawn at `now`, in frames.
    struct Loop {
        double start = 0, length = 1;
    };
    Loop ShownLoop(Clock::time_point now) const;
    // MS-R47: whether the waveform's bitmap could not be made (the plate is then drawn
    // without it, and the bitmap is tried again only for another key or after a while).
    bool WaveFailed() const { return wave_failed_; }
    // Where the snowflake is while Freeze holds, relative to the view; empty otherwise.
    VSTGUI::CRect SnowflakeBox() const;
    // The nebula's drift (0.2.2): the speed it heads for, in window units per second
    // (signed: forward to the right), from the effective tempo of the display; 0 while
    // stopped, frozen, resting or without a file. AdvanceDrift moves the drift on to `now`
    // (each frame), easing its speed towards that target.
    double DriftTarget() const;
    void AdvanceDrift(Clock::time_point now) noexcept;
    double DriftSpeed() const { return drift_speed_; }
    // Grains that ended and still glow at `now` (tests).
    std::size_t Afterglows(Clock::time_point now) const;

  private:
    double Share(double x) const;
    double Ahead(Clock::time_point now) const;
    void DrawDropHighlight(VSTGUI::CDrawContext* context);
    void DrawWave(VSTGUI::CDrawContext* context, const VSTGUI::CRect& plate, double loop_left,
                  double loop_right, Clock::time_point now);
    // A grain's glowing path: its head at `x` (where it was `since` seconds ago, 0 for a
    // sounding grain) moving `speed` units a second; the path glows back to where it was
    // `reach` seconds ago, at most the chosen trail time.
    void DrawTrail(VSTGUI::CDrawContext* context, double x, double y, double speed, double since, double reach,
                   double thickness, double alpha);

    // A grain that ended (0.2.2): where and when, how fast it moved, how long it lived.
    struct Ended {
        GrainDot dot; // position: where it ended
        Clock::time_point at;
        double life = 0;
    };
    static constexpr std::size_t kMaxEnded = 96;
    void Keep(const Ended& ended) noexcept;
    std::array<Ended, kMaxEnded> ended_{};
    std::size_t ended_count_ = 0;
    bool stopped_ = false;
    double drift_speed_ = 0, drift_a_ = 0, drift_b_ = 0; // b: the farther nebula
    Clock::time_point drift_at_{};

    EngineDisplay display_{};
    std::shared_ptr<const WaveOverview> wave_;
    std::shared_ptr<const Skin> skin_;
    bool resting_ = false; // the same display for a while: nothing moves on
    Clock::time_point rest_at_{}; // when it began to rest (MS-R57)
    Clock::time_point shown_at_{};
    Clock::time_point loop_changed_at_{}; // START or SIZE changed here (MS-R46)
    Clock::time_point born_ = Clock::now();
    Clock::time_point drawn_at_{};
    VSTGUI::CCoord inset_ = 0;
    double blend_ = 0.5;
    double pitch_ = 0;
    double trail_seconds_ = kTrailDefaultMs / 1000.0;
    VSTGUI::CColor tail_ink_{255, 200, 140, 255}; // the tails' colour this frame
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
    bool wave_failed_ = false; // for the key above
    Clock::time_point wave_failed_at_{};
};

} // namespace messier
