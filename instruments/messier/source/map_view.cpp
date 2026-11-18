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
#include <limits>

namespace messier {

using namespace VSTGUI;
namespace L = layout;

namespace {
constexpr double kPi = 3.14159265358979323846;
// A shooting star's tail: where the grain was this long ago, at its measured speed.
constexpr double kTailSeconds = 0.12;
constexpr CCoord kMaxTail = 80;
// The plate is drawn again at least this often while the window is open (0.2.1: the
// nebula moves, playhead and grains move on between displays).
constexpr auto kFrameInterval = std::chrono::milliseconds(25);
// Between displays, playhead and grains move on for at most this long.
constexpr double kMaxAhead = 0.15;
// Two moves of a grain agree when their speeds differ by less than this share (MS-R43).
constexpr double kAgree = 0.35;

double Seconds(MapView::Clock::duration d) {
    return std::chrono::duration<double>(d).count();
}

void FillPath(CDrawContext* context, CGraphicsPath* path, CColor colour) {
    context->setFillColor(colour);
    context->drawGraphicsPath(path, CDrawContext::kPathFilled);
}

// `move` across the end of a file of `frames`, the shorter way; NaN if both ways are
// long enough to be either (MS-R43: no direction is guessed).
double ShortMove(double move, double frames) {
    if(move > frames / 2)
        move -= frames;
    else if(move < -frames / 2)
        move += frames;
    return std::abs(move) <= frames / 4 ? move : std::numeric_limits<double>::quiet_NaN();
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
    const bool same_wave = wave == wave_;
    if(same_wave && std::memcmp(&display, &display_, sizeof display) == 0) {
        // The same again: no audio block in between (a large host buffer), or the engine
        // rests (STOP, Bypass, a host that stopped processing). Only a display unchanged
        // for longer than a large buffer stops playhead and grains where they are.
        if(Seconds(now - shown_at_) > 0.1)
            resting_ = true;
        return;
    }
    resting_ = false;
    const double seconds = Seconds(now - shown_at_);
    FollowPlayhead(display_, display, seconds);
    FollowGrains(display_, display, seconds);
    shown_at_ = now;
    display_ = display;
    wave_ = std::move(wave);
    Guarded([&] { invalid(); }); // a noexcept function (MS-R37)
}

void MapView::FollowPlayhead(const EngineDisplay& before, const EngineDisplay& now, double seconds) {
    const bool comparable = before.sequence == now.sequence && before.frames == now.frames && now.frames > 0
                            && std::abs(before.loop_start - now.loop_start) < 1.0
                            && std::abs(before.loop_length - now.loop_length) < 1.0;
    if(now.frozen || now.frames == 0) {
        head_speed_ = 0;
        head_measured_ = now.frozen != 0;
        return;
    }
    if(!comparable || !(seconds > 0.004 && seconds < 0.5)) {
        if(!comparable)
            head_measured_ = false;
        return;
    }
    const double length = now.loop_length;
    double move = now.playhead - before.playhead;
    if(move > length / 2) // around the loop
        move -= length;
    else if(move < -length / 2)
        move += length;
    const double speed = move / seconds;
    head_speed_ = head_measured_ ? 0.5 * head_speed_ + 0.5 * speed : speed;
    head_measured_ = true;
}

void MapView::FollowGrains(const EngineDisplay& before, const EngineDisplay& now, double seconds) {
    // The display carries no grain numbers. A grain keeps its panorama and its window
    // phase grows, so a grain of the display before with the same panorama, a smaller
    // phase and a position near where it would be now may be the same one. MS-R43: a
    // match that two candidates fit about equally, or that jumps across more than a
    // quarter of the file, is not taken; a speed counts only once two moves in a row
    // agree, so a new grain that takes the place of an old one shows no tail.
    std::array<Trail, EngineDisplay::kMaxGrains> next{};
    bool used[EngineDisplay::kMaxGrains] = {};
    const bool same_file = before.sequence == now.sequence && before.frames == now.frames && now.frames > 0;
    const double frames = now.frames;
    const std::uint32_t count = std::min(now.grain_count, EngineDisplay::kMaxGrains);
    const std::uint32_t count_before = std::min(before.grain_count, EngineDisplay::kMaxGrains);
    for(std::uint32_t i = 0; i < count; ++i) {
        const GrainDot& g = now.grains[i];
        Trail trail;
        trail.pan = g.pan;
        trail.phase = g.phase;
        trail.position = g.position;
        if(same_file && seconds > 0.004 && seconds < 0.5) {
            int best = -1;
            double best_error = std::numeric_limits<double>::infinity(), second_error = best_error, best_move = 0;
            for(std::uint32_t j = 0; j < count_before; ++j) {
                const Trail& old = trails_[j];
                if(used[j] || old.pan != g.pan || !(g.phase > old.phase))
                    continue;
                const double move = ShortMove(g.position - old.position, frames);
                if(std::isnan(move))
                    continue;
                const double error = std::abs(move - (old.matches > 0 ? old.speed * seconds : 0.0));
                if(error < best_error) {
                    second_error = best_error;
                    best = static_cast<int>(j);
                    best_error = error;
                    best_move = move;
                } else if(error < second_error) {
                    second_error = error;
                }
            }
            // Ambiguous: another candidate fits nearly as well.
            const bool clear = best >= 0 && !(second_error < 2.0 * best_error + 64.0);
            if(clear) {
                used[best] = true;
                const Trail& old = trails_[static_cast<std::size_t>(best)];
                const double speed = best_move / seconds;
                const double phase_rate = (g.phase - old.phase) / seconds;
                const bool agrees = old.matches > 0
                                    && std::abs(speed - old.speed) <= kAgree * std::max(std::abs(speed), std::abs(old.speed));
                trail.matches = agrees ? std::min(old.matches + 1, 8) : 1;
                trail.speed = agrees ? 0.5 * old.speed + 0.5 * speed : speed;
                trail.phase_rate = old.matches > 0 ? 0.5 * old.phase_rate + 0.5 * phase_rate : phase_rate;
            }
        }
        next[i] = trail;
    }
    trails_ = next;
}

double MapView::GrainSpeed(std::uint32_t index) const {
    return index < display_.grain_count && index < trails_.size() && trails_[index].matches >= 2
               ? trails_[index].speed
               : 0.0;
}

double MapView::LoopStart() const {
    const double frames = display_.frames;
    if(frames <= 0)
        return 0;
    return start_known_ ? std::min(start_ * frames, frames - 1) : display_.loop_start;
}

double MapView::LoopLength() const {
    const double frames = display_.frames;
    if(frames <= 0)
        return 1;
    if(!start_known_ || !size_known_)
        return display_.loop_length;
    return std::max(1.0, size_ * size_ * (frames - LoopStart()));
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

void MapView::SetBlend(double blend) noexcept {
    blend_ = std::clamp(blend, 0.0, 1.0);
}

void MapView::SetStart(double start) noexcept {
    start = std::clamp(start, 0.0, 1.0);
    const bool changed = !start_known_ || start != start_;
    start_ = start;
    start_known_ = true;
    if(changed)
        Guarded([&] { invalid(); }); // the slider and the loop follow at once
}

void MapView::SetSize(double size) noexcept {
    size = std::clamp(size, 0.0, 1.0);
    const bool changed = !size_known_ || size != size_;
    size_ = size;
    size_known_ = true;
    if(changed)
        Guarded([&] { invalid(); });
}

void MapView::Tick() noexcept {
    if(Clock::now() - drawn_at_ >= kFrameInterval)
        Guarded([&] { invalid(); });
}

void MapView::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void MapView::DrawWave(CDrawContext* context, const CRect& plate, double loop_left, double loop_right) {
    if(!skin_ || !wave_ || wave_->columns == 0 || wave_->sequence != display_.sequence)
        return;
    const SkinColours& c = skin_->Colours();
    const CGraphicsTransform t = context->getCurrentTransform();
    const double scale = std::max(1.0, std::round(context->getScaleFactor() * std::abs(t.m11) * 4) / 4);
    const CPoint size(plate.getWidth(), plate.getHeight());
    if(!wave_cache_ || cached_wave_ != wave_ || cached_skin_ != skin_ || cached_scale_ != scale
       || cached_size_ != size) {
        // Once per file, look and size: the waveform as lines into a bitmap, in the
        // plate's wave colour; drawn then with two strengths, inside and outside the loop.
        wave_cache_ = nullptr;
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
        cached_wave_ = wave_;
        cached_skin_ = skin_;
        cached_scale_ = scale;
        cached_size_ = size;
    }
    if(!wave_cache_)
        return;
    // Faint outside the loop, clearer inside: behind the grains, above the nebula.
    context->drawBitmap(wave_cache_, plate, CPoint(0, 0), 0.16f);
    if(loop_right > loop_left) {
        const CRect inside(loop_left, plate.top, loop_right, plate.bottom);
        context->drawBitmap(wave_cache_, inside, CPoint(loop_left - plate.left, 0), 0.30f);
    }
}

void MapView::DrawUnchecked(CDrawContext* context) {
    const Clock::time_point now = Clock::now();
    drawn_at_ = now;
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
    auto x_of = [&](double frame) { return left + width * frame / frames; };
    // How far playhead and grains have moved on since the display (0.2.1).
    const double ahead = resting_ ? 0.0 : std::clamp(Seconds(now - shown_at_), 0.0, kMaxAhead);
    const double loop_start = LoopStart(), loop_length = LoopLength();

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
        // Two nebulae move slowly against each other, swell a little and take turns
        // (0.2.1: periods of 45 to 90 s, about 6 units a second at most); they step back
        // as BLEND goes towards Granular.
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
            const CCoord cx = (plate.left + plate.right) / 2 + dx, cy = (plate.top + plate.bottom) / 2 + dy;
            DrawSkinImage(context, image, CRect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2), static_cast<float>(alpha));
        };
        cloud(nebula_a, wave_of(70, 0) * 70, wave_of(53, 1.1) * 12, 1.0 + 0.05 * wave_of(90, 0.4), strength * (0.35 + 0.65 * fade));
        cloud(nebula_b, -wave_of(83, 1.0) * 80, wave_of(61, 2.3) * 12, 1.0 + 0.05 * wave_of(77, 2.0),
              strength * (0.35 + 0.65 * (1.0 - fade)));

