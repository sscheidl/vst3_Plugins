// Copied from Kekule (Torus Classic) 0.3.1, source/widgets.cpp: the keyboard and
// the switch; only the namespace is changed.
#include "widgets.h"

#include "guard.h"

#include "view_layout.h"

#include "reference_dsp.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/clinestyle.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <string>
#include <utility>

namespace messier {

using namespace VSTGUI;

namespace {

constexpr CColor kWhiteKey(232, 232, 236);
constexpr CColor kBlackKey(44, 46, 54);
constexpr CColor kGap(28, 30, 36);
constexpr CColor kPressed(236, 172, 64);
constexpr CColor kLabel(120, 124, 136);
constexpr CColor kOffFrame(64, 68, 80);
constexpr CColor kOffText(150, 156, 168);

CRect ToCRect(const KeyRect& key, const CRect& origin) {
    return CRect(origin.left + key.left, origin.top + key.top, origin.left + key.right,
                 origin.top + key.bottom);
}

void FillRoundRect(CDrawContext* context, const CRect& rect, CCoord radius, CColor color) {
    auto path = owned(context->createRoundRectGraphicsPath(rect, radius));
    if(!path)
        return;
    context->setFillColor(color);
    context->drawGraphicsPath(path, CDrawContext::kPathFilled);
}

void StrokeRoundRect(CDrawContext* context, CRect rect, CCoord radius, CColor color) {
    // The stroke is centred on the outline: keep it inside the rectangle.
    rect.inset(1.0, 1.0);
    auto path = owned(context->createRoundRectGraphicsPath(rect, radius - 0.01));
    if(!path)
        return;
    context->setFrameColor(color);
    context->setLineWidth(1.5);
    context->drawGraphicsPath(path, CDrawContext::kPathStroked);
}

} // namespace

KeyboardView::KeyboardView(const CRect& size, std::function<void(int)> strike,
                           std::function<void(int)> release)
: CView(size),
  strike_(std::move(strike)),
  release_(std::move(release)),
  label_font_(makeOwned<CFontDesc>("Segoe UI", 9)) {}

void KeyboardView::ReleaseHeld() {
    Release();
}

void KeyboardView::SetLatch(bool on) {
    latch_ = on;
    if(!on)
        Release(); // the held key ends with the latch
}

void KeyboardView::SetTranspose(int semitones) {
    const int transpose = ClampKeyboardTranspose(semitones);
    if(transpose == transpose_)
        return;
    Release(); // the host gets the note-off of the old pitch first
    transpose_ = transpose;
    invalid();
}

void KeyboardView::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void KeyboardView::DrawUnchecked(CDrawContext* context) {
    const CRect area = getViewSize();
    const double width = area.getWidth();
    const double height = area.getHeight();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);

    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        if(IsBlackKey(note))
            continue;
        CRect key = ToCRect(KeyRectFor(note, width, height), area);
        key.inset(0.75, 0.0); // the gap between white keys
        FillRoundRect(context, key, 3.0, note + transpose_ == pressed_ ? kPressed : kWhiteKey);
        if(note % 12 == 0) { // name the Cs
            context->setFont(label_font_);
            context->setFontColor(kLabel);
            const std::string name = "C" + std::to_string((note + transpose_) / 12 - 1);
            CRect label(key.left, key.bottom - 14.0, key.right, key.bottom - 2.0);
            context->drawString(name.c_str(), label, kCenterText);
        }
    }
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        if(!IsBlackKey(note))
            continue;
        CRect key = ToCRect(KeyRectFor(note, width, height), area);
        // A dark outline sets the black key off against a pressed white one.
        FillRoundRect(context, key, 2.5, kGap);
        key.inset(1.0, 0.0);
        key.bottom -= 1.0;
        FillRoundRect(context, key, 2.0, note + transpose_ == pressed_ ? kPressed : kBlackKey);
    }
    setDirty(false);
}

int KeyboardView::NoteAtPoint(const CPoint& where) const {
    const CRect area = getViewSize();
    const int note = NoteAt(where.x - area.left, where.y - area.top, area.getWidth(),
                            area.getHeight());
    return note < 0 ? -1 : note + transpose_;
}

