#pragma once

#include "editor.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <vector>

namespace taureon::toy_piano_classic {


// Parameter side of the plug-in: declares the parameters, tells the host which
// MIDI controllers drive them and creates the editor window.
class Controller : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping {
  public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // The controller's own state: the zoom of the editor window.
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                     Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    void editorAttached(Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
    void editorRemoved(Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;

    // Plays a note through the processor, as the screen keyboard does.
    void PlayNote(int note);

    // IMidiMapping
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::CtrlNumber midiControllerNumber,
        Steinberg::Vst::ParamID& id) SMTG_OVERRIDE;

    OBJ_METHODS(Controller, EditController)
#pragma warning(push)
#pragma warning(disable : 4458) // the SDK macro names a parameter like a class member
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(EditController)
#pragma warning(pop)
    REFCOUNT_METHODS(EditController)

  private:
    std::vector<Editor*> editors_; // the open windows; they are not owned here
    EditorSettings editor_;        // window size and keyboard, saved with the project
};

} // namespace taureon::toy_piano_classic
