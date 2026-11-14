#pragma once

#include "editor.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <vector>

namespace torus_classic {

// Parameter side of the plug-in: declares the parameters, tells the host which
// MIDI controllers drive them and creates the editor window.
class Controller : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping {
  public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    // The sound's state, as the processor saved it. A damaged state changes
    // nothing. The pitch wheel returns to its centre, as in the processor.
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // The controller's own state: the window's zoom and whether the keyboard
    // shows. Neither is a musical parameter; the host does not see them as one.
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
    const EditorSettings& WindowSettings() const { return editor_; }

    // Plays a note through the processor, as the screen keyboard does. False
    // if the host offers no messages or the processor refused the note; then
    // nothing sounds. ReleaseNote tells the processor the key was let go: the
    // sound does not care, the note the host records does.
    bool PlayNote(int note);
    bool ReleaseNote(int note);

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
    bool SendKey(int note, int velocity);

    std::vector<OpenEditor> editors_;
    bool tune_free_ = false; // mirrors kParamTuneFree for Tune's text
    EditorSettings editor_; // window size and keyboard, saved with the project
};

} // namespace torus_classic