void KeyboardView::Press(int note) {
    if(note == pressed_)
        return;
    if(pressed_ >= 0 && release_)
        release_(pressed_); // gliding on: the key before is let go
    pressed_ = note;
    invalid();
    if(note >= 0 && strike_)
        strike_(note);
}

void KeyboardView::Release() {
    dragging_ = false;
    if(pressed_ >= 0) {
        if(release_)
            release_(pressed_);
        pressed_ = -1;
        invalid();
    }
}

void KeyboardView::MouseDownOn(int note) {
    if(latch_) {
        // The held key again: let go. Another key: the hold moves there.
        if(note >= 0 && note == pressed_) {
            Release();
        } else if(note >= 0) {
            Press(note);
            dragging_ = true;
        }
        return;
    }
    Release(); // a key still held, should a mouse-up have been lost
    dragging_ = true;
    Press(note);
}

void KeyboardView::MouseMoveTo(int note) {
    if(!dragging_ || (latch_ && note < 0)) // latched: off the keyboard keeps the last key
        return;
    if(note != pressed_)
        Press(note); // off the keyboard: nothing lit, nothing struck
}

void KeyboardView::MouseUp() {
    if(latch_)
        dragging_ = false; // the key stays held
    else
        Release();
}

// MS-R37: the mouse handlers are exception boundaries (guard.h).
void KeyboardView::onMouseDownEvent(MouseDownEvent& event) {
    Guarded([&] {
        if(!event.buttonState.isLeft())
            return;
        MouseDownOn(NoteAtPoint(event.mousePosition));
        event.consumed = true;
    });
}

void KeyboardView::onMouseMoveEvent(MouseMoveEvent& event) {
    Guarded([&] {
        if(!dragging_)
            return;
        MouseMoveTo(NoteAtPoint(event.mousePosition));
        event.consumed = true;
    });
}

void KeyboardView::onMouseUpEvent(MouseUpEvent& event) {
    Guarded([&] {
        if(!dragging_)
            return;
        MouseUp();
        event.consumed = true;
    });
}

void KeyboardView::onMouseCancelEvent(MouseCancelEvent& event) {
    Guarded([&] { MouseUp(); });
    event.consumed = true;
}

namespace {

// The core's window bank, built once (about 14 KB).
const float (&WindowBank())[neb::W_BANK][neb::WIN_LEN + 1] {
    static float bank[neb::W_BANK][neb::WIN_LEN + 1];
    static const bool built = (neb::BuildWindows(bank), true);
    (void)built;
    return bank;
}

} // namespace

float WindowShapeView::Sample(float pot, float share) {
    float mixed[neb::WIN_LEN + 1];
    neb::WinMix(WindowBank(), std::clamp(pot, 0.0f, 1.0f), mixed);
    const int i = std::clamp(static_cast<int>(share * neb::WIN_LEN + 0.5f), 0, neb::WIN_LEN);
    return mixed[i];
}

void WindowShapeView::SetValue(float pot) {
    pot = std::clamp(pot, 0.0f, 1.0f);
    if(pot == pot_)
        return;
    pot_ = pot;
    invalid();
}

