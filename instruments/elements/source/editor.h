#pragma once

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>

namespace VSTGUI {
class CKnob;
class CTextLabel;
} // namespace VSTGUI

namespace elements_synth {

class KeyboardView;
class ToggleButton;

// Window size at zoom 1 on a display without scaling: title, two rows of
// knobs and the status line, and below them the keyboard when it is shown.
// The window's sizing and scaling are those of Torus Classic 0.2.0.
constexpr double kEditorWidth = 724;
constexpr double kEditorTopHeight = 344;
constexpr double kKeyboardSectionHeight = 86;
// Limits of the window zoom the user can choose by resizing.
constexpr double kMinEditorZoom = 0.5;
constexpr double kMaxEditorZoom = 2.0;

inline double ClampEditorZoom(double zoom) {
    return zoom >= kMinEditorZoom ? (zoom <= kMaxEditorZoom ? zoom : kMaxEditorZoom)
                                  : kMinEditorZoom; // NaN becomes the minimum
}

inline double EditorBaseHeight(bool keyboard) {
    return kEditorTopHeight + (keyboard ? kKeyboardSectionHeight : 0.0);
}

struct EditorSize {
    int width;
    int height;
};

// Window size in pixels. `total` is the user's zoom times the display scale.
inline EditorSize EditorSizeFor(double total, bool keyboard) {
    return {static_cast<int>(std::lround(kEditorWidth * total)),
            static_cast<int>(std::lround(EditorBaseHeight(keyboard) * total))};
}

// The zoom a window of this size stands for: the largest that fits into it.
inline double EditorZoomForSize(int width, int height, double scale, bool keyboard) {
    return ClampEditorZoom(
        std::min(width / kEditorWidth, height / EditorBaseHeight(keyboard)) / scale);
}

// The size the host may use when the user drags the window from `current`
// towards `asked`. Keeps the aspect ratio and the zoom limits.
//
// An edge drag changes one dimension; the other follows it, so the edge stays
// under the mouse. A corner drag (`corner`) takes the allowed size nearest to
// the asked one, which depends on the mouse position alone.
inline EditorSize ConstrainEditorSize(EditorSize asked, EditorSize current, double scale,
                                      bool keyboard, bool corner) {
    const double width = kEditorWidth;
    const double height = EditorBaseHeight(keyboard);
    double total = asked.width / width;
    if(corner)
        total = (asked.width * width + asked.height * height) / (width * width + height * height);
    else if(asked.width == current.width && asked.height != current.height)
        total = asked.height / height;
    return EditorSizeFor(scale * ClampEditorZoom(total / scale), keyboard);
}

// Tells edge drags from corner drags across the requests of one drag: once a
// drag has moved both dimensions, it stays a corner drag until it ends.
class DragConstraint {
  public:
    using Clock = std::chrono::steady_clock;
    // A pause this long between two requests starts a new drag.
    static constexpr auto kNewDragPause = std::chrono::milliseconds(250);

    EditorSize Apply(EditorSize asked, EditorSize current, double scale, bool keyboard,
                     Clock::time_point now) {
        if(now - last_ > kNewDragPause)
            corner_ = false;
        last_ = now;
        if(asked.width != current.width && asked.height != current.height)
            corner_ = true;
        return ConstrainEditorSize(asked, current, scale, keyboard, corner_);
    }

