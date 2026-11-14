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
    // Also called when a window is destroyed without having been removed
    // first, which some hosts may do. Forgets it either way.
    void editorDestroyed(Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
    // Number of windows the controller keeps up to date; for the tests.
    std::size_t OpenEditorCount() const { return editors_.size(); }

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
    // The open windows, not owned here. The view pointer identifies an entry
    // even while its Editor part is already being destroyed.
    struct OpenEditor {
        Steinberg::Vst::EditorView* view;
        Editor* editor;
    };
    std::vector<OpenEditor> editors_;
    EditorSettings editor_;        // window size and keyboard, saved with the project
};

} // namespace taureon::toy_piano_classic
