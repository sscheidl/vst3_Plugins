#include "controller.h"

#include "editor.h"
#include "keyboard_layout.h"
#include "parameters.h"
#include "plugin_ids.h"
#include "state_io.h"
#include "value_text.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace taureon::toy_piano_generative {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Typed text, read strictly (see value_text.h). The SDK's own readers accept
// "foo" and "12junk"; for a stepped RangeParameter "foo" even yields an
// uninitialised number.
bool ReadTyped(const TChar* string, const char* unit, double& value) {
    if(!string)
        return false;
    return ParseTypedNumber(StringConvert::convert(string), unit, value);
}

// A range in plain units, typed strictly, with or without its unit. With
// steps, a typed value is rounded to the nearest step; out-of-range values
// are clamped.
class TypedRangeParameter : public RangeParameter {
  public:
    using RangeParameter::RangeParameter;

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        double plain = 0.0;
        if(!ReadTyped(string, StringConvert::convert(info.units).c_str(), plain))
            return false;
        if(info.stepCount > 0)
            plain = std::round(plain);
        normalized = toNormalized(std::clamp(plain, getMin(), getMax()));
        return true;
    }

    OBJ_METHODS(TypedRangeParameter, RangeParameter)
};

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
        if(!ReadTyped(string, "ms", milliseconds))
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
        if(string && EqualsIgnoringCase(StripUnit(StringConvert::convert(string), "dB"), "-inf")) {
            normalized = 0.0; // silence, as displayed
            return true;
        }
        double db = 0.0;
        if(!ReadTyped(string, "dB", db))
            return false;
        normalized = VolumeNormalized(db);
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE { return VolumeDb(normalized); }
    ParamValue toNormalized(ParamValue db) const SMTG_OVERRIDE { return VolumeNormalized(db); }

    OBJ_METHODS(VolumeParameter, Parameter)
};

// The mutation in percent. The knob follows the sequencer's law: its first
// half covers 0 to 20 %, its second half 20 to 100 %.
class MutationParameter : public Parameter {
  public:
    MutationParameter(const TChar* title, ParamID tag)
    : Parameter(title, tag, STR16("%"), kDefaultMutation, 0, ParameterInfo::kCanAutomate) {}

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        char text[32];
        std::snprintf(text, sizeof(text), "%d", generative::MutationPercent(normalized));
        UString(string, 128).fromAscii(text);
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        double percent = 0.0;
        if(!ReadTyped(string, "%", percent))
            return false;
        normalized = toNormalized(percent);
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE {
        return generative::MutationPercent(normalized);
    }

    ParamValue toNormalized(ParamValue percent) const SMTG_OVERRIDE {
        return generative::MutationPosition(
            static_cast<int>(std::lround(std::clamp(percent, 0.0, 100.0))));
    }

    OBJ_METHODS(MutationParameter, Parameter)
};

// A choice from a list of names.
StringListParameter* MakeList(const TChar* title, ParamID tag,
                              std::initializer_list<const char*> names, ParamValue normalized) {
    auto* list = new StringListParameter(title, tag, nullptr,
                                         ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    for(const char* name : names) {
        String128 string{};
        UString(string, 128).fromAscii(name);
        list->appendString(string);
    }
    list->getInfo().defaultNormalizedValue = normalized;
    list->setNormalized(normalized);
    return list;
}

} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditController::initialize(context);
    if(result != kResultOk)
        return result;

    parameters.addParameter(new TypedRangeParameter(STR16("Master Pitch"), kParamMasterPitch,
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
    parameters.addParameter(new TypedRangeParameter(STR16("Pitch Bend"), kParamPitchBend, STR16("st"),
                                               -kPitchBendRange, kPitchBendRange, 0, 0,
                                               ParameterInfo::kCanAutomate));

    // The sequencer.
    parameters.addParameter(MakeList(STR16("Run"), kParamRun, {"Off", "On"}, kDefaultRun));
    parameters.addParameter(new TypedRangeParameter(STR16("Algorithm"), kParamAlgorithm, nullptr,
                                                    1, generative::kNumAlgorithms, 1,
                                                    kAlgorithmSteps, ParameterInfo::kCanAutomate));
    parameters.addParameter(new MutationParameter(STR16("Mutation"), kParamMutation));
    parameters.addParameter(new TypedRangeParameter(
        STR16("Note Probability"), kParamNoteProbability, STR16("%"), 0, 100,
        generative::kDefaultNoteProbability, kNoteProbabilitySteps, ParameterInfo::kCanAutomate));
    parameters.addParameter(MakeList(STR16("Scale"), kParamScale,
                                     {"Major", "Minor", "Pentatonic", "Odd", "Octave", "Fifths",
                                      "Tonic"},
                                     kDefaultScale));
    parameters.addParameter(MakeList(STR16("Tonic"), kParamTonic,
                                     {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#",
                                      "B"},
                                     kDefaultTonic));
    parameters.addParameter(new TypedRangeParameter(STR16("Octave"), kParamOctave, nullptr, 0,
                                                    generative::kMaxOctave,
                                                    generative::kDefaultOctave, kOctaveSteps,
                                                    ParameterInfo::kCanAutomate));
    parameters.addParameter(new TypedRangeParameter(STR16("Length"), kParamLength, nullptr, 1,
                                                    generative::kNumSteps, generative::kNumSteps,
                                                    kLengthSteps, ParameterInfo::kCanAutomate));
    parameters.addParameter(MakeList(STR16("Clock Divide"), kParamClockDivide,
                                     {"1", "2", "3", "4", "6", "8", "16", "32", "64"},
                                     kDefaultClockDivide));
    auto* tuning = new TypedRangeParameter(STR16("Tuning"), kParamTuning, STR16("%"), 0, 100, 0,
                                           0, ParameterInfo::kCanAutomate);
    tuning->setPrecision(0);
    parameters.addParameter(tuning);
    parameters.addParameter(
        MakeList(STR16("Keys Transpose"), kParamKeysTranspose, {"Off", "On"},
                 kDefaultKeysTranspose));
    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    SavedState saved;
    if(!ReadState(stream, saved))
        return kResultFalse;
    const generative::State& sequencer = saved.sequencer;
    setParamNormalized(kParamMasterPitch, saved.master_pitch);
    setParamNormalized(kParamAttack, saved.attack);
    setParamNormalized(kParamDecay, saved.decay);
    setParamNormalized(kParamVolume, saved.volume);
    setParamNormalized(kParamTuning, saved.tuning);
    setParamNormalized(kParamRun, saved.run ? 1.0 : 0.0);
    setParamNormalized(kParamKeysTranspose, saved.keys_transpose ? 1.0 : 0.0);
    setParamNormalized(kParamAlgorithm, StepNormalized(sequencer.algorithm - 1, kAlgorithmSteps));
    setParamNormalized(kParamMutation, generative::MutationPosition(sequencer.mutation));
    setParamNormalized(kParamNoteProbability,
                       StepNormalized(sequencer.note_probability, kNoteProbabilitySteps));
    setParamNormalized(kParamScale, StepNormalized(sequencer.scale_mode, kScaleSteps));
    setParamNormalized(kParamTonic, StepNormalized(sequencer.tonic, kTonicSteps));
    setParamNormalized(kParamOctave, StepNormalized(sequencer.octave, kOctaveSteps));
    setParamNormalized(kParamLength, StepNormalized(sequencer.length - 1, kLengthSteps));
    setParamNormalized(kParamClockDivide,
                       StepNormalized(sequencer.clock_divide, kClockDivideSteps));
    return kResultOk;
}

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value) {
    const tresult result = EditController::setParamNormalized(tag, value);
    if(result == kResultOk)
        for(const OpenEditor& open : editors_)
            open.editor->ParameterChanged(tag, getParamNormalized(tag));
    return result;
}

