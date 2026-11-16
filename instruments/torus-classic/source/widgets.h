// The window's own controls, drawn as vectors: the screen keyboard, the KEYS
// switch, the tuning lock, the model selector and the choice of the number
// of voices. Keyboard and switch follow Toy Piano Classic.
#pragma once

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <functional>

namespace torus_classic {

// The on-screen keyboard. A click strikes a key; dragging across the keys
// strikes each new one, like a glissando. Letting go of a key is reported
// too: the sound does not stop (a struck resonator rings until it has
// decayed), but the note the host records needs its end.
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

// The tuning lock beside the Tune knob: a small padlock in a circle, without
// text. Closed, Tune moves in semitones; open, it is free. A click opens or
// closes it. What it means is also its tooltip.
class LockButton : public VSTGUI::CView {
  public:
    LockButton(const VSTGUI::CRect& size, bool locked, std::function<void(bool locked)> toggled);

    // Shows the lock closed or open, without calling back.
    void SetLocked(bool locked);
    bool Locked() const { return locked_; }
    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

  private:
    void UpdateTooltip();

    bool locked_;
    std::function<void(bool)> toggled_;
};

// A few labelled cells in a row, one of them lit: the number of voices.
class ChoiceSelector : public VSTGUI::CView {
  public:
    // `labels` must outlive the view.
    ChoiceSelector(const VSTGUI::CRect& size, const char* const* labels, int count, int choice,
                   std::function<void(int choice)> chosen);

    // Shows another choice as selected, without calling back.
    void SetChoice(int choice);
    int Choice() const { return choice_; }
    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

  private:
    const char* const* labels_;
    int count_, choice_;
    std::function<void(int)> chosen_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
};

// Six labelled buttons, one of them lit: the synthesis model.
class ModelSelector : public VSTGUI::CView {
  public:
    ModelSelector(const VSTGUI::CRect& size, int model, std::function<void(int model)> chosen);

    // Shows another model as selected, without calling back.
    void SetModel(int model);
    int Model() const { return model_; }
    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

  private:
    int model_;
    std::function<void(int)> chosen_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
};

} // namespace torus_classic
