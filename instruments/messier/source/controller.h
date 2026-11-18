#pragma once

#include "display_data.h"
#include "editor.h"
#include "file_loader.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <memory>
#include <string>
#include <vector>

namespace messier {

// Parameter side of the plug-in: declares the parameters, tells the host which MIDI
// controllers drive them, creates the editor window, sends the window's file
// choice to the processor and shows what the processor reports back. After
// Kekule 0.3.1.
class Controller : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping {
  public:
    // Null if the controller cannot be made (MS-R19), see Processor::createInstance.
    static Steinberg::FUnknown* createInstance(void*) noexcept {
        try {
            return static_cast<Steinberg::Vst::IEditController*>(new Controller());
        } catch(...) {
            return nullptr;
        }
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    // A processor connects, perhaps another one than before (MS-R17): what the
    // controller knows of the old one's files is let go, and it asks the new one for
    // its status.
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
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
    // Asks the processor for what the window shows (since 0.1.4): sent on every tick
    // of an open window's timer, never while no window is open. Never throws.
    bool RequestDisplay() noexcept;
    // The newest display the processor sent, and the overview of its file if the
    // controller has it (else null). For the window and tests.
    const EngineDisplay& Display() const { return display_; }
    const WaveOverview* Wave() const { return ShownWave().get(); }
    // The connection generation (MS-R21); it grows with every connect. For tests.
    Steinberg::int64 Generation() const { return generation_; }

    // The host asks for a parameter's text, or the value of a typed text; guarded
    // (MS-R11).
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
                                                        Steinberg::Vst::ParamValue valueNormalized,
                                                        Steinberg::Vst::String128 string) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag,
                                                        Steinberg::Vst::TChar* string,
                                                        Steinberg::Vst::ParamValue& valueNormalized)
        SMTG_OVERRIDE;

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
    Steinberg::tresult ReadDisplay(Steinberg::Vst::IMessage* message) noexcept;
    void ShowDisplay() noexcept;
    Steinberg::tresult ReadDisplayUnchecked(Steinberg::Vst::IMessage* message);
    // wave_ if it belongs to the file display_ shows, else null. Both come from the
    // current connection only (MS-R21).
    std::shared_ptr<const WaveOverview> ShownWave() const noexcept {
        return wave_ && display_.frames != 0 && wave_->sequence == display_.sequence
                       && wave_->frames == display_.frames
                   ? wave_
                   : nullptr;
    }
    // A message with the current generation attached; null without memory.
    Steinberg::IPtr<Steinberg::Vst::IMessage> NewMessage(const char* id);
    // MS-R04, MS-R12, MS-R17, MS-R20: setDirty for the files from the window that
    // became the sound, counted from parameter 199 (the count modulo 65536) and from
    // the status (the full count and the processor it belongs to). The contract: at
    // least one call for every change the host passes on, one per file up to
    // kMostDirtyCalls for one report; a host needs one to know.
    void CountFromParameter(std::uint32_t count) noexcept;
    void CountFromStatus(std::uint64_t processor, std::uint32_t count) noexcept;
    void MarkChanged(std::uint32_t loads) noexcept;
    // Forgets the display of another processor's files.
    void ForgetDisplay() noexcept;

    std::vector<OpenEditor> editors_;
    EditorSettings editor_;
    FileStatus file_;
    // The loads of processor counted_for_ (0: none named yet) that have been counted.
    std::uint32_t user_loads_counted_ = 0;
    std::uint64_t counted_for_ = 0;
    // From connect() until the first status of that connection (MS-R20): which
    // processor sends parameter 199 is not known. Every change then marks the project,
    // and marked_since_connect_ says how many loads that stood for, so that the status
    // adds only what is missing.
    bool peer_unconfirmed_ = true;
    std::uint32_t marked_since_connect_ = 0;
    bool parameter_since_connect_ = false;
    std::uint32_t last_parameter_ = 0;
    // The connection generation (MS-R21) and, within it, the newest status serial and
    // display reply taken.
    Steinberg::int64 generation_ = 1;
    std::uint32_t last_status_serial_ = 0;
    std::uint64_t last_reply_ = 0;
    static constexpr std::uint32_t kMostDirtyCalls = 64;
    EngineDisplay display_{};
    std::shared_ptr<const WaveOverview> wave_; // may belong to an older file than display_
};

} // namespace messier
