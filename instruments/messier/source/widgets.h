// The window's own controls, drawn as vectors: the screen keyboard and the
// switches. Copied from Kekule (Torus Classic) 0.3.1, source/widgets.*, without
// its tuning lock and selectors; only the namespace is changed.
#pragma once

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <functional>

namespace messier {

// The on-screen keyboard. A click strikes a key; dragging across the keys
// strikes each new one, like a glissando. Letting go of a key is reported
// too: in Messier it ends the note (the loop returns to its base pitch and
// level once no key is held), and the note the host records needs its end.
class KeyboardView : public VSTGUI::CView {
  public:
    KeyboardView(const VSTGUI::CRect& size, std::function<void(int note)> strike,
                 std::function<void(int note)> release);

    // Lets go of the key that is held, if any: before the view goes away.
    void ReleaseHeld();
    // Moves the keys by whole octaves (see ClampKeyboardTranspose). A key
    // held at that moment is let go first, at its old pitch.
    void SetTranspose(int semitones);

    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) override;

  private:
    int NoteAtPoint(const VSTGUI::CPoint& where) const;
    void Press(int note);
    void Release();

    std::function<void(int)> strike_, release_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> label_font_;
    bool dragging_ = false;
    int pressed_ = -1; // the note of the held key, transposed; -1 for none
    int transpose_ = 0;
};

// A small switch drawn as a rounded label: filled when on, outlined when off.
// As an action (the octave keys) it stays outlined and only calls back; its
// text dims while it is disabled.
class ToggleButton : public VSTGUI::CView {
  public:
    enum class Style { Toggle, Action };
    ToggleButton(const VSTGUI::CRect& size, const char* title, bool on,
                 std::function<void(bool on)> toggled, Style style = Style::Toggle);

    void SetOn(bool on);
    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

  private:
    const char* title_;
    bool on_;
    Style style_;
    std::function<void(bool)> toggled_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
};

} // namespace messier
