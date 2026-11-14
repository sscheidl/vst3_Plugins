#include "controller.h"

#include "editor.h"
#include "parameters.h"
#include "plugin_ids.h"
#include "state_io.h"
#include "value_text.h"
#include "view_layout.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace torus_classic {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Typed text is read strictly (see value_text.h): one finite number,
// optionally the unit, nothing else. The SDK's own readers accept "foo" and
// "12junk". On failure the output value is left as it was.
bool ReadTyped(const TChar* string, const char* unit, double& value) {
    if(!string)
        return false;
    return ParseTypedNumber(StringConvert::convert(string), unit, value);
}

// Formats a value; a result that rounds to zero carries no minus sign.
void Print(String128 string, const char* format, double value) {
    char text[32];
    std::snprintf(text, sizeof(text), format, value);
    const char* shown = text;
    if(text[0] == '-' && std::strspn(text + 1, "0.") == std::strlen(text + 1))
        shown = text + 1;
    UString(string, 128).fromAscii(shown);
}

// A continuous parameter shown in its own unit: `scale` times the distance
// from `zero`, e.g. semitones around the centre or percent from 0.
class LinearParameter : public Parameter {
  public:
    LinearParameter(const TChar* title, ParamID tag, const char* unit, const TChar* unit16,
                    double zero, double scale, const char* format, ParamValue default_normalized)
    : Parameter(title, tag, unit16, default_normalized, 0, ParameterInfo::kCanAutomate),
      unit_(unit),
      zero_(zero),
      scale_(scale),
      format_(format) {}

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        Print(string, format_, toPlain(normalized));
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        double plain = 0.0;
        if(!ReadTyped(string, unit_, plain))
            return false;
        normalized = toNormalized(plain); // out-of-range values are clamped
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE {
        return (Clamp01(normalized) - zero_) * scale_;
    }
    ParamValue toNormalized(ParamValue plain) const SMTG_OVERRIDE {
        return Clamp01(plain / scale_ + zero_);
    }

    OBJ_METHODS(LinearParameter, Parameter)

  private:
    const char* unit_;
    double zero_, scale_;
    const char* format_;
};

// The synthesis model: six named steps. Display, typed text, normalized
// values and the engine all go through ModelIndex().
class ModelParameter : public Parameter {
  public:
    ModelParameter(const TChar* title, ParamID tag)
    : Parameter(title, tag, nullptr, kDefaultModel, kModelSteps,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsList) {}

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        UString(string, 128).fromAscii(kModelNames[ModelIndex(normalized)]);
    }

    // A model's name, in any case, or its number from 1 to 6.
    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        if(!string)
            return false;
        const std::string text = StringConvert::convert(string);
        const std::string_view name = StripUnit(text, "");
        for(int model = 0; model < kModelCount; ++model) {
            if(EqualsIgnoringCase(name, kModelNames[model])) {
                normalized = ModelNormalized(model);
                return true;
            }
        }
        double number = 0.0;
        if(!ParseTypedNumber(text, "", number) || number != std::floor(number) || number < 1.0
           || number > kModelCount)
            return false;
        normalized = ModelNormalized(static_cast<int>(number) - 1);
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE {
        return ModelIndex(normalized);
    }
    ParamValue toNormalized(ParamValue plain) const SMTG_OVERRIDE {
        return ModelNormalized(static_cast<int>(std::lround(std::clamp(plain, 0.0, 5.0))));
    }

    OBJ_METHODS(ModelParameter, Parameter)
};

// The pitch wheel, shown in semitones on the firmware's 14-bit curve.
class PitchBendParameter : public Parameter {
  public:
    PitchBendParameter(const TChar* title, ParamID tag)
    : Parameter(title, tag, STR16("st"), kDefaultPitchBend, 0, ParameterInfo::kCanAutomate) {}

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        Print(string, "%.2f", PitchBendSemitones(normalized));
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        double semitones = 0.0;
        if(!ReadTyped(string, "st", semitones))
            return false;
        normalized = PitchBendNormalized(semitones);
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE {
        return PitchBendSemitones(normalized);
    }
    ParamValue toNormalized(ParamValue semitones) const SMTG_OVERRIDE {
        return PitchBendNormalized(semitones);
    }

