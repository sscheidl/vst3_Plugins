#include "map_view.h"

#include "fisheye.h"
#include "gui_layout.h"
#include "guard.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/clinestyle.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/platformfactory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace messier {

using namespace VSTGUI;
namespace L = layout;

namespace {
constexpr double kPi = 3.14159265358979323846;
// A grain's glowing path (0.2.2): at most this share of the file (beta 1: 220 of 840
// units).
constexpr double kMaxTrailShare = 0.26;
// Round 4: the waveform's peaks drawn at this share of its body's light.
constexpr double kPeakLayer = 0.38;
// Round 2: a sounding grain's tail is at least this long (window units), to show its way.
constexpr double kMinTail = 7.0;
// The nebula's drift (0.2.2): window units a second per 1x of tempo, at most kMaxDrift,
// its speed eased towards the tempo's over about kDriftEase seconds.
constexpr double kDriftPerTempo = 9.0;
constexpr double kMaxDrift = 22.0;
constexpr double kDriftEase = 0.7;
// The plate is drawn again at least this often while the window is open (0.2.1: the
// nebula moves, playhead and grains move on between displays).
constexpr auto kFrameInterval = std::chrono::milliseconds(25);
// Between displays, playhead and grains move on for at most this long.
constexpr double kMaxAhead = 0.15;
// The same display for longer than this (a large host buffer is shorter): the engine
// rests, nothing moves on.
constexpr double kRestAfter = 0.1;
// 0.2.4 (MS-R53): how far, in seconds of its window, a grain of the next display may be
// from where the display before puts it and still be the same grain.
constexpr double kSameGrainSlack = 0.003;
// 0.2.3: without any new display for this long (the host stopped processing, the
// displays stopped coming) the nebula's drift comes to rest as at STOP.
constexpr double kDriftStaleAfter = 0.5;
// The nebulae's tiles repeat at this share of their width; the farther one drifts slower.
constexpr double kTileShare = 0.9;
constexpr double kFarDrift = 0.62;
// A waveform bitmap that could not be made is tried again after this (MS-R47).
constexpr auto kWaveRetry = std::chrono::seconds(2);
// The WAVE redesign, step 2: the depth layer is made at this device scale, whatever the
// window's (a blurred picture scales up without loss, and it stays cheap at 200 %), blurred
// by kDepthBlur window units, kDepthTall times as tall as the peaks, at kDepthLayer of the
// waveform's light.
constexpr double kDepthScale = 0.5;
constexpr double kDepthBlur = 9.0;
constexpr double kDepthTall = 1.3;
constexpr double kDepthLayer = 0.65;
// Step 1: the crisp outline of the peaks, at this share of the light.
constexpr double kOutline = 0.6;
// Round 6: the waveform's body and detail are drawn at this light (times WAVE).
constexpr double kWaveLight = 0.5;
// The detail layer (outline, band, bulge and the core) is drawn at kDetailLight, so that the
// core can be bright; outline, band and bulge are made kDetailDim fainter in it, which keeps
// them as they were.
constexpr double kDetailLight = 0.85;
constexpr double kDetailDim = kWaveLight / kDetailLight;
// Round 6 (the user's wish): the warm white core round the middle line, over at most
// kCoreHalf either side, fading with kCoreWidth, full once the body reaches kCoreFull.
constexpr double kCoreHalf = 0.09, kCoreWidth = 0.045, kCoreFull = 0.25, kCoreLight = 0.95;
const VSTGUI::CColor kCoreInk(255, 238, 214);

double Seconds(MapView::Clock::duration d) {
    return std::chrono::duration<double>(d).count();
}

void FillPath(CDrawContext* context, CGraphicsPath* path, CColor colour) {
    context->setFillColor(colour);
    context->drawGraphicsPath(path, CDrawContext::kPathFilled);
}

CColor Mix(CColor a, CColor b, double k) {
    const auto mix = [&](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * k)); };
    return CColor(mix(a.red, b.red), mix(a.green, b.green), mix(a.blue, b.blue), mix(a.alpha, b.alpha));
}

// `position` wrapped into [from, from + length).
double Wrap(double position, double from, double length) {
    if(!(length > 0))
        return from;
    const double into = std::fmod(position - from, length);
    return from + (into < 0 ? into + length : into);
}
} // namespace

MapView::MapView(const CRect& size) : CView(size) {
    // Since 0.1.8 the map takes the mouse: START, SPRAY, and drops (VSTGUI looks for
    // drop targets only among views that take the mouse).
    setMouseEnabled(true);
}

void MapView::Show(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept {
    ShowAt(display, std::move(wave), Clock::now());
}

void MapView::ShowAt(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave,
                     Clock::time_point now) noexcept {
    if(std::memcmp(&display, &display_, sizeof display) == 0) {
        // The same again, the count of rendered quanta too: no audio block in between
        // (a large host buffer), or the engine rests (STOP, Bypass, a host that stopped
        // processing). Only a display unchanged for longer than a large buffer stops
        // playhead and grains where the display has them.
        if(!resting_ && Seconds(now - shown_at_) > kRestAfter) {
            resting_ = true;
            rest_at_ = now; // MS-R57: ends predicted until now stay ended
        }
        // 0.2.3: an overview that arrives late is drawn, but it is no new display: the
        // playhead's and grains' prediction and the resting go on as they were.
        if(wave != wave_) {
            wave_ = std::move(wave);
            Guarded([&] { invalid(); });
        }
        return;
    }
    // 0.2.2: the grains of the display before that have ended by now glow on a while;
    // another file clears them all (its grains read elsewhere).
    if(display.sequence != display_.sequence || display.frames != display_.frames) {
        ended_count_ = 0;
        stars_ = {}; // B2A.2: no star of the file before flies on
        next_star_ = 0;
    } else if(!resting_) {
        // MS-R53 (0.2.4): on the audio's own time, the quanta rendered between the two
        // displays, not the time between their arrivals. Each grain of the display before
        // is moved on by that time; one still inside its window must be in the new display
        // and takes the grain there that is nearest to where it should be; only then the
        // ended ones look for one left over (made just before its end).
        const std::uint32_t quanta = display.rendered - display_.rendered; // wraps as the count does
        const double audio = quanta * (LooperEngine::kQuantum / LooperEngine::kSampleRate);
        const std::uint32_t old_count = std::min(display_.grain_count, EngineDisplay::kMaxGrains);
        const std::uint32_t new_count = std::min(display.grain_count, EngineDisplay::kMaxGrains);
        std::array<bool, EngineDisplay::kMaxGrains> taken{};
        const auto take = [&](const GrainDot& g, double expected) {
            // MS-R55 (0.2.5): a grain whose start the engine numbered is the one with that
            // number, and no other.
            if(g.id != 0) {
                for(std::uint32_t k = 0; k < new_count; ++k)
                    if(!taken[k] && display.grains[k].id == g.id) {
                        taken[k] = true;
                        return true;
                    }
                return false;
            }
            // Without numbers (a display made elsewhere): the same grain has its pan, speed
            // and window rate for its whole life, its window where it should be by now (a
            // little slack for the input's FM).
            const double slack = 0.01 + g.phase_rate * kSameGrainSlack;
            int best = -1;
            double best_miss = slack;
            for(std::uint32_t k = 0; k < new_count; ++k) {
                const GrainDot& h = display.grains[k];
                if(taken[k] || h.id != 0 || h.pan != g.pan || h.speed != g.speed || h.phase_rate != g.phase_rate)
                    continue;
                const double miss = std::abs(h.phase - expected);
                if(miss <= best_miss) {
                    best = static_cast<int>(k);
                    best_miss = miss;
                }
            }
            if(best >= 0)
                taken[static_cast<std::size_t>(best)] = true;
            return best >= 0;
        };
        const bool usable = quanta > 0 && audio < 10.0;
        if(usable)
            for(std::uint32_t i = 0; i < old_count; ++i) {
                const GrainDot& g = display_.grains[i];
                const double expected = g.phase + g.phase_rate * audio;
                if(g.phase_rate > 0 && g.phase < 1 && expected < 1)
                    take(g, expected);
            }
        for(std::uint32_t i = 0; usable && i < old_count; ++i) {
            const GrainDot& g = display_.grains[i];
            if(!(g.phase_rate > 0) || !(g.phase < 1))
                continue;
            const double left = (1.0 - g.phase) / g.phase_rate; // audio seconds it still sounded
            // Compared in seconds before a time point is made (MS-R52).
            // B2A.3: kept as long as either view shows it (GALAXY's stars glow longer than
            // WAVE's trail, which DrawTrail cuts at kTrail itself).
            constexpr double keep = std::max(kTrail, kStarMinLife + kStarGlow);
            if(!(left <= audio) || audio - left >= keep)
                continue; // sounds on in the new display, or its glow is gone
            if(take(g, g.phase + g.phase_rate * audio))
                continue; // the new display was made just before its end
            Ended ended;
            ended.dot = g;
            ended.dot.position = static_cast<float>(Wrap(g.position + g.speed * left, 0.0, display_.frames));
            // It ended `audio - left` seconds before the new display.
            ended.at = now - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(audio - left));
            ended.life = 1.0 / g.phase_rate;
            Keep(ended);
        }
    }
    // MS-R45: nothing else is kept from the display before; each carries its own speeds.
    resting_ = false;
    shown_at_ = now;
    display_ = display;
    wave_ = std::move(wave);
    Guarded([&] { invalid(); }); // a noexcept function (MS-R37)
}

void MapView::Keep(const Ended& ended) noexcept {
    if(ended_count_ < kMaxEnded) {
        ended_[ended_count_++] = ended;
        return;
    }
    // Full: in place of the one that ended first.
    std::size_t oldest = 0;
    for(std::size_t i = 1; i < kMaxEnded; ++i)
        if(ended_[i].at < ended_[oldest].at)
            oldest = i;
    if(ended_[oldest].at < ended.at)
        ended_[oldest] = ended;
}

std::size_t MapView::Afterglows(Clock::time_point now) const {
    std::size_t glowing = 0;
    for(std::size_t i = 0; i < ended_count_; ++i)
        glowing += now >= ended_[i].at && Seconds(now - ended_[i].at) < kTrail ? 1 : 0;
    return glowing;
}

double MapView::DriftTarget() const {
    const EngineDisplay& d = display_;
    if(stopped_ || resting_ || d.frozen || d.frames == 0)
        return 0.0;
    // The effective signed tempo: the core's own playhead speed through the file over the
    // file's rate (1 forward at the file's pitch). Never the change of the read position,
    // which jumps back at every turn of the loop or the file.
    const double rate = wave_ && wave_->sequence == d.sequence && wave_->rate > 0 ? wave_->rate : 48000.0;
    const double tempo = d.head_speed / rate;
    return std::isfinite(tempo) ? std::clamp(tempo * kDriftPerTempo, -kMaxDrift, kMaxDrift) : 0.0;
}