void WindowShapeView::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void WindowShapeView::DrawUnchecked(CDrawContext* context) {
    float mixed[neb::WIN_LEN + 1];
    neb::WinMix(WindowBank(), pot_, mixed);
    const CRect area = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    if(auto path = owned(context->createGraphicsPath())) {
        constexpr int kPoints = 48;
        for(int k = 0; k <= kPoints; ++k) {
            const float y = mixed[k * neb::WIN_LEN / kPoints];
            const CPoint p(area.left + area.getWidth() * k / kPoints,
                           area.bottom - 1.0 - (area.getHeight() - 2.0) * std::clamp(y, 0.0f, 1.0f));
            if(k == 0)
                path->beginSubpath(CPoint(area.left, area.bottom - 1.0));
            path->addLine(p);
        }
        path->addLine(CPoint(area.right, area.bottom - 1.0));
        context->setFrameColor(CColor(232, 232, 236));
        context->setLineWidth(1.4);
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    setDirty(false);
}

ToggleButton::ToggleButton(const CRect& size, const char* title, bool on,
                           std::function<void(bool)> toggled, Style style)
: CView(size),
  title_(title),
  on_(on),
  style_(style),
  toggled_(std::move(toggled)),
  font_(makeOwned<CFontDesc>("Segoe UI", 10, kBoldFace)) {}

void ToggleButton::SetOn(bool on) {
    if(on == on_)
        return;
    on_ = on;
    invalid();
}

void ToggleButton::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void ToggleButton::DrawUnchecked(CDrawContext* context) {
    const CRect area = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    if(on_)
        FillRoundRect(context, area, area.getHeight() / 2.0, kPressed);
    else
        StrokeRoundRect(context, area, area.getHeight() / 2.0 - 1.0, kOffFrame);
    context->setFont(font_);
    context->setFontColor(on_ ? kGap : (getMouseEnabled() ? kOffText : kLabel));
    context->drawString(title_, area, kCenterText);
    setDirty(false);
}

void ToggleButton::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft() || !getMouseEnabled())
        return;
    if(style_ != Style::Action)
        on_ = !on_;
    event.consumed = true;
    // MS-R37: what the button does (LOAD..., KEYS, LATCH, STOP, FREEZE, RESET, the
    // octave keys) runs inside the window's exception boundary. If it could not be
    // done, the button shows its old state again.
    const bool done = Guarded(false, [&] {
        if(toggled_)
            toggled_(on_);
        return true;
    });
    if(!done && style_ != Style::Action)
        on_ = !on_;
    Guarded([&] { invalid(); });
}

ResizeGrip::ResizeGrip(const CRect& size, CColor colour, std::function<void()> begin,
                       std::function<void(double, double)> drag)
: CView(size), colour_(colour), begin_(std::move(begin)), drag_(std::move(drag)) {
    setTooltipText("Drag to resize the window");
}

double ResizeGrip::Zoom() const {
    const CFrame* frame = getFrame();
    return frame ? frame->getZoom() : 1.0;
}

void ResizeGrip::Press(double x, double y) {
    dragging_ = true;
    start_x_ = x;
    start_y_ = y;
    if(begin_)
        begin_();
}

void ResizeGrip::DragTo(double x, double y) {
    if(dragging_ && drag_)
        drag_(x - start_x_, y - start_y_);
}

void ResizeGrip::Let() {
    dragging_ = false;
}

void ResizeGrip::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void ResizeGrip::DrawUnchecked(CDrawContext* context) {
    // Three diagonal hatch lines towards the corner, as Windows draws a size grip.
    const CRect area = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    context->setFrameColor(colour_);
    context->setLineWidth(1.5);
    const double size = std::min(area.getWidth(), area.getHeight());
    for(int i = 1; i <= 3; ++i) {
        const double inset = size * (1.0 - 0.28 * i);
        context->drawLine(CPoint(area.right - 2, area.top + inset), CPoint(area.left + inset, area.bottom - 2));
    }
}

void ResizeGrip::onMouseEnterEvent(MouseEnterEvent& event) {
    Guarded([&] {
        if(CFrame* frame = getFrame())
            frame->setCursor(kCursorNWSESize);
    });
    event.consumed = true;
}

void ResizeGrip::onMouseExitEvent(MouseExitEvent& event) {
    Guarded([&] {
        if(CFrame* frame = getFrame())
            frame->setCursor(kCursorDefault);
    });
    event.consumed = true;
}

// The mouse position is in the frame's units (the grip sits directly in the frame);
// times the zoom it is in window pixels from the window's corner at the top left, which
// a resize from the lower right does not move.
void ResizeGrip::onMouseDownEvent(MouseDownEvent& event) {
    Guarded([&] {
        if(!event.buttonState.isLeft())
            return;
        const double zoom = Zoom();
        Press(event.mousePosition.x * zoom, event.mousePosition.y * zoom);
        event.consumed = true;
    });
}

void ResizeGrip::onMouseMoveEvent(MouseMoveEvent& event) {
    Guarded([&] {
        if(!dragging_)
            return;
        const double zoom = Zoom();
        DragTo(event.mousePosition.x * zoom, event.mousePosition.y * zoom);
        event.consumed = true;
    });
}

void ResizeGrip::onMouseUpEvent(MouseUpEvent& event) {
    Let();
    event.consumed = true;
}

void ResizeGrip::onMouseCancelEvent(MouseCancelEvent& event) {
    Let();
    event.consumed = true;
}

} // namespace messier
