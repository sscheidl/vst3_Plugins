// The keyboard's and the switches' behaviour copied from Kekule (Torus Classic) 0.3.1,
// source/widgets.cpp; drawn since 0.2.0 with the GUI preset's bitmaps (gui_skin.h).
#include "widgets.h"

#include "gui_layout.h"
#include "guard.h"

#include "view_layout.h"

#include "reference_dsp.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/clinestyle.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace messier {

using namespace VSTGUI;

namespace {

// Without a skin (tests, or a window that could not load one).
constexpr CColor kWhiteKey(232, 232, 236);
constexpr CColor kBlackKey(44, 46, 54);
constexpr CColor kGap(28, 30, 36);
constexpr CColor kPressed(236, 172, 64);
constexpr CColor kLabel(120, 124, 136);
constexpr CColor kOffFrame(64, 68, 80);
constexpr CColor kOffText(150, 156, 168);
constexpr CColor kKeyLabel(96, 92, 84); // the Cs on the key bitmap
constexpr double kPi = 3.14159265358979323846;

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

// A point at `angle` degrees clockwise from twelve o'clock.
CPoint Polar(const CPoint& centre, double angle, double radius) {
    const double a = angle * kPi / 180.0;
    return CPoint(centre.x + std::sin(a) * radius, centre.y - std::cos(a) * radius);
}

} // namespace

KeyboardView::KeyboardView(const CRect& size, std::function<void(int)> strike,
                           std::function<void(int)> release)
: CView(size),
  strike_(std::move(strike)),
  release_(std::move(release)),
  label_font_(makeOwned<CFontDesc>("Segoe UI", 9)) {}

void KeyboardView::SetSkin(std::shared_ptr<const Skin> skin) {
    skin_ = std::move(skin);
    invalid();
}

void KeyboardView::ReleaseHeld() {
    Release();
}

void KeyboardView::ForgetHeld() {
    dragging_ = false;
    if(pressed_ >= 0) {
        pressed_ = -1;
        invalid();
    }
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
    const SkinImage* white = skin_ ? &skin_->Image(SkinImageId::kKeyWhite) : nullptr;
    const SkinImage* black = skin_ ? &skin_->Image(SkinImageId::kKeyBlack) : nullptr;
    const CColor pressed = skin_ ? WithAlpha(skin_->Colours().control, 0.85) : kPressed;
    // Beta 2 (the user's touch-pad keyboard), round 3 one octave: with the delivered pads,
    // every key a flat pad (view_layout.h): the black notes above, centred between their
    // white keys, the white notes below; a struck key lights its pad in the accent colour.
    const SkinImage* pad = skin_ ? &skin_->Image(SkinImageId::kPad) : nullptr;
    const SkinImage* pad_lit = skin_ ? &skin_->Image(SkinImageId::kPadLit) : nullptr;
    if(pad && *pad) {
        for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
            CRect box = ToCRect(PadRectFor(note, width, height), area);
            const bool down = note + transpose_ == pressed_;
            box.inset(2.0, 2.0);
            if(box.getWidth() < 8 || box.getHeight() < 8)
                continue;
            DrawNinePart(context, down && pad_lit && *pad_lit ? *pad_lit : *pad, box, 10);
            if(down)
                FillRoundRect(context, CRect(box).inset(2.0, 2.0), 4.0, WithAlpha(skin_->Colours().control, 0.45));
            if(note % 12 == 0) { // name the Cs
                context->setFont(skin_->Mono(9));
                context->setFontColor(WithAlpha(skin_->Colours().dim, 0.8));
                const std::string name = "C" + std::to_string((note + transpose_) / 12 - 1);
                context->drawString(name.c_str(), box, kCenterText); // round 5: in the pad's middle
            }
        }
        setDirty(false);
        return;
    }

    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        if(IsBlackKey(note))
            continue;
        const CRect key = ToCRect(KeyRectFor(note, width, height), area);
        const bool down = note + transpose_ == pressed_;
        if(white && *white) {
            DrawSkinImage(context, *white, key);
            if(down)
                FillRoundRect(context, CRect(key).inset(2.0, 1.0), 3.0, pressed);
        } else {
            CRect plain = key;
            plain.inset(0.75, 0.0); // the gap between white keys
            FillRoundRect(context, plain, 3.0, down ? kPressed : kWhiteKey);
        }
        if(note % 12 == 0) { // name the Cs
            context->setFont(skin_ ? skin_->Mono(9) : label_font_.get());
            context->setFontColor(skin_ ? kKeyLabel : kLabel);
            const std::string name = "C" + std::to_string((note + transpose_) / 12 - 1);
            CRect label(key.left, key.bottom - 15.0, key.right, key.bottom - 3.0);
            context->drawString(name.c_str(), label, kCenterText);
        }
    }
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        if(!IsBlackKey(note))
            continue;
        CRect key = ToCRect(KeyRectFor(note, width, height), area);
        const bool down = note + transpose_ == pressed_;
        if(black && *black) {
            DrawSkinImage(context, *black, key);
            if(down)
                FillRoundRect(context, CRect(key).inset(2.0, 1.5), 2.0, pressed);
            continue;
        }
        // A dark outline sets the black key off against a pressed white one.
        FillRoundRect(context, key, 2.5, kGap);
        key.inset(1.0, 0.0);
        key.bottom -= 1.0;
        FillRoundRect(context, key, 2.0, down ? kPressed : kBlackKey);
    }
    setDirty(false);
}

