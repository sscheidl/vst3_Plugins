// The round plate (1.0 beta 2, the user's design of 2026-11-20): the measuring plate of
// beta 1 laid onto the round glass as through a fish-eye lens (fisheye.h). In GALAXY a
// slowly turning spiral nebula lies behind it, in WAVE the waveform of the file and the
// grid. The loop, the spray range, the playhead, and the grains are drawn in
// both views, at the places of the plate (the user's choice: the grains as before). The
// seconds axis runs outside the ring, along its upper arc.
//
// Beta 1's plate, of which this keeps the data, the timing and the mouse:
//
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

    // The glass is the circle in the middle of the view: its radius leaves the ring and
    // the axis' room (gui_layout.h) inside the view's shorter side.
    explicit MapView(const VSTGUI::CRect& size);
    // GALAXY (true, the default) or WAVE behind the plate's marks (beta 2).
    void SetGalaxy(bool galaxy) noexcept;
    bool Galaxy() const { return galaxy_; }

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
    // STOP (0.2.2): the nebula's drift (beta 2: the galaxy's turn) comes to rest at once,
    // not only once the displays stand still.
    void SetStopped(bool stopped) noexcept { stopped_ = stopped; }
    // How long a grain's path glows on after the grain passed (display only; fixed since
    // 0.2.6, the user's 450 ms).
    static constexpr double kTrail = kTrailDefaultMs / 1000.0;
    // The waveform's brightness in percent (0.2.6, WAVE): scales inside and outside the
    // loop alike, so the loop stays as clear.
    void SetWave(std::int32_t percent) noexcept;
    // The window's timer and the user's edits: the plate is drawn again, about as often
    // as displays come, so that nebula, playhead and grains move on.
    void Tick() noexcept;
    // MS-R66 (the GUI review of beta 2): whether anything on the glass moves at `now` (turn,
    // drift, playhead, grains, afterglows, a loop preview, a pending recolour); only then does
    // Tick draw again, and only the glass with its ring (GlassBox, relative to the view).
    bool Moving(Clock::time_point now) const;
    VSTGUI::CRect GlassBox() const;

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
    // x and y are relative to the view's top left; false if nothing was done. Without y
    // (negative) the point lies on the glass' middle line across (beta 2: the place in the
    // file comes from the point through the lens).
    bool PressAt(double x, double y = -1.0);
    void DragTo(double x, double y = -1.0);
    void Release();
    bool Wheel(double delta, bool fine);
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override;
    // Beta 2: only the round glass and its ring take the mouse; the view's corners belong
    // to the knobs and the panel around it.
    bool hitTest(const VSTGUI::CPoint& where, const VSTGUI::Event& event) override;

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
    // The glass: its centre relative to the view, and its radius.
    VSTGUI::CPoint GlassCentre() const;
    double GlassRadius() const;
    // Where a place of the plate lies on the glass, relative to the view (beta 2):
    // `share` of the file, `down` from -1 (top, L) to 1 (bottom, R).
    VSTGUI::CPoint PlateToView(double share, double down) const;
    // The share of the file at a point of the view (0 to 1).
    double ShareAt(double x, double y) const;
    // The loop as drawn at `now`, in frames.
    struct Loop {
        double start = 0, length = 1;
    };
    Loop ShownLoop(Clock::time_point now) const;
    // MS-R47: whether the waveform's bitmap could not be made (the plate is then drawn
    // without it, and the bitmap is tried again only for another key or after a while).
    bool WaveFailed() const { return wave_failed_; }
    // The nebula's drift (0.2.2): the speed it heads for, in window units per second
    // (signed: forward to the right), from the effective tempo of the display; 0 while
    // stopped, frozen, resting or without a file. AdvanceDrift moves the drift on to `now`
    // (each frame), easing its speed towards that target. Beta 2: the diffuse and the dark
    // nebula over the galaxy drift on small circles with it.
    double DriftTarget() const;
    void AdvanceDrift(Clock::time_point now) noexcept;
    double DriftSpeed() const { return drift_speed_; }
    // The turn of the glass' layers (beta 2, the user's DAW test): the nebula about once in
    // kGalaxyTurn seconds, the dust masks once in kDustTurn seconds, the second layer slower;
    // clockwise while the playback goes forward, the other way backward (also at 0x the
    // last direction), coming to rest softly at STOP, Freeze, without a file or displays.
    // Beta 2 (the user's second look): the dust turns clearly faster than the nebula, the
    // second layer slower, so that they visibly move against it.
    // Round 2 (the user's test): the turn follows SPEED, the base rate at 1x, at most
    // kMaxTurn times it, the other way backward; about 0x keeps kSlowestTurn of it in the
    // last direction. The dark clouds turn against the galaxy.
    static constexpr double kGalaxyTurn = 240.0, kDustTurn = 150.0, kTurnEase = 1.5;
    static constexpr double kMaxTurn = 4.0, kSlowestTurn = 0.05;
    double TurnSpeed() const { return turn_speed_; } // -kMaxTurn .. kMaxTurn
    // Round 5: one step of the turn's speed towards `target` over `dt` seconds, S-shaped
    // (critically damped, time constant kTurnEase); `rate` is the speed's own change.
    static void EaseTurn(double& speed, double& rate, double target, double dt) noexcept;
    double GalaxyAngle() const { return galaxy_angle_; }
    // The dust experiment (the user's brief of 2026-11-20): one of three black dust masks
    // and the second layer of dark clouds turn over the nebula and only darken it,
    // C x (1 - strength x mask), at most 5 % in its centre. GALAXY only, behind the grains.
    enum class Dust { kWisps, kVeil, kBands };
    struct DustChoice {
        Dust kind = Dust::kWisps;
        double strength = 0.5; // the darkest it darkens, 0 for none (round 2: clearly visible)
    };
    static constexpr double kMaxDust = 0.7;
    // Round 5: BLEND sets how strongly the dust darkens, from kDustBlendLeast to
    // kDustBlendMost times its strength, and grows it by up to kDustBlendGrow.
    static constexpr double kDustBlendLeast = 0.5, kDustBlendMost = 1.8, kDustBlendGrow = 0.12;
    // The choice for windows opened from now on (the preview program). The side-by-side
    // test build also reads the environment variable MESSIER_DUST, e.g. "wisps:20",
    // "bands:30", "veil:10" or "off".
    static void ChooseDust(DustChoice choice) noexcept;
    static DustChoice CurrentDust() noexcept;
    // Grains that ended and still glow at `now` (tests).
    std::size_t Afterglows(Clock::time_point now) const;
    // GALAXY's starfield (beta 2, the user's wishes): each grain is a star flying from near
    // the centre outwards while the playback goes forward, from the rim inwards backward,
    // and standing where it appeared at 0x; ever faster further out, as stars coming
    // closer; in a direction of its own, the same for its number, all round. Small and
    // bright near the centre, larger, darker and more diffuse further out; it glows on for
    // kStarGlow after its end.
    struct Star {
        double x = 0, y = 0;
    };
    static constexpr double kStarGlow = 1.2;
    // Round 4: a star keeps its shape at least this long, whatever its grain's length; its
    // light (0..1) and its rays (0..1) at `age` seconds after its birth, for a grain that
    // sounded `life` seconds. Both are 0 once its glow is over.
    static constexpr double kStarMinLife = 0.45;
    struct StarLight {
        double bright = 0, rays = 0;
    };
    static StarLight StarLook(double age, double life);
    // WAVE (round 3): where a grain lies down the plate: its panorama (lens::PanDown) plus
    // a spread of its own of up to kGrainSpread either way, fixed for its number.
    static constexpr double kGrainSpread = 0.4;
    static double GrainDown(const GrainDot& grain);
    static Star StarDirection(std::uint32_t number);                 // a unit vector
    static double StarStart(std::uint32_t number, double tempo);     // its distance at birth (0..1)
    static double StarFly(double reach, double tempo, double seconds); // its distance after flying

  private:
    double Ahead(Clock::time_point now) const;
    void DrawDropHighlight(VSTGUI::CDrawContext* context);
    void DrawWave(VSTGUI::CDrawContext* context, const VSTGUI::CRect& glass, Clock::time_point now);
    void DrawGalaxy(VSTGUI::CDrawContext* context, const VSTGUI::CRect& glass);
    void DrawDust(VSTGUI::CDrawContext* context, const VSTGUI::CRect& glass);
    double turn_direction_ = 1.0, turn_speed_ = 0.0, turn_rate_ = 0.0, galaxy_angle_ = 0.0;
    double TurnTarget(Clock::time_point now) noexcept; // also keeps the last direction
    // A layer drawn turned: its pixels and its turned bitmap, made again when the angle has
    // moved on by kTurnStep or the source changed.
    struct Turned {
        const void* source = nullptr; // taken only once its pixels are read (B2A.8)
        PixelBuffer pixels, turned; // the source, and the turned copy (reused, MS-R63)
        VSTGUI::SharedPointer<VSTGUI::CBitmap> drawn;
        double angle = 1e9;
    };
    // Degrees between two turned copies at full speed, finer while the turn is slow; the dust
    // masks are soft and turn in coarser steps (MS-R64).
    static constexpr double kTurnStep = 0.2, kDustTurnStep = 0.6;
    VSTGUI::CBitmap* TurnedNow(Turned& layer, VSTGUI::CBitmap* source, double angle,
                               double step = kTurnStep) noexcept;
    std::array<Turned, 2> nebula_turned_{}; // the nebula (or its arms), its haze
    DustChoice dust_ = CurrentDust();
    // Two layers: the chosen mask, and the second layer of dark clouds (dust_clouds),
    // turning more slowly. Each: its angle, the mask with the strength and the limits at its
    // centre and rim in its alpha (made for this skin), and its turned bitmap.
    struct DustLayer {
        double angle = 0;
        std::shared_ptr<const Skin> skin; // MS-R68: held, so no new skin takes its address
        VSTGUI::SharedPointer<VSTGUI::CBitmap> mask;
        Turned turned;
    };
    std::array<DustLayer, 2> dust_layers_{};
    static constexpr double kSecondDustSpeed = 0.4; // of the first layer's
    void DrawDustLayer(VSTGUI::CDrawContext* context, const VSTGUI::CRect& glass, DustLayer& layer,
                       SkinImageId id);
    void DrawStarfield(VSTGUI::CDrawContext* context, VSTGUI::CPoint centre, double radius, Clock::time_point now);
    // MS-R65: the stars' halo, glow and rays as small bitmaps, for the tints and the device
    // scale they were made for.
    struct Sprites {
        VSTGUI::CColor halo_ink, glow_ink, ray_ink;
        double scale = 0;
        VSTGUI::SharedPointer<VSTGUI::CBitmap> halo, glow, across, down;
    };
    Sprites sprites_;
    void UpdateSprites(VSTGUI::CDrawContext* context, VSTGUI::CColor core_ink) noexcept;
    void DrawRays(VSTGUI::CDrawContext* context, VSTGUI::CCoord x, VSTGUI::CCoord y, VSTGUI::CCoord reach,
                  double glow) const;
    // Each star's distance, flown on frame by frame with the tempo of the moment.
    struct StarState {
        std::uint32_t number = 0;
        double reach = 0;
        Clock::time_point at{};
        bool used = false;
    };
    std::array<StarState, 160> stars_{};
    std::size_t next_star_ = 0;
    // `heading`: the tempo the playback heads for (B2A.1), which decides where a new star is
    // born; `tempo`: the eased one it flies with.
    StarState& StarOf(std::uint32_t number, double age, double tempo, double heading, Clock::time_point now) noexcept;
    // A grain's glowing path (beta 2: along its line of the plate, bent by the lens): its
    // head at `share` of the file (where it was `since` seconds ago, 0 for a sounding
    // grain) moving `speed` shares a second; the path glows back to where it was `reach`
    // seconds ago, at most the chosen trail time.
    void DrawTrail(VSTGUI::CDrawContext* context, double share, double down, double speed, double since,
                   double reach, double thickness, double alpha);
    // A region of the plate between two shares of the file, over its whole height.
    void FillShares(VSTGUI::CDrawContext* context, double from, double to, VSTGUI::CColor colour);
    // A line of the plate down at a share of the file, from `top` to `bottom`.
    void StrokeDown(VSTGUI::CDrawContext* context, double share, double top, double bottom);

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
    bool galaxy_ = true;
    double blend_ = 0.5;
    double pitch_ = 0;
    double wave_gain_ = 1.0;
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
    VSTGUI::CColor cached_colour_; // beta 2: HUE turns the waveform's colour
    double cached_scale_ = 0;
    VSTGUI::CPoint cached_size_;
    bool wave_failed_ = false; // for the key above
    Clock::time_point wave_failed_at_{};
    // The galaxy's two layers (beta 2, until the delivered bitmaps: made in code), for the
    // colours, device scale and size they were made for.
    VSTGUI::SharedPointer<VSTGUI::CBitmap> galaxy_arms_, galaxy_haze_;
    // The delivered layers of the nebula, tinted for the skin and its colours (HUE).
    SkinImage tinted_arms_, tinted_haze_;
    // The coloured nebula with HUE's turn of its hue, for the skin, the turn and the size.
    SkinImage turned_nebula_;
    std::shared_ptr<const Skin> turned_skin_; // MS-R68: held, as cached_skin_
    double turned_by_ = 0;
    bool turned_two_ = false;
    std::shared_ptr<const Skin> tinted_skin_;
    // MS-R67: a new colour (HUE) is made at most every kRecolourEvery; meanwhile the last one
    // is drawn, and the pending one follows (Moving).
    static constexpr std::chrono::milliseconds kRecolourEvery{100};
    Clock::time_point recoloured_at_{};
    bool recolour_pending_ = false;
    bool Recolour(Clock::time_point now) noexcept; // true: make the new colour now
    VSTGUI::CColor tinted_a_, tinted_b_;
    VSTGUI::CColor galaxy_tint_a_, galaxy_tint_b_;
    double galaxy_scale_ = 0;
    double galaxy_radius_ = 0;
    GuiPreset galaxy_variant_ = GuiPreset::kSpiral; // beta 2: the nebula variant
    bool galaxy_failed_ = false;
    Clock::time_point galaxy_failed_at_{};
};

} // namespace messier
