#pragma once

#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <array>

namespace VSTGUI {
class CKnob;
class CTextLabel;
} // namespace VSTGUI

namespace taureon::toy_piano_classic {

// The plug-in window: four knobs, each with its name above and its value below.
// Fixed layout; the host's display scale factor zooms it as a whole.
class Editor : public Steinberg::Vst::VSTGUIEditor,
               public VSTGUI::IControlListener,
               public Steinberg::IPlugViewContentScaleSupport {
  public:
    explicit Editor(Steinberg::Vst::EditController* controller);

    // Called by the controller whenever a parameter changes, whatever the
    // source: this window, host automation or a MIDI controller.
    void ParameterChanged(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized);

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close() SMTG_OVERRIDE;
    void valueChanged(VSTGUI::CControl* control) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) SMTG_OVERRIDE;
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
    Steinberg::ViewRect ScaledSize() const;

    std::array<Knob, 4> knobs_{};
    double scale_ = 1.0;
};

} // namespace taureon::toy_piano_classic