void MapView::AdvanceDrift(Clock::time_point now) noexcept {
    const double dt = drift_at_ == Clock::time_point{} ? 0.0 : std::clamp(Seconds(now - drift_at_), 0.0, 0.1);
    drift_at_ = now;
    // 0.2.3: no new display for a while, and the drift rests as at STOP.
    const double target = Seconds(now - shown_at_) > kDriftStaleAfter ? 0.0 : DriftTarget();
    drift_speed_ += (target - drift_speed_) * (1.0 - std::exp(-dt / kDriftEase));
    if(target == 0.0 && std::abs(drift_speed_) < 0.01)
        drift_speed_ = 0.0; // at rest
    // 0.2.3: each nebula's place as a share of its tile period (which does not swell), so
    // that neither the swell nor a long drift makes a tile jump.
    const double period = std::max(1.0, (getViewSize().getWidth() + 120) * kTileShare);
    const auto turn = [](double share) { return share - std::floor(share); };
    drift_a_ = turn(drift_a_ + drift_speed_ * dt / period);
    drift_b_ = turn(drift_b_ + kFarDrift * drift_speed_ * dt / period);
    // Beta 2 (the user's DAW test): the glass' layers turn steadily and come to rest softly
    // where the drift rests for a reason other than 0x. Round 2: as fast as the playback,
    // the base rate at 1x, the other way backward, eased (kTurnEase); at about 0x a slow
    // drift in the last direction, so that the picture never freezes while it plays.
    const double turn_target = TurnTarget(now);
    EaseTurn(turn_speed_, turn_rate_, turn_target, dt);
    const auto degrees = [](double a) { return a - 360.0 * std::floor(a / 360.0); };
    galaxy_angle_ = degrees(galaxy_angle_ + turn_speed_ * dt * 360.0 / kGalaxyTurn);
    // Round 2: the dark clouds turn against the galaxy, so that they show by their motion.
    for(std::size_t k = 0; k < dust_layers_.size(); ++k) {
        const double speed = k == 0 ? 1.0 : kSecondDustSpeed;
        dust_layers_[k].angle = degrees(dust_layers_[k].angle - turn_speed_ * dt * 360.0 / kDustTurn * speed);
    }
}

void MapView::EaseTurn(double& speed, double& rate, double target, double dt) noexcept {
    // Round 5 (the user's test: FREEZE started and stopped the turn a little jerkily): a
    // critically damped follower, so that the speed leaves and reaches its target along an S
    // without overshoot, and its change of speed has no step either.
    if(!(dt > 0.0) || !std::isfinite(target))
        return;
    constexpr double omega = 2.0 / kTurnEase;
    rate += (omega * omega * (target - speed) - 2.0 * omega * rate) * dt;
    speed += rate * dt;
    if(target == 0.0 && std::abs(speed) < 0.001 && std::abs(rate) < 0.001)
        speed = rate = 0.0;
}

double MapView::TurnTarget(Clock::time_point now) noexcept {
    const EngineDisplay& d = display_;
    const double rate = wave_ && wave_->sequence == d.sequence && wave_->rate > 0 ? wave_->rate : 48000.0;
    const double tempo = d.head_speed / rate;
    if(!std::isfinite(tempo))
        return 0.0;
    if(std::abs(tempo) > kSlowestTurn)
        turn_direction_ = tempo > 0 ? 1.0 : -1.0;
    const bool rest = stopped_ || resting_ || d.frozen || d.frames == 0 || Seconds(now - shown_at_) > kDriftStaleAfter;
    if(rest)
        return 0.0;
    if(std::abs(tempo) < kSlowestTurn)
        return turn_direction_ * kSlowestTurn;
    return std::clamp(tempo, -kMaxTurn, kMaxTurn);
}

double MapView::Ahead(Clock::time_point now) const {
    return resting_ ? 0.0 : std::clamp(Seconds(now - shown_at_), 0.0, kMaxAhead);
}

double MapView::GrainSpeed(std::uint32_t index) const {
    return index < std::min(display_.grain_count, EngineDisplay::kMaxGrains) ? display_.grains[index].speed : 0.0;
}

double MapView::GrainAt(std::uint32_t index, Clock::time_point now) const {
    if(index >= std::min(display_.grain_count, EngineDisplay::kMaxGrains) || display_.frames == 0)
        return 0.0;
    const GrainDot& g = display_.grains[index];
    // The core wraps a grain around the whole file.
    return Wrap(g.position + g.speed * Ahead(now), 0.0, display_.frames);
}

double MapView::HeadAt(Clock::time_point now) const {
    const EngineDisplay& d = display_;
    if(d.frames == 0)
        return 0.0;
    if(d.frozen || d.head_speed == 0)
        return d.playhead;
    // MS-R46: inside the loop of the display the playhead belongs to, not the loop that
    // START and SIZE in the window will make.
    return Wrap(d.playhead + d.head_speed * Ahead(now), d.loop_start, d.loop_length);
}

double MapView::EndedFor(std::uint32_t index, Clock::time_point now) const {
    if(index >= std::min(display_.grain_count, EngineDisplay::kMaxGrains))
        return -1.0;
    const GrainDot& g = display_.grains[index];
    if(!(g.phase_rate > 0))
        return -1.0;
    // MS-R54 (0.2.4): on the wall clock, not stopped where the prediction of places stops
    // (kMaxAhead): a grain predicted to have ended fades out even if no display comes.
    // MS-R57 (0.2.5): once the display rests, no grain ends any more, but one predicted to
    // have ended before keeps fading on the wall clock.
    const double wall = std::max(0.0, Seconds(now - shown_at_));
    const double ended_by = resting_ ? std::max(0.0, Seconds(rest_at_ - shown_at_)) : wall;
    const double left = (1.0 - g.phase) / g.phase_rate;
    return ended_by >= left ? wall - left : -1.0;
}

MapView::Loop MapView::ShownLoop(Clock::time_point now) const {
    const double frames = display_.frames;
    if(frames <= 0)
        return {};
    if(start_known_ && size_known_ && now - loop_changed_at_ < kPreview) {
        // A moment after the window's edit: the loop as the core will set it (0.2.1).
        const double start = std::min(start_ * frames, frames - 1);
        return {start, std::max(1.0, size_ * size_ * (frames - start))};
    }
    return {display_.loop_start, display_.loop_length};
}

CPoint MapView::GlassCentre() const {
    const CRect all = getViewSize();
    return CPoint(all.getWidth() / 2, all.getHeight() / 2);
}

double MapView::GlassRadius() const {
    const CRect all = getViewSize();
    return std::max(1.0, std::min(all.getWidth(), all.getHeight()) / 2 - L::kAxisRoom - L::kRingWidth);
}

CPoint MapView::PlateToView(double share, double down) const {
    const lens::Point p = lens::ToGlass(share, down);
    const CPoint c = GlassCentre();
    const double r = GlassRadius();
    return CPoint(c.x + p.x * r, c.y + p.y * r);
}

double MapView::ShareAt(double x, double y) const {
    const CPoint c = GlassCentre();
    const double r = GlassRadius();
    return lens::FromGlass((x - c.x) / r, (y - c.y) / r).x;
}

bool MapView::hitTest(const CPoint& where, const Event& event) {
    if(!CView::hitTest(where, event))
        return false;
    const CRect all = getViewSize();
    const CPoint c = GlassCentre();
    return std::hypot(where.x - all.left - c.x, where.y - all.top - c.y) <= GlassRadius() + L::kRingWidth;
}

void MapView::SetGalaxy(bool galaxy) noexcept {
    if(galaxy == galaxy_)
        return;
    galaxy_ = galaxy;
    Guarded([&] { invalid(); });
}

void MapView::SetSkin(std::shared_ptr<const Skin> skin) noexcept {
    skin_ = std::move(skin);
    Guarded([&] { invalid(); });
}

void MapView::SetPitch(double octaves) noexcept {
    pitch_ = std::isfinite(octaves) ? std::clamp(octaves, -3.0, 2.0) : 0.0;
}

CColor MapView::PitchTint(const CColor& warm, double octaves) {
    // 0.2.3 (brief point 13), beta 2 as a Doppler shift (the user's DAW test): warm at 0
    // octaves, towards red down to -3 and blue up to +2, up to 85 % of the way.
    if(!std::isfinite(octaves) || octaves == 0)
        return warm;
    const double share = octaves < 0 ? std::min(1.0, -octaves / 3.0) : std::min(1.0, octaves / 2.0);
    const CColor to = octaves < 0 ? CColor(255, 72, 56) : CColor(84, 140, 255);
    const double k = 0.85 * std::pow(share, 0.7);
    const auto mix = [&](uint8_t a, uint8_t b) { return static_cast<uint8_t>(std::lround(a + (b - a) * k)); };
    return CColor(mix(warm.red, to.red), mix(warm.green, to.green), mix(warm.blue, to.blue), warm.alpha);
}

void MapView::SetBlend(double blend) noexcept {
    blend_ = std::clamp(blend, 0.0, 1.0);
}

void MapView::SetWave(std::int32_t percent) noexcept {
    const double gain = std::clamp(percent, kWaveMinPercent, kWaveMaxPercent) / 100.0;
    if(gain == wave_gain_)
        return;
    wave_gain_ = gain;
    Guarded([&] { invalid(); });
}

void MapView::SetStart(double start) noexcept {
    start = std::clamp(start, 0.0, 1.0);
    const bool changed = !start_known_ || start != start_;
    start_ = start;
    start_known_ = true;
    if(changed) {
        loop_changed_at_ = Clock::now();
        Guarded([&] { invalid(); }); // the slider and the loop follow at once
    }
}

void MapView::SetSize(double size) noexcept {
    size = std::clamp(size, 0.0, 1.0);
    const bool changed = !size_known_ || size != size_;
    size_ = size;
    size_known_ = true;
    if(changed) {
        loop_changed_at_ = Clock::now();
        Guarded([&] { invalid(); });
    }
}

void MapView::Tick() noexcept {
    const Clock::time_point now = Clock::now();
    if(now - drawn_at_ >= kFrameInterval && Moving(now))
        Guarded([&] {
            CRect box = GlassBox();
            box.offset(getViewSize().getTopLeft());
            invalidRect(box);
        });
}

bool MapView::Moving(Clock::time_point now) const {
    const EngineDisplay& d = display_;
    if(turn_speed_ != 0.0 || turn_rate_ != 0.0 || drift_speed_ != 0.0 || recolour_pending_)
        return true;
    if(start_known_ && size_known_ && now - loop_changed_at_ < kPreview + kFrameInterval)
        return true;
    if(d.frames > 0 && !resting_ && (d.grain_count > 0 || (!d.frozen && d.head_speed != 0.0f)))
        return true; // grains, and the playhead between displays
    for(std::size_t i = 0; i < ended_count_; ++i)
        if(now >= ended_[i].at && Seconds(now - ended_[i].at) < kStarMinLife + kStarGlow)
            return true; // an afterglow
    // The turn and drift come to rest on the next frame after their target fell to 0.
    return d.frames > 0 && DriftTarget() != 0.0;
}

CRect MapView::GlassBox() const {
    const CPoint c = GlassCentre();
    const double reach = GlassRadius() + L::kRingWidth + 2; // the ring and the position triangle
    return CRect(c.x - reach, c.y - reach, c.x + reach, c.y + reach);
}

bool MapView::Recolour(Clock::time_point now) noexcept {
    if(now - recoloured_at_ < kRecolourEvery) {
        recolour_pending_ = true;
        return false;
    }
    recoloured_at_ = now;
    recolour_pending_ = false;
    return true;
}

void MapView::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}


