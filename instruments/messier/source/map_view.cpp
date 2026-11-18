#include "map_view.h"

#include "guard.h"

#include "vstgui/lib/events.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace messier {

using namespace VSTGUI;

namespace {
constexpr CColor kMapBack(20, 22, 30);
constexpr CColor kLoopFill(52, 58, 84);
constexpr CColor kSprayFill(70, 110, 150, 90);
constexpr CColor kPlayhead(236, 236, 240);
constexpr CColor kWave(120, 170, 200);
constexpr CColor kWaveOutside(70, 90, 110);
constexpr CColor kGrain(255, 214, 150);
constexpr CColor kDim(150, 156, 168);
constexpr CColor kDropFrame(255, 214, 150);
constexpr CColor kDropShade(10, 12, 18, 170);
constexpr CColor kDropRefused(240, 150, 60);
constexpr CCoord kWaveHeight = 34; // the waveform strip at the bottom
constexpr CCoord kGap = 4;
constexpr double kPi = 3.14159265358979323846;
} // namespace

MapView::MapView(const CRect& size) : CView(size) {
    font_ = makeOwned<CFontDesc>("Segoe UI", 11);
    // Since 0.1.8 the map takes the mouse: START, SPRAY, and drops (VSTGUI looks for
    // drop targets only among views that take the mouse).
    setMouseEnabled(true);
}

void MapView::Show(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept {
    const bool same_wave = wave == wave_;
    if(same_wave && std::memcmp(&display, &display_, sizeof display) == 0)
        return;
    display_ = display;
    wave_ = std::move(wave);
    Guarded([&] { invalid(); }); // a noexcept function (MS-R37)
}

void MapView::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void MapView::DrawUnchecked(CDrawContext* context) {
    const CRect all = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    context->setFillColor(kMapBack);
    context->drawRect(all, kDrawFilled);
    const EngineDisplay& d = display_;
    if(d.frames == 0) {
        context->setFont(font_);
        context->setFontColor(kDim);
        context->drawString("no sample  -  drop a WAV here or use LOAD...", all, kCenterText);
        DrawDropHighlight(context);
        setDirty(false);
        return;
    }
    const CCoord left = all.left, width = all.getWidth();
    const CRect map(all.left, all.top, all.right, all.bottom - kWaveHeight - kGap);
    const CRect strip(all.left, all.bottom - kWaveHeight, all.right, all.bottom);
    const double frames = d.frames;
    auto x_of = [&](double frame) { return left + width * frame / frames; };

    // The loop, over map and waveform.
    const CCoord loop_left = x_of(d.loop_start);
    const CCoord loop_right = x_of(std::min(frames, d.loop_start + d.loop_length));
    context->setFillColor(kLoopFill);
    context->drawRect(CRect(loop_left, map.top, std::max(loop_right, loop_left + 1), map.bottom),
                      kDrawFilled);

    // The spray range: grains start within it, wrapped around the file as the core
    // wraps them.
    if(d.spray > 0) {
        context->setFillColor(kSprayFill);
        const double from = d.playhead - d.spray, to = d.playhead + d.spray;
        if(to - from >= frames) {
            context->drawRect(map, kDrawFilled);
        } else {
            for(double shift : {-frames, 0.0, frames}) {
                const double a = std::max(0.0, from + shift), b = std::min(frames, to + shift);
                if(b > a)
                    context->drawRect(CRect(x_of(a), map.top, x_of(b), map.bottom), kDrawFilled);
            }
        }
    }

    // The waveform, brighter inside the loop.
    if(wave_ && wave_->columns > 0) {
        const WaveOverview& w = *wave_;
        const CCoord mid = (strip.top + strip.bottom) / 2, half = strip.getHeight() / 2;
        const int pixels = std::max(1, static_cast<int>(width));
        for(int px = 0; px < pixels; ++px) {
            const std::uint32_t c0 = static_cast<std::uint32_t>(std::uint64_t(px) * w.columns / pixels);
            const std::uint32_t c1 = std::max<std::uint32_t>(
                c0 + 1, static_cast<std::uint32_t>(std::uint64_t(px + 1) * w.columns / pixels));
            int low = w.low[c0], high = w.high[c0];
            for(std::uint32_t c = c0; c < c1 && c < w.columns; ++c) {
                low = std::min<int>(low, w.low[c]);
                high = std::max<int>(high, w.high[c]);
            }
            const CCoord x = left + px + 0.5;
            const bool inside = x >= loop_left && x <= loop_right;
            context->setFrameColor(inside ? kWave : kWaveOutside);
            context->setLineWidth(1);
            context->drawLine(CPoint(x, mid - half * high / 32768.0),
                              CPoint(x, mid - half * low / 32768.0 + 0.5));
        }
    }

    // The sounding grains: across their read position, down their panorama,
    // brightest in the middle of their window.
    for(std::uint32_t i = 0; i < d.grain_count; ++i) {
        const GrainDot& g = d.grains[i];
        const double level = std::sin(kPi * g.phase);
        const CCoord x = x_of(g.position);
        const CCoord y = map.top + 4 + (map.getHeight() - 8) * g.pan;
        CColor color = kGrain;
        color.alpha = static_cast<uint8_t>(60 + 195 * level);
        context->setFillColor(color);
        const CCoord r = 2.0 + 2.0 * level;
        context->drawEllipse(CRect(x - r, y - r, x + r, y + r), kDrawFilled);
    }

    // The playhead, across map and waveform.
    const CCoord head = x_of(d.playhead);
    context->setFrameColor(kPlayhead);
    context->setLineWidth(1.5);
    context->drawLine(CPoint(head, map.top), CPoint(head, strip.bottom));

    context->setFont(font_);
    context->setFontColor(kDim);
    context->drawString("L", CRect(map.left + 4, map.top + 2, map.left + 20, map.top + 16), kLeftText);
    context->drawString("R", CRect(map.left + 4, map.bottom - 16, map.left + 20, map.bottom - 2), kLeftText);
    DrawDropHighlight(context);
    setDirty(false);
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
    if(drop_state_ == DropShown::kNone)
        return;
    const bool usable = drop_state_ == DropShown::kUsable;
    const CColor colour = usable ? kDropFrame : kDropRefused;
    CRect all = getViewSize();
    context->setFillColor(kDropShade);
    context->drawRect(all, kDrawFilled);
    CRect frame = all;
    frame.inset(2, 2);
    context->setFrameColor(colour);
    context->setLineWidth(2);
    const CCoord dash[] = {6, 4};
    context->setLineStyle(CLineStyle(CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dash));
    context->drawRect(frame, kDrawStroked);
    context->setLineStyle(kLineSolid);
    context->setFont(font_);
    context->setFontColor(colour);
    const std::string text = usable ? std::string("Drop WAV to load") : "Can't load: " + drop_reason_;
    context->drawString(text.c_str(), all, kCenterText);
}

double MapView::Share(double x) const {
    const double width = getViewSize().getWidth();
    return width > 0 ? std::clamp(x / width, 0.0, 1.0) : 0.0;
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
