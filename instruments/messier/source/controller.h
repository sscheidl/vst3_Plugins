#pragma once

#include "editor.h"
#include "file_loader.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <string>
#include <vector>

namespace messier {

// Parameter side of the plug-in: declares the parameters, tells the host which MIDI
// controllers drive them, creates the editor window, sends the window's file
// choice to the processor and shows what the processor reports back. After
// Kekule 0.3.1.
class Controller : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping {
  public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    // The sound's state, as the processor saved it. A damaged state changes
    // nothing. The pitch wheel returns to its centre, as in the processor.
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // The controller's own state: the window's zoom, the keyboard and its octave.
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                     Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
    // The processor's file status arrives here.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    void editorAttached(Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
    void editorRemoved(Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
    void editorDestroyed(Steinberg::Vst::EditorView* editor) SMTG_OVERRIDE;
    std::size_t OpenEditorCount() const { return editors_.size(); }
    const EditorSettings& WindowSettings() const { return editor_; }

    // Plays and releases a note through the processor, as the screen keyboard does.
    // False if the host offers no messages or the processor refused it.
    bool PlayNote(int note);
    bool ReleaseNote(int note);
    // Asks the processor to load a file (UTF-8 path), as the LOAD button does.
    bool LoadFile(const std::string& path);
    // Asks the processor for the file status. Sent while a load runs, when a
    // window opens and after a project was loaded.
    bool RequestFileStatus() noexcept;
    // RESET from the window: a message to the processor (MS-R08).
    bool PressReset() noexcept;
    const FileStatus& File() const { return file_; }

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
    struct OpenEditor {
        Steinberg::Vst::EditorView* view;
        Editor* editor;
    };
    void AddParameters();
    // A value set as part of a loaded state, then shown; never throws.
    void SetValue(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) noexcept;
    void ShowParameter(Steinberg::Vst::ParamID tag) noexcept;
    bool SendKey(int note, int velocity) noexcept;
    bool SendKeyUnchecked(int note, int velocity);
    bool SendLoad(const std::string& path);
    bool SendSimple(const char* id) noexcept;
    Editor* MakeEditor();
    void ShowFile() noexcept;

    std::vector<OpenEditor> editors_;
    EditorSettings editor_;
    FileStatus file_;
};

} // namespace messier