namespace {

// The scale of device pixels a bitmap for this context needs, in quarters.
double DeviceScale(CDrawContext* context) {
    const CGraphicsTransform t = context->getCurrentTransform();
    return std::max(1.0, std::round(context->getScaleFactor() * std::abs(t.m11) * 4) / 4);
}

// A soft round cloud: `colour` at `alpha` in its middle, fading out to its edge.
void Blob(CDrawContext* context, CPoint centre, double radius, CColor colour, double alpha) {
    auto path = owned(context->createGraphicsPath());
    auto gradient = owned(CGradient::create(0.0, 1.0, WithAlpha(colour, alpha), WithAlpha(colour, 0.0)));
    if(!path || !gradient)
        return;
    gradient->addColorStop(0.45, WithAlpha(colour, alpha * 0.45));
    path->addEllipse(CRect(centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius));
    context->fillRadialGradient(path, *gradient, centre, radius);
}

// One layer of a nebula (beta 2, a stand-in made in code until the delivered bitmaps of
// docs/GRAPHICS_BRIEF_BETA2.md), of soft clouds: for the spiral the arms, two logarithmic
// spirals round a lavender core, or the haze, a looser spiral of larger and fainter
// clouds; the barred spiral the same from the ends of a bar; the ring nebula a textured
// ring and a faint halo; the diffuse nebula irregular clumps. Everything inside the circle,
// so that the square's corners stay empty when it turns. No points: every point on the
// plate is a grain.
SharedPointer<CBitmap> MakeGalaxyLayer(double size, double scale, CColor tint_a, CColor tint_b, bool arms,
                                       GuiPreset variant) {
    auto offscreen = COffscreenContext::create(CPoint(size, size), scale);
    if(!offscreen)
        return nullptr;
    offscreen->beginDraw();
    offscreen->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const double r = size / 2;
    const CPoint c(r, r);
    std::uint32_t seed = arms ? 20261120u : 4711u;
    const auto random = [&] {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) / 16777216.0;
    };
    const CColor white(255, 255, 255);
    const auto cloud = [&](double angle, double from_centre, double radius, CColor colour, double alpha) {
        radius = std::min(radius, r * 0.98 - from_centre);
        if(radius <= 0.5)
            return;
        Blob(offscreen, CPoint(c.x + std::cos(angle) * from_centre, c.y + std::sin(angle) * from_centre), radius,
             colour, alpha);
    };
    if(variant == GuiPreset::kRing) {
        const int count = arms ? 150 : 50;
        for(int i = 0; i < count; ++i) {
            const double angle = random() * 2 * kPi;
            const double along = arms ? r * (0.38 + 0.22 * random()) * (1.0 + 0.12 * std::cos(2 * angle))
                                      : r * (0.2 + 0.6 * random());
            cloud(angle, along, r * (arms ? 0.05 + 0.07 * random() : 0.15 + 0.15 * random()),
                  Mix(arms ? Mix(tint_a, white, 0.2) : tint_b, tint_b, random() * 0.6), arms ? 0.32 * (0.5 + 0.5 * random())
                                                                                              : 0.12);
        }
        if(arms)
            Blob(offscreen, c, r * 0.3, tint_b, 0.25);
    } else if(variant == GuiPreset::kCloud) {
        const int count = arms ? 90 : 40;
        for(int i = 0; i < count; ++i) {
            // Clumps around a few centres, not round the middle.
            const double centre_angle = std::floor(random() * 5) * 1.3;
            const double centre_along = r * 0.3 * (0.4 + 0.6 * std::fmod(centre_angle * 7.1, 1.0));
            const double x = std::cos(centre_angle) * centre_along + (random() - 0.5) * r * 0.6;
            const double y = std::sin(centre_angle) * centre_along + (random() - 0.5) * r * 0.5;
            cloud(std::atan2(y, x), std::hypot(x, y), r * (arms ? 0.06 + 0.12 * random() : 0.18 + 0.2 * random()),
                  Mix(Mix(tint_a, white, arms ? 0.25 * random() : 0.0), tint_b, random()), arms ? 0.35 * random() : 0.14);
        }
    } else if(arms) {
        const bool barred = variant == GuiPreset::kBarred;
        if(barred)
            for(int i = -8; i <= 8; ++i)
                cloud(0.0, i * r * 0.035, r * 0.1, Mix(tint_a, white, 0.35), 0.35);
        for(int arm = 0; arm < 2; ++arm)
            for(int i = 0; i < 140; ++i) {
                const double theta = i * 0.075;
                const double along = (barred ? r * 0.28 : 0.0) + r * 0.07 * std::exp((barred ? 0.22 : 0.28) * theta);
                if(along > r * 0.9)
                    break;
                const double share = along / r;
                const double angle = theta + arm * kPi + (random() - 0.5) * 0.3;
                const double from_centre = along + (random() - 0.5) * r * 0.07;
                cloud(angle, from_centre, r * (0.05 + 0.15 * share) * (0.7 + 0.6 * random()),
                      Mix(Mix(tint_a, white, 0.25 * (1 - share)), tint_b, share),
                      (0.5 - 0.36 * share) * (0.6 + 0.4 * random()));
            }
        Blob(offscreen, c, r * 0.34, Mix(tint_a, white, 0.45), 0.8);
        Blob(offscreen, c, r * 0.13, Mix(tint_a, white, 0.8), 0.85);
    } else {
        for(int arm = 0; arm < 2; ++arm)
            for(int i = 0; i < 40; ++i) {
                const double theta = 0.6 + i * 0.2;
                const double along = r * 0.12 * std::exp(0.22 * theta);
                if(along > r * 0.85)
                    break;
                const double share = along / r;
                cloud(theta + arm * kPi + (random() - 0.5) * 0.6, along,
                      r * (0.12 + 0.2 * share) * (0.7 + 0.6 * random()), Mix(tint_b, tint_a, 0.3 * random()),
                      0.2 * (1.0 - 0.6 * share));
            }
        Blob(offscreen, c, r * 0.8, tint_b, 0.25);
    }
    offscreen->endDraw();
    return offscreen->getBitmap();
}

} // namespace

void MapView::DrawGalaxy(CDrawContext* context, const CRect& glass) {
    if(!skin_)
        return;
    const SkinColours& c = skin_->Colours();
    // Round 2 (the user's decision): WAVE sets its brightness here as it sets the
    // waveform's in WAVE; above 100 % a second, fainter pass brightens it. Round 5 (the
    // user's experiment): BLEND no longer fades it; it thickens the dark clouds (DrawDust).
    const double level = wave_gain_;
    const auto paint = [&](CBitmap* bitmap) {
        if(!bitmap)
            return;
        context->drawBitmap(bitmap, glass, CPoint(0, 0), static_cast<float>(std::min(1.0, level)));
        if(level > 1.0)
            context->drawBitmap(bitmap, glass, CPoint(0, 0), static_cast<float>(std::min(1.0, level - 1.0)));
    };
    // Beta 2, after the user's DAW test: the nebula stands still (turned bitmaps were cut to
    // wrong rectangles by Direct2D in a host); the dust mask turns over it (DrawDust).
    const SkinImage& coloured = skin_->Image(SkinImageId::kNebula);
    const SkinImage& arms = skin_->Image(SkinImageId::kNebulaArms);
    const SkinImage& haze = skin_->Image(SkinImageId::kNebulaHaze);
    if(coloured) {
        // The user's wish: the nebula in colour; HUE turns its hue by as much as it turns
        // the nebula's colour from the design's.
        double design = 0, now_hue = 0, saturation = 0, lightness = 0;
        skin_->BaseColours().nebula_a.toHSL(design, saturation, lightness);
        c.nebula_a.toHSL(now_hue, saturation, lightness);
        const double by = std::remainder(now_hue - design, 360.0);
        const bool two = DeviceScale(context) > 1.01;
        // MS-R67: a new HUE alone waits for Recolour; another skin or size is made at once.
        const bool other = turned_skin_ != skin_ || turned_two_ != two || !turned_nebula_;
        if(other || (turned_by_ != by && Recolour(Clock::now()))) {
            turned_skin_ = skin_;
            turned_by_ = by;
            turned_two_ = two;
            turned_nebula_ = std::abs(by) < 0.5 ? coloured : HueTurnedCopy(coloured, by, two);
        }
        // MS-R63: the size the device needs (HueTurnedCopy makes only that one).
        CBitmap* source = two && turned_nebula_.x2 ? turned_nebula_.x2.get()
                          : turned_nebula_.x1   ? turned_nebula_.x1.get()
                                                : turned_nebula_.x2.get();
        paint(TurnedNow(nebula_turned_[0], source, galaxy_angle_));
    } else if(arms || haze) {
        // The white layers of the first delivery, in the nebula's colours; tinted again
        // only for another skin or HUE.
        const bool other = tinted_skin_ != skin_ || (!tinted_arms_ && !tinted_haze_);
        if(other || ((tinted_a_ != c.nebula_a || tinted_b_ != c.nebula_b) && Recolour(Clock::now()))) {
            tinted_skin_ = skin_;
            tinted_a_ = c.nebula_a;
            tinted_b_ = c.nebula_b;
            tinted_arms_ = arms ? TintedCopy(arms, c.nebula_a) : SkinImage{};
            tinted_haze_ = haze ? TintedCopy(haze, c.nebula_b) : SkinImage{};
        }
        const bool two = DeviceScale(context) > 1.01;
        const auto pick = [&](const SkinImage& image) {
            return two && image.x2 ? image.x2.get() : image.x1 ? image.x1.get() : image.x2.get();
        };
        paint(TurnedNow(nebula_turned_[1], pick(tinted_haze_), galaxy_angle_));
        paint(TurnedNow(nebula_turned_[0], pick(tinted_arms_), galaxy_angle_));
    } else {
        // No bitmaps: the stand-in made in code.
        const double scale = DeviceScale(context);
        const double radius = glass.getWidth() / 2;
        const auto now = Clock::now();
        const bool same_key = galaxy_tint_a_ == c.nebula_a && galaxy_tint_b_ == c.nebula_b && galaxy_scale_ == scale
                              && galaxy_radius_ == radius && galaxy_variant_ == skin_->Preset();
        if(!galaxy_arms_ || !galaxy_haze_ || !same_key) {
            // As the waveform's bitmap (MS-R47): one that could not be made is not tried
            // again on every frame.
            if(same_key && galaxy_failed_ && now - galaxy_failed_at_ < kWaveRetry)
                return;
            galaxy_tint_a_ = c.nebula_a;
            galaxy_tint_b_ = c.nebula_b;
            galaxy_scale_ = scale;
            galaxy_radius_ = radius;
            galaxy_failed_ = true;
            galaxy_failed_at_ = now;
            galaxy_variant_ = skin_->Preset();
            galaxy_arms_ = MakeGalaxyLayer(glass.getWidth(), scale, c.nebula_a, c.nebula_b, true, galaxy_variant_);
            galaxy_haze_ = MakeGalaxyLayer(glass.getWidth(), scale, c.nebula_a, c.nebula_b, false, galaxy_variant_);
            galaxy_failed_ = !galaxy_arms_ || !galaxy_haze_;
        }
        if(!galaxy_arms_ || !galaxy_haze_)
            return;
        paint(TurnedNow(nebula_turned_[1], galaxy_haze_, galaxy_angle_));
        paint(TurnedNow(nebula_turned_[0], galaxy_arms_, galaxy_angle_));
    }
    DrawDust(context, glass);
}

