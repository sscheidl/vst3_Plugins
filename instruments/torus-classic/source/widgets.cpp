#include "widgets.h"

#include "view_layout.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/clinestyle.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <string>
#include <utility>

namespace torus_classic {

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
    Release(); // the host gets the note-off of the old pitch first
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

LockButton::LockButton(const CRect& size, bool locked, std::function<void(bool)> toggled)
: CView(size), locked_(locked), toggled_(std::move(toggled)) {
    UpdateTooltip();
}

void LockButton::UpdateTooltip() {
    setTooltipText(locked_ ? "Tune lock, closed: Tune moves in semitones. Click for free tuning."
                           : "Tune lock, open: free tuning. Click for semitones.");
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
    // every zoom. Closed and quiet when Tune is on its grid; open and lit
    // when it is free.
    const CRect area = getViewSize();
    const CCoord s = std::min(area.getWidth(), area.getHeight());
    const CPoint c = area.getCenter();
    const CColor colour = locked_ ? kOffText : kPressed;
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);

    CRect circle(c.x - s / 2.0, c.y - s / 2.0, c.x + s / 2.0, c.y + s / 2.0);
    circle.inset(1.0, 1.0);
    context->setFillColor(kGap);
    context->setFrameColor(locked_ ? kOffFrame : kPressed);
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

ChoiceSelector::ChoiceSelector(const CRect& size, const char* const* labels, int count,
                               int choice, std::function<void(int)> chosen)
: CView(size),
  labels_(labels),
  count_(count > 0 ? count : 1),
  choice_(choice),
  chosen_(std::move(chosen)),
  font_(makeOwned<CFontDesc>("Segoe UI", 10, kBoldFace)) {}

void ChoiceSelector::SetChoice(int choice) {
    if(choice == choice_)
        return;
    choice_ = choice;
    invalid();
}

void ChoiceSelector::draw(CDrawContext* context) {
    const CRect area = getViewSize();
    const CCoord cell = area.getWidth() / count_;
    const CCoord radius = area.getHeight() / 2.0;
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    StrokeRoundRect(context, area, radius - 1.0, kOffFrame);
    context->setFont(font_);
    for(int i = 0; i < count_; ++i) {
        const CRect box(area.left + i * cell, area.top, area.left + (i + 1) * cell, area.bottom);
        if(i == choice_)
            FillRoundRect(context, box, radius, kPressed);
        context->setFontColor(i == choice_ ? kGap : kOffText);
        context->drawString(labels_[i], box, kCenterText);
    }
    setDirty(false);
}

void ChoiceSelector::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft())
        return;
    const CRect area = getViewSize();
    const int choice = std::clamp(
        static_cast<int>((event.mousePosition.x - area.left) / (area.getWidth() / count_)), 0,
        count_ - 1);
    if(choice != choice_) {
        choice_ = choice;
        invalid();
        if(chosen_)
            chosen_(choice);
    }
    event.consumed = true;
}

ModelSelector::ModelSelector(const CRect& size, int model, std::function<void(int)> chosen)
: CView(size),
  model_(model),
  chosen_(std::move(chosen)),
  font_(makeOwned<CFontDesc>("Segoe UI", 11, kBoldFace)) {}

void ModelSelector::SetModel(int model) {
    if(model == model_)
        return;
    model_ = model;
    invalid();
}

void ModelSelector::draw(CDrawContext* context) {
    const CRect area = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    context->setFont(font_);
    for(int model = 0; model < kModelCount; ++model) {
        const CRect cell =
            ToCRect(ModelRectFor(model, area.getWidth(), area.getHeight()), area);
        const bool selected = model == model_;
        if(selected)
            FillRoundRect(context, cell, 5.0, kPressed);
        else
            StrokeRoundRect(context, cell, 4.0, kOffFrame);
        context->setFontColor(selected ? kGap : kOffText);
        context->drawString(kModelNames[model], cell, kCenterText);
    }
    setDirty(false);
}

void ModelSelector::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft())
        return;
    const CRect area = getViewSize();
    const int model = ModelAt(event.mousePosition.x - area.left, event.mousePosition.y - area.top,
                              area.getWidth(), area.getHeight());
    if(model >= 0 && model != model_) {
        model_ = model;
        invalid();
        if(chosen_)
            chosen_(model);
    }
    event.consumed = true;
}

} // namespace torus_classic
