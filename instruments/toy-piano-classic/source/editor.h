#pragma once

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace VSTGUI {
class CKnob;
class CTextLabel;
} // namespace VSTGUI

namespace taureon::toy_piano_classic {

// Size of the window at zoom 1 on a display without scaling.
constexpr double kEditorWidth = 460;
constexpr double kEditorHeight = 190;
// Limits of the window zoom the user can choose by resizing.
constexpr double kMinEditorZoom = 0.5;
constexpr double kMaxEditorZoom = 2.0;

inline double ClampEditorZoom(double zoom) {
    return zoom >= kMinEditorZoom ? (zoom <= kMaxEditorZoom ? zoom : kMaxEditorZoom)
                                  : kMinEditorZoom; // NaN becomes the minimum
}

struct EditorSize {
    int width;
    int height;
};

// Window size in pixels. `total` is the user's zoom times the display scale.
inline EditorSize EditorSizeFor(double total) {
    return {static_cast<int>(std::lround(kEditorWidth * total)),
            static_cast<int>(std::lround(kEditorHeight * total))};
}

// The zoom a window of this size stands for: the largest that fits into it.
inline double EditorZoomForSize(int width, int height, double scale) {
    return ClampEditorZoom(std::min(width / kEditorWidth, height / kEditorHeight) / scale);
}

// The size the host may use when the user drags the window from `current`
// towards `asked`. Keeps the aspect ratio and the zoom limits. The edge being
// dragged decides: whichever of width and height moved further.
inline EditorSize ConstrainEditorSize(EditorSize asked, EditorSize current, double scale) {
    const bool width_leads =
        std::abs(asked.width - current.width) >= std::abs(asked.height - current.height);
    const double total = width_leads ? asked.width / kEditorWidth : asked.height / kEditorHeight;
    return EditorSizeFor(scale * ClampEditorZoom(total / scale));
}

// The plug-in window: four knobs, each with its name above and its value below.
// The layout is fixed and drawn as vectors, so the window scales as a whole:
// by the zoom the user picks when resizing it, times the host's display scale
// factor. The aspect ratio stays the same.
class Editor : public Steinberg::Vst::VSTGUIEditor,
               public VSTGUI::IControlListener,
               public Steinberg::IPlugViewContentScaleSupport {
  public:
    // `zoom_changed` is called whenever the user has resized the window.
    Editor(Steinberg::Vst::EditController* controller, double zoom,
           std::function<void(double)> zoom_changed);

    // Called by the controller whenever a parameter changes, whatever the
    // source: this window, host automation or a MIDI controller.
    void ParameterChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized);
    // Called by the controller when a loaded project brings its own zoom.
    void SetZoom(double zoom);

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close() SMTG_OVERRIDE;
    void valueChanged(VSTGUI::CControl* control) SMTG_OVERRIDE;
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
    // Window size in pixels for the current zoom and display scale.
    Steinberg::ViewRect ScaledSize() const;
    // Draws the content at the current zoom into a frame of the given size.
    void LayOut(Steinberg::int32 width, Steinberg::int32 height);
    // Asks the host to give the window the size ScaledSize() says.
    void RequestScaledSize();

    std::array<Knob, 4> knobs_{};
    double scale_ = 1.0; // the host's display scale factor
    double zoom_ = 1.0;  // chosen by the user
    std::function<void(double)> zoom_changed_;
};

} // namespace taureon::toy_piano_classic
