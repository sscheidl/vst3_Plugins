// The window's own controls: the screen keyboard, the switches, the knobs, the WINDOW
// symbol and the resize grip. The keyboard's and the switches' behaviour come from
// Kekule (Torus Classic) 0.3.1, source/widgets.*; since 0.2.0 they are drawn with the
// bitmaps and colours of the GUI preset (gui_skin.h), and without a skin as plain
// vectors.
#pragma once

#include "gui_skin.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/cview.h"

#include <functional>
#include <memory>

namespace messier {

// The on-screen keyboard. A click strikes a key; dragging across the keys
// strikes each new one, like a glissando. Letting go of a key is reported
// too: in Messier it ends the note (the loop returns to its base pitch and
// level once no key is held), and the note the host records needs its end.
class KeyboardView : public VSTGUI::CView {
  public:
    KeyboardView(const VSTGUI::CRect& size, std::function<void(int note)> strike,
                 std::function<void(int note)> release);

    void SetSkin(std::shared_ptr<const Skin> skin);
    // Lets go of the key that is held, if any: before the view goes away.
    void ReleaseHeld();
    // Latch (since 0.1.8, the user's wish): a struck key stays held after the mouse
    // is let go, so the pitch stays changed. Clicking another key moves the hold
    // there, clicking the held key again lets it go, and so does switching latch off.
    void SetLatch(bool on);
    bool Latch() const { return latch_; }
    int Held() const { return pressed_; } // the held note, transposed; -1 for none
    // What the mouse events do, by note (transposed, -1 off the keys); public for tests,
    // as VSTGUI's events need its platform.
    void MouseDownOn(int note);
    void MouseMoveTo(int note);
    void MouseUp();
    // Moves the keys by whole octaves (see ClampKeyboardTranspose). A key
    // held at that moment is let go first, at its old pitch.
    void SetTranspose(int semitones);

    void draw(VSTGUI::CDrawContext* context) override;
    void DrawUnchecked(VSTGUI::CDrawContext* context);
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) override;

  private:
    int NoteAtPoint(const VSTGUI::CPoint& where) const;
    void Press(int note);
    void Release();

    std::function<void(int)> strike_, release_;
    std::shared_ptr<const Skin> skin_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> label_font_;
    bool dragging_ = false;
    bool latch_ = false;
    int pressed_ = -1; // the note of the held key, transposed; -1 for none
    int transpose_ = 0;
};

// The WINDOW knob's value as a symbol (since 0.1.8): the grain window the core
// mixes from its seven shapes for this pot position, drawn as a small curve. The
// shapes are the core's own (BuildWindows, WinMix).
class WindowShapeView : public VSTGUI::CView {
  public:
    explicit WindowShapeView(const VSTGUI::CRect& size) : CView(size) { setMouseEnabled(false); }
    void SetValue(float pot);
    void SetColour(VSTGUI::CColor colour);
    void draw(VSTGUI::CDrawContext* context) override;
    void DrawUnchecked(VSTGUI::CDrawContext* context);
    // For tests: the curve's height (0..1) at a share of its length.
    static float Sample(float pot, float share);

  private:
    float pot_ = 0.0f;
    VSTGUI::CColor colour_{232, 232, 236};
};

// A key of the window: a switch (FREEZE, STOP, KEYS, LATCH) shows its state, pressed
// in (the preset's pressed face) and with its text in the controls' light colour while
// on; an action (LOAD..., RESET, the octave keys, the size and preset menus) only calls
// back.
// With a skin drawn from the preset's button bitmaps (nine parts), without one as a
// rounded outline. Its text dims while it is disabled.
class ToggleButton : public VSTGUI::CView {
  public:
    enum class Style { Toggle, Action };
    // Instead of text, a symbol (0.2.0): the window size and the GUI preset menus.
    enum class Icon { kNone, kSize, kPreset };
    ToggleButton(const VSTGUI::CRect& size, const char* title, bool on,
                 std::function<void(bool on)> toggled, Style style = Style::Toggle);

    void SetOn(bool on);
    bool On() const { return on_; }
    void SetSkin(std::shared_ptr<const Skin> skin);
    void SetIcon(Icon icon);
    // The text always in the controls' light colour (0.2.2: the octave keys); dimmed
    // while disabled.
    void SetValueInk(bool on);
    void draw(VSTGUI::CDrawContext* context) override;
    void DrawUnchecked(VSTGUI::CDrawContext* context);
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

  private:
    const char* title_;
    bool on_;
    Style style_;
    Icon icon_ = Icon::kNone;
    bool value_ink_ = false;
    std::function<void(bool)> toggled_;
    std::shared_ptr<const Skin> skin_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
};

// A knob of the window (0.2.0): a scale of ticks, the value as an arc in the accent
// colour (from the centre for a bipolar parameter), the preset's turned-metal cap and a
// pointer drawn over it. Mouse, wheel and double-click (default value) are CKnob's.
class MetalKnob : public VSTGUI::CKnob {
  public:
    MetalKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag, bool small,
              bool bipolar);
    void SetSkin(std::shared_ptr<const Skin> skin);
    void draw(VSTGUI::CDrawContext* context) override;
    void DrawUnchecked(VSTGUI::CDrawContext* context);

  private:
    bool small_, bipolar_;
    std::shared_ptr<const Skin> skin_;
};

// The resize grip in the window's lower right corner (0.1.11, the user's wish): a small
// hatched square that is easier to hit than the window's edge. Dragging it reports how
// far the mouse has moved since the press, in the window's pixels (frame units times
// the frame's zoom, which the resize itself changes). The view sits directly in the
// frame.
class ResizeGrip : public VSTGUI::CView {
  public:
    ResizeGrip(const VSTGUI::CRect& size, VSTGUI::CColor colour, std::function<void()> begin,
               std::function<void(double dx, double dy)> drag);

    void SetColour(VSTGUI::CColor colour);
    // What the mouse events do, in window pixels; public for tests, as VSTGUI's events
    // need its platform.
    void Press(double x, double y);
    void DragTo(double x, double y);
    void Let();
    bool Dragging() const { return dragging_; }

    void draw(VSTGUI::CDrawContext* context) override;
    void DrawUnchecked(VSTGUI::CDrawContext* context);
    void onMouseEnterEvent(VSTGUI::MouseEnterEvent& event) override;
    void onMouseExitEvent(VSTGUI::MouseExitEvent& event) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) override;

  private:
    double Zoom() const;
    VSTGUI::CColor colour_;
    std::function<void()> begin_;
    std::function<void(double, double)> drag_;
    bool dragging_ = false;
    double start_x_ = 0.0, start_y_ = 0.0;
};

} // namespace messier
