#include "map_view.h"

#include "gui_layout.h"
#include "guard.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/clinestyle.h"
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
// Without new displays the nebula is drawn again this often, so that it drifts.
constexpr auto kDriftInterval = std::chrono::milliseconds(120);

void FillPath(CDrawContext* context, CGraphicsPath* path, CColor colour) {
    context->setFillColor(colour);
    context->drawGraphicsPath(path, CDrawContext::kPathFilled);
}
} // namespace

MapView::MapView(const CRect& size, CCoord time_inset) : CView(size), inset_(time_inset) {
    // Since 0.1.8 the map takes the mouse: START, SPRAY, and drops (VSTGUI looks for
    // drop targets only among views that take the mouse).
    setMouseEnabled(true);
}

void MapView::Show(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept {
    const bool same_wave = wave == wave_;
    if(same_wave && std::memcmp(&display, &display_, sizeof display) == 0)
        return;
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - shown_at_).count();
    FollowGrains(display_, display, seconds);
    shown_at_ = now;
    display_ = display;
    wave_ = std::move(wave);
    Guarded([&] { invalid(); }); // a noexcept function (MS-R37)
}

void MapView::FollowGrains(const EngineDisplay& before, const EngineDisplay& now, double seconds) {
    // The display carries no grain numbers. A grain keeps its panorama and its window
    // phase grows, so a grain of the display before with the same panorama, a smaller
    // phase and a position nearest to where it would be now is taken as the same one.
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
            double best_error = std::numeric_limits<double>::infinity(), best_move = 0;
            for(std::uint32_t j = 0; j < count_before; ++j) {
                const Trail& old = trails_[j];
                if(used[j] || old.pan != g.pan || !(g.phase > old.phase))
                    continue;
                double move = g.position - old.position;
                if(move > frames / 2) // across the end of the file
                    move -= frames;
                else if(move < -frames / 2)
                    move += frames;
                const double error = std::abs(move - (old.measured ? old.speed * seconds : 0.0));
                if(error < best_error) {
                    best = static_cast<int>(j);
                    best_error = error;
                    best_move = move;
                }
            }
            if(best >= 0) {
                used[best] = true;
                const Trail& old = trails_[static_cast<std::size_t>(best)];
                const double speed = best_move / seconds;
                trail.speed = old.measured ? 0.5 * old.speed + 0.5 * speed : speed;
                trail.measured = true;
            }
        }
        next[i] = trail;
    }
    trails_ = next;
}

double MapView::GrainSpeed(std::uint32_t index) const {
    return index < display_.grain_count && index < trails_.size() && trails_[index].measured ? trails_[index].speed
                                                                                            : 0.0;
}

void MapView::SetSkin(std::shared_ptr<const Skin> skin) noexcept {
    skin_ = std::move(skin);
    Guarded([&] { invalid(); });
}

void MapView::SetBlend(double blend) noexcept {
    blend = std::clamp(blend, 0.0, 1.0);
    if(blend == blend_)
        return;
    blend_ = blend;
    Guarded([&] { invalid(); });
}

void MapView::SetStart(double start) noexcept {
    start = std::clamp(start, 0.0, 1.0);
    if(start == start_)
        return;
    start_ = start;
    Guarded([&] { invalid(); });
}

void MapView::Tick() noexcept {
    if(std::chrono::steady_clock::now() - drawn_at_ >= kDriftInterval)
        Guarded([&] { invalid(); });
}

