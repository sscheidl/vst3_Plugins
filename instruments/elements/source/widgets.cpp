#include "widgets.h"

#include "view_layout.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <string>
#include <cmath>
#include <utility>

namespace elements_synth {

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

void KeyboardView::SetTranspose(int semitones) {
    const int transpose = ClampKeyboardTranspose(semitones);
    if(transpose == transpose_)
        return;
    Release(); // the old MIDI pitch must be released before changing range
    transpose_ = transpose;
    invalid();
}

void KeyboardView::draw(CDrawContext* context) {
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
    const int note = NoteAt(where.x - area.left, where.y - area.top,
                            area.getWidth(), area.getHeight());
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

void KeyboardView::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft())
        return;
    Release(); // a key still held, should a mouse-up have been lost
    dragging_ = true;
    Press(NoteAtPoint(event.mousePosition));
    event.consumed = true;
}

void KeyboardView::onMouseMoveEvent(MouseMoveEvent& event) {
    if(!dragging_)
        return;
    const int note = NoteAtPoint(event.mousePosition);
    if(note != pressed_)
        Press(note); // off the keyboard: nothing lit, nothing struck
    event.consumed = true;
}

void KeyboardView::onMouseUpEvent(MouseUpEvent& event) {
    if(!dragging_)
        return;
    Release();
    event.consumed = true;
}

void KeyboardView::onMouseCancelEvent(MouseCancelEvent& event) {
    Release();
    event.consumed = true;
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
    const CRect area = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    if(style_ == Style::Snowflake) {
        context->setLineWidth(1.5);
        context->setFrameColor(on_ ? kPressed : kOffText);
        context->setFillColor(on_ ? kPressed : kGap);
        CRect circle = area;
        circle.inset(1, 1);
        context->drawEllipse(circle, on_ ? kDrawFilledAndStroked : kDrawStroked);
        context->setFrameColor(on_ ? kGap : kOffText);
        const CPoint centre((area.left + area.right) / 2, (area.top + area.bottom) / 2);
        const double radius = area.getWidth() * 0.29;
        for(int i = 0; i < 6; ++i) {
            const double angle = i * 3.141592653589793 / 3;
            const double dx = std::cos(angle), dy = std::sin(angle);
            const CPoint tip(centre.x + radius * dx, centre.y + radius * dy);
            context->drawLine(centre, tip);
            const CPoint branch(centre.x + radius * 0.58 * dx,
                                centre.y + radius * 0.58 * dy);
            for(const double side : {-1.0, 1.0})
                context->drawLine(branch, CPoint(branch.x + radius * (0.27 * dx - side * 0.24 * dy),
                                                branch.y + radius * (0.27 * dy + side * 0.24 * dx)));
        }
        setDirty(false);
        return;
    }
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
    invalid();
    if(toggled_)
        toggled_(on_);
    event.consumed = true;
}

} // namespace elements_synth
