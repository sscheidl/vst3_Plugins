#include "map_view.h"

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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace messier {

using namespace VSTGUI;
namespace L = layout;

namespace {
constexpr double kPi = 3.14159265358979323846;
// A grain's glowing path (0.2.2): at most this long on the plate.
constexpr CCoord kMaxTrail = 220;
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

double Seconds(MapView::Clock::duration d) {
    return std::chrono::duration<double>(d).count();
}

void FillPath(CDrawContext* context, CGraphicsPath* path, CColor colour) {
    context->setFillColor(colour);
    context->drawGraphicsPath(path, CDrawContext::kPathFilled);
}

// `position` wrapped into [from, from + length).
double Wrap(double position, double from, double length) {
    if(!(length > 0))
        return from;
    const double into = std::fmod(position - from, length);
    return from + (into < 0 ? into + length : into);
}
} // namespace

MapView::MapView(const CRect& size, CCoord time_inset) : CView(size), inset_(time_inset) {
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
            if(!(left <= audio) || audio - left >= kTrail)
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
    const double period = std::max(1.0, (getViewSize().getWidth() - 2 * inset_ + 120) * kTileShare);
    const auto turn = [](double share) { return share - std::floor(share); };
    drift_a_ = turn(drift_a_ + drift_speed_ * dt / period);
    drift_b_ = turn(drift_b_ + kFarDrift * drift_speed_ * dt / period);
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

CRect MapView::SnowflakeBox() const {
    if(!display_.frozen || display_.frames == 0)
        return CRect();
    const CCoord width = getViewSize().getWidth() - 2 * inset_;
    const CCoord head = inset_ + width * display_.playhead / display_.frames;
    const CCoord y = L::kPlateTop + 46;
    return CRect(head - 11, y - 11, head + 11, y + 11);
}

void MapView::SetSkin(std::shared_ptr<const Skin> skin) noexcept {
    skin_ = std::move(skin);
    Guarded([&] { invalid(); });
}

void MapView::SetPitch(double octaves) noexcept {
    pitch_ = std::isfinite(octaves) ? std::clamp(octaves, -3.0, 2.0) : 0.0;
}

CColor MapView::PitchTint(const CColor& warm, double octaves) {
    // 0.2.3 (brief point 13): warm at 0 octaves, towards a muted rose down to -3 and a
    // light blue-violet up to +2; at most 55 % of the way, so that it stays a tint.
    if(!std::isfinite(octaves) || octaves == 0)
        return warm;
    const double share = octaves < 0 ? std::min(1.0, -octaves / 3.0) : std::min(1.0, octaves / 2.0);
    const CColor to = octaves < 0 ? CColor(222, 138, 150) : CColor(168, 180, 255);
    const double k = 0.55 * std::pow(share, 0.8);
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
    if(Clock::now() - drawn_at_ >= kFrameInterval)
        Guarded([&] { invalid(); });
}

void MapView::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void MapView::DrawWave(CDrawContext* context, const CRect& plate, double loop_left, double loop_right,
                       Clock::time_point now) {
    if(!skin_ || !wave_ || wave_->columns == 0 || wave_->sequence != display_.sequence)
        return;
    const SkinColours& c = skin_->Colours();
    const CGraphicsTransform t = context->getCurrentTransform();
    const double scale = std::max(1.0, std::round(context->getScaleFactor() * std::abs(t.m11) * 4) / 4);
    const CPoint size(plate.getWidth(), plate.getHeight());
    const bool same_key = cached_wave_ == wave_ && cached_skin_ == skin_ && cached_scale_ == scale
                          && cached_size_ == size;
    if(!wave_cache_ || !same_key) {
        // MS-R47: a bitmap that could not be made for this key is not tried on every
        // frame of the animation; the plate goes on without the waveform.
        if(same_key && wave_failed_ && now - wave_failed_at_ < kWaveRetry)
            return;
        // Once per file, look and size: the waveform as lines into a bitmap, in the
        // plate's wave colour; drawn then with two strengths, inside and outside the loop.
        wave_cache_ = nullptr;
        cached_wave_ = wave_;
        cached_skin_ = skin_;
        cached_scale_ = scale;
        cached_size_ = size;
        wave_failed_ = true; // until the bitmap is made
        wave_failed_at_ = now;
        auto offscreen = COffscreenContext::create(size, scale);
        if(!offscreen)
            return;
        offscreen->beginDraw();
        offscreen->setDrawMode(kAntiAliasing | kNonIntegralMode);
        offscreen->setFrameColor(c.plate_wave);
        offscreen->setLineWidth(1.0 / scale);
        const WaveOverview& w = *wave_;
        const CCoord mid = size.y / 2, half = size.y * 0.42;
        const int pixels = std::max(1, static_cast<int>(size.x * scale));
        for(int px = 0; px < pixels; ++px) {
            const std::uint32_t c0 = static_cast<std::uint32_t>(std::uint64_t(px) * w.columns / pixels);
            const std::uint32_t c1 = std::max<std::uint32_t>(
                c0 + 1, static_cast<std::uint32_t>(std::uint64_t(px + 1) * w.columns / pixels));
            int low = w.low[std::min(c0, w.columns - 1)], high = w.high[std::min(c0, w.columns - 1)];
            for(std::uint32_t k = c0; k < c1 && k < w.columns; ++k) {
                low = std::min<int>(low, w.low[k]);
                high = std::max<int>(high, w.high[k]);
            }
            const CCoord x = (px + 0.5) / scale;
            offscreen->drawLine(CPoint(x, mid - half * high / 32768.0),
                                CPoint(x, mid - half * low / 32768.0 + 1.0 / scale));
        }
        offscreen->endDraw();
        wave_cache_ = offscreen->getBitmap();
        wave_failed_ = !wave_cache_;
    }
    if(!wave_cache_)
        return;
    // Faint outside the loop, clearer inside: behind the grains, above the nebula. Each
    // part once (MS-R47: 16 % and, since 0.2.2, 40 % inside, not one over the other).
    loop_left = std::clamp<double>(loop_left, plate.left, plate.right);
    loop_right = std::clamp<double>(loop_right, loop_left, plate.right);
    const auto part = [&](double from, double to, float alpha) {
        if(to > from)
            context->drawBitmap(wave_cache_, CRect(from, plate.top, to, plate.bottom), CPoint(from - plate.left, 0),
                                alpha);
    };
    // 0.2.6: WAVE scales all three alike (at most fully opaque).
    const auto level = [&](double alpha) { return static_cast<float>(std::min(1.0, alpha * wave_gain_)); };
    part(plate.left, loop_left, level(0.16));
    part(loop_left, loop_right, level(0.40));
    part(loop_right, plate.right, level(0.16));
}

void MapView::DrawTrail(CDrawContext* context, double x, double y, double speed, double since, double reach,
                        double thickness, double alpha) {
    // A point of the path glows 1 - age / kTrail, age being how long ago the grain
    // passed it; eased, so the glow fades softly.
    reach = std::min(reach, kTrail);
    if(!(reach > since) || !(alpha > 0.0))
        return;
    CCoord length = std::abs(speed) * (reach - since);
    if(length < 1.0)
        return;
    double far = reach;
    if(length > kMaxTrail) { // a fast grain on a short file: only the newest part
        far = since + (reach - since) * kMaxTrail / length;
        length = kMaxTrail;
    }
    const CCoord from = speed > 0 ? x - length : x + length;
    const auto glow = [&](double age) { return std::pow(std::clamp(1.0 - age / kTrail, 0.0, 1.0), 1.6) * alpha; };
    auto path = owned(context->createGraphicsPath());
    if(!path)
        return;
    path->addRect(CRect(std::min(from, x), y - thickness / 2, std::max(from, x), y + thickness / 2));
    auto gradient = owned(CGradient::create(0.0, 1.0, WithAlpha(tail_ink_, glow(far)), WithAlpha(tail_ink_, glow(since))));
    if(!gradient)
        return;
    gradient->addColorStop(0.5, WithAlpha(tail_ink_, glow((far + since) / 2)));
    context->fillLinearGradient(path, *gradient, CPoint(from, y), CPoint(x, y));
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
    const CCoord top = all.top;
    const CCoord left = all.left + inset_, right = all.right - inset_, width = right - left;
    const CRect plate(left, top + L::kPlateTop, right, top + L::kPlateTop + L::kPlateHeight);
    const CRect bezel(plate.left - L::kBezelEdge, plate.top - L::kBezelEdge, plate.right + L::kBezelEdge,
                      plate.bottom + L::kBezelEdge);
    const CRect slider(left, top + L::kSliderTop, right, top + L::kSliderTop + L::kSliderHeight);
    const CRect row(left, top + L::kRowTop, right, top + L::kRowTop + L::kRowHeight);
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    context->setLineStyle(kLineSolid);

    const EngineDisplay& d = display_;
    const bool file = d.frames > 0;
    const double frames = file ? d.frames : 1.0;
    const double rate = wave_ && wave_->sequence == d.sequence ? wave_->rate : 0;
    const double seconds = file && rate > 0 ? frames / rate : 0.0;
    auto x_of = [&](double frame) { return all.left + FrameX(frame); }; // as START (0.2.4)
    const Loop loop = ShownLoop(now);
    const double loop_start = loop.start, loop_length = loop.length;

    // The seconds axis on the metal.
    const double step = L::AxisStep(seconds, width);
    if(step > 0) {
        const double minor = step / 5 / seconds * width >= 6 ? step / 5 : step / 2;
        context->setLineWidth(1);
        for(long k = 0; k * minor <= seconds + 1e-9; ++k) {
            const double t = k * minor;
            const bool major = std::abs(std::remainder(t, step)) < minor * 0.01;
            const CCoord x = std::round(left + t / seconds * width) + 0.5;
            context->setFrameColor(WithAlpha(c.text, major ? 0.85 : 0.45));
            context->drawLine(CPoint(x, top + (major ? 13 : 16)), CPoint(x, top + L::kAxisBottom));
        }
        const CFontRef font = skin.Sans(11.5);
        for(long k = 0; k * step <= seconds + 1e-9; ++k) {
            const double t = k * step;
            const std::string label = L::AxisLabel(t, step);
            const CCoord w = TrackedWidth(context, label, 0.4, font);
            const CCoord x = std::clamp(left + t / seconds * width - w / 2, left, right - w);
            DrawTracked(context, label, CRect(x, top - 1, x + w + 2, top + 13), kLeftText, 0.4, font, c.text, &c);
        }
    }

    // The plate.
    const CCoord loop_left = file ? x_of(loop_start) : left;
    const CCoord loop_right = file ? x_of(std::min(frames, loop_start + loop_length)) : left;
    {
        ConcatClip clip(*context, plate);
        const SkinImage& glass = skin.Image(SkinImageId::kPlate);
        if(glass) {
            DrawSkinImage(context, glass, plate);
        } else {
            context->setFillColor(c.glass);
            context->drawRect(plate, kDrawFilled);
        }
        // Two nebulae swell a little, rise and sink a little and take turns (0.2.1: periods
        // of 45 to 90 s); they step back as BLEND goes towards Granular. Across they drift
        // with the tempo (0.2.2): forward to the right, backward to the left, the farther
        // one slower; at rest without tempo. Each repeats across the plate, so the drift
        // can go on in one direction.
        const double t = Seconds(now - born_);
        const auto wave_of = [&](double period, double phase) { return std::sin(2 * kPi * t / period + phase); };
        const double fade = 0.5 + 0.5 * wave_of(45, 0);
        const double strength = 0.18 + 0.62 * (1.0 - blend_);
        const SkinImage& nebula_a = skin.Image(SkinImageId::kNebulaA);
        const SkinImage& nebula_b = skin.Image(SkinImageId::kNebulaB);
        auto cloud = [&](const SkinImage& image, double dx, double dy, double swell, double alpha) {
            if(!image || alpha <= 0.002)
                return;
            const CCoord w = (width + 120) * swell, h = (plate.getHeight() + 40) * swell;
            // The clouds' faded ends overlap a little; the period does not swell (0.2.3).
            const CCoord period = (width + 120) * kTileShare;
            const CCoord cx0 = (plate.left + plate.right) / 2 + std::fmod(dx, period);
            const CCoord cy = (plate.top + plate.bottom) / 2 + dy;
            for(int n = -2; n <= 2; ++n) {
                const CCoord cx = cx0 + n * period;
                if(cx + w / 2 <= plate.left || cx - w / 2 >= plate.right)
                    continue;
                DrawSkinImage(context, image, CRect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2),
                              static_cast<float>(alpha));
            }
        };
        cloud(nebula_a, drift_a_ * (width + 120) * kTileShare, wave_of(53, 1.1) * 12, 1.0 + 0.05 * wave_of(90, 0.4),
              strength * (0.35 + 0.65 * fade));
        cloud(nebula_b, (drift_b_ * kTileShare + 0.45) * (width + 120), wave_of(61, 2.3) * 12,
              1.0 + 0.05 * wave_of(77, 2.0), strength * (0.35 + 0.65 * (1.0 - fade)));

        // The waveform on the glass (0.2.1, the user's trial).
        if(file)
            DrawWave(context, plate, loop_left, loop_right, now);

        // The grid: faint lines at the axis' labels, crosses between them at each quarter
        // of the panorama.
        context->setLineWidth(1);
        if(step > 0) {
            context->setFrameColor(c.grid_line);
            for(long k = 1; k * step < seconds; ++k) {
                const CCoord x = std::round(left + k * step / seconds * width) + 0.5;
                context->drawLine(CPoint(x, plate.top), CPoint(x, plate.bottom));
            }
        }
        if(skin.Preset() == GuiPreset::kGold) {
            // 0.2.5: Gold's technical glass also has faint lines across, at the quarters of
            // the panorama (its scanlines are in the plate bitmap).
            context->setFrameColor(c.grid_line);
            for(int p = 1; p < 4; ++p) {
                const CCoord y = std::round(plate.top + p / 4.0 * plate.getHeight()) + 0.5;
                context->drawLine(CPoint(plate.left, y), CPoint(plate.right, y));
            }
        }
        context->setFrameColor(c.grid_cross);
        const double cross_step = step > 0 ? step / 2 : 0;
        const int crosses = cross_step > 0 ? static_cast<int>(seconds / cross_step) : 16;
        for(int q = 1; q <= crosses; ++q) {
            const double share = cross_step > 0 ? q * cross_step / seconds : q / 16.0;
            if(share >= 0.999)
                break;
            const CCoord x = std::round(left + share * width) + 0.5;
            for(int p = 1; p < 4; ++p) {
                const CCoord y = std::round(plate.top + p / 4.0 * plate.getHeight()) + 0.5;
                context->drawLine(CPoint(x - 4, y), CPoint(x + 4, y));
                context->drawLine(CPoint(x, y - 4), CPoint(x, y + 4));
            }
        }
        context->setFont(skin.Sans(12));
        context->setFontColor(c.plate_text);
        context->drawString("L", CRect(plate.left + 9, plate.top + 6, plate.left + 30, plate.top + 22), kLeftText);
        context->drawString("R", CRect(plate.left + 9, plate.bottom - 22, plate.left + 30, plate.bottom - 6),
                            kLeftText);

        if(!file) {
            DrawTracked(context, "no sample  -  drop a WAV here or use LOAD...", plate, kCenterText, 0.3,
                        skin.Italic(16), c.plate_text);
        } else {
            // The loop, faintly, with its edges as accent brackets; the spray range around
            // the playhead, wrapped around the file as the core wraps it.
            context->setFillColor(c.loop_fill);
            context->drawRect(CRect(loop_left, plate.top, std::max(loop_right, loop_left + 1), plate.bottom),
                              kDrawFilled);
            if(d.spray > 0) {
                context->setFillColor(c.spray_fill);
                const double from = d.playhead - d.spray, to = d.playhead + d.spray;
                if(to - from >= frames) {
                    context->drawRect(plate, kDrawFilled);
                } else {
                    for(double shift : {-frames, 0.0, frames}) {
                        const double a = std::max(0.0, from + shift), b = std::min(frames, to + shift);
                        if(b > a)
                            context->drawRect(CRect(x_of(a), plate.top, x_of(b), plate.bottom), kDrawFilled);
                    }
                }
            }
            context->setLineWidth(1.5);
            for(const auto& [x, dir] : {std::pair{loop_left + 0.75, 1.0}, std::pair{loop_right - 0.75, -1.0}}) {
                context->setFrameColor(WithAlpha(c.marker, 0.5));
                context->drawLine(CPoint(x, plate.top + 6), CPoint(x, plate.bottom - 6));
                context->setFrameColor(c.marker);
                if(auto path = owned(context->createGraphicsPath())) {
                    path->beginSubpath(CPoint(x + dir * 7, plate.top + 1));
                    path->addLine(CPoint(x, plate.top + 1));
                    path->addLine(CPoint(x, plate.top + 9));
                    path->beginSubpath(CPoint(x, plate.bottom - 9));
                    path->addLine(CPoint(x, plate.bottom - 1));
                    path->addLine(CPoint(x + dir * 7, plate.bottom - 1));
                    context->drawGraphicsPath(path, CDrawContext::kPathStroked);
                }
            }

            // The grains as shooting stars, in front of nebula, waveform and loop (0.2.2): a
            // warm-white head that flares with the grain's window and burns out, and its
            // path through the file glowing on for kTrail (across only; down is its
            // panorama, which does not move), also a while after the grain ended. Between
            // displays they move on at their own speeds.
            const double alpha = 0.85 + 0.15 * blend_;
            // 0.2.3: PITCH tints halo, rays and tails; the head's core stays ivory.
            tail_ink_ = PitchTint(c.tail, pitch_);
            const CColor ray_ink = PitchTint(c.grain, pitch_);
            // Many grains at once: fainter rays, so that the plate stays calm.
            const double calm = std::min(1.0, 6.0 / std::max<std::uint32_t>(1, d.grain_count));
            const double frame_width = width / frames;
            const double ahead = Ahead(now);
            const auto y_of = [&](float pan) {
                return plate.top + 12 + std::clamp(static_cast<double>(pan), 0.0, 1.0) * (plate.getHeight() - 24);
            };
            for(std::size_t i = 0; i < ended_count_; ++i) {
                const Ended& e = ended_[i];
                if(now < e.at)
                    continue;
                const double since = Seconds(now - e.at);
                DrawTrail(context, x_of(e.dot.position), y_of(e.dot.pan), e.dot.speed * frame_width, since,
                          since + e.life, 1.4, alpha * 0.75);
            }
            for(std::uint32_t i = 0; i < std::min(d.grain_count, EngineDisplay::kMaxGrains); ++i) {
                const GrainDot& g = d.grains[i];
                const double phase = std::max(0.0, g.phase + g.phase_rate * ahead);
                const double life = g.phase_rate > 0 ? 1.0 / g.phase_rate : 0.0;
                const CCoord y = y_of(g.pan);
                const double speed = GrainSpeed(i) * frame_width;
                const double since = EndedFor(i, now);
                if(since >= 0) {
                    // Over by now, and not yet among the ended (that comes with the next
                    // display): only its glow, from where it ended.
                    if(life <= 0)
                        continue;
                    const double at = Wrap(g.position + g.speed * (1.0 - g.phase) * life, 0.0, frames);
                    DrawTrail(context, x_of(at), y, speed, since, since + life, 1.4, alpha * 0.75);
                    continue;
                }
                const double level = std::sin(kPi * phase);
                const double burn = std::max(0.0, (phase - 0.6) / 0.4);
                const CCoord x = x_of(GrainAt(i, now));
                DrawTrail(context, x, y, speed, 0.0, phase * life, 1.0 + 1.0 * level, alpha * (0.45 + 0.4 * level));
                const CCoord r = 1.2 + 2.4 * level * (1.0 - 0.5 * burn);
                // A soft warm halo; a thin dark seam keeps the head apart from a bright
                // waveform behind it.
                const CCoord halo = r * 3.6 + 3.0;
                if(auto path = owned(context->createGraphicsPath())) {
                    path->addEllipse(CRect(x - halo, y - halo, x + halo, y + halo));
                    if(auto gradient = owned(CGradient::create(0.0, 1.0, WithAlpha(tail_ink_, alpha * level * 0.55),
                                                               WithAlpha(tail_ink_, 0.0)))) {
                        gradient->addColorStop(0.3, WithAlpha(tail_ink_, alpha * level * 0.22));
                        context->fillRadialGradient(path, *gradient, CPoint(x, y), halo);
                    }
                }
                // 0.2.3 (brief point 13): a bright head gets four fine diffraction rays,
                // across and down, that fade outwards and with the head.
                if(level > 0.35) {
                    const double glow = alpha * calm * (level - 0.35) / 0.65 * 0.85;
                    const CCoord reach = r + 7.0 + 14.0 * level, half = 0.5;
                    const auto ray = [&](CPoint to, CRect bar) {
                        auto path = owned(context->createGraphicsPath());
                        auto gradient = owned(CGradient::create(0.0, 1.0, WithAlpha(ray_ink, glow), WithAlpha(ray_ink, 0.0)));
                        if(!path || !gradient)
                            return;
                        gradient->addColorStop(0.35, WithAlpha(ray_ink, glow * 0.35));
                        path->addRect(bar);
                        context->fillLinearGradient(path, *gradient, CPoint(x, y), to);
                    };
                    ray(CPoint(x + reach, y), CRect(x, y - half, x + reach, y + half));
                    ray(CPoint(x - reach, y), CRect(x - reach, y - half, x, y + half));
                    ray(CPoint(x, y + reach), CRect(x - half, y, x + half, y + reach));
                    ray(CPoint(x, y - reach), CRect(x - half, y - reach, x + half, y));
                }
                context->setFillColor(CColor(0, 0, 0, static_cast<uint8_t>(40 + 50 * level)));
                context->drawEllipse(CRect(x - r - 0.7, y - r - 0.7, x + r + 0.7, y + r + 0.7), kDrawFilled);
                context->setFillColor(WithAlpha(c.grain, alpha * (0.75 + 0.25 * level)));
                context->drawEllipse(CRect(x - r, y - r, x + r, y + r), kDrawFilled);
            }
        }
    }

    // The bezel over the glass' edge.
    const SkinImage& frame = skin.Image(SkinImageId::kBezel);
    if(frame) {
        DrawNinePart(context, frame, bezel, L::kBezelEdge);
    } else {
        context->setFrameColor(c.dim);
        context->setLineWidth(2);
        context->drawRect(CRect(plate).extend(1, 1), kDrawStroked);
    }

    if(file) {
        // The playhead across the plate, moved on at its speed inside its loop; dashed,
        // with a snowflake, while Freeze holds it.
        // 0.2.2: a dark shadow on both sides keeps it readable over a bright waveform.
        const CCoord x = std::round(x_of(HeadAt(now))) + 0.5;
        if(d.frozen) {
            const CCoord dash[] = {4, 3.2};
            context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dash));
        }
        context->setFrameColor(CColor(0, 0, 0, 110));
        context->setLineWidth(3.25);
        context->drawLine(CPoint(x, plate.top), CPoint(x, plate.bottom));
        context->setFrameColor(c.playhead);
        context->setLineWidth(1.25);
        context->drawLine(CPoint(x, plate.top), CPoint(x, plate.bottom));
        context->setLineStyle(kLineSolid);
        // The position triangle in its own lane (0.2.2), clear of the axis labels.
        if(auto path = owned(context->createGraphicsPath())) {
            path->beginSubpath(CPoint(x - 6, top + L::kMarkerTop));
            path->addLine(CPoint(x + 6, top + L::kMarkerTop));
            path->addLine(CPoint(x, top + L::kMarkerBottom));
            path->closeSubpath();
            FillPath(context, path, c.marker);
        }
        if(d.frozen) {
            const CRect box = SnowflakeBox().offset(all.left, all.top);
            const CPoint centre = box.getCenter();
            context->setFillColor(CColor(10, 12, 16, 217));
            context->drawRect(box, kDrawFilled);
            context->setFrameColor(CColor(230, 232, 238, 204));
            context->setLineWidth(1);
            context->drawRect(CRect(box).inset(0.5, 0.5), kDrawStroked);
            context->setFrameColor(CColor(242, 242, 244));
            context->setLineWidth(1.3);
            for(int k = 0; k < 3; ++k) { // six arms
                const double a = k * kPi / 3;
                const CCoord ax = std::cos(a) * 7, ay = std::sin(a) * 7;
                context->drawLine(CPoint(centre.x - ax, centre.y - ay), CPoint(centre.x + ax, centre.y + ay));
                for(double end : {-1.0, 1.0}) { // the small branches near each end
                    const CPoint tip(centre.x + end * ax * 0.62, centre.y + end * ay * 0.62);
                    for(double turn : {-0.6, 0.6}) {
                        const double b = a + (end < 0 ? kPi : 0.0) + turn * 1.2;
                        context->drawLine(tip, CPoint(tip.x + std::cos(b) * 2.6, tip.y + std::sin(b) * 2.6));
                    }
                }
            }
        }
    }

    // The START slider: the same time axis; its thumb stands where the loop starts.
    {
        const CCoord groove_top = slider.top + 14;
        context->setFillColor(c.groove);
        context->drawRect(CRect(left, groove_top, right, groove_top + 4), kDrawFilled);
        context->setFrameColor(c.rule_light);
        context->setLineWidth(1);
        context->drawLine(CPoint(left, groove_top + 4.5), CPoint(right, groove_top + 4.5));
        const CCoord thumb_x = all.left + StartX();
        context->setFillColor(WithAlpha(c.control, 0.95));
        context->drawRect(CRect(left, groove_top + 0.5, thumb_x, groove_top + 3.5), kDrawFilled);
        context->setFrameColor(WithAlpha(c.text, 0.5));
        for(int q = 0; q <= 40; ++q) {
            const CCoord x = std::round(left + q / 40.0 * (width - 1)) + 0.5;
            context->drawLine(CPoint(x, slider.top + (q % 5 == 0 ? 22 : 24)), CPoint(x, slider.top + 28));
        }
        const SkinImage& thumb = skin.Image(SkinImageId::kThumb);
        const CRect knob(thumb_x - 8, slider.top + 1, thumb_x + 8, slider.top + 29);
        if(thumb) {
            DrawSkinImage(context, thumb, knob);
        } else {
            context->setFillColor(c.dim);
            context->drawRect(knob, kDrawFilled);
        }
    }

    // START's value and the legend.
    {
        const CFontRef caps = skin.Sans(12.5);
        const CCoord label = TrackedWidth(context, "START", 1.4, caps);
        DrawTracked(context, "START", CRect(row.left, row.top, row.left + label + 2, row.bottom), kLeftText, 1.4, caps,
                    c.text, &c);
        char value[48];
        const int percent = static_cast<int>(std::lround(start_ * 100));
        if(file && rate > 0)
            std::snprintf(value, sizeof value, "%.2f s \xC2\xB7 %d %%", loop_start / rate, percent);
        else
            std::snprintf(value, sizeof value, "%d %%", percent);
        context->setFont(skin.Mono(12));
        context->setFontColor(c.value);
        context->drawString(value, CRect(row.left + label + 10, row.top, row.left + label + 200, row.bottom), kLeftText);

        const CFontRef small = skin.Sans(10.5);
        const char* names[4] = {"PLAYHEAD", "LOOP", "SPRAY", "GRAIN \xC2\xB7 L ABOVE, R BELOW"};
        CCoord widths[4], total = 0;
        for(int k = 0; k < 4; ++k) {
            widths[k] = TrackedWidth(context, names[k], 1.0, small);
            total += 22 + 6 + widths[k] + (k < 3 ? 16 : 0);
        }
        CCoord x = row.right - total;
        const CCoord mid = (row.top + row.bottom) / 2;
        for(int k = 0; k < 4; ++k) {
            switch(k) {
                case 0:
                    context->setFrameColor(c.playhead);
                    context->setLineWidth(1.5);
                    context->drawLine(CPoint(x + 11, mid - 5), CPoint(x + 11, mid + 5));
                    break;
                case 1:
                    context->setFrameColor(c.marker);
                    context->setLineWidth(1.8);
                    for(CCoord dir : {1.0, -1.0}) {
                        const CCoord at = dir > 0 ? x + 2 : x + 20;
                        if(auto path = owned(context->createGraphicsPath())) {
                            path->beginSubpath(CPoint(at + dir * 4, mid - 4));
                            path->addLine(CPoint(at, mid - 4));
                            path->addLine(CPoint(at, mid + 4));
                            path->addLine(CPoint(at + dir * 4, mid + 4));
                            context->drawGraphicsPath(path, CDrawContext::kPathStroked);
                        }
                    }
                    break;
                case 2:
                    context->setFillColor(WithAlpha(c.text, 0.25));
                    context->drawRect(CRect(x + 2, mid - 4, x + 20, mid + 4), kDrawFilled);
                    break;
                default:
                    if(auto path = owned(context->createGraphicsPath())) {
                        path->addRect(CRect(x + 1, mid - 0.8, x + 16, mid + 0.8));
                        if(auto gradient = owned(CGradient::create(0.0, 1.0, WithAlpha(c.tail, 0.0), c.tail)))
                            context->fillLinearGradient(path, *gradient, CPoint(x + 1, mid), CPoint(x + 16, mid));
                    }
                    context->setFillColor(c.tail);
                    context->drawEllipse(CRect(x + 15, mid - 2.6, x + 20.2, mid + 2.6), kDrawFilled);
                    break;
            }
            x += 28;
            DrawTracked(context, names[k], CRect(x, row.top, x + widths[k] + 2, row.bottom), kLeftText, 1.0, small,
                        c.text, &c);
            x += widths[k] + 16;
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
    const CRect area(all.left, all.top + L::kPlateTop - L::kBezelEdge, all.right,
                     all.top + L::kPlateTop + L::kPlateHeight + L::kBezelEdge);
    context->setFillColor(CColor(10, 12, 18, 170));
    context->drawRect(area, kDrawFilled);
    CRect frame = area;
    frame.inset(2, 2);
    context->setFrameColor(colour);
    context->setLineWidth(2);
    const CCoord dash[] = {6, 4};
    context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dash));
    context->drawRect(frame, kDrawStroked);
    context->setLineStyle(kLineSolid);
    const std::string text = usable ? std::string("Drop WAV to load") : "Can't load: " + drop_reason_;
    DrawTracked(context, text, area, kCenterText, 0.5, skin_->Italic(18), colour);
}