namespace {
MapView::DustChoice g_dust_choice;
bool g_dust_chosen = false;
} // namespace

void MapView::ChooseDust(DustChoice choice) noexcept {
    choice.strength = std::isfinite(choice.strength) ? std::clamp(choice.strength, 0.0, kMaxDust) : 0.0;
    g_dust_choice = choice;
    g_dust_chosen = true;
}

MapView::DustChoice MapView::CurrentDust() noexcept {
    DustChoice choice = g_dust_choice;
#ifdef MESSIER_SIDE_BY_SIDE
    // The side-by-side test build: MESSIER_DUST, for comparing in a host without a control.
    if(!g_dust_chosen) {
        char* text = nullptr;
        std::size_t length = 0;
        if(_dupenv_s(&text, &length, "MESSIER_DUST") == 0 && text) {
            const std::string value(text);
            std::free(text);
            if(value == "off") {
                choice.strength = 0;
            } else {
                const std::size_t colon = value.find(':');
                const std::string kind = value.substr(0, colon);
                choice.kind = kind == "veil" ? Dust::kVeil : kind == "bands" ? Dust::kBands : Dust::kWisps;
                if(colon != std::string::npos)
                    choice.strength = std::clamp(std::atof(value.c_str() + colon + 1) / 100.0, 0.0, kMaxDust);
            }
        }
    }
#endif
    return choice;
}

CBitmap* MapView::TurnedNow(Turned& layer, CBitmap* source, double angle, double step) noexcept {
    if(!source)
        return nullptr;
    try {
        if(layer.source != source) {
            // B2A.8: the source is taken only once its pixels are read; a failed read is
            // tried again on the next frame.
            PixelBuffer pixels;
            if(!ReadPixels(source, pixels))
                return nullptr;
            layer.pixels = std::move(pixels);
            layer.source = source;
            layer.drawn = nullptr;
            layer.angle = 1e9;
        }
        const double moved = std::abs(std::remainder(angle - layer.angle, 360.0));
        // Round 5: finer steps while the turn is slow (easing in and out), so that it never
        // moves in visible jumps; about as many new bitmaps a second as at full speed.
        step *= std::clamp(std::abs(turn_speed_), 0.25, 1.0);
        if(!layer.drawn || moved >= step) {
            TurnPixels(layer.pixels, angle, layer.turned); // MS-R63: its buffer kept
            if(auto bitmap = MakeBitmap(layer.turned)) {
                layer.drawn = bitmap;
                layer.angle = angle;
            }
        }
    } catch(...) {
    }
    return layer.drawn;
}

void MapView::DrawDust(CDrawContext* context, const CRect& glass) {
    if(!skin_ || !(dust_.strength > 0))
        return;
    const SkinImageId id = dust_.kind == Dust::kVeil    ? SkinImageId::kDustVeil
                           : dust_.kind == Dust::kBands ? SkinImageId::kDustBands
                                                        : SkinImageId::kDustWisps;
    DrawDustLayer(context, glass, dust_layers_[0], id);
    DrawDustLayer(context, glass, dust_layers_[1], SkinImageId::kDustClouds);
}

void MapView::DrawDustLayer(CDrawContext* context, const CRect& glass, DustLayer& layer, SkinImageId id) {
    const SkinImage& mask = skin_->Image(id);
    CBitmap* source = mask.x1 ? mask.x1.get() : mask.x2.get();
    if(!source)
        return;
    if(layer.skin != skin_) {
        // The mask made black (only its alpha counts), scaled so that its darkest place
        // darkens by the strength (round 2: with firmer edges, its faint parts left out),
        // limited towards its centre (5 % within 50 units, the
        // full strength from 110 on) and cleared from 172 to 176 units, so that nothing of
        // its square shows. A soft mask: its 1x size serves at every zoom.
        // B2A.8: the skin is taken only once the mask is made (an empty mask counts).
        layer.mask = nullptr;
        layer.turned = {};
        PixelBuffer pixels;
        if(!ReadPixels(source, pixels))
            return;
        double darkest = 0;
        for(std::size_t i = 3; i < pixels.bytes.size(); i += 4)
            darkest = std::max(darkest, pixels.bytes[i] / 255.0);
        if(!(darkest > 0)) {
            layer.skin = skin_;
            return;
        }
        const double units = mask.size.x / pixels.width;
        const auto smooth = [](double x) {
            x = std::clamp(x, 0.0, 1.0);
            return x * x * (3 - 2 * x);
        };
        for(std::uint32_t y = 0; y < pixels.height; ++y)
            for(std::uint32_t x = 0; x < pixels.width; ++x) {
                std::uint8_t* px = &pixels.bytes[(std::size_t(y) * pixels.width + x) * 4];
                const double r = std::hypot(x + 0.5 - pixels.width / 2.0, y + 0.5 - pixels.height / 2.0) * units;
                const double limit = (0.05 + 0.95 * smooth((r - 50.0) / 60.0)) * (1.0 - smooth((r - 172.0) / 4.0));
                const double shape = smooth((px[3] / 255.0 / darkest - 0.12) / 0.55);
                const double alpha = std::min(shape * std::min(1.0, dust_.strength * kDustBlendMost), limit);
                px[0] = px[1] = px[2] = 0; // black, premultiplied
                px[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0));
            }
        layer.mask = MakeBitmap(pixels);
        if(layer.mask)
            layer.skin = skin_;
    }
    if(CBitmap* drawn = TurnedNow(layer.turned, layer.mask, layer.angle, kDustTurnStep)) {
        // Round 5: BLEND towards Granular thickens the dust: darker (the mask is made for
        // the strongest, drawn fainter at BLEND 0: kDustBlendLeast / kDustBlendMost of it)
        // and a little larger, round the glass' centre (its rim then lies under the ring).
        const double darker = (kDustBlendLeast + (kDustBlendMost - kDustBlendLeast) * blend_) / kDustBlendMost;
        const double grow = 1.0 + kDustBlendGrow * blend_;
        CRect area = glass;
        area.inset(-glass.getWidth() * (grow - 1.0) / 2, -glass.getHeight() * (grow - 1.0) / 2);
        if(grow != 1.0) {
            // Drawn at the larger size: scaled by the context, not by a turned copy.
            const CGraphicsTransform t(grow, 0, 0, grow, area.left - glass.left * grow, area.top - glass.top * grow);
            CDrawContext::Transform scaled(*context, t);
            context->drawBitmap(drawn, glass, CPoint(0, 0), static_cast<float>(darker));
        } else {
            context->drawBitmap(drawn, glass, CPoint(0, 0), static_cast<float>(darker));
        }
    }
}

namespace {
// An integer hash of a star's number, 0 .. 1, the same every time.
double StarHash(std::uint32_t number, std::uint32_t salt) {
    std::uint32_t h = (number + salt * 0x632BE5ABu) * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777216.0;
}
// The flight: the distance grows (or shrinks backward) as k x tempo x (c + distance).
constexpr double kStarRate = 1.39, kStarNear = 0.12;

// MS-R65 (the GUI review of beta 2): the soft parts of a star (halo, glow, rays) drawn once
// per colour into small bitmaps, then only scaled and faded per star: no paths or gradients
// per frame. A round sprite of kSpriteSize units, white at its middle (alpha 1), `stop_alpha`
// at `stop_at` of its radius, clear at its rim.
constexpr double kSpriteSize = 32, kRayLength = 64, kRayThickness = 2;
SharedPointer<CBitmap> MakeGlowSprite(double scale, CColor ink, double stop_at, double stop_alpha) {
    auto offscreen = COffscreenContext::create(CPoint(kSpriteSize, kSpriteSize), scale);
    if(!offscreen)
        return nullptr;
    offscreen->beginDraw();
    offscreen->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const double r = kSpriteSize / 2;
    if(auto path = owned(offscreen->createGraphicsPath())) {
        path->addEllipse(CRect(0, 0, kSpriteSize, kSpriteSize));
        if(auto gradient = owned(CGradient::create(0.0, 1.0, WithAlpha(ink, 1.0), WithAlpha(ink, 0.0)))) {
            gradient->addColorStop(stop_at, WithAlpha(ink, stop_alpha));
            offscreen->fillRadialGradient(path, *gradient, CPoint(r, r), r);
        }
    }
    offscreen->endDraw();
    return offscreen->getBitmap();
}

// Two diffraction rays through the middle, across or down, fading from it to both ends as
// beta 2's rays did (0.45 at 0.3 of their reach); kRayThickness thick, drawn 1.2 units.
SharedPointer<CBitmap> MakeRaySprite(double scale, CColor ink, bool across) {
    const CPoint size = across ? CPoint(kRayLength, kRayThickness) : CPoint(kRayThickness, kRayLength);
    auto offscreen = COffscreenContext::create(size, scale);
    if(!offscreen)
        return nullptr;
    offscreen->beginDraw();
    offscreen->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CPoint middle(size.x / 2, size.y / 2);
    for(const double side : {-1.0, 1.0}) {
        auto path = owned(offscreen->createGraphicsPath());
        auto gradient = owned(CGradient::create(0.0, 1.0, WithAlpha(ink, 1.0), WithAlpha(ink, 0.0)));
        if(!path || !gradient)
            continue;
        gradient->addColorStop(0.3, WithAlpha(ink, 0.45));
        const CPoint end = across ? CPoint(side < 0 ? 0 : size.x, middle.y) : CPoint(middle.x, side < 0 ? 0 : size.y);
        path->addRect(across ? CRect(std::min(middle.x, end.x), 0, std::max(middle.x, end.x), size.y)
                             : CRect(0, std::min(middle.y, end.y), size.x, std::max(middle.y, end.y)));
        offscreen->fillLinearGradient(path, *gradient, middle, end);
    }
    offscreen->endDraw();
    return offscreen->getBitmap();
}

// A sprite scaled into `dest` (axis-aligned: Direct2D's clip of a turned bitmap is not
// involved) and faded to `alpha`.
void DrawSprite(CDrawContext* context, CBitmap* sprite, const CRect& dest, double alpha) {
    if(!sprite || !(alpha > 0.004) || !(dest.getWidth() > 0) || !(dest.getHeight() > 0))
        return;
    const double sx = dest.getWidth() / sprite->getWidth(), sy = dest.getHeight() / sprite->getHeight();
    CDrawContext::Transform scaled(*context, CGraphicsTransform(sx, 0, 0, sy, dest.left, dest.top));
    context->drawBitmap(sprite, CRect(0, 0, sprite->getWidth(), sprite->getHeight()), CPoint(0, 0),
                        static_cast<float>(std::min(1.0, alpha)));
}

// Many grains at once (round 2): shorter and fainter rays, so that the plate and the
// playhead stay readable; 1 up to 6 grains.
double Calm(std::uint32_t grains) {
    return std::min(1.0, std::sqrt(6.0 / std::max<std::uint32_t>(1, grains)));
}
} // namespace

double MapView::GrainDown(const GrainDot& grain) {
    // Round 3 (the user's choice "pan plus spread"): its panorama, and a spread of its own,
    // the same for its whole life, so that the grains fill the waveform without jittering.
    const std::uint32_t number = grain.id ? grain.id : static_cast<std::uint32_t>(grain.position);
    const double spread = (2.0 * StarHash(number, 3) - 1.0) * kGrainSpread;
    return std::clamp(lens::PanDown(grain.pan) + spread, -0.92, 0.92);
}

