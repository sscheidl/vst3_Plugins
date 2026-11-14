#include "controller.h"

#include "editor.h"
#include "parameters.h"
#include "plugin_ids.h"
#include "state_io.h"
#include "value_text.h"
#include "view_layout.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace elements_synth {

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

// Coarse tuning: whole semitones, a stepped parameter.
class CoarseParameter : public Parameter {
  public:
    CoarseParameter(const TChar* title, ParamID tag, ParamValue default_normalized)
    : Parameter(title, tag, STR16("st"), default_normalized, kCoarseSteps,
                ParameterInfo::kCanAutomate) {}

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        Print(string, "%.0f", CoarseSemitones(normalized));
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        double semitones = 0.0;
        if(!ReadTyped(string, "st", semitones))
            return false;
        normalized = toNormalized(semitones); // clamped, then rounded
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE {
        return CoarseSemitones(normalized);
    }
    ParamValue toNormalized(ParamValue semitones) const SMTG_OVERRIDE {
        return CoarseNormalized(static_cast<int>(
            std::lround(std::clamp(semitones, -static_cast<double>(kCoarseRange),
                                   static_cast<double>(kCoarseRange)))));
    }

    OBJ_METHODS(CoarseParameter, Parameter)
};

// A switch: "Off" and "On".
class SwitchParameter : public Parameter {
  public:
    SwitchParameter(const TChar* title, ParamID tag, ParamValue default_normalized)
    : Parameter(title, tag, nullptr, default_normalized, 1,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsList) {}

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        UString(string, 128).fromAscii(IsOn(normalized) ? "On" : "Off");
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        if(!string)
            return false;
        const std::string text = StringConvert::convert(string);
        const std::string_view word = StripUnit(text, "");
        if(EqualsIgnoringCase(word, "on") || word == "1")
            normalized = 1.0;
        else if(EqualsIgnoringCase(word, "off") || word == "0")
            normalized = 0.0;
        else
            return false;
        return true;
    }

    ParamValue toPlain(ParamValue normalized) const SMTG_OVERRIDE {
        return IsOn(normalized) ? 1.0 : 0.0;
    }
    ParamValue toNormalized(ParamValue plain) const SMTG_OVERRIDE { return plain >= 0.5 ? 1.0 : 0.0; }

    OBJ_METHODS(SwitchParameter, Parameter)
};

// The pitch wheel, shown in semitones.
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

    // In the order of the IDs. The values from 0 to 1 are the hardware's pots.
    const struct {
        const TChar* title;
        ParamID id;
    } pots[] = {{STR16("Contour"), kParamContour},
                {STR16("Bow"), kParamBowLevel},
                {STR16("Bow Timbre"), kParamBowTimbre},
                {STR16("Blow"), kParamBlowLevel},
                {STR16("Flow"), kParamBlowFlow},
                {STR16("Blow Timbre"), kParamBlowTimbre},
                {STR16("Strike"), kParamStrikeLevel},
                {STR16("Mallet"), kParamStrikeMallet},
                {STR16("Strike Timbre"), kParamStrikeTimbre}};
    for(const auto& pot : pots)
        parameters.addParameter(new LinearParameter(pot.title, pot.id, "", nullptr, 0.0, 1.0,
                                                    "%.3f", DefaultOf(Index(static_cast<ParamId>(pot.id)))));
    parameters.addParameter(
        new CoarseParameter(STR16("Coarse"), kParamCoarse, DefaultOf(Index(kParamCoarse))));
    parameters.addParameter(new LinearParameter(STR16("Fine"), kParamFine, "st", STR16("st"), 0.5,
                                                2.0 * kFineRange, "%.2f",
                                                DefaultOf(Index(kParamFine))));
    const struct {
        const TChar* title;
        ParamID id;
    } more[] = {{STR16("Geometry"), kParamGeometry},
                {STR16("Brightness"), kParamBrightness},
                {STR16("Damping"), kParamDamping},
                {STR16("Position"), kParamPosition},
                {STR16("Space"), kParamSpace}};
    for(const auto& pot : more)
        parameters.addParameter(new LinearParameter(pot.title, pot.id, "", nullptr, 0.0, 1.0,
                                                    "%.3f", DefaultOf(Index(static_cast<ParamId>(pot.id)))));
    parameters.addParameter(
        new SwitchParameter(STR16("Freeze"), kParamFreeze, DefaultOf(Index(kParamFreeze))));
    parameters.addParameter(new LinearParameter(STR16("Velocity"), kParamVelocity, "%",
                                                STR16("%"), 0.0, 100.0, "%.1f",
                                                DefaultOf(Index(kParamVelocity))));
    parameters.addParameter(new LinearParameter(STR16("Volume"), kParamVolume, "%", STR16("%"),
                                                0.0, 100.0, "%.1f", DefaultOf(Index(kParamVolume))));
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
        setParamNormalized(kFirstParam + static_cast<ParamID>(kSavedIndex[static_cast<std::size_t>(i)]),
                           values[static_cast<std::size_t>(i)]);
    setParamNormalized(kParamPitchBend, kDefaultPitchBend);
    return kResultOk;
}

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value) {
    const tresult result = EditController::setParamNormalized(tag, value);
    if(result != kResultOk)
        return result;
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
    return SendKey(note, kKeyboardVelocity);
}

bool Controller::ReleaseNote(int note) {
    return SendKey(note, 0);
}

bool Controller::SendKey(int note, int velocity) {
    if(note < 0 || note > 127)
        return false;
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message || !message->getAttributes())
        return false; // the host offers no messages: nothing to play through
    message->setMessageID(kNoteMessageId);
    message->getAttributes()->setInt(kNoteMessageNote, note);
    message->getAttributes()->setInt(kNoteMessageVelocity, velocity);
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
    callbacks.release = [this](int note) { ReleaseNote(note); };
    return new Editor(this, editor_, status_, std::move(callbacks));
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
        case kMidiCcGeometry: id = kParamGeometry; return kResultTrue;
        case kMidiCcBrightness: id = kParamBrightness; return kResultTrue;
        case kMidiCcDamping: id = kParamDamping; return kResultTrue;
        case kMidiCcPosition: id = kParamPosition; return kResultTrue;
        case kMidiCcVolume: id = kParamVolume; return kResultTrue;
        case kMidiCcSpace: id = kParamSpace; return kResultTrue;
        default: return kResultFalse;
    }
}

tresult PLUGIN_API Controller::notify(IMessage* message) {
    if(!message || !message->getMessageID()
       || std::strcmp(message->getMessageID(), kStatusMessageId) != 0)
        return EditController::notify(message);
    IAttributeList* attributes = message->getAttributes();
    double host_rate = 0.0, engine_rate = 0.0;
    int64 latency = 0;
    if(!attributes || attributes->getFloat(kStatusHostRate, host_rate) != kResultOk
       || attributes->getFloat(kStatusEngineRate, engine_rate) != kResultOk
       || attributes->getInt(kStatusLatency, latency) != kResultOk)
        return kInvalidArgument;
    if(!std::isfinite(host_rate) || !std::isfinite(engine_rate) || host_rate <= 0.0
       || engine_rate <= 0.0 || latency < 0)
        return kInvalidArgument;
    status_.known = true;
    status_.host_rate = host_rate;
    status_.engine_rate = engine_rate;
    status_.latency = static_cast<int>(latency);
    for(const OpenEditor& open : editors_)
        open.editor->ShowStatus(status_);
    return kResultOk;
}

FUnknown* CreateControllerInstance(void*) {
    return Controller::createInstance(nullptr);
}

} // namespace elements_synth