    OBJ_METHODS(PitchBendParameter, Parameter)
};

} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditController::initialize(context);
    if(result != kResultOk)
        return result;

    // Tune is continuous: no semitone steps, as on the Daisy kit's knob.
    parameters.addParameter(new LinearParameter(STR16("Tune"), kParamTune, "st", STR16("st"), 0.5,
                                                2.0 * kTuneRange, "%.2f", kDefaultTune));
    const struct {
        const TChar* title;
        ParamID id;
        double default_value;
    } units[] = {{STR16("Structure"), kParamStructure, kDefaultStructure},
                 {STR16("Brightness"), kParamBrightness, kDefaultBrightness},
                 {STR16("Damping"), kParamDamping, kDefaultDamping},
                 {STR16("Position"), kParamPosition, kDefaultPosition},
                 {STR16("Excitation"), kParamExcitation, kDefaultExcitation}};
    for(const auto& unit : units)
        parameters.addParameter(new LinearParameter(unit.title, unit.id, "", nullptr, 0.0, 1.0,
                                                    "%.3f", unit.default_value));
    parameters.addParameter(new ModelParameter(STR16("Model"), kParamModel));
    parameters.addParameter(new LinearParameter(STR16("Volume"), kParamVolume, "%", STR16("%"),
                                                0.0, 100.0, "%.1f", kDefaultVolume));
    // The pitch wheel arrives as a parameter, the way VST3 delivers MIDI
    // controllers. It can be automated, has no knob and is not saved.
    parameters.addParameter(new PitchBendParameter(STR16("Pitch Bend"), kParamPitchBend));
    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    SavedValues values;
    if(!ReadSavedValues(state, values))
        return kResultFalse;
    for(int i = 0; i < kSavedParamCount; ++i)
        setParamNormalized(kFirstParam + static_cast<ParamID>(i), values[i]);
    setParamNormalized(kParamPitchBend, kDefaultPitchBend);
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
       || !stream.writeInt8(editor_.keyboard ? 1 : 0))
        return kResultFalse;
    return kResultOk;
}

tresult PLUGIN_API Controller::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    // Read and check everything first; a damaged state changes nothing.
    IBStreamer stream(state, kLittleEndian);
    int32 version = 0;
    double zoom = 1.0;
    int8 keyboard = 1;
    if(!stream.readInt32(version) || version != kControllerStateVersion
       || !stream.readDouble(zoom) || !stream.readInt8(keyboard))
        return kResultFalse;
    if(!std::isfinite(zoom) || (keyboard != 0 && keyboard != 1))
        return kResultFalse;
    editor_.zoom = ClampEditorZoom(zoom); // a finite zoom outside 50..200 % is pulled in
    editor_.keyboard = keyboard != 0;
    for(const OpenEditor& open : editors_)
        open.editor->ApplySettings(editor_);
    return kResultOk;
}

bool Controller::PlayNote(int note) {
    if(note < 0 || note > 127)
        return false;
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message || !message->getAttributes())
        return false; // the host offers no messages: nothing to play through
    message->setMessageID(kNoteMessageId);
    message->getAttributes()->setInt(kNoteMessageNote, note);
    message->getAttributes()->setInt(kNoteMessageVelocity, kKeyboardVelocity);
    return sendMessage(message) == kResultOk;
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if(!name || std::strcmp(name, ViewType::kEditor) != 0)
        return nullptr;
    // The window holds a reference to this controller, so `this` outlives it.
    EditorCallbacks callbacks;
    callbacks.changed = [this](const EditorSettings& settings) { editor_ = settings; };
    // Window size and keyboard belong to the project: the host should offer to
    // save. They are not parameters; no performEdit goes out for them.
    callbacks.settled = [this] { setDirty(true); };
    callbacks.strike = [this](int note) { PlayNote(note); };
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

tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16 channel,
                                                           CtrlNumber midiControllerNumber,
                                                           ParamID& id) {
    // Event bus 0, every MIDI channel.
    if(busIndex != 0 || channel < 0 || channel > 15)
        return kResultFalse;
    switch(midiControllerNumber) {
        case kPitchBend: id = kParamPitchBend; return kResultTrue;
        case kMidiCcTune: id = kParamTune; return kResultTrue;
        case kMidiCcStructure: id = kParamStructure; return kResultTrue;
        case kMidiCcBrightness: id = kParamBrightness; return kResultTrue;
        case kMidiCcDamping: id = kParamDamping; return kResultTrue;
        case kMidiCcPosition: id = kParamPosition; return kResultTrue;
        case kMidiCcExcitation: id = kParamExcitation; return kResultTrue;
        case kMidiCcModel: id = kParamModel; return kResultTrue;
        case kMidiCcVolume: id = kParamVolume; return kResultTrue;
        default: return kResultFalse;
    }
}

FUnknown* CreateControllerInstance(void*) {
    return Controller::createInstance(nullptr);
}

} // namespace torus_classic
