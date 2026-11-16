#include "keyboard_view.h"

#include "keyboard_layout.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <string>
#include <utility>

namespace taureon::toy_piano_generative {

using namespace VSTGUI;

namespace {

constexpr CColor kWhiteKey(232, 232, 236);
constexpr CColor kBlackKey(44, 46, 54);
constexpr CColor kGap(28, 30, 36);
constexpr CColor kPressed(236, 172, 64);
constexpr CColor kMarkedWhite(240, 208, 150);
constexpr CColor kMarkedBlack(150, 112, 48);
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

} // namespace

KeyboardView::KeyboardView(const CRect& size, std::function<void(int)> strike)
: CView(size), strike_(std::move(strike)), label_font_(makeOwned<CFontDesc>("Segoe UI", 9)) {}

void KeyboardView::SetShift(int semitones) {
    if(semitones == shift_)
        return;
    shift_ = semitones;
    invalid();
}

void KeyboardView::SetMarked(int note) {
    if(note == marked_)
        return;
    marked_ = note;
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
        FillRoundRect(context, key, 3.0,
                      note == pressed_ ? kPressed
                                       : (note + shift_ == marked_ ? kMarkedWhite : kWhiteKey));
        if(note % 12 == 0) { // name the Cs
            context->setFont(label_font_);
            context->setFontColor(kLabel);
            const std::string name = "C" + std::to_string((note + shift_) / 12 - 1);
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
        FillRoundRect(context, key, 2.0,
                      note == pressed_ ? kPressed
                                       : (note + shift_ == marked_ ? kMarkedBlack : kBlackKey));
    }
    setDirty(false);
}

int KeyboardView::NoteAtPoint(const CPoint& where) const {
    const CRect area = getViewSize();
    return NoteAt(where.x - area.left, where.y - area.top, area.getWidth(), area.getHeight());
}

void KeyboardView::Press(int note) {
    if(note == pressed_)
        return;
    pressed_ = note;
    invalid();
    if(note >= 0 && strike_)
        strike_(note + shift_);
}

void KeyboardView::Release() {
    dragging_ = false;
    if(pressed_ >= 0) {
        pressed_ = -1;
        invalid();
    }
}

void KeyboardView::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft())
        return;
    dragging_ = true;
    pressed_ = -1; // a click on the key just played strikes it again
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
                           std::function<void(bool)> toggled)
: CView(size),
  title_(title),
  on_(on),
  toggled_(std::move(toggled)),
  font_(makeOwned<CFontDesc>("Segoe UI", 10, kBoldFace)) {}

void ToggleButton::SetOn(bool on) {
    if(on == on_)
        return;
    on_ = on;
    invalid();
}

void ToggleButton::draw(CDrawContext* context) {
    CRect area = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    if(on_) {
        FillRoundRect(context, area, area.getHeight() / 2.0, kPressed);
    } else {
        // The stroke is centred on the outline: keep it inside the view.
        area.inset(1.0, 1.0);
        auto path =
            owned(context->createRoundRectGraphicsPath(area, area.getHeight() / 2.0 - 0.01));
        if(path) {
            context->setFrameColor(kOffFrame);
            context->setLineWidth(1.5);
            context->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
    }
    context->setFont(font_);
    context->setFontColor(on_ ? kGap : kOffText);
    context->drawString(title_, getViewSize(), kCenterText);
    setDirty(false);
}

void ToggleButton::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft())
        return;
    on_ = !on_;
    invalid();
    if(toggled_)
        toggled_(on_);
    event.consumed = true;
}

LockButton::LockButton(const CRect& size, bool locked, std::function<void(bool)> toggled)
: CView(size), locked_(locked), toggled_(std::move(toggled)) {
    UpdateTooltip();
}

void LockButton::UpdateTooltip() {
    setTooltipText(locked_ ? "Mutation lock, closed: the pattern is frozen. Click to let it mutate."
                           : "Mutation lock, open: the pattern mutates. Click to freeze it.");
}

void LockButton::SetLocked(bool locked) {
    if(locked == locked_)
        return;
    locked_ = locked;
    UpdateTooltip();
    invalid();
}

void LockButton::draw(CDrawContext* context) {
    // Everything is a fraction of the view's size `s`, so it stays sharp at
    // every zoom. Closed and lit while the pattern is frozen; open and quiet
    // while it mutates.
    const CRect area = getViewSize();
    const CCoord s = std::min(area.getWidth(), area.getHeight());
    const CPoint c = area.getCenter();
    const CColor colour = locked_ ? kPressed : kOffText;
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);

    CRect circle(c.x - s / 2.0, c.y - s / 2.0, c.x + s / 2.0, c.y + s / 2.0);
    circle.inset(1.0, 1.0);
    context->setFillColor(kGap);
    context->setFrameColor(locked_ ? kPressed : kOffFrame);
    context->setLineWidth(1.5);
    context->setLineStyle(kLineSolid);
    context->drawEllipse(circle, kDrawFilledAndStroked);

    // The body of the padlock.
    const CCoord body_top = c.y - 0.02 * s;
    FillRoundRect(context, CRect(c.x - 0.21 * s, body_top, c.x + 0.21 * s, body_top + 0.30 * s),
                  0.05 * s, colour);

    // Its shackle: two legs and a half circle. Open, it is lifted and its
    // right leg ends above the body.
    const CCoord radius = 0.13 * s;
    const CCoord bow = body_top - 0.08 * s - (locked_ ? 0.0 : 0.09 * s);
    const CCoord k = 0.5523 * radius; // a quarter circle as a cubic curve
    if(auto path = owned(context->createGraphicsPath())) {
        path->beginSubpath(c.x - radius, body_top);
        path->addLine(c.x - radius, bow);
        path->addBezierCurve(c.x - radius, bow - k, c.x - k, bow - radius, c.x, bow - radius);
        path->addBezierCurve(c.x + k, bow - radius, c.x + radius, bow - k, c.x + radius, bow);
        path->addLine(c.x + radius, locked_ ? body_top : bow + 0.05 * s);
        context->setFrameColor(colour);
        context->setLineWidth(0.09 * s);
        context->setLineStyle(CLineStyle(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
        context->setLineStyle(kLineSolid);
    }
    setDirty(false);
}

void LockButton::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft())
        return;
    locked_ = !locked_;
    UpdateTooltip();
    invalid();
    if(toggled_)
        toggled_(locked_);
    event.consumed = true;
}

} // namespace taureon::toy_piano_generative