MapView::Star MapView::StarDirection(std::uint32_t number) {
    const double angle = 2 * kPi * StarHash(number, 1);
    return {std::cos(angle), std::sin(angle)};
}

double MapView::StarStart(std::uint32_t number, double tempo) {
    if(tempo > 0.05)
        return 0.04; // near the centre, flying out
    if(tempo < -0.05)
        return 0.97; // at the rim, flying in
    return 0.15 + 0.75 * StarHash(number, 2); // standing anywhere
}

double MapView::StarFly(double reach, double tempo, double seconds) {
    if(!std::isfinite(reach) || !std::isfinite(tempo) || !std::isfinite(seconds) || tempo == 0.0 || seconds == 0.0)
        return reach; // standing
    return std::clamp((kStarNear + reach) * std::exp(kStarRate * tempo * seconds) - kStarNear, 0.0, 1.2);
}

MapView::StarState& MapView::StarOf(std::uint32_t number, double age, double tempo, double heading,
                                    Clock::time_point now) noexcept {
    for(StarState& star : stars_)
        if(star.used && star.number == number) {
            const double dt = std::clamp(Seconds(now - star.at), 0.0, 0.25);
            star.reach = StarFly(star.reach, tempo, dt);
            star.at = now;
            return star;
        }
    // A new one: where it would be had it flown at this tempo since its birth.
    StarState& star = stars_[next_star_];
    next_star_ = (next_star_ + 1) % stars_.size();
    star = {number, StarFly(StarStart(number, heading), tempo, std::clamp(age, 0.0, 3.0)), now, true};
    return star;
}

void MapView::UpdateSprites(CDrawContext* context, CColor core_ink) noexcept {
    // MS-R65: made again only for another tint (PITCH, HUE) or device scale.
    const double scale = DeviceScale(context);
    const CColor white(255, 255, 255);
    const CColor halo = tail_ink_, glow = Mix(core_ink, white, 0.6), rays = Mix(core_ink, white, 0.45);
    if(sprites_.halo && sprites_.scale == scale && sprites_.halo_ink == halo && sprites_.glow_ink == glow
       && sprites_.ray_ink == rays)
        return;
    try {
        sprites_ = {};
        sprites_.scale = scale;
        sprites_.halo_ink = halo;
        sprites_.glow_ink = glow;
        sprites_.ray_ink = rays;
        sprites_.halo = MakeGlowSprite(scale, halo, 0.35, 0.42);
        sprites_.glow = MakeGlowSprite(scale, glow, 0.4, 0.37);
        sprites_.across = MakeRaySprite(scale, rays, true);
        sprites_.down = MakeRaySprite(scale, rays, false);
    } catch(...) {
        sprites_ = {};
    }
}

void MapView::DrawRays(CDrawContext* context, CCoord x, CCoord y, CCoord reach, double glow) const {
    // Four diffraction rays of a grain's head (0.2.3 point 13; round 2: clearer), across
    // and down from (x, y), `reach` long, `glow` the alpha at the head, fading outwards.
    if(!(glow > 0.01) || !(reach > 1.0))
        return;
    constexpr CCoord half = 0.6;
    DrawSprite(context, sprites_.across, CRect(x - reach, y - half, x + reach, y + half), glow);
    DrawSprite(context, sprites_.down, CRect(x - half, y - reach, x + half, y + reach), glow);
}

MapView::StarLight MapView::StarLook(double age, double life) {
    // Round 4 (the user's test: most grains were plain dots): a star keeps its shape for at
    // least kStarMinLife, however short its grain; then it glows on for kStarGlow, its rays
    // fading over that time and gone with it.
    if(!std::isfinite(age) || !std::isfinite(life) || age < 0)
        return {};
    const double shape = std::max(life, kStarMinLife);
    if(age < shape) {
        const double x = age / shape;
        return {0.8 + 0.2 * std::sin(kPi * x), std::min(1.0, x / 0.12)};
    }
    const double u = (age - shape) / kStarGlow;
    if(u >= 1.0)
        return {};
    return {0.8 * std::pow(1.0 - u, 1.4), std::pow(1.0 - u, 2.0)};
}

void MapView::DrawStarfield(CDrawContext* context, CPoint centre, double radius, Clock::time_point now) {
    if(!skin_)
        return;
    const SkinColours& c = skin_->Colours();
    const EngineDisplay& d = display_;
    // The Doppler shift of PITCH: streaks, halo and core, red down, blue up.
    tail_ink_ = PitchTint(c.tail, pitch_);
    const CColor core_ink = PitchTint(c.grain, pitch_);
    UpdateSprites(context, core_ink);
    const double alpha = 0.85 + 0.15 * blend_;
    // The tempo of the moment, eased as the drift is (0 at STOP, Freeze and rest), and the
    // one it heads for (B2A.1: where new stars are born, also while the tempo eases in).
    const double tempo = drift_speed_ / kDriftPerTempo;
    const double heading = DriftTarget() / kDriftPerTempo;
    const auto smooth = [](double x) {
        x = std::clamp(x, 0.0, 1.0);
        return x * x * (3 - 2 * x);
    };
    const CColor white(255, 255, 255);
    // The stars of this frame: the ended ones still glowing, then the sounding ones, each by
    // its number and age (round 4: counted first, so that many make the rays calmer).
    struct Shown {
        std::uint32_t number;
        double age, life;
    };
    std::array<Shown, kMaxEnded + EngineDisplay::kMaxGrains> shown{};
    std::size_t count = 0;
    for(std::size_t i = 0; i < ended_count_; ++i) {
        const Ended& e = ended_[i];
        if(now < e.at)
            continue;
        const double age = e.life + Seconds(now - e.at);
        if(StarLook(age, e.life).bright > 0.0)
            shown[count++] = {e.dot.id ? e.dot.id : static_cast<std::uint32_t>(e.dot.position), age, e.life};
    }
    const double ahead = Ahead(now);
    for(std::uint32_t i = 0; i < std::min(d.grain_count, EngineDisplay::kMaxGrains); ++i) {
        const GrainDot& g = d.grains[i];
        if(!(g.phase_rate > 0))
            continue;
        const double life = 1.0 / g.phase_rate;
        const double since = EndedFor(i, now);
        const double age = since >= 0 ? life + since : std::clamp(g.phase + g.phase_rate * ahead, 0.0, 1.0) * life;
        shown[count++] = {g.id ? g.id : static_cast<std::uint32_t>(g.position), age, life};
    }
    const double calm = Calm(static_cast<std::uint32_t>(count));
    // One star: `age` seconds after its birth, `bright` 0..1, its rays 0..1.
    const auto star = [&](std::uint32_t number, double age, double bright, double rays) {
        StarState& state = StarOf(number, age, tempo, heading, now);
        const double reach = state.reach;
        if(!(reach > 0.015) || reach >= 1.0 || !(bright > 0.0))
            return;
        const Star dir = StarDirection(number);
        const double out = std::clamp(reach, 0.0, 1.0);
        const double edge = (1.0 - smooth((out - 0.9) / 0.1)) * smooth((reach - 0.015) / 0.03);
        const double light = alpha * bright * edge;
        const auto at = [&](double r) { return CPoint(centre.x + dir.x * r * radius, centre.y + dir.y * r * radius); };
        const CPoint h = at(out);
        // The streak behind it: where it was a little before, at this tempo.
        const double before = std::clamp(StarFly(reach, tempo, -0.15), 0.0, 1.0);
        if(std::abs(out - before) * radius > 1.0) {
            const CPoint t = at(before);
            constexpr int kPieces = 4;
            context->setLineWidth(0.7 + 2.6 * out);
            for(int k = 0; k < kPieces; ++k) {
                const double f0 = static_cast<double>(k) / kPieces, f1 = static_cast<double>(k + 1) / kPieces;
                context->setFrameColor(WithAlpha(tail_ink_, light * 0.6 * f1 * f1));
                context->drawLine(CPoint(t.x + (h.x - t.x) * f0, t.y + (h.y - t.y) * f0),
                                  CPoint(t.x + (h.x - t.x) * f1, t.y + (h.y - t.y) * f1));
            }
        }
        // Small and bright near the centre, larger, darker and more diffuse further out
        // (beta 2, the user's second look: clearly larger, as in the original).
        const double size = 0.8 + 6.5 * out;
        const CCoord halo = size * (2.2 + 2.5 * out);
        DrawSprite(context, sprites_.halo, CRect(h.x - halo, h.y - halo, h.x + halo, h.y + halo),
                   light * (0.6 - 0.25 * out));
        // Round 4 (the user's test: the dark seam of round 3 read as a black outline on the
        // painted galaxies): none; instead a soft light glow round a larger, brighter core,
        // which lightens whatever lies behind it.
        const CCoord core = 1.25 * size * (1.0 - 0.5 * out);
        const CCoord glow = core * 2.6 + 2.0;
        DrawSprite(context, sprites_.glow, CRect(h.x - glow, h.y - glow, h.x + glow, h.y + glow), light * 0.75);
        DrawRays(context, h.x, h.y, (core * 2.5 + 6.0 + 16.0 * rays) * (0.5 + 0.5 * calm),
                 light * rays * (0.4 + 0.6 * calm));
        context->setFillColor(WithAlpha(Mix(core_ink, white, 0.7), std::min(1.0, light * 1.1)));
        context->drawEllipse(CRect(h.x - core, h.y - core, h.x + core, h.y + core), kDrawFilled);
        const CCoord hot = core * 0.55;
        context->setFillColor(WithAlpha(white, std::min(1.0, light * 1.2)));
        context->drawEllipse(CRect(h.x - hot, h.y - hot, h.x + hot, h.y + hot), kDrawFilled);
    };
    for(std::size_t i = 0; i < count; ++i) {
        const StarLight look = StarLook(shown[i].age, shown[i].life);
        star(shown[i].number, shown[i].age, look.bright, look.rays);
    }
}

void MapView::WaveColumns(const WaveOverview& w, int count, std::vector<WaveColumn>& columns) {
    columns.clear();
    if(w.columns == 0 || count < 1)
        return;
    columns.reserve(static_cast<std::size_t>(count));
    const std::uint32_t total = std::min<std::uint32_t>(w.columns, WaveOverview::kMaxColumns);
    for(int k = 0; k < count; ++k) {
        const std::uint32_t c0 = std::min(total - 1, static_cast<std::uint32_t>(std::uint64_t(k) * total / count));
        const std::uint32_t c1 =
            std::max<std::uint32_t>(c0 + 1, static_cast<std::uint32_t>(std::uint64_t(k + 1) * total / count));
        int low = w.low[c0], high = w.high[c0], body = 0;
        for(std::uint32_t j = c0; j < c1 && j < total; ++j) {
            low = std::min<int>(low, w.low[j]);
            high = std::max<int>(high, w.high[j]);
            body = std::max<int>(body, w.rms[j]);
        }
        WaveColumn column;
        column.share = (k + 0.5) / count;
        column.top = -kWaveHeight * high / 32768.0;
        column.bottom = -kWaveHeight * low / 32768.0;
        column.body = std::clamp(kWaveHeight * body / 32768.0, 0.0, kWaveHeight * std::max(high, -low) / 32768.0);
        columns.push_back(column);
    }
}