double MapView::StartX() const {
    // MS-R56 (0.2.5): with a file, START's place as the core clamps the loop's start, to
    // the last frame; so the thumb stays on the bracket also at 100 % of a short file.
    const double frames = display_.frames;
    if(frames > 0)
        return FrameX(std::min(start_ * frames, frames - 1));
    return inset_ + (getViewSize().getWidth() - 2 * inset_) * start_;
}

double MapView::FrameX(double frame) const {
    const double frames = display_.frames;
    return frames > 0 ? inset_ + (getViewSize().getWidth() - 2 * inset_) * frame / frames : inset_;
}

double MapView::Share(double x) const {
    const double width = getViewSize().getWidth() - 2 * inset_;
    return width > 0 ? std::clamp((x - inset_) / width, 0.0, 1.0) : 0.0;
}

bool MapView::PressAt(double x, double y) {
    // 0.2.1: a click on the snowflake ends Freeze, and places nothing.
    if(y >= 0 && SnowflakeBox().pointInside(CPoint(x, y)) && edits_.freeze_off) {
        try {
            edits_.freeze_off();
        } catch(...) {
        }
        return true;
    }
    if(display_.frames == 0 || !edits_.start) // no file: nothing to place START in
        return false;
    try {
        if(edits_.start_begin)
            edits_.start_begin();
        dragging_ = true;
        edits_.start(Share(x));
    } catch(...) {
    }
    return true;
}

void MapView::DragTo(double x) {
    if(!dragging_)
        return;
    try {
        edits_.start(Share(x));
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
        DragTo(event.mousePosition.x - getViewSize().left);
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