  private:
    bool corner_ = false;
    Clock::time_point last_{};
};

// What the window remembers between sessions; saved with the project.
struct EditorSettings {
    double zoom = 1.0;
    bool keyboard = true;
};

// What the processor reported about the rates it runs at.
struct EngineStatus {
    bool known = false;
    double host_rate = 0.0;
    double engine_rate = 0.0;
    int latency = 0; // host samples
};

// The status line for it.
std::string StatusText(const EngineStatus& status);

// How the window talks back to the controller.
struct EditorCallbacks {
    // The settings changed. Called at once, also for every step of a drag.
    std::function<void(const EditorSettings&)> changed;
    // The user has finished changing the settings: time to tell the host the
    // project needs saving. Called once per change, not per step of a drag.
    std::function<void()> settled;
    // A key on the screen keyboard was pressed, or let go.
    std::function<void(int note)> strike;
    std::function<void(int note)> release;
};

// The plug-in window: nine knobs for the exciters, nine for resonator, space
// and playing, a small switch that freezes the reverb beside the Space knob,
// a status line, and an on-screen keyboard that a button shows and hides.
// The layout is fixed and drawn as vectors, so
// the window scales as a whole: by the zoom the user picks when resizing it,
// times the host's display scale factor. The aspect ratio stays the same.
//
// The window never changes its own size. It asks the host (Resize()) and
// changes in onSize(), once the host has agreed.
class Editor : public Steinberg::Vst::VSTGUIEditor,
               public VSTGUI::IControlListener,
               public Steinberg::IPlugViewContentScaleSupport {
  public:
    static constexpr std::size_t kKnobCount = 18;
    static constexpr std::size_t kKnobsPerRow = 9;

    Editor(Steinberg::Vst::EditController* controller, const EditorSettings& settings,
           const EngineStatus& status, EditorCallbacks callbacks);
    ~Editor() override;

    // Shows or hides the keyboard, as the KEYS button does. If the host
    // refuses the new window size, nothing changes.
    void SetKeyboardVisible(bool show);
    bool KeyboardVisible() const { return settings_.keyboard; }
    const EditorSettings& Settings() const { return settings_; }
    // Freezes the reverb, as a click on FREEZE does: an edit the host records.
    void SetFreeze(bool freeze);
    // Called by the controller when the processor reports its rates.
    void ShowStatus(const EngineStatus& status);
    // What the window currently shows; -1 while it is closed. For tests.
    float ShownKnobValue(Steinberg::Vst::ParamID id) const;
    // The knob of a parameter while the window is open, else nullptr. For tests.
    VSTGUI::CKnob* KnobOf(Steinberg::Vst::ParamID id) const;

    // Called by the controller whenever a parameter changes, whatever the
    // source: this window, host automation or a MIDI controller.
    void ParameterChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized);
    // Called by the controller when a loaded project brings its own settings.
    void ApplySettings(const EditorSettings& settings);

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close() SMTG_OVERRIDE;
    // A knob moved. The begin and the end of a knob edit reach the host
    // through the frame (VSTGUIEditor::beginEdit/endEdit); the listener's
    // controlBeginEdit/controlEndEdit are left alone, or the host would hear
    // of every edit twice.
    void valueChanged(VSTGUI::CControl* control) SMTG_OVERRIDE;
    VSTGUI::CMessageResult notify(VSTGUI::CBaseObject* sender, const char* message) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* size) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) SMTG_OVERRIDE;

    DELEGATE_REFCOUNT(VSTGUIEditor)
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                                 void** obj) SMTG_OVERRIDE;

  private:
    struct Knob {
        Steinberg::Vst::ParamID id = 0;
        VSTGUI::CKnob* knob = nullptr; // owned by the frame
        VSTGUI::CTextLabel* value = nullptr;
    };
    void ShowValue(Knob& knob, Steinberg::Vst::ParamValue normalized);
    // Forgets the views of a closed window.
    void ForgetViews();
    // Window size in pixels for the current zoom, display scale and keyboard.
    Steinberg::ViewRect ScaledSize() const;
    // The zoom the content is drawn at in a window of this size.
    double DisplayZoom(Steinberg::int32 width, Steinberg::int32 height) const;
    // Fills a frame of the given size with the content.
    void LayOut(Steinberg::int32 width, Steinberg::int32 height);
    // Asks the host for the size ScaledSize() says. True if the window has it
    // or the host agreed; then onSize() does the rest.
    bool Resize();
    // Records a change by the user; `settled` follows once it has stopped.
    void UserChanged();

    std::array<Knob, kKnobCount> knobs_{};
    KeyboardView* keyboard_view_ = nullptr; // owned by the frame, like the next two
    ToggleButton* keyboard_button_ = nullptr;
    ToggleButton* freeze_button_ = nullptr;
    VSTGUI::CTextLabel* status_label_ = nullptr;
    EngineStatus status_;
    double scale_ = 1.0; // the host's display scale factor
    EditorSettings settings_;
    EditorCallbacks callbacks_;
    double applied_zoom_ = 0.0; // the total zoom the frame was last set to
    DragConstraint drag_;
    bool settle_pending_ = false;
    std::chrono::steady_clock::time_point last_change_{};
};

} // namespace elements_synth
