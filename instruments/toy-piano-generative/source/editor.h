#pragma once

#include "keyboard_layout.h"

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

namespace taureon::toy_piano_generative {

class KeyboardView;
class LockButton;
class ToggleButton;

// Window size at zoom 1 on a display without scaling: the two rows of knobs,
// and below them the keyboard when it is shown.
constexpr double kEditorWidth = 640;
constexpr double kEditorTopHeight = 316;
constexpr double kKeyboardSectionHeight = 82;
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

// Tells edge drags from corner drags across the requests of one drag.
//
// An edge drag never moves both dimensions; a corner drag does, though not in
// every request: the mouse can meet the old height by chance. So once a drag
// has moved both, it stays a corner drag until it ends. Up to 0.2.0 the rule
// was decided anew for every request, and corner drags jumped.
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
    // Which two octaves the keyboard shows: semitones above C4, whole octaves.
    int keyboard_shift = kKeyboardDefaultShift;
};

// How the window talks back to the controller.
struct EditorCallbacks {
    // The settings changed. Called at once, also for every step of a drag.
    std::function<void(const EditorSettings&)> changed;
    // The user has finished changing the settings: time to tell the host the
    // project needs saving. Called once per change, not per step of a drag.
    std::function<void()> settled;
    // A key on the screen keyboard was struck.
    std::function<void(int note)> strike;
    // An octave button was pressed: the sequence moves by this many octaves.
    std::function<void(int octaves)> octave;
};

// The plug-in window: a row of knobs for the sound, a row for the sequencer,
// and an on-screen keyboard that a button shows and hides. The layout is fixed
// and drawn as vectors, so the window scales as a whole: by the zoom the user
// picks when resizing it, times the host's display scale factor. The aspect
// ratio stays the same.
class Editor : public Steinberg::Vst::VSTGUIEditor,
               public VSTGUI::IControlListener,
               public Steinberg::IPlugViewContentScaleSupport {
  public:
    Editor(Steinberg::Vst::EditController* controller, const EditorSettings& settings,
           EditorCallbacks callbacks);
    ~Editor() override;

    // Shows or hides the keyboard, as the KEYS button does. If the host
    // refuses the new window size, nothing changes.
    void SetKeyboardVisible(bool show);
    // What a click on the mutation lock does: an edit the host records.
    void SetMutationLock(bool locked);
    // 1 for the closed lock, 0 for the open one, -1 while the window is closed.
    int ShownMutationLock() const;
    // The tooltip over the Algorithm knob and its value; empty while closed.
    std::string AlgorithmTooltip() const;
    // True while Mutation and Notes are shown dimmed; false while closed.
    bool SequencerControlsDimmed() const;
    // The text in the middle of the small pass knob; empty while closed.
    std::string MutateEveryText() const;
    bool KeyboardVisible() const { return settings_.keyboard; }
    // What an octave button does: moves the visible keys by octaves, as far
    // as they go, and tells the controller to move the sequence along.
    void ShiftKeyboard(int octaves);
    const EditorSettings& Settings() const { return settings_; }

    // Called by the controller whenever a parameter changes, whatever the
    // source: this window, host automation or a MIDI controller.
    void ParameterChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized);
    // Called by the controller when a loaded project brings its own settings.
    void ApplySettings(const EditorSettings& settings);

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close() SMTG_OVERRIDE;
    void valueChanged(VSTGUI::CControl* control) SMTG_OVERRIDE;
    void controlBeginEdit(VSTGUI::CControl* control) SMTG_OVERRIDE;
    void controlEndEdit(VSTGUI::CControl* control) SMTG_OVERRIDE;
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
        VSTGUI::CTextLabel* name = nullptr; // none for the small pass knob
    };
    // Mutation, Notes and their small controls dim while Ladder plays,
    // which uses none of them.
    void UpdateDimming();
    void ShowValue(Knob& knob, Steinberg::Vst::ParamValue normalized);
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
    // Sets a switch's parameter as one edit of the user.
    void EditSwitch(Steinberg::Vst::ParamID id, bool on);
    // Lights the key at which the sequence's tonic sounds, while keys transpose.
    void UpdateMarkedKey();

    // The 13 knobs of the two rows, then the small pass knob at Mutation.
    std::array<Knob, 14> knobs_{};
    KeyboardView* keyboard_view_ = nullptr; // owned by the frame, as all below
    ToggleButton* keyboard_button_ = nullptr;
    ToggleButton* transpose_button_ = nullptr;
    ToggleButton* run_button_ = nullptr;
    ToggleButton* octave_down_ = nullptr;
    ToggleButton* octave_up_ = nullptr;
    LockButton* mutation_lock_ = nullptr;
    double scale_ = 1.0; // the host's display scale factor
    EditorSettings settings_;
    EditorCallbacks callbacks_;
    double applied_zoom_ = 0.0; // the total zoom the frame was last set to
    DragConstraint drag_;
    bool settle_pending_ = false;
    std::chrono::steady_clock::time_point last_change_{};
};

} // namespace taureon::toy_piano_generative