        // The waveform on the glass (0.2.1, the user's trial).
        if(file)
            DrawWave(context, plate, loop_left, loop_right);

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
                context->setFrameColor(WithAlpha(c.accent, 0.5));
                context->drawLine(CPoint(x, plate.top + 6), CPoint(x, plate.bottom - 6));
                context->setFrameColor(c.accent);
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

            // The grains as shooting stars: a head that flares with the grain's window and
            // burns out, and a tail along its real motion through the file (across only;
            // down is its panorama, which does not move). Between displays they move on at
            // their measured speeds.
            const double alpha = 0.35 + 0.65 * blend_;
            const double frame_width = width / frames;
            for(std::uint32_t i = 0; i < std::min(d.grain_count, EngineDisplay::kMaxGrains); ++i) {
                const GrainDot& g = d.grains[i];
                const Trail& trail = trails_[i];
                const bool sure = trail.matches >= 2;
                double phase = g.phase + (sure ? trail.phase_rate * ahead : 0.0);
                if(phase >= 1.0)
                    continue; // over by now
                phase = std::clamp(phase, 0.0, 1.0);
                double position = g.position + (sure ? trail.speed * ahead : 0.0);
                position = std::fmod(position, frames);
                if(position < 0)
                    position += frames;
                const double level = std::sin(kPi * phase);
                const double burn = std::max(0.0, (phase - 0.6) / 0.4);
                const CCoord x = x_of(position);
                const CCoord y = plate.top + 12 + std::clamp(static_cast<double>(g.pan), 0.0, 1.0) * (plate.getHeight() - 24);
                const double speed = GrainSpeed(i);
                const CCoord tail = std::min(kMaxTail, std::abs(speed) * kTailSeconds * frame_width)
                                    * std::min(1.0, phase * 3.0);
                if(tail >= 1.5) {
                    const CCoord from = speed > 0 ? x - tail : x + tail;
                    const CCoord thick = 0.8 + 1.4 * level;
                    if(auto path = owned(context->createGraphicsPath())) {
                        path->addRect(CRect(std::min(from, x), y - thick / 2, std::max(from, x), y + thick / 2));
                        if(auto gradient = owned(CGradient::create(0.0, 1.0, WithAlpha(c.tail, 0.0),
                                                                    WithAlpha(c.tail, alpha * (0.6 * level + 0.12)))))
                            context->fillLinearGradient(path, *gradient, CPoint(from, y), CPoint(x, y));
                    }
                }
                const CCoord r = 1.0 + 2.4 * level * (1.0 - 0.5 * burn);
                for(int ring = 3; ring >= 1; --ring) { // the glow
                    const CCoord rr = r * (1.0 + 1.2 * ring);
                    context->setFillColor(WithAlpha(c.tail, alpha * level * 0.16));
                    context->drawEllipse(CRect(x - rr, y - rr, x + rr, y + rr), kDrawFilled);
                }
                context->setFillColor(WithAlpha(c.grain, alpha * (0.3 + 0.7 * level)));
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
        // The playhead across the plate, moved on at its measured speed inside the loop;
        // dashed, with a snowflake, while Freeze holds it.
        double head = d.playhead;
        if(!d.frozen && head_measured_ && loop_length > 0) {
            head += head_speed_ * ahead;
            const double into = std::fmod(head - loop_start, loop_length);
            head = loop_start + (into < 0 ? into + loop_length : into);
        }
        const CCoord x = std::round(x_of(head)) + 0.5;
        context->setFrameColor(c.playhead);
        context->setLineWidth(1.25);
        if(d.frozen) {
            const CCoord dash[] = {4, 3.2};
            context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dash));
        }
        context->drawLine(CPoint(x, plate.top), CPoint(x, plate.bottom));
        context->setLineStyle(kLineSolid);
        if(auto path = owned(context->createGraphicsPath())) {
            path->beginSubpath(CPoint(x - 6, top + 8));
            path->addLine(CPoint(x + 6, top + 8));
            path->addLine(CPoint(x, top + L::kAxisBottom - 1));
            path->closeSubpath();
            FillPath(context, path, c.accent);
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
        const CCoord thumb_x = left + start_ * width;
        context->setFillColor(WithAlpha(c.accent, 0.95));
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
        context->setFontColor(c.accent);
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
                    context->setFrameColor(c.accent);
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
    const CColor colour = usable ? c.accent : c.warning;
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
