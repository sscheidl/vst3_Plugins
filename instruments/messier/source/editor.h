#pragma once

#include "gui_layout.h"
#include "gui_skin.h"
#include "looper_engine.h"
#include "view_layout.h"
#include "wave_overview.h"

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

namespace VSTGUI {
class CKnob;
class CTextLabel;
} // namespace VSTGUI

namespace messier {

struct FileStatus;
class KeyboardView;
class MapView;
class MetalKnob;
class PanelView;
class ResizeGrip;
class ToggleButton;
class WindowShapeView;

// The window's timer: how often it asks for the display (and, while a file loads,
// for the file status). About 33 times a second.
constexpr std::chrono::milliseconds kDisplayInterval{30};

// Window size at zoom 1 on a display without scaling (0.2.0, the window study): header,
// file line, the measuring plate with waveform and START slider, the two rows of knobs,
// and below them the keyboard when it is shown (gui_layout.h).
constexpr double kEditorWidth = layout::kWidth;
constexpr double kEditorTopHeight = layout::kTopHeight;
constexpr double kKeyboardSectionHeight = layout::kKeyboardSection;
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
    int transpose = kKeyboardDefaultTranspose; // of the keyboard, in semitones
    GuiPreset preset = GuiPreset::kSlate;       // since 0.2.0
};

// The sizes the window's size menu offers (0.2.0), as zoom.
constexpr double kMenuZooms[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};

// How the window talks back to the controller.
struct EditorCallbacks {
    // The settings changed. Called at once, also for every step of a drag.
    std::function<void(const EditorSettings&)> changed;
    // The user has finished changing the settings: time to tell the host the
    // project needs saving. Called once per change, not per step of a drag.
    std::function<void()> settled;
    // A key on the screen keyboard was struck, or let go.
    std::function<void(int note)> strike;
    std::function<void(int note)> release;
    // The user chose a file to load (UTF-8 path).
    std::function<void(const std::string& path)> load;
    // The window's timer (kDisplayInterval): time to ask the processor for the
    // display and how loading goes.
    std::function<void()> poll;
    // The RESET button was pressed.
    std::function<void()> reset;
};