void MapView::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void MapView::DrawUnchecked(CDrawContext* context) {
    drawn_at_ = std::chrono::steady_clock::now();
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
    const CRect wave_box(left, top + L::kWaveTop, right, top + L::kWaveTop + L::kWaveHeight);
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
        const CFontRef font = skin.Caps(12);
        for(long k = 0; k * step <= seconds + 1e-9; ++k) {
            const double t = k * step;
            const std::string label = L::AxisLabel(t, step);
            const CCoord w = TrackedWidth(context, label, 0.5, font);
            const CCoord x = std::clamp(left + t / seconds * width - w / 2, left, right - w);
            DrawTracked(context, label, CRect(x, top - 1, x + w + 2, top + 13), kLeftText, 0.5, font, c.text, &c);
        }
    }

    // The plate.
    {
        ConcatClip clip(*context, plate);
        const SkinImage& glass = skin.Image(SkinImageId::kPlate);
        if(glass) {
            DrawSkinImage(context, glass, plate);
        } else {
            context->setFillColor(c.glass);
            context->drawRect(plate, kDrawFilled);
        }
        // Two nebulae drift against each other and take turns; they step back as BLEND
        // goes towards Granular.
        const double t = std::chrono::duration<double>(drawn_at_ - born_).count();
        const double fade = 0.5 + 0.5 * std::sin(t * 0.045);
        const double strength = 0.18 + 0.62 * (1.0 - blend_);
        const SkinImage& nebula_a = skin.Image(SkinImageId::kNebulaA);
        const SkinImage& nebula_b = skin.Image(SkinImageId::kNebulaB);
        const CCoord spread_x = 40, spread_y = 14;
        if(nebula_a) {
            const CCoord dx = std::sin(t * 0.021) * 30, dy = std::cos(t * 0.017) * 8;
            DrawSkinImage(context, nebula_a,
                          CRect(plate.left - spread_x + dx, plate.top - spread_y + dy, plate.right + spread_x + dx,
                                plate.bottom + spread_y + dy),
                          static_cast<float>(strength * fade));
        }
        if(nebula_b) {
            const CCoord dx = std::cos(t * 0.019) * 34, dy = std::sin(t * 0.023) * 8;
            DrawSkinImage(context, nebula_b,
                          CRect(plate.left - spread_x + dx, plate.top - spread_y + dy, plate.right + spread_x + dx,
                                plate.bottom + spread_y + dy),
                          static_cast<float>(strength * (1.0 - fade)));
        }

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
        const CFontRef pan_font = skin.Caps(13);
        context->setFont(pan_font);
        context->setFontColor(c.plate_text);
        context->drawString("L", CRect(plate.left + 9, plate.top + 6, plate.left + 30, plate.top + 22), kLeftText);
        context->drawString("R", CRect(plate.left + 9, plate.bottom - 22, plate.left + 30, plate.bottom - 6),
                            kLeftText);

        if(!file) {
            DrawTracked(context, "no sample  -  drop a WAV here or use LOAD...", plate, kCenterText, 0.3,
                        skin.Italic(16), c.plate_text);
        } else {
            // The loop, faintly, with dashed edges; the spray range around the playhead,
            // wrapped around the file as the core wraps it.
            const CCoord loop_left = x_of(d.loop_start);
            const CCoord loop_right = x_of(std::min(frames, d.loop_start + d.loop_length));
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
            const CCoord dash[] = {3, 3};
            context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dash));
            context->setFrameColor(c.loop_edge);
            context->drawLine(CPoint(loop_left + 0.5, plate.top), CPoint(loop_left + 0.5, plate.bottom));
            context->drawLine(CPoint(loop_right - 0.5, plate.top), CPoint(loop_right - 0.5, plate.bottom));
            context->setLineStyle(kLineSolid);

            // The grains as shooting stars: a head that flares with the grain's window and
            // burns out, and a tail along its real motion through the file (across only;
            // down is its panorama, which does not move).
            const double alpha = 0.35 + 0.65 * blend_;
            const double frame_width = width / frames;
            for(std::uint32_t i = 0; i < std::min(d.grain_count, EngineDisplay::kMaxGrains); ++i) {
                const GrainDot& g = d.grains[i];
                const double phase = std::clamp(static_cast<double>(g.phase), 0.0, 1.0);
                const double level = std::sin(kPi * phase);
                const double burn = std::max(0.0, (phase - 0.6) / 0.4);
                const CCoord x = x_of(g.position);
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

    // The waveform on the metal, in the same scale, light inside the loop, with the
    // loop's brackets.
    CCoord loop_left = left, loop_right = left;
    if(file) {
        loop_left = x_of(d.loop_start);
        loop_right = x_of(std::min(frames, d.loop_start + d.loop_length));
    }
    if(file && wave_ && wave_->columns > 0 && wave_->sequence == d.sequence) {
        const WaveOverview& w = *wave_;
        const CCoord mid = (wave_box.top + wave_box.bottom) / 2, half = wave_box.getHeight() / 2 - 3;
        const int pixels = std::max(1, static_cast<int>(width));
        context->setLineWidth(1);
        for(int px = 0; px < pixels; ++px) {
            const std::uint32_t c0 = static_cast<std::uint32_t>(std::uint64_t(px) * w.columns / pixels);
            const std::uint32_t c1 = std::max<std::uint32_t>(
                c0 + 1, static_cast<std::uint32_t>(std::uint64_t(px + 1) * w.columns / pixels));
            int low = w.low[c0], high = w.high[c0];
            for(std::uint32_t k = c0; k < c1 && k < w.columns; ++k) {
                low = std::min<int>(low, w.low[k]);
                high = std::max<int>(high, w.high[k]);
            }
            const CCoord x = left + px + 0.5;
            const bool inside = x >= loop_left && x <= loop_right;
            context->setFrameColor(inside ? WithAlpha(c.wave, 0.95) : WithAlpha(c.wave_outside, 0.75));
            context->drawLine(CPoint(x, mid - half * high / 32768.0), CPoint(x, mid - half * low / 32768.0 + 1.0));
        }
    }
    if(file) {
        context->setFrameColor(c.accent);
        context->setLineWidth(2);
        auto bracket = [&](CCoord x, CCoord dir) {
            if(auto path = owned(context->createGraphicsPath())) {
                path->beginSubpath(CPoint(x + dir * 6, wave_box.top + 1));
                path->addLine(CPoint(x, wave_box.top + 1));
                path->addLine(CPoint(x, wave_box.bottom - 1));
                path->addLine(CPoint(x + dir * 6, wave_box.bottom - 1));
                context->drawGraphicsPath(path, CDrawContext::kPathStroked);
            }
        };
        bracket(loop_left + 1, 1);
        bracket(loop_right - 1, -1);

        // The playhead from the axis through plate and waveform; dashed, with a snowflake,
        // while Freeze holds it.
        const CCoord head = std::round(x_of(d.playhead)) + 0.5;
        context->setFrameColor(c.playhead);
        context->setLineWidth(1.25);
        if(d.frozen) {
            const CCoord dash[] = {4, 3.2};
            context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dash));
        }
        context->drawLine(CPoint(head, plate.top), CPoint(head, wave_box.bottom));
        context->setLineStyle(kLineSolid);
        if(auto path = owned(context->createGraphicsPath())) {
            path->beginSubpath(CPoint(head - 6, top + 8));
            path->addLine(CPoint(head + 6, top + 8));
            path->addLine(CPoint(head, top + L::kAxisBottom - 1));
            path->closeSubpath();
            FillPath(context, path, c.accent);
        }
        if(d.frozen) {
            const CCoord fy = plate.top + 46;
            const CRect box(head - 11, fy - 11, head + 11, fy + 11);
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
                context->drawLine(CPoint(head - ax, fy - ay), CPoint(head + ax, fy + ay));
                for(double end : {-1.0, 1.0}) { // the small branches near each end
                    const CPoint tip(head + end * ax * 0.62, fy + end * ay * 0.62);
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
        const CFontRef caps = skin.Caps(13);
        const CCoord label = TrackedWidth(context, "START", 2.0, caps);
        DrawTracked(context, "START", CRect(row.left, row.top, row.left + label + 2, row.bottom), kLeftText, 2.0, caps,
                    c.text, &c);
        char value[48];
        const int percent = static_cast<int>(std::lround(start_ * 100));
        if(file && rate > 0)
            std::snprintf(value, sizeof value, "%.2f s \xC2\xB7 %d %%", d.loop_start / rate, percent);
        else
            std::snprintf(value, sizeof value, "%d %%", percent);
        context->setFont(skin.Mono(12));
        context->setFontColor(c.accent);
        context->drawString(value, CRect(row.left + label + 10, row.top, row.left + label + 200, row.bottom), kLeftText);

        const CFontRef small = skin.Caps(11);
        const char* names[4] = {"PLAYHEAD", "LOOP", "SPRAY", "GRAIN \xC2\xB7 L ABOVE, R BELOW"};
        CCoord widths[4], total = 0;
        for(int k = 0; k < 4; ++k) {
            widths[k] = TrackedWidth(context, names[k], 1.2, small);
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
            DrawTracked(context, names[k], CRect(x, row.top, x + widths[k] + 2, row.bottom), kLeftText, 1.2, small,
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
                     all.top + L::kWaveTop + L::kWaveHeight);
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

bool MapView::PressAt(double x) {
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
        if(PressAt(event.mousePosition.x - getViewSize().left))
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
