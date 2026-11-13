#include "controller.h"

#include "editor.h"
#include "keyboard_layout.h"
#include "parameters.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace taureon::toy_piano_classic {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// A time on the firmware's logarithmic pot curve, shown and typed in milliseconds.
class LogTimeParameter : public Parameter {
  public:
    LogTimeParameter(const TChar* title, ParamID tag, double minimum_seconds,
                     double maximum_seconds, ParamValue default_normalized)
    : Parameter(title, tag, STR16("ms"), default_normalized, 0, ParameterInfo::kCanAutomate),
      minimum_(minimum_seconds),
      maximum_(maximum_seconds) {}

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        const double milliseconds = toPlain(normalized);
        char text[32];
        std::snprintf(text, sizeof(text), milliseconds < 100.0 ? "%.1f" : "%.0f", milliseconds);
        UString(string, 128).fromAscii(text);
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        double milliseconds = 0.0;
        if(!UString(const_cast<TChar*>(string), -1).scanFloat(milliseconds))
            return false;
        normalized = toNormalized(milliseconds);
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE {
        return 1000.0
               * toy_piano::LogTime(static_cast<float>(normalized), static_cast<float>(minimum_),
                                    static_cast<float>(maximum_));
    }

    ParamValue toNormalized(ParamValue milliseconds) const SMTG_OVERRIDE {
        return LogTimePosition(milliseconds / 1000.0, minimum_, maximum_);
    }

    OBJ_METHODS(LogTimeParameter, Parameter)

  private:
    double minimum_, maximum_;
};

// The volume in dB. The lowest position is silence and reads "-inf".
class VolumeParameter : public Parameter {
  public:
    VolumeParameter(const TChar* title, ParamID tag)
    : Parameter(title, tag, STR16("dB"), kDefaultVolume, 0, ParameterInfo::kCanAutomate) {}

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        char text[32];
        if(normalized > 0.0)
            std::snprintf(text, sizeof(text), "%+.1f", VolumeDb(normalized));
        else
            std::snprintf(text, sizeof(text), "-inf");
        UString(string, 128).fromAscii(text);
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        double db = 0.0;
        if(!UString(const_cast<TChar*>(string), -1).scanFloat(db))
            return false;
        normalized = VolumeNormalized(db);
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE { return VolumeDb(normalized); }
    ParamValue toNormalized(ParamValue db) const SMTG_OVERRIDE { return VolumeNormalized(db); }

    OBJ_METHODS(VolumeParameter, Parameter)
};

} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditController::initialize(context);
    if(result != kResultOk)
        return result;

    parameters.addParameter(new RangeParameter(STR16("Master Pitch"), kParamMasterPitch,
                                               STR16("st"), -kMasterPitchRange, kMasterPitchRange,
                                               0, kMasterPitchSteps,
                                               ParameterInfo::kCanAutomate));
    parameters.addParameter(new LogTimeParameter(STR16("Attack"), kParamAttack,
                                                 toy_piano::kAttackMin, toy_piano::kAttackMax,
                                                 kDefaultAttack));
    parameters.addParameter(new LogTimeParameter(STR16("Decay"), kParamDecay,
                                                 toy_piano::kDecayMin, toy_piano::kDecayMax,
                                                 kDefaultDecay));
    parameters.addParameter(new VolumeParameter(STR16("Volume"), kParamVolume));
    // The pitch wheel arrives as a parameter, the way VST3 delivers MIDI
    // controllers. It has no knob and is not saved.
    parameters.addParameter(new RangeParameter(STR16("Pitch Bend"), kParamPitchBend, STR16("st"),
                                               -kPitchBendRange, kPitchBendRange, 0, 0,
                                               ParameterInfo::kCanAutomate));
    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    int32 version = 0;
    double master_pitch = 0.0, attack = 0.0, decay = 0.0, volume = 0.0;
    if(!stream.readInt32(version) || version != kStateVersion || !stream.readDouble(master_pitch)
       || !stream.readDouble(attack) || !stream.readDouble(decay) || !stream.readDouble(volume))
        return kResultFalse;
    setParamNormalized(kParamMasterPitch, Clamp01(master_pitch));
    setParamNormalized(kParamAttack, Clamp01(attack));
    setParamNormalized(kParamDecay, Clamp01(decay));
    setParamNormalized(kParamVolume, Clamp01(volume));
    return kResultOk;
}

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value) {
    const tresult result = EditController::setParamNormalized(tag, value);
    if(result == kResultOk)
        for(Editor* editor : editors_)
            editor->ParameterChanged(tag, getParamNormalized(tag));
    return result;
}

tresult PLUGIN_API Controller::getState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    if(!stream.writeInt32(kControllerStateVersion) || !stream.writeDouble(editor_.zoom)
       || !stream.writeInt8(editor_.keyboard ? 1 : 0))
        return kResultFalse;
    return kResultOk;
}

tresult PLUGIN_API Controller::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    int32 version = 0;
    double zoom = 1.0;
    int8 keyboard = 1; // version 1 (plug-in 0.2.0) had no keyboard: show it
    if(!stream.readInt32(version) || version < 1 || version > kControllerStateVersion
       || !stream.readDouble(zoom))
        return kResultFalse;
    if(version >= 2 && !stream.readInt8(keyboard))
        return kResultFalse;
    editor_.zoom = ClampEditorZoom(zoom);
    editor_.keyboard = keyboard != 0;
    for(Editor* editor : editors_)
        editor->ApplySettings(editor_);
    return kResultOk;
}

void Controller::PlayNote(int note) {
    if(note < 0 || note > 127)
        return;
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message)
        return; // the host offers no messages: nothing to play through
    message->setMessageID(kNoteMessageId);
    message->getAttributes()->setInt(kNoteMessageNote, note);
    message->getAttributes()->setInt(kNoteMessageVelocity, kKeyboardVelocity);
    sendMessage(message);
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if(!name || std::strcmp(name, ViewType::kEditor) != 0)
        return nullptr;
    // The window holds a reference to this controller, so `this` outlives it.
    EditorCallbacks callbacks;
    callbacks.changed = [this](const EditorSettings& settings) { editor_ = settings; };
    // Window size and keyboard belong to the project: the host should offer to save.
    callbacks.settled = [this] { setDirty(true); };
    callbacks.strike = [this](int note) { PlayNote(note); };
    return new Editor(this, editor_, std::move(callbacks));
}

void Controller::editorAttached(EditorView* editor) {
    if(auto* own = dynamic_cast<Editor*>(editor))
        editors_.push_back(own);
}

void Controller::editorRemoved(EditorView* editor) {
    editors_.erase(std::remove_if(editors_.begin(), editors_.end(),
                                  [editor](Editor* own) {
                                      return static_cast<EditorView*>(own) == editor;
                                  }),
                   editors_.end());
}

tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16 /*channel*/,
                                                           CtrlNumber midiControllerNumber,
                                                           ParamID& id) {
    if(busIndex != 0)
        return kResultFalse;
    switch(midiControllerNumber) {
        case kPitchBend: id = kParamPitchBend; return kResultTrue;
        case kMidiCcMasterPitch: id = kParamMasterPitch; return kResultTrue;
        case kMidiCcAttack: id = kParamAttack; return kResultTrue;
        case kMidiCcDecay: id = kParamDecay; return kResultTrue;
        default: return kResultFalse;
    }
}

FUnknown* CreateControllerInstance(void*) {
    return Controller::createInstance(nullptr);
}

} // namespace taureon::toy_piano_classic