// The plug-in window (0.2.0, after the window study, version 3): a header with STOP,
// KEYS and the menus for the window size and the GUI preset; the sample's file line
// with its LOAD button; the measuring plate (MapView) with the waveform and the START
// slider on one time axis; seven knobs for the other pots' primary functions and
// Volume, below them six small knobs for the secondary functions under the pot they
// belong to, the FREEZE switch and the RESET button; and an on-screen keyboard that
// KEYS shows and hides, with LATCH and two octave keys beside it. Surfaces come from
// the GUI preset's bitmaps (gui_skin.h); everything that carries information is drawn.
// The layout is fixed, so the window scales as a whole: by the zoom the user picks
// (resizing, the grip or the size menu), times the host's display scale factor. The
// aspect ratio stays the same. Keyboard and sizing code are Kekule's (Torus Classic
// 0.3.1).
//
// The window never changes its own size. It asks the host (Resize()) and
// changes in onSize(), once the host has agreed.
class Editor : public Steinberg::Vst::VSTGUIEditor,
               public VSTGUI::IControlListener,
               public Steinberg::IPlugViewContentScaleSupport {
  public:
    // 7 primaries (START is the slider under the plate since 0.2.0), Volume, 6 secondaries.
    static constexpr std::size_t kKnobCount = 14;

    Editor(Steinberg::Vst::EditController* controller, const EditorSettings& settings,
           EditorCallbacks callbacks);
    ~Editor() override;

    // Shows or hides the keyboard, as the KEYS button does. If the host
    // refuses the new window size, nothing changes.
    void SetKeyboardVisible(bool show);
    void SetLatch(bool on);
    void SetStop(bool on);
    // For tests: the screen keyboard while the window is open, else nullptr.
    KeyboardView* Keyboard() const { return keyboard_view_; }
    bool KeyboardVisible() const { return settings_.keyboard; }
    // Moves the keyboard by octaves, as the octave keys do; saved with the
    // project like the window size.
    void SetKeyboardTranspose(int semitones);
    const EditorSettings& Settings() const { return settings_; }
    // The GUI preset, as its menu sets it (0.2.0): saved with the project. A preset whose
    // bitmaps are not delivered yet is kept as the setting and shown as Slate.
    void SetGuiPreset(GuiPreset preset);
    // The window size as its menu sets it (0.2.0). If the host refuses, nothing changes.
    void SetZoom(double zoom);
    // Freeze as the window's switch does: an edit the host records.
    void SetFreeze(bool on);
    // RESET as the window's button does: a message to the processor through the
    // controller (MS-R08). Not recorded as automation; parameter 121 stays for that.
    void PressReset();
    // Opens the file dialog, as the LOAD button does.
    void ChooseFile();
    // Called by the controller with what the processor reports about the sample.
    void ShowFileStatus(const FileStatus& status) noexcept;
    // Called by the controller with the newest display and the overview of its file
    // (null if the controller has none). Kept while the window is closed, drawn
    // while it is open.
    void ShowDisplay(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept;
    // The map while the window is open, else nullptr. For tests.
    const MapView* Map() const { return map_view_; }
    // What the window shows; empty while it is closed. For tests.
    std::string ShownFileName() const;
    std::string ShownFileInfo() const;
    int ShownFreeze() const; // 1 on, 0 off, -1 closed
    float ShownKnobValue(Steinberg::Vst::ParamID id) const;
    // The knob of a parameter while the window is open, else nullptr. For tests.
    VSTGUI::CKnob* KnobOf(Steinberg::Vst::ParamID id) const;

    // Called by the controller whenever a parameter changes, whatever the
    // source: this window, host automation or a MIDI controller.
    void ParameterChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized);
    // Called by the controller when a loaded project brings its own settings.
    void ApplySettings(const EditorSettings& settings);

    // MS-R24: building the window lets no exception out. If it cannot be built, what
    // was built is let go, open() answers false, and attached() undoes the SDK's
    // attachment and answers an error instead of an empty window.
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
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
        MetalKnob* knob = nullptr; // owned by the frame
        VSTGUI::CTextLabel* value = nullptr;
    };
    // The menus of the header (0.2.0).
    void ShowPresetMenu();
    void ShowSizeMenu();
    // Hands the current skin to every view and label; draws the window anew.
    void ApplySkin();
    void ShowValue(Knob& knob, Steinberg::Vst::ParamValue normalized);
    // Puts the file texts into the labels, if the window is open.
    void ApplyFileTexts();
    void ShowFileStatusUnchecked(const FileStatus& status);
    // Forgets the views of a closed window.
    void ForgetViews();
    bool OpenUnchecked(void* parent, const VSTGUI::PlatformType& platformType);
    // MS-R37: the bodies of checkSizeConstraint, onSize and the first part of close.
    Steinberg::tresult CheckSizeUnchecked(Steinberg::ViewRect* size);
    Steinberg::tresult OnSizeUnchecked(Steinberg::ViewRect* newSize);
    void FinishEdits();
    // Adds a view to the frame at once, so that the frame owns it (MS-R24); a view the
    // frame could not take is let go.
    template <typename View>
    View* Add(View* view) {
        try {
            frame->addView(view);
        } catch(...) {
            view->forget();
            throw;
        }
        return view;
    }
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
    // The resize grip (0.1.11): the size when the drag began, and the zoom for a drag
    // of (dx, dy) window pixels from it, as a corner drag of the window would give.
    void GripBegin();
    void GripDrag(double dx, double dy);
    EditorSize grip_start_{0, 0};

    std::array<Knob, kKnobCount> knobs_{};
    KeyboardView* keyboard_view_ = nullptr; // owned by the frame, like the views below
    ToggleButton* keyboard_button_ = nullptr;
    ToggleButton* octave_down_ = nullptr;
    ToggleButton* octave_up_ = nullptr;
    ToggleButton* freeze_button_ = nullptr;
    VSTGUI::CTextLabel* file_name_ = nullptr;
    VSTGUI::CTextLabel* file_info_ = nullptr;
    MapView* map_view_ = nullptr;
    // Since 0.1.8: the file line as a drop zone for WAV files (it carries the file
    // name's tooltip, as the labels take no mouse); owned by the frame.
    VSTGUI::CView* file_zone_ = nullptr;
    // Since 0.1.8: the WINDOW knob's value as a curve instead of a text; owned by the frame.
    WindowShapeView* window_shape_ = nullptr;
    // Since 0.1.8: LATCH for the screen keyboard. Not saved; a window opens unlatched.
    ToggleButton* latch_button_ = nullptr;
    ToggleButton* stop_button_ = nullptr; // since 0.1.8
    ResizeGrip* grip_ = nullptr;          // since 0.1.11
    // Since 0.2.0: the background, the header's menus, the catalogue number of the file.
    PanelView* panel_ = nullptr;
    ToggleButton* size_button_ = nullptr;
    ToggleButton* preset_button_ = nullptr;
    VSTGUI::CTextLabel* catalogue_ = nullptr;
    std::uint64_t catalogue_shown_ = ~std::uint64_t(0);
    std::shared_ptr<const Skin> skin_; // the look of the open window
    bool latch_ = false;
    EngineDisplay shown_display_{};             // the newest display, also while closed
    std::shared_ptr<const WaveOverview> shown_wave_;
    std::string file_name_text_, file_info_text_, file_tooltip_;
    bool file_warning_ = false;
    bool dialog_open_ = false;
    std::shared_ptr<bool> alive_; // false once this object is destroyed
    double scale_ = 1.0; // the host's display scale factor
    EditorSettings settings_;
    EditorCallbacks callbacks_;
    double applied_zoom_ = 0.0; // the total zoom the frame was last set to
    DragConstraint drag_;
    bool settle_pending_ = false;
    std::chrono::steady_clock::time_point last_change_{};
};

} // namespace messier
