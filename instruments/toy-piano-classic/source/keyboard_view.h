#pragma once

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cview.h"

#include <functional>

namespace taureon::toy_piano_classic {

// The on-screen keyboard. A click strikes a key; dragging across the keys
// strikes each new one, like a glissando. There is no note-off: the engine
// plays every strike to its end, as in the firmware.
class KeyboardView : public VSTGUI::CView {
  public:
    KeyboardView(const VSTGUI::CRect& size, std::function<void(int note)> strike);

    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) override;

  private:
    int NoteAtPoint(const VSTGUI::CPoint& where) const;
    void Press(int note);
    void Release();

    std::function<void(int)> strike_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> label_font_;
    bool dragging_ = false;
    int pressed_ = -1; // the key under the held mouse button, -1 for none
};

// A small switch drawn as a rounded label: filled when on, outlined when off.
class ToggleButton : public VSTGUI::CView {
  public:
    ToggleButton(const VSTGUI::CRect& size, const char* title, bool on,
                 std::function<void(bool on)> toggled);

    void SetOn(bool on);
    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

  private:
    const char* title_;
    bool on_;
    std::function<void(bool)> toggled_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
};

} // namespace taureon::toy_piano_classic