tresult PLUGIN_API Controller::getState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    if(!stream.writeInt32(kControllerStateVersion) || !stream.writeDouble(editor_.zoom)
       || !stream.writeInt8(editor_.keyboard ? 1 : 0)
       || !stream.writeInt8(static_cast<int8>(editor_.keyboard_shift)))
        return kResultFalse;
    return kResultOk;
}

tresult PLUGIN_API Controller::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    int32 version = 0;
    double zoom = 1.0;
    int8 keyboard = 1, shift = 0;
    if(!stream.readInt32(version) || version != kControllerStateVersion
       || !stream.readDouble(zoom) || !stream.readInt8(keyboard) || !stream.readInt8(shift))
        return kResultFalse;
    editor_.zoom = ClampEditorZoom(zoom);
    editor_.keyboard = keyboard != 0;
    editor_.keyboard_shift = ClampKeyboardShift(shift);
    for(const OpenEditor& open : editors_)
        open.editor->ApplySettings(editor_);
    return kResultOk;
}

void Controller::EditParameter(ParamID id, ParamValue normalized) {
    beginEdit(id);
    setParamNormalized(id, normalized);
    performEdit(id, getParamNormalized(id));
    endEdit(id);
}

void Controller::KeyStruck(int note) {
    if(IsOn(getParamNormalized(kParamKeysTranspose)))
        TransposeToKey(note);
    else
        PlayNote(note);
}

void Controller::TransposeToKey(int note) {
    const KeyTransposition to = TranspositionForKey(note);
    EditParameter(kParamTonic, StepNormalized(to.tonic, kTonicSteps));
    EditParameter(kParamOctave, StepNormalized(to.octave, kOctaveSteps));
}

void Controller::ShiftOctave(int octaves) {
    const int octave = StepIndex(getParamNormalized(kParamOctave), kOctaveSteps);
    const int moved = std::clamp(octave + octaves, 0, kOctaveSteps);
    if(moved != octave)
        EditParameter(kParamOctave, StepNormalized(moved, kOctaveSteps));
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
    callbacks.strike = [this](int note) { KeyStruck(note); };
    callbacks.octave = [this](int octaves) { ShiftOctave(octaves); };
    return new Editor(this, editor_, std::move(callbacks));
}

void Controller::editorAttached(EditorView* editor) {
    auto* own = dynamic_cast<Editor*>(editor);
    if(!own)
        return;
    for(const OpenEditor& open : editors_)
        if(open.view == editor)
            return; // attached twice: keep one entry
    editors_.push_back({editor, own});
}

void Controller::editorRemoved(EditorView* editor) {
    editors_.erase(std::remove_if(editors_.begin(), editors_.end(),
                                  [editor](const OpenEditor& open) { return open.view == editor; }),
                   editors_.end());
}

void Controller::editorDestroyed(EditorView* editor) {
    editorRemoved(editor);
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

} // namespace taureon::toy_piano_generative