void MapView::DrawWave(CDrawContext* context, const CRect& glass, Clock::time_point now) {
    if(!skin_ || !wave_ || wave_->columns == 0 || wave_->sequence != display_.sequence)
        return;
    const SkinColours& c = skin_->Colours();
    const double scale = DeviceScale(context);
    const CPoint size(glass.getWidth(), glass.getHeight());
    const std::array<CColor, 3> colours{c.plate_wave, c.nebula_a, c.nebula_b};
    const bool same_rest = cached_wave_ == wave_ && cached_skin_ == skin_ && cached_scale_ == scale
                           && cached_size_ == size;
    const bool same_key = same_rest && cached_colours_ == colours;
    // MS-R67: a new colour alone (HUE) is made at most every kRecolourEvery.
    bool rebuild = !wave_layers_.body || !same_key;
    if(rebuild && wave_layers_.body && same_rest && !Recolour(now))
        rebuild = false;
    if(rebuild) {
        // MS-R47: bitmaps that could not be made for this key are not tried on every
        // frame of the animation; the plate goes on without the waveform.
        if(same_key && wave_failed_ && now - wave_failed_at_ < kWaveRetry)
            return;
        wave_layers_ = {};
        cached_wave_ = wave_;
        cached_skin_ = skin_;
        cached_colours_ = colours;
        cached_scale_ = scale;
        cached_size_ = size;
        wave_failed_at_ = now;
        MakeWaveLayers(c, scale, size);
        wave_failed_ = !wave_layers_.body;
    }
    if(!wave_layers_.body)
        return;
    // 0.2.6: WAVE scales it (at most fully opaque). Clearer inside the loop: the veil
    // outside it follows in DrawUnchecked.
    // Round 3: drawn stronger, as its ends and peaks are darkened in the bitmap.
    // Round 6 (the user's test): more transparent (was 0.62).
    const float light = static_cast<float>(std::min(1.0, kWaveLight * wave_gain_));
    if(wave_layers_.depth)
        context->drawBitmap(wave_layers_.depth, glass, CPoint(0, 0),
                            static_cast<float>(std::min(1.0, kDepthLayer * wave_gain_)));
    context->drawBitmap(wave_layers_.body, glass, CPoint(0, 0), light);
    if(wave_layers_.detail)
        context->drawBitmap(wave_layers_.detail, glass, CPoint(0, 0),
                            static_cast<float>(std::min(1.0, kDetailLight * wave_gain_)));
}

void MapView::MakeWaveLayers(const SkinColours& c, double scale, CPoint size) {
    // Once per file, look and size: the waveform as lines into bitmaps, each column of the
    // file bent by the lens (beta 2). The WAVE redesign: in three layers, from one list of
    // columns per resolution.
    const WaveOverview& w = *wave_;
    const double r = size.x / 2;
    const CColor white(255, 255, 255);
    const auto at = [&](double share, double down) {
        const lens::Point p = lens::ToGlass(share, down);
        return CPoint(r + p.x * r, r + p.y * r);
    };
    // Round 3 (the user's wish): like a galaxy seen edge-on. The waveform sinks softly into
    // the dark glass at both ends and towards its peaks; it is brightest in the middle of
    // the file and along the middle line, with a thin bright band there and a glowing
    // bulge in the centre.
    const auto smooth = [](double x) {
        x = std::clamp(x, 0.0, 1.0);
        return x * x * (3 - 2 * x);
    };
    const auto along = [&](double share) {
        const double ends = smooth(share / 0.14) * smooth((1.0 - share) / 0.14);
        const double bulge = 0.55 + 0.45 * std::exp(-std::pow(share - 0.5, 2) / (0.24 * 0.24));
        return ends * bulge;
    };
    const auto across = [](double down) { return 0.28 + 0.72 * std::exp(-std::pow(down / 0.4, 2)); };
    // Step 3: the colour over the amplitude, in the preset's tints (and HUE's): a bright,
    // slightly warm core round the middle line, the wave colour half way, cooler and
    // deeper towards the peaks (the nebula's second tint).
    const CColor warm = Mix(Mix(c.plate_wave, white, 0.42), CColor(255, 214, 172), 0.2);
    const CColor cool = Mix(c.plate_wave, c.nebula_b, 0.65);
    const auto ink = [&](double down) {
        const double t = std::clamp(std::abs(down) / kWaveHeight, 0.0, 1.0);
        return t < 0.5 ? Mix(warm, c.plate_wave, t / 0.5) : Mix(c.plate_wave, cool, (t - 0.5) / 0.5);
    };
    // One column's span from `top` to `bottom` in pieces, each as bright as its height
    // allows; `lift` mixes in white.
    const auto span = [&](COffscreenContext& into, double pixel, double share, double top, double bottom,
                          double strength, double lift, bool coloured, CColor plain) {
        bottom += 0.5 * pixel / r;
        const int pieces = std::clamp(static_cast<int>(std::ceil((bottom - top) / 0.08)), 1, 24);
        for(int p = 0; p < pieces; ++p) {
            const double d0 = top + (bottom - top) * p / pieces, d1 = top + (bottom - top) * (p + 1) / pieces;
            const double middle = (d0 + d1) / 2;
            const CColor colour = coloured ? Mix(ink(middle), white, lift) : plain;
            into.setFrameColor(WithAlpha(colour, strength * across(middle)));
            into.drawLine(at(share, d0), at(share, d1));
        }
    };

    // The body: the translucent peaks and, over them, the dense RMS body (round 4, the
    // user's experiment with layers), now coloured over the amplitude (step 3).
    // The middle of the glass is enlarged by the lens: as many columns as it needs.
    WaveColumns(w, std::max(64, static_cast<int>(size.x * scale * (1.0 + lens::kLens))), wave_columns_);
    {
        auto offscreen = COffscreenContext::create(size, scale);
        if(!offscreen)
            return;
        offscreen->beginDraw();
        offscreen->setDrawMode(kAntiAliasing | kNonIntegralMode);
        offscreen->setLineWidth(1.0 / scale);
        for(const WaveColumn& column : wave_columns_) {
            const double light = along(column.share);
            if(!(light > 0.01))
                continue;
            span(*offscreen, 1.0 / scale, column.share, column.top, column.bottom, kPeakLayer * light, 0.0, true,
                 white);
            if(column.body > 0.0)
                span(*offscreen, 1.0 / scale, column.share, -column.body, column.body, light, 0.18, true, white);
        }
        offscreen->endDraw();
        wave_layers_.body = offscreen->getBitmap();
        if(!wave_layers_.body)
            return;
    }

    // Step 1: the detail: a crisp outline along both peaks, so that transients read
    // clearly; then the bright band along the middle line and the bulge (round 3).
    if(auto offscreen = COffscreenContext::create(size, scale)) {
        offscreen->beginDraw();
        offscreen->setDrawMode(kAntiAliasing | kNonIntegralMode);
        offscreen->setLineWidth(std::max(0.6, 1.0 / scale));
        const CColor edge = Mix(c.plate_wave, white, 0.55);
        for(std::size_t k = 1; k < wave_columns_.size(); ++k) {
            const WaveColumn& a = wave_columns_[k - 1];
            const WaveColumn& b = wave_columns_[k];
            const double light = along((a.share + b.share) / 2);
            if(!(light > 0.01))
                continue;
            for(const auto& [from, to] : {std::pair{a.top, b.top}, std::pair{a.bottom, b.bottom}}) {
                offscreen->setFrameColor(
                    WithAlpha(edge, kOutline * kDetailDim * light * (0.5 + 0.5 * across((from + to) / 2))));
                offscreen->drawLine(at(a.share, from), at(b.share, to));
            }
        }
        // Round 6 (the user's wish, "the kick"): in the innermost part of the body a narrow
        // passage into a bright, warm white core, as loud as its column.
        for(const WaveColumn& column : wave_columns_) {
            const double light = along(column.share);
            const double core = std::min(column.body, kCoreHalf);
            if(!(light > 0.01) || !(core > 0.0))
                continue;
            const double loud = std::min(1.0, column.body / kCoreFull);
            const int pieces = std::max(2, static_cast<int>(std::ceil(2 * core / 0.008)));
            offscreen->setLineWidth(1.0 / scale);
            for(int p = 0; p < pieces; ++p) {
                const double d0 = -core + 2 * core * p / pieces, d1 = -core + 2 * core * (p + 1) / pieces;
                const double near = std::exp(-std::pow((d0 + d1) / 2 / kCoreWidth, 2));
                offscreen->setFrameColor(WithAlpha(kCoreInk, kCoreLight * light * loud * near));
                offscreen->drawLine(at(column.share, d0), at(column.share, d1 + 0.5 / (r * scale)));
            }
        }
        const CColor band = Mix(c.plate_wave, white, 0.35);
        constexpr int kBand = 96;
        for(const auto& [width, strength] : {std::pair{4.0, 0.16 * kDetailDim}, std::pair{1.2, 0.5 * kDetailDim}}) {
            offscreen->setLineWidth(width);
            for(int k = 0; k < kBand; ++k) {
                const double s0 = static_cast<double>(k) / kBand, s1 = static_cast<double>(k + 1) / kBand;
                offscreen->setFrameColor(WithAlpha(band, strength * along((s0 + s1) / 2)));
                offscreen->drawLine(at(s0, 0.0), at(s1, 0.0));
            }
        }
        Blob(offscreen, CPoint(r, r), r * 0.34, band, 0.22 * kDetailDim); // the bulge
        offscreen->endDraw();
        wave_layers_.detail = offscreen->getBitmap();
    }

    // Step 2: the depth: the peaks, somewhat taller, in a mix of the wave colour and the
    // nebula's first tint, blurred into a soft halo. Made at kDepthScale and blurred on the
    // CPU once per key, so it costs nothing per frame and little at 200 %.
    // An offscreen context below scale 1 does not shrink what it draws (Direct2D), so it is
    // made at scale 1 and kDepthScale of the size, and drawn into through an axis-aligned
    // scale, which the turned-bitmap clip issue does not touch.
    const CPoint small(std::max(1.0, std::ceil(size.x * kDepthScale)), std::max(1.0, std::ceil(size.y * kDepthScale)));
    if(auto offscreen = COffscreenContext::create(small, 1.0)) {
        std::vector<WaveColumn> columns;
        WaveColumns(w, std::max(32, static_cast<int>(size.x * kDepthScale * (1.0 + lens::kLens))), columns);
        offscreen->beginDraw();
        {
            CDrawContext::Transform shrink(*offscreen, CGraphicsTransform().scale(kDepthScale, kDepthScale));
            offscreen->setDrawMode(kAntiAliasing | kNonIntegralMode);
            offscreen->setLineWidth(1.0 / kDepthScale);
            const CColor haze = Mix(c.plate_wave, c.nebula_a, 0.5);
            for(const WaveColumn& column : columns) {
                const double light = along(column.share);
                if(!(light > 0.01))
                    continue;
                span(*offscreen, 1.0 / kDepthScale, column.share, std::max(-0.97, column.top * kDepthTall),
                     std::min(0.97, column.bottom * kDepthTall), light, 0.0, false, haze);
            }
        }
        offscreen->endDraw();
        PixelBuffer pixels;
        if(auto bitmap = offscreen->getBitmap(); bitmap && ReadPixels(bitmap, pixels)) {
            BlurPixels(pixels, std::max(1, static_cast<int>(std::lround(kDepthBlur * kDepthScale))));
            pixels.scale = pixels.width / size.x; // its pixels span the whole glass
            wave_layers_.depth = MakeBitmap(pixels);
        }
    }
}