int KeyboardView::NoteAtPoint(const CPoint& where) const {
    const CRect area = getViewSize();
    // Round 3: the pads where they are drawn.
    const bool pads = skin_ && skin_->Image(SkinImageId::kPad);
    const int note = (pads ? PadNoteAt : NoteAt)(where.x - area.left, where.y - area.top, area.getWidth(),
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

void WindowShapeView::SetColour(CColor colour) {
    colour_ = colour;
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
        context->setFrameColor(colour_);
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

void ToggleButton::SetSkin(std::shared_ptr<const Skin> skin) {
    skin_ = std::move(skin);
    invalid();
}

void ToggleButton::SetValueInk(bool on) {
    value_ink_ = on;
    invalid();
}

void ToggleButton::SetFlat(bool flat) {
    flat_ = flat;
    invalid();
}

void ToggleButton::SetIcon(Icon icon) {
    icon_ = icon;
    invalid();
}

void ToggleButton::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void ToggleButton::DrawUnchecked(CDrawContext* context) {
    const CRect area = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const bool down = on_ && style_ == Style::Toggle;
    if(!skin_) {
        if(on_)
            FillRoundRect(context, area, area.getHeight() / 2.0, kPressed);
        else
            StrokeRoundRect(context, area, area.getHeight() / 2.0 - 1.0, kOffFrame);
        context->setFont(font_);
        context->setFontColor(on_ ? kGap : (getMouseEnabled() ? kOffText : kLabel));
        context->drawString(title_, area, kCenterText);
        setDirty(false);
        return;
    }
    const SkinColours& c = skin_->Colours();
    if(flat_) {
        // DETAILS (0.2.4): the word and a chevron on the panel, lit while open.
        const CColor ink = on_ ? c.value : c.text;
        const CFontRef font = skin_->Sans(12);
        const CCoord word = TrackedWidth(context, title_, 1.8, font);
        const CCoord left = std::round(area.left + (area.getWidth() - word - 16) / 2);
        DrawTracked(context, title_, CRect(left, area.top, left + word + 2, area.bottom), kLeftText, 1.8, font, ink);
        const CCoord x = left + word + 10, y = std::round(area.top + area.getHeight() / 2);
        context->setFrameColor(ink);
        context->setLineWidth(1.4);
        const CCoord dir = on_ ? -1.0 : 1.0; // down: it opens; up: it closes
        context->drawLine(CPoint(x - 4, y - 2 * dir), CPoint(x, y + 2 * dir));
        context->drawLine(CPoint(x, y + 2 * dir), CPoint(x + 4, y - 2 * dir));
        setDirty(false);
        return;
    }
    // Beta 2 (the user's wish): one face for both states, no change of light; a switch that
    // is on is a little brighter and shows its text in the controls' light colour.
    const SkinImage& face = skin_->Image(SkinImageId::kButton);
    if(face) {
        DrawNinePart(context, face, area, 8);
    } else {
        FillRoundRect(context, area, 3.0, c.metal);
        StrokeRoundRect(context, area, 3.0, c.rule_dark);
    }
    if(down)
        FillRoundRect(context, CRect(area).inset(1.5, 1.5), 3.0, CColor(255, 255, 255, 14));
    // 0.2.2: the octave keys carry the light colour always. Disabled: dimmed.
    const CColor lit = down || value_ink_ ? c.value : c.button_text;
    const CColor ink = !getMouseEnabled() ? WithAlpha(value_ink_ ? c.value : c.dim, value_ink_ ? 0.32 : 0.6) : lit;
    const CFontRef font = skin_->Sans(12);
    if(icon_ == Icon::kNone) {
        DrawTracked(context, title_, area, kCenterText, 1.8, font, ink);
        setDirty(false);
        return;
    }
    if(icon_ == Icon::kKeyboard || icon_ == Icon::kFreeze || icon_ == Icon::kReset) {
        // Beta 2 (the user's wish): a symbol before the text, both centred together.
        const CCoord word = TrackedWidth(context, title_, 1.2, font);
        const CCoord symbol = icon_ == Icon::kKeyboard ? kKeyIconWidth : 14;
        const CCoord gap = icon_ == Icon::kKeyboard ? 5 : 6;
        const CCoord left = std::round(area.left + (area.getWidth() - word - symbol - gap) / 2);
        const CCoord mid = std::round(area.top + area.getHeight() / 2);
        context->setFrameColor(ink);
        context->setFillColor(ink);
        context->setLineStyle(CLineStyle(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
        if(icon_ == Icon::kKeyboard) {
            // Piano keys (round 2, the user's test; round 5: F to B): white keys of 3 px with
            // a gap of 1 px, in the ink; the black keys F#, G#, A#, 3 px wide over the gaps
            // and 8 of the 13 px high, dark. Whole pixels at 1x.
            const CCoord top = mid - 6;
            for(const KeyIconRect& key : KeyIconRects()) {
                context->setFillColor(key.black ? CColor(14, 15, 18, ink.alpha) : ink);
                context->drawRect(CRect(left + key.left, top + key.top, left + key.right, top + key.bottom), kDrawFilled);
            }
        } else if(icon_ == Icon::kFreeze) {
            // A snowflake: three bars, each end with two small branches.
            const CPoint c0(left + 7, mid);
            context->setLineWidth(1.2);
            for(int k = 0; k < 3; ++k) {
                const double a = kPi / 2 + k * kPi / 3;
                const CCoord ax = std::cos(a) * 6.5, ay = std::sin(a) * 6.5;
                context->drawLine(CPoint(c0.x - ax, c0.y - ay), CPoint(c0.x + ax, c0.y + ay));
                for(const double end : {-1.0, 1.0}) {
                    const CPoint tip(c0.x + end * ax * 0.6, c0.y + end * ay * 0.6);
                    for(const double turn : {-0.7, 0.7}) {
                        const double b = a + (end < 0 ? kPi : 0.0) + turn;
                        context->drawLine(tip, CPoint(tip.x + std::cos(b) * 2.4, tip.y + std::sin(b) * 2.4));
                    }
                }
            }
        } else {
            // Two circular arrows.
            const CPoint c0(left + 7, mid);
            context->setLineWidth(1.3);
            for(const double start : {200.0, 20.0}) {
                if(auto path = owned(context->createGraphicsPath())) {
                    path->addArc(CRect(c0.x - 5.5, c0.y - 5.5, c0.x + 5.5, c0.y + 5.5), start, start + 130.0, true);
                    context->drawGraphicsPath(path, CDrawContext::kPathStroked);
                }
                const double end = (start + 130.0) * kPi / 180.0;
                const CPoint tip(c0.x + std::cos(end) * 5.5, c0.y + std::sin(end) * 5.5);
                const double along = end + kPi / 2; // the arrow points along the circle
                for(const double side : {-0.55, 0.55}) {
                    const double b = along + kPi + side;
                    context->drawLine(tip, CPoint(tip.x + std::cos(b) * 3.2, tip.y + std::sin(b) * 3.2));
                }
            }
        }
        context->setLineStyle(kLineSolid);
        DrawTracked(context, title_, CRect(left + symbol + gap, area.top, left + symbol + gap + 2 + word, area.bottom),
                    kLeftText, 1.2, font, ink);
        setDirty(false);
        return;
    }
    // The symbols, 16 units square in the middle.
    const CPoint o(std::round(area.left + area.getWidth() / 2 - 8), std::round(area.top + area.getHeight() / 2 - 8));
    context->setFrameColor(ink);
    context->setFillColor(ink);
    context->setLineWidth(1.4);
    if(icon_ == Icon::kSize) {
        // A window with an arrow out of its corner.
        context->drawRect(CRect(o.x + 2, o.y + 4, o.x + 14, o.y + 13), kDrawStroked);
        context->drawLine(CPoint(o.x + 9, o.y + 4), CPoint(o.x + 9, o.y + 2));
        context->drawLine(CPoint(o.x + 9, o.y + 2), CPoint(o.x + 15, o.y + 2));
        context->drawLine(CPoint(o.x + 14, o.y + 2), CPoint(o.x + 14, o.y + 6));
        context->drawLine(CPoint(o.x + 14, o.y + 2), CPoint(o.x + 10, o.y + 6));
    } else {
        // Beta 2, the nebula menu: a small spiral of two arms.
        for(const double start : {0.0, kPi}) {
            auto path = owned(context->createGraphicsPath());
            if(!path)
                continue;
            for(int i = 0; i <= 24; ++i) {
                const double theta = i * 0.2;
                const double radius = 1.2 + 1.35 * theta;
                const CPoint p(o.x + 8 + std::cos(theta + start) * radius, o.y + 8 + std::sin(theta + start) * radius);
                if(i == 0)
                    path->beginSubpath(p);
                else
                    path->addLine(p);
            }
            context->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
    }
    setDirty(false);
}

void ToggleButton::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft() || !getMouseEnabled())
        return;
    if(style_ != Style::Action)
        on_ = !on_;
    event.consumed = true;
    // C2 (0.2.9): no button follows the mouse; without this the window would keep the
    // mouse capture after a dialog or menu and swallow the next click elsewhere.
    event.ignoreFollowUpMoveAndUpEvents(true);
    // MS-R37: what the button does (LOAD..., KEYS, LATCH, STOP, FREEZE, RESET, the
    // octave keys, the menus) runs inside the window's exception boundary. If it could
    // not be done, the button shows its old state again.
    const bool done = Guarded(false, [&] {
        if(toggled_)
            toggled_(on_);
        return true;
    });
    if(!done && style_ != Style::Action)
        on_ = !on_;
    Guarded([&] { invalid(); });
}

MetalKnob::MetalKnob(const CRect& size, IControlListener* listener, int32_t tag, bool small, bool bipolar)
: CKnob(size, listener, tag, nullptr, nullptr), small_(small), bipolar_(bipolar) {}

bool MetalKnob::hitTest(const CPoint& where, const Event& event) {
    if(!CKnob::hitTest(where, event))
        return false;
    const CRect box = getViewSize();
    return std::hypot(where.x - box.getCenter().x, where.y - box.getCenter().y) <= box.getWidth() / 2;
}

void MetalKnob::SetSkin(std::shared_ptr<const Skin> skin) {
    skin_ = std::move(skin);
    invalid();
}

void MetalKnob::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void MetalKnob::DrawUnchecked(CDrawContext* context) {
    setDirty(false);
    if(!skin_)
        return;
    const SkinColours& c = skin_->Colours();
    const CRect area = getViewSize();
    const CPoint centre = area.getCenter();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    context->setLineStyle(CLineStyle(CLineStyle::kLineCapRound));
    // Geometry for the delivered caps: 48 units across on 72 (large), 30 on 48 (small).
    const double outer = area.getWidth() / 2 - 1.5;
    const double cap = small_ ? 15.0 : 24.0;
    const double arc = small_ ? 17.6 : 28.0;
    const double first = -140.0, last = 140.0;
    const double level = std::clamp(static_cast<double>(getValueNormalized()), 0.0, 1.0);
    const double at = first + level * (last - first);

    // The scale: beta 2 only on the small knobs (the user's DAW test; on the large ones it
    // reached into the glass at BLEND).
    const int ticks = small_ ? 10 : -1;
    for(int i = 0; i <= ticks; ++i) {
        const double a = first + (last - first) * i / ticks;
        const bool major = i % 5 == 0;
        context->setFrameColor(c.tick);
        context->setLineWidth(major ? 1.1 : 0.7);
        context->drawLine(Polar(centre, a, outer), Polar(centre, a, outer - (major ? (small_ ? 3.0 : 4.0) : 2.2)));
    }
    // The value as an arc.
    const double from = bipolar_ ? 0.0 : first;
    const double low = std::min(from, at), high = std::max(from, at);
    if(high - low >= 0.5) {
        if(auto path = owned(context->createGraphicsPath())) {
            path->addArc(CRect(centre.x - arc, centre.y - arc, centre.x + arc, centre.y + arc), low - 90.0,
                         high - 90.0, true);
            context->setFrameColor(c.control);
            context->setLineWidth(small_ ? 1.8 : 2.4);
            context->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
    }
    // The cap and the pointer over it.
    const SkinImage& image = skin_->Image(small_ ? SkinImageId::kKnobSmall : SkinImageId::kKnobLarge);
    if(image) {
        DrawSkinImage(context, image, area);
    } else {
        context->setFillColor(c.dim);
        context->drawEllipse(CRect(centre.x - cap, centre.y - cap, centre.x + cap, centre.y + cap), kDrawFilled);
    }
    context->setFrameColor(c.pointer);
    context->setLineWidth(small_ ? 1.8 : 2.6);
    context->drawLine(Polar(centre, at, cap * 0.2), Polar(centre, at, cap - 2.5));
    context->setLineStyle(kLineSolid);
}

ResizeGrip::ResizeGrip(const CRect& size, CColor colour, std::function<void()> begin,
                       std::function<void(double, double)> drag)
: CView(size), colour_(colour), begin_(std::move(begin)), drag_(std::move(drag)) {
    setTooltipText("Drag to resize the window");
}

void ResizeGrip::SetColour(CColor colour) {
    colour_ = colour;
    invalid();
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

StartBar::StartBar(const CRect& size, double track_left, double track_right)
: CView(size), track_left_(track_left), track_right_(track_right) {
    setMouseEnabled(true);
}

void StartBar::SetSkin(std::shared_ptr<const Skin> skin) {
    skin_ = std::move(skin);
    invalid();
}

void StartBar::SetStart(double start) noexcept {
    start = std::clamp(start, 0.0, 1.0);
    if(start == start_)
        return;
    start_ = start;
    Guarded([&] { invalid(); });
}

void StartBar::SetSize(double size) noexcept {
    size = std::isfinite(size) ? std::clamp(size, 0.0, 1.0) : 1.0;
    if(size == size_)
        return;
    size_ = size;
    Guarded([&] { invalid(); });
}

void StartBar::SetFile(std::uint32_t frames, double rate) noexcept {
    if(frames == frames_ && rate == rate_)
        return;
    frames_ = frames;
    rate_ = std::isfinite(rate) && rate > 0 ? rate : 0.0;
    Guarded([&] { invalid(); });
}

double StartBar::TrackLeft() const {
    return track_left_;
}

double StartBar::TrackRight() const {
    return std::max(TrackLeft() + 1, std::min(track_right_, getViewSize().getWidth() - layout::kThumbRoom));
}

double StartBar::FrameX(double frame) const {
    return frames_ > 0 ? TrackLeft() + (TrackRight() - TrackLeft()) * frame / frames_ : TrackLeft();
}

double StartBar::StartX() const {
    // MS-R56 (0.2.5): with a file, START's place as the core clamps the loop's start, to
    // the last frame; so the thumb stays where the loop begins, also at 100 % of a short file.
    if(frames_ > 0)
        return FrameX(std::min(start_ * frames_, frames_ - 1.0));
    return TrackLeft() + (TrackRight() - TrackLeft()) * start_;
}

double StartBar::LoopFrames() const {
    // As the core sets the loop: SIZE squared of the rest after START, at least a frame.
    const double start = std::min(start_ * frames_, frames_ - 1.0);
    return std::max(1.0, size_ * size_ * (frames_ - start));
}

double StartBar::EndX() const {
    if(frames_ > 0)
        return FrameX(std::min<double>(frames_, std::min(start_ * frames_, frames_ - 1.0) + LoopFrames()));
    return TrackLeft() + (TrackRight() - TrackLeft()) * (start_ + size_ * size_ * (1.0 - start_));
}

double StartBar::SizeAt(double x) const {
    const double share = Share(x);
    const double start = frames_ > 0 ? std::min(start_ * frames_, frames_ - 1.0) / frames_ : start_;
    if(!(start < 1.0))
        return size_;
    return std::sqrt(std::clamp((share - start) / (1.0 - start), 0.0, 1.0));
}

double StartBar::Share(double x) const {
    return std::clamp((x - TrackLeft()) / (TrackRight() - TrackLeft()), 0.0, 1.0);
}

bool StartBar::PressAt(double x) {
    if(frames_ == 0 || !edits_.start || x < TrackLeft() - 10 || x > TrackRight() + 10)
        return false;
    // The end thumb when the press is on it and nearer to it than to START's (at the same
    // place: right of it); anywhere else START, as before.
    const double to_end = std::abs(x - EndX()), to_start = std::abs(x - StartX());
    const bool end = edits_.size && to_end <= 12.0 && (to_end < to_start || (to_end == to_start && x > StartX()));
    try {
        dragging_ = true;
        dragging_end_ = end;
        if(end) {
            if(edits_.size_begin)
                edits_.size_begin();
            sent_ = SizeAt(x);
            edits_.size(sent_);
        } else {
            if(edits_.begin)
                edits_.begin();
            sent_ = Share(x);
            edits_.start(sent_);
        }
    } catch(...) {
    }
    return true;
}

void StartBar::DragTo(double x) {
    if(!dragging_)
        return;
    try {
        // B2B.6: a value the host already has is not sent again.
        const double value = dragging_end_ ? SizeAt(x) : Share(x);
        if(value == sent_)
            return;
        sent_ = value;
        if(dragging_end_)
            edits_.size(value);
        else
            edits_.start(value);
    } catch(...) {
    }
}

void StartBar::Release() {
    if(!dragging_)
        return;
    dragging_ = false;
    const bool end = dragging_end_;
    dragging_end_ = false;
    Guarded([&] { invalid(); });
    try {
        if(end && edits_.size_end)
            edits_.size_end();
        else if(!end && edits_.end)
            edits_.end();
    } catch(...) {
    }
}

void StartBar::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void StartBar::DrawUnchecked(CDrawContext* context) {
    setDirty(false);
    if(!skin_)
        return;
    const SkinColours& c = skin_->Colours();
    const CRect all = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CCoord mid = std::round(all.top + all.getHeight() / 2);
    const CCoord left = all.left + TrackLeft(), right = all.left + TrackRight();
    // The name.
    const CFontRef caps = skin_->Sans(12.5);
    DrawTracked(context, "START", CRect(all.left, all.top, left - 8, all.bottom), kLeftText, 1.4, caps, c.text, &c);
    // Round 5: and the loop's end thumb's.
    DrawTracked(context, "END", CRect(right + 8, all.top, all.right, all.bottom), kRightText, 1.4, caps, c.text, &c);
    // The groove, filled up to the thumb in START's colour (beta 2: HUE's), with a scale.
    context->setFillColor(c.groove);
    context->drawRect(CRect(left, mid - 2, right, mid + 2), kDrawFilled);
    context->setFrameColor(c.rule_light);
    context->setLineWidth(1);
    context->drawLine(CPoint(left, mid + 2.5), CPoint(right, mid + 2.5));
    const CCoord thumb_x = all.left + StartX();
    const CCoord end_x = all.left + EndX();
    // Round 2: the loop between the two thumbs filled in START's colour, the file before it
    // faintly.
    context->setFillColor(WithAlpha(c.start_fill, 0.35));
    context->drawRect(CRect(left, mid - 1.5, thumb_x, mid + 1.5), kDrawFilled);
    context->setFillColor(WithAlpha(c.start_fill, 0.95));
    context->drawRect(CRect(thumb_x, mid - 2, std::max(end_x, thumb_x + 1), mid + 2), kDrawFilled);
    context->setFrameColor(WithAlpha(c.text, 0.45));
    for(int q = 0; q <= 40; ++q) {
        const CCoord x = std::round(left + q / 40.0 * (right - left - 1)) + 0.5;
        context->drawLine(CPoint(x, mid + (q % 5 == 0 ? 7 : 9)), CPoint(x, mid + 12));
    }
    const SkinImage& thumb = skin_->Image(SkinImageId::kThumb);
    // Beta 2: the round grip of 24 x 24, or beta 1's of 16 x 28 until it is delivered.
    const CPoint grip = thumb ? thumb.size : CPoint(16, 28);
    // The end thumb first, so that START's lies on top where they meet.
    for(const CCoord x : {end_x, thumb_x}) {
        const CRect knob(x - grip.x / 2, mid - grip.y / 2, x + grip.x / 2, mid + grip.y / 2);
        if(thumb) {
            DrawSkinImage(context, thumb, knob);
        } else {
            context->setFillColor(c.dim);
            context->drawRect(knob, kDrawFilled);
        }
    }
    // Round 4 (the user's wish): no value box; the host shows START's value as text.
}

void StartBar::onMouseDownEvent(MouseDownEvent& event) {
    Guarded([&] {
        if(event.buttonState.isLeft() && PressAt(event.mousePosition.x - getViewSize().left))
            event.consumed = true;
    });
}

void StartBar::onMouseMoveEvent(MouseMoveEvent& event) {
    Guarded([&] {
        if(!dragging_)
            return;
        DragTo(event.mousePosition.x - getViewSize().left);
        event.consumed = true;
    });
}

void StartBar::onMouseUpEvent(MouseUpEvent& event) {
    Guarded([&] {
        if(!dragging_)
            return;
        Release();
        event.consumed = true;
    });
}

void StartBar::onMouseCancelEvent(MouseCancelEvent& event) {
    Guarded([&] { Release(); });
    event.consumed = true;
}

ViewSwitch::ViewSwitch(const CRect& size, std::function<void(bool)> chosen)
: CView(size), chosen_(std::move(chosen)) {
    setMouseEnabled(true);
}

void ViewSwitch::SetGalaxy(bool galaxy) {
    if(galaxy == galaxy_)
        return;
    galaxy_ = galaxy;
    invalid();
}

void ViewSwitch::SetSkin(std::shared_ptr<const Skin> skin) {
    skin_ = std::move(skin);
    invalid();
}

int ViewSwitch::SideAt(double x, double y) const {
    // Relative to the BLEND knob's centre, the view's middle: the half-arcs (with a margin
    // either side of the ring) and the names beside them.
    const double dx = x - getViewSize().getWidth() / 2, dy = y - getViewSize().getHeight() / 2;
    const double r = std::hypot(dx, dy);
    double angle = std::atan2(dx, -dy) * 180.0 / kPi; // clockwise from twelve o'clock
    if(angle < 0)
        angle += 360.0;
    constexpr double slack = 8; // degrees and units
    const bool on_ring = std::abs(r - layout::kArcRadius) <= layout::kArcWidth / 2 + slack;
    const double name_from = layout::kArcRadius + layout::kArcWidth / 2;
    const bool on_name = std::abs(dy) <= layout::kArcKeyHeight / 2 + 1 && std::abs(dx) >= name_from; // its key
    if((on_ring && angle >= layout::kGalaxyArcFrom - slack && angle <= layout::kGalaxyArcTo + slack) || (on_name && dx < 0))
        return -1;
    if((on_ring && angle >= layout::kWaveArcFrom - slack && angle <= layout::kWaveArcTo + slack) || (on_name && dx > 0))
        return 1;
    return 0;
}

bool ViewSwitch::PressAt(double x, double y) {
    const int side = SideAt(x, y);
    if(side == 0)
        return false;
    const bool galaxy = side < 0;
    if(galaxy == galaxy_)
        return false;
    galaxy_ = galaxy;
    if(chosen_)
        chosen_(galaxy);
    return true;
}

bool ViewSwitch::hitTest(const CPoint& where, const Event& event) {
    if(!CView::hitTest(where, event))
        return false;
    const CRect all = getViewSize();
    return SideAt(where.x - all.left, where.y - all.top) != 0; // the knob in the middle keeps its own
}

void ViewSwitch::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void ViewSwitch::DrawUnchecked(CDrawContext* context) {
    setDirty(false);
    if(!skin_)
        return;
    const SkinColours& c = skin_->Colours();
    const CRect all = getViewSize();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CPoint centre(all.left + all.getWidth() / 2, all.top + all.getHeight() / 2);
    const double r = layout::kArcRadius;
    const CRect ring(centre.x - r, centre.y - r, centre.x + r, centre.y + r);
    // VSTGUI's arcs count from three o'clock, clockwise.
    const auto arc = [&](double from, double to, bool lit) {
        auto path = owned(context->createGraphicsPath());
        if(!path)
            return;
        path->addArc(ring, from - 90.0, to - 90.0, true);
        context->setLineStyle(CLineStyle(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
        context->setLineWidth(lit ? layout::kArcWidth : 2.0);
        context->setFrameColor(lit ? WithAlpha(c.control, 0.9) : WithAlpha(c.dim, 0.45));
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
        context->setLineStyle(kLineSolid);
    };
    arc(layout::kGalaxyArcFrom, layout::kGalaxyArcTo, galaxy_);
    arc(layout::kWaveArcFrom, layout::kWaveArcTo, !galaxy_);
    // Round 8 (the user's wish): each name on a flat key let into the chassis, rounded at its
    // outer end and hollowed along the arc at its inner one; discreet, not a button like the
    // others. The chosen one is a little lighter in the accent colour.
    const CFontRef font = skin_->Sans(11);
    const double inner = r + layout::kArcWidth / 2 + layout::kArcKeyGap;
    const double half = layout::kArcKeyHeight / 2;
    const double outer = all.getWidth() / 2 - 1; // the key's far end, from the centre
    const auto key = [&](double side, CCoord down) -> SharedPointer<CGraphicsPath> {
        auto path = owned(context->createGraphicsPath());
        if(!path)
            return nullptr;
        constexpr int kSteps = 12;
        // Along the hollow inner end from its top to its bottom, then round the outer end.
        const double reach = std::asin(half / inner);
        for(int k = 0; k <= kSteps; ++k) {
            const double a = -reach + 2 * reach * k / kSteps;
            const CPoint p(centre.x + side * inner * std::cos(a), centre.y + inner * std::sin(a) + down);
            if(k == 0)
                path->beginSubpath(p);
            else
                path->addLine(p);
        }
        const double cx = outer - half; // the outer end's centre
        for(int k = 0; k <= kSteps; ++k) {
            const double a = kPi / 2 - kPi * k / kSteps; // from the bottom round to the top
            path->addLine(CPoint(centre.x + side * (cx + half * std::cos(a)), centre.y + half * std::sin(a) + down));
        }
        path->closeSubpath();
        return path;
    };
    const auto draw_key = [&](double side, bool chosen) {
        // Let in: a light edge shows below, a dark one above; the bed darker than the chassis.
        context->setLineWidth(1);
        if(auto below = key(side, 1)) {
            context->setFrameColor(c.rule_light);
            context->drawGraphicsPath(below, CDrawContext::kPathStroked);
        }
        if(auto bed = key(side, 0)) {
            context->setFillColor(CColor(0, 0, 0, chosen ? 46 : 62));
            context->drawGraphicsPath(bed, CDrawContext::kPathFilled);
            if(chosen) {
                context->setFillColor(WithAlpha(c.control, 0.13));
                context->drawGraphicsPath(bed, CDrawContext::kPathFilled);
            }
            context->setFrameColor(c.rule_dark);
            context->drawGraphicsPath(bed, CDrawContext::kPathStroked);
        }
    };
    draw_key(-1, galaxy_);
    draw_key(1, !galaxy_);
    // The names in the middle of their keys' straight part.
    const double text_from = inner + 2, text_to = outer - half * 0.55;
    DrawTracked(context, "GALAXY", CRect(centre.x - text_to, centre.y - 8, centre.x - text_from, centre.y + 8),
                kCenterText, 1.5, font, galaxy_ ? c.value : c.dim);
    DrawTracked(context, "WAVE", CRect(centre.x + text_from, centre.y - 8, centre.x + text_to, centre.y + 8), kCenterText,
                1.5, font, galaxy_ ? c.dim : c.value);
}

void ViewSwitch::onMouseDownEvent(MouseDownEvent& event) {
    if(!event.buttonState.isLeft())
        return;
    const CRect all = getViewSize();
    if(SideAt(event.mousePosition.x - all.left, event.mousePosition.y - all.top) == 0)
        return;
    event.consumed = true;
    event.ignoreFollowUpMoveAndUpEvents(true);
    // MS-R37: what the switch does runs inside the window's exception boundary.
    Guarded([&] {
        if(PressAt(event.mousePosition.x - all.left, event.mousePosition.y - all.top))
            invalid();
    });
}

} // namespace messier