void MapView::FillShares(CDrawContext* context, double from, double to, CColor colour) {
    from = std::clamp(from, 0.0, 1.0);
    to = std::clamp(to, from, 1.0);
    if(!(to > from))
        return;
    auto path = owned(context->createGraphicsPath());
    if(!path)
        return;
    const CRect all = getViewSize();
    const auto at = [&](double share, double down) {
        const CPoint p = PlateToView(share, down);
        return CPoint(all.left + p.x, all.top + p.y);
    };
    constexpr int kDown = 16;
    const int across = std::clamp(static_cast<int>((to - from) * 48) + 1, 1, 48);
    path->beginSubpath(at(from, -1));
    for(int i = 1; i <= kDown; ++i)
        path->addLine(at(from, -1.0 + 2.0 * i / kDown));
    for(int j = 1; j <= across; ++j)
        path->addLine(at(from + (to - from) * j / across, 1));
    for(int i = 1; i <= kDown; ++i)
        path->addLine(at(to, 1.0 - 2.0 * i / kDown));
    for(int j = 1; j < across; ++j)
        path->addLine(at(to - (to - from) * j / across, -1));
    path->closeSubpath();
    FillPath(context, path, colour);
}

void MapView::StrokeDown(CDrawContext* context, double share, double top, double bottom) {
    auto path = owned(context->createGraphicsPath());
    if(!path)
        return;
    const CRect all = getViewSize();
    constexpr int kSteps = 24;
    for(int i = 0; i <= kSteps; ++i) {
        const CPoint p = PlateToView(share, top + (bottom - top) * i / kSteps);
        const CPoint q(all.left + p.x, all.top + p.y);
        if(i == 0)
            path->beginSubpath(q);
        else
            path->addLine(q);
    }
    context->drawGraphicsPath(path, CDrawContext::kPathStroked);
}

void MapView::DrawTrail(CDrawContext* context, double share, double down, double speed, double since, double reach,
                        double thickness, double alpha) {
    // A point of the path glows 1 - age / kTrail, age being how long ago the grain
    // passed it; eased, so the glow fades softly.
    reach = std::min(reach, kTrail);
    if(!(reach > since) || !(alpha > 0.0))
        return;
    double length = std::abs(speed) * (reach - since);                // shares of the file
    const double across = 2.0 * GlassRadius() * (1.0 + lens::kLens); // units a share takes, at most
    // Round 2 (the user's test): a sounding grain shows the way it reads the file, also when
    // it moves too slowly for a tail of its own (a long file): a short stub behind it.
    if(since == 0.0 && speed != 0.0 && std::isfinite(speed))
        length = std::max(length, kMinTail / across);
    if(length * across < 1.0)
        return;
    double far = reach;
    if(length > kMaxTrailShare) { // a fast grain on a short file: only the newest part
        far = since + (reach - since) * kMaxTrailShare / length;
        length = kMaxTrailShare;
    }
    const double behind = speed > 0 ? -1.0 : 1.0;
    const auto glow = [&](double age) { return std::pow(std::clamp(1.0 - age / kTrail, 0.0, 1.0), 1.6) * alpha; };
    const int pieces = std::clamp(static_cast<int>(std::ceil(length * across / 6.0)), 1, 24);
    const CRect all = getViewSize();
    context->setLineWidth(thickness);
    for(int k = 0; k < pieces; ++k) {
        const double f0 = static_cast<double>(k) / pieces, f1 = static_cast<double>(k + 1) / pieces;
        const double s0 = Wrap(share + behind * length * f0, 0.0, 1.0);
        const double s1 = Wrap(share + behind * length * f1, 0.0, 1.0);
        if(std::abs(s1 - s0) > 0.5)
            continue; // round the file's end: the next piece goes on at its other end
        const CPoint p0 = PlateToView(s0, down), p1 = PlateToView(s1, down);
        context->setFrameColor(WithAlpha(tail_ink_, glow(since + (f0 + f1) / 2 * (far - since))));
        context->drawLine(CPoint(all.left + p0.x, all.top + p0.y), CPoint(all.left + p1.x, all.top + p1.y));
    }
}

void MapView::DrawUnchecked(CDrawContext* context) {
    const Clock::time_point now = Clock::now();
    drawn_at_ = now;
    AdvanceDrift(now);
    setDirty(false);
    if(!skin_)
        return;
    const Skin& skin = *skin_;
    const SkinColours& c = skin.Colours();
    const CRect all = getViewSize();
    const CPoint centre(all.left + GlassCentre().x, all.top + GlassCentre().y);
    const double radius = GlassRadius();
    const CRect glass(centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius);
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    context->setLineStyle(kLineSolid);

    const EngineDisplay& d = display_;
    const bool file = d.frames > 0;
    const double frames = file ? d.frames : 1.0;
    const double rate = wave_ && wave_->sequence == d.sequence ? wave_->rate : 0;
    const double seconds = file && rate > 0 ? frames / rate : 0.0;
    const auto at = [&](double share, double down) {
        const CPoint p = PlateToView(share, down);
        return CPoint(all.left + p.x, all.top + p.y);
    };
    const Loop loop = ShownLoop(now);
    const double loop_from = loop.start / frames, loop_to = std::min(1.0, (loop.start + loop.length) / frames);

    // The glass, darker towards its rim: the delivered bitmap, or drawn.
    if(const SkinImage& image = skin.Image(SkinImageId::kGlass)) {
        DrawSkinImage(context, image, glass);
    } else if(auto path = owned(context->createGraphicsPath())) {
        path->addEllipse(glass);
        FillPath(context, path, c.glass);
        if(auto gradient = owned(CGradient::create(0.0, 1.0, CColor(0, 0, 0, 0), CColor(0, 0, 0, 120)))) {
            gradient->addColorStop(0.6, CColor(0, 0, 0, 0));
            context->fillRadialGradient(path, *gradient, centre, radius);
        }
    }

    // Behind the marks: the galaxy, or the waveform with its veil outside the loop and the
    // grid (WAVE).
    const double step = L::AxisStep(seconds, 330, 54);
    if(galaxy_) {
        DrawGalaxy(context, glass);
    } else {
        if(file) {
            DrawWave(context, glass, now);
            // 0.2.2: clearer inside the loop; beta 2: a veil of glass outside it.
            const CColor veil = WithAlpha(c.glass, 0.6);
            FillShares(context, 0.0, loop_from, veil);
            FillShares(context, loop_to, 1.0, veil);
        }
        // The grid: faint lines at the axis' labels, crosses between them at each quarter
        // of the panorama.
        context->setLineWidth(1);
        context->setFrameColor(c.grid_line);
        if(step > 0)
            for(long k = 1; k * step < seconds; ++k)
                StrokeDown(context, k * step / seconds, -1, 1);
        context->setFrameColor(c.grid_cross);
        const double cross_step = step > 0 ? step / 2 : 0;
        const int crosses = cross_step > 0 ? static_cast<int>(seconds / cross_step) : 16;
        for(int q = 1; q <= crosses; ++q) {
            const double share = cross_step > 0 ? q * cross_step / seconds : q / 16.0;
            if(share >= 0.999)
                break;
            for(const double down : {-0.5, 0.0, 0.5}) {
                const CPoint p = at(share, down);
                context->drawLine(CPoint(p.x - 4, p.y), CPoint(p.x + 4, p.y));
                context->drawLine(CPoint(p.x, p.y - 4), CPoint(p.x, p.y + 4));
            }
        }
    }
    // L and R only in WAVE, where down is the grains' panorama (round 2: the starfield's
    // directions do not show the stereo field).
    if(!galaxy_) {
        context->setFont(skin.Sans(12));
        context->setFontColor(c.plate_text);
        context->drawString("L", CRect(centre.x - 10, glass.top + 6, centre.x + 10, glass.top + 22), kCenterText);
        context->drawString("R", CRect(centre.x - 10, glass.bottom - 22, centre.x + 10, glass.bottom - 6), kCenterText);
    }

    if(!file) {
        DrawTracked(context, "no sample", CRect(glass.left, centre.y - 24, glass.right, centre.y - 2), kCenterText,
                    0.3, skin.Italic(17), c.plate_text);
        DrawTracked(context, "drop a WAV here or use LOAD...", CRect(glass.left, centre.y + 2, glass.right, centre.y + 22),
                    kCenterText, 0.3, skin.Italic(13), c.plate_text);
    } else if(galaxy_) {
        DrawStarfield(context, centre, radius, now);
    } else {
        // The loop, faintly, with its edges in the accent colour; the spray range around
        // the playhead, wrapped around the file as the core wraps it.
        FillShares(context, loop_from, std::max(loop_to, loop_from + 0.5 / (radius * 2)), c.loop_fill);
        if(d.spray > 0) {
            const double from = (d.playhead - d.spray) / frames, to = (d.playhead + d.spray) / frames;
            if(to - from >= 1.0) {
                FillShares(context, 0.0, 1.0, c.spray_fill);
            } else {
                for(const double shift : {-1.0, 0.0, 1.0}) {
                    const double a = std::max(0.0, from + shift), b = std::min(1.0, to + shift);
                    if(b > a)
                        FillShares(context, a, b, c.spray_fill);
                }
            }
        }
        context->setLineWidth(1.5);
        for(const double share : {loop_from, loop_to}) {
            context->setFrameColor(WithAlpha(c.marker, 0.5));
            StrokeDown(context, share, -0.9, 0.9);
            context->setFrameColor(c.marker); // the brackets' ends
            StrokeDown(context, share, -1.0, -0.9);
            StrokeDown(context, share, 0.9, 1.0);
        }

        // The grains as shooting stars, in front of the background and the loop (0.2.2): a
        // warm-white head that flares with the grain's window and burns out, and its path
        // through the file glowing on for kTrail (across only; down is its panorama, which
        // does not move), also a while after the grain ended. Between displays they move
        // on at their own speeds. Beta 2: along their lines, bent by the lens.
        const double alpha = 0.85 + 0.15 * blend_;
        // 0.2.3: PITCH tints halo, rays and tails; the head's core stays ivory.
        tail_ink_ = PitchTint(c.tail, pitch_);
        const CColor ray_ink = PitchTint(c.grain, pitch_);
        UpdateSprites(context, ray_ink);
        // Many grains at once: shorter and fainter rays, so that the plate stays calm.
        const double calm = Calm(d.grain_count);
        const double ahead = Ahead(now);
        for(std::size_t i = 0; i < ended_count_; ++i) {
            const Ended& e = ended_[i];
            if(now < e.at)
                continue;
            const double since = Seconds(now - e.at);
            DrawTrail(context, e.dot.position / frames, GrainDown(e.dot), e.dot.speed / frames, since,
                      since + e.life, 1.4, alpha * 0.75);
        }
        for(std::uint32_t i = 0; i < std::min(d.grain_count, EngineDisplay::kMaxGrains); ++i) {
            const GrainDot& g = d.grains[i];
            const double phase = std::max(0.0, g.phase + g.phase_rate * ahead);
            const double life = g.phase_rate > 0 ? 1.0 / g.phase_rate : 0.0;
            const double down = GrainDown(g);
            const double speed = GrainSpeed(i) / frames;
            const double since = EndedFor(i, now);
            if(since >= 0) {
                // Over by now, and not yet among the ended (that comes with the next
                // display): only its glow, from where it ended.
                if(life <= 0)
                    continue;
                const double ended_at = Wrap(g.position + g.speed * (1.0 - g.phase) * life, 0.0, frames);
                DrawTrail(context, ended_at / frames, down, speed, since, since + life, 1.4, alpha * 0.75);
                continue;
            }
            const double level = std::sin(kPi * phase);
            const double burn = std::max(0.0, (phase - 0.6) / 0.4);
            const double share = GrainAt(i, now) / frames;
            DrawTrail(context, share, down, speed, 0.0, phase * life, 1.0 + 1.0 * level, alpha * (0.45 + 0.4 * level));
            const CPoint head = at(share, down);
            const CCoord x = head.x, y = head.y;
            const CCoord r = 1.2 + 2.4 * level * (1.0 - 0.5 * burn);
            // A soft warm halo; a thin dark seam keeps the head apart from a bright
            // background behind it.
            const CCoord halo = r * 3.6 + 3.0;
            DrawSprite(context, sprites_.halo, CRect(x - halo, y - halo, x + halo, y + halo), alpha * level * 0.55);
            // 0.2.3 (brief point 13): four fine diffraction rays, across and down, that fade
            // outwards and with the head (round 2: clearer, gone when it has burnt out).
            DrawRays(context, x, y, (r + 8.0 + 18.0 * level) * (0.5 + 0.5 * calm), alpha * level * (0.4 + 0.6 * calm));
            context->setFillColor(CColor(0, 0, 0, static_cast<uint8_t>(40 + 50 * level)));
            context->drawEllipse(CRect(x - r - 0.7, y - r - 0.7, x + r + 0.7, y + r + 0.7), kDrawFilled);
            context->setFillColor(WithAlpha(Mix(c.grain, CColor(255, 255, 255), 0.4), alpha * (0.8 + 0.2 * level)));
            context->drawEllipse(CRect(x - r, y - r, x + r, y + r), kDrawFilled);
            const CCoord hot = r * 0.5; // round 2: its hottest middle white
            context->setFillColor(WithAlpha(CColor(255, 255, 255), alpha * level));
            context->drawEllipse(CRect(x - hot, y - hot, x + hot, y + hot), kDrawFilled);
        }
    }

    // The domed glass' faint reflections over everything on the plate (beta 2, if delivered).
    if(const SkinImage& dome = skin.Image(SkinImageId::kDome))
        DrawSkinImage(context, dome, glass);

    // The ring over the glass' edge (beta 2): the delivered bitmap (radius 180 of 216 to its
    // canvas' edge), or drawn: turned metal lit from the top left, a dark seam at the glass
    // and a light edge outside.
    if(const SkinImage& ring = skin.Image(SkinImageId::kRing)) {
        const double half = radius * ring.size.x / 2 / L::kGlassRadius;
        DrawSkinImage(context, ring, CRect(centre.x - half, centre.y - half, centre.x + half, centre.y + half));
    } else {
        const double outer = radius + L::kRingWidth;
        if(auto path = owned(context->createGraphicsPath())) {
            path->addEllipse(CRect(centre.x - outer, centre.y - outer, centre.x + outer, centre.y + outer));
            path->addEllipse(glass);
            if(auto gradient = owned(CGradient::create(0.0, 1.0, Mix(c.metal, CColor(255, 255, 255), 0.22),
                                                       Mix(c.metal, CColor(0, 0, 0), 0.35)))) {
                gradient->addColorStop(0.5, Mix(c.metal, c.stain, 0.5));
                context->fillLinearGradient(path, *gradient, CPoint(centre.x - outer, centre.y - outer),
                                            CPoint(centre.x + outer, centre.y + outer), true);
            }
        }
        context->setLineWidth(1.5);
        context->setFrameColor(c.rule_dark);
        context->drawEllipse(CRect(glass).extend(0.75, 0.75), kDrawStroked);
        context->setLineWidth(1);
        context->setFrameColor(c.rule_light);
        context->drawEllipse(
            CRect(centre.x - outer + 0.5, centre.y - outer + 0.5, centre.x + outer - 0.5, centre.y + outer - 0.5),
            kDrawStroked);
    }

    // The seconds axis (beta 2): ticks on the ring's upper arc, where the plate's top edge
    // runs along the rim, and their labels outside it.
    if(step > 0) {
        const double minor = step / 5 / seconds * 330 >= 6 ? step / 5 : step / 2;
        context->setLineWidth(1);
        const CFontRef font = skin.Sans(10.5);
        for(long k = 0; k * minor <= seconds + 1e-9; ++k) {
            const double t = k * minor;
            const bool major = std::abs(std::remainder(t, step)) < minor * 0.01;
            const lens::Point rim = lens::ToGlass(t / seconds, -1.0);
            const double reach = major ? L::kRingWidth - 3 : L::kRingWidth * 0.45;
            context->setFrameColor(WithAlpha(c.text, major ? 0.8 : 0.4));
            context->drawLine(CPoint(centre.x + rim.x * (radius + 2), centre.y + rim.y * (radius + 2)),
                              CPoint(centre.x + rim.x * (radius + reach), centre.y + rim.y * (radius + reach)));
            if(!major)
                continue;
            const std::string label = L::AxisLabel(t, step);
            const CCoord w = TrackedWidth(context, label, 0.3, font);
            const double out = radius + L::kRingWidth + 9;
            const CPoint p(centre.x + rim.x * out, centre.y + rim.y * out);
            DrawTracked(context, label, CRect(p.x - w / 2 - 1, p.y - 7, p.x + w / 2 + 1, p.y + 7), kCenterText, 0.3,
                        font, c.text, &c);
        }
    }

    if(file) {
        // The playhead down the plate, moved on at its speed inside its loop; dashed, with
        // (beta 2: no snowflake) while Freeze holds it. 0.2.2: a dark shadow on both sides keeps it
        // readable over a bright background.
        // Beta 2: in GALAXY only on the ring.
        const double share = HeadAt(now) / frames;
        if(!galaxy_) {
            if(d.frozen) {
                const CCoord dash[] = {4, 3.2};
                context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dash));
            }
            context->setFrameColor(CColor(0, 0, 0, 110));
            context->setLineWidth(3.25);
            StrokeDown(context, share, -1, 1);
            context->setFrameColor(c.playhead);
            context->setLineWidth(1.25);
            StrokeDown(context, share, -1, 1);
            context->setLineStyle(kLineSolid);
        }
        // The position triangle on the ring (beta 2), pointing at the glass.
        if(auto path = owned(context->createGraphicsPath())) {
            const lens::Point rim = lens::ToGlass(share, -1.0);
            const CPoint tip(centre.x + rim.x * (radius + 1), centre.y + rim.y * (radius + 1));
            const CPoint base(centre.x + rim.x * (radius + 11), centre.y + rim.y * (radius + 11));
            const CPoint across(-rim.y * 5.5, rim.x * 5.5);
            path->beginSubpath(tip);
            path->addLine(CPoint(base.x + across.x, base.y + across.y));
            path->addLine(CPoint(base.x - across.x, base.y - across.y));
            path->closeSubpath();
            FillPath(context, path, c.marker);
        }
    }
    DrawDropHighlight(context);
}

void MapView::SetDropState(DropShown state, const std::string& reason) noexcept {
    try {
        if(state == drop_state_ && reason == drop_reason_)
            return;
        drop_reason_ = reason;
    } catch(...) {
        drop_reason_.clear();
    }
    drop_state_ = state;
    Guarded([&] { invalid(); }); // a noexcept function (MS-R37)
}

void MapView::DrawDropHighlight(CDrawContext* context) {
    if(drop_state_ == DropShown::kNone || !skin_)
        return;
    const SkinColours& c = skin_->Colours();
    const bool usable = drop_state_ == DropShown::kUsable;
    const CColor colour = usable ? c.control : c.warning;
    const CRect all = getViewSize();
    const CPoint centre(all.left + GlassCentre().x, all.top + GlassCentre().y);
    const double outer = GlassRadius() + L::kRingWidth;
    const CRect area(centre.x - outer, centre.y - outer, centre.x + outer, centre.y + outer);
    context->setFillColor(CColor(10, 12, 18, 170));
    context->drawEllipse(area, kDrawFilled);
    context->setFrameColor(colour);
    context->setLineWidth(2);
    const CCoord dash[] = {6, 4};
    context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dash));
    context->drawEllipse(CRect(area).inset(3, 3), kDrawStroked);
    context->setLineStyle(kLineSolid);
    const std::string text = usable ? std::string("Drop WAV to load") : "Can't load: " + drop_reason_;
    DrawTracked(context, text, CRect(area).inset(24, 0), kCenterText, 0.5, skin_->Italic(17), colour);
}

bool MapView::PressAt(double x, double y) {
    if(display_.frames == 0 || !edits_.start) // no file: nothing to place START in
        return false;
    try {
        if(edits_.start_begin)
            edits_.start_begin();
        dragging_ = true;
        edits_.start(ShareAt(x, y >= 0 ? y : GlassCentre().y));
    } catch(...) {
    }
    return true;
}

void MapView::DragTo(double x, double y) {
    if(!dragging_)
        return;
    try {
        edits_.start(ShareAt(x, y >= 0 ? y : GlassCentre().y));
    } catch(...) {
    }
}

void MapView::Release() {
    if(!dragging_)
        return;
    dragging_ = false;
    try {
        if(edits_.start_end)
            edits_.start_end();
    } catch(...) {
    }
}

bool MapView::Wheel(double delta, bool fine) {
    if(delta == 0.0 || !edits_.spray)
        return false;
    try {
        edits_.spray((delta > 0 ? 1.0 : -1.0) * (fine ? 0.005 : 0.025));
    } catch(...) {
    }
    return true;
}

// MS-R37: the mouse handlers are exception boundaries (guard.h).
void MapView::onMouseDownEvent(MouseDownEvent& event) {
    Guarded([&] {
        if(!event.buttonState.isLeft())
            return;
        const CRect all = getViewSize();
        if(PressAt(event.mousePosition.x - all.left, event.mousePosition.y - all.top))
            event.consumed = true;
    });
}

void MapView::onMouseMoveEvent(MouseMoveEvent& event) {
    Guarded([&] {
        if(!dragging_)
            return;
        const CRect all = getViewSize();
        DragTo(event.mousePosition.x - all.left, std::max(0.0, event.mousePosition.y - all.top));
        event.consumed = true;
    });
}

void MapView::onMouseUpEvent(MouseUpEvent& event) {
    Guarded([&] {
        if(!dragging_)
            return;
        Release();
        event.consumed = true;
    });
}

void MapView::onMouseCancelEvent(MouseCancelEvent& event) {
    Guarded([&] { Release(); });
    event.consumed = true;
}

void MapView::onMouseWheelEvent(MouseWheelEvent& event) {
    Guarded([&] {
        double delta = event.deltaY;
        if(event.flags & MouseWheelEvent::DirectionInvertedFromDevice)
            delta = -delta;
        if(Wheel(delta, event.modifiers.has(ModifierKey::Shift)))
            event.consumed = true;
    });
}

} // namespace messier
