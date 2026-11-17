#include "controller.h"

#include "editor.h"
#include "parameters.h"
#include "plugin_ids.h"
#include "state_io.h"
#include "value_display.h"
#include "view_layout.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace messier {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// UTF-8 into a host string.
void Put(String128 string, const std::string& text) {
    const std::u16string wide = StringConvert::convert(text);
    const std::size_t count = std::min<std::size_t>(wide.size(), 127);
    for(std::size_t i = 0; i < count; ++i)
        string[i] = static_cast<TChar>(wide[i]);
    string[count] = 0;
}

// A parameter shown and typed through value_display.h: the value the firmware's
// law makes of the pot, in its unit.
class DisplayParameter : public Parameter {
  public:
    DisplayParameter(const TChar* title, ParamID tag, int32 steps, int32 flags)
    : Parameter(title, tag, nullptr, kDefaults[static_cast<std::size_t>(Index(tag))], steps, flags) {
        UString(info.units, str16BufferSize(String128)).fromAscii(UnitOf(tag));
    }

    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        Put(string, FormatValue(getInfo().id, normalized));
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        if(!string)
            return false;
        return ParseValue(getInfo().id, StringConvert::convert(string), normalized);
    }

    OBJ_METHODS(DisplayParameter, Parameter)
};

bool GetText(IAttributeList* attributes, const char* id, std::string& text) {
    const void* data = nullptr;
    uint32 size = 0;
    if(!attributes || attributes->getBinary(id, data, size) != kResultOk)
        return false;
    text.assign(static_cast<const char*>(data), size);
    return true;
}

} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditController::initialize(context);
    if(result != kResultOk)
        return result;
    const struct {
        const TChar* title;
        ParamID id;
    } pots[] = {{STR16("Start"), kParamStart},     {STR16("Speed"), kParamSpeed},
                {STR16("Size"), kParamSize},       {STR16("Density"), kParamDensity},
                {STR16("Pitch"), kParamPitch},     {STR16("Overlap"), kParamOverlap},
                {STR16("Blend"), kParamBlend},     {STR16("Window"), kParamWindow},
                {STR16("Spray"), kParamSpray},     {STR16("Pan Random"), kParamPanRandom},
                {STR16("Displacement"), kParamDisplacement},
                {STR16("Pitch Random"), kParamPitchRandom},
                {STR16("Size Random"), kParamSizeRandom}, {STR16("Mask"), kParamMask}};
    for(const auto& pot : pots)
        parameters.addParameter(new DisplayParameter(pot.title, pot.id, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(new DisplayParameter(STR16("Freeze"), kParamFreeze, 1,
                                                 ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
    // A button: the press acts. Not saved with the project.
    parameters.addParameter(new DisplayParameter(STR16("Reset"), kParamReset, 1, ParameterInfo::kCanAutomate));
    parameters.addParameter(new DisplayParameter(STR16("Volume"), kParamVolume, 0, ParameterInfo::kCanAutomate));
    // The pitch wheel arrives as a parameter, the way VST3 delivers MIDI
    // controllers. It can be automated, has no knob and is not saved.
    parameters.addParameter(new DisplayParameter(STR16("Pitch Bend"), kParamPitchBend, 0,
                                                 ParameterInfo::kCanAutomate));
    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    SavedState saved;
    if(!ReadSavedState(state, saved))
        return kResultFalse;
    for(int i = 0; i < kSavedParamCount; ++i)
        setParamNormalized(kParamIds[static_cast<std::size_t>(kSavedIndex[static_cast<std::size_t>(i)])],
                           saved.values[static_cast<std::size_t>(i)]);
    setParamNormalized(kParamReset, 0.0);
    setParamNormalized(kParamPitchBend, kDefaultPitchBend);
    // The processor has read the file by now; it says how that went.
    file_.path = saved.path;
    file_.sound = saved.path.empty() ? SoundState::kNone : SoundState::kReady;
    file_.info.clear();
    ShowFile();
    RequestFileStatus();
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
       || !stream.writeInt8(editor_.keyboard ? 1 : 0) || !stream.writeInt32(editor_.transpose))
        return kResultFalse;
    return kResultOk;
}

tresult PLUGIN_API Controller::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    int32 version = 0;
    double zoom = 1.0;
    int8 keyboard = 1;
    int32 transpose = kKeyboardDefaultTranspose;
    if(!stream.readInt32(version) || version != kControllerStateVersion || !stream.readDouble(zoom)
       || !stream.readInt8(keyboard) || !stream.readInt32(transpose))
        return kResultFalse;
    if(!std::isfinite(zoom) || (keyboard != 0 && keyboard != 1) || transpose % 12 != 0
       || transpose < -kKeyboardTransposeLimit || transpose > kKeyboardTransposeLimit)
        return kResultFalse;
    editor_.zoom = ClampEditorZoom(zoom);
    editor_.keyboard = keyboard != 0;
    editor_.transpose = transpose;
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
        return false;
    message->setMessageID(kNoteMessageId);
    message->getAttributes()->setInt(kNoteMessageNote, note);
    message->getAttributes()->setInt(kNoteMessageVelocity, velocity);
    return sendMessage(message) == kResultOk;
}

bool Controller::LoadFile(const std::string& path) {
    if(path.empty() || path.size() > static_cast<std::size_t>(kMaxPathBytes) || !IsValidUtf8(path))
        return false;
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message || !message->getAttributes())
        return false;
    message->setMessageID(kLoadMessageId);
    message->getAttributes()->setBinary(kAttrPath, path.data(), static_cast<uint32>(path.size()));
    // The processor answers with its status at once ("loading").
    return sendMessage(message) == kResultOk;
}

bool Controller::RequestFileStatus() {
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message)
        return false;
    message->setMessageID(kStatusRequestId);
    return sendMessage(message) == kResultOk;
}

tresult PLUGIN_API Controller::notify(IMessage* message) {
    if(!message || !message->getMessageID()
       || std::strcmp(message->getMessageID(), kStatusMessageId) != 0)
        return EditController::notify(message);
    IAttributeList* attributes = message->getAttributes();
    FileStatus status;
    int64 code = 0, serial = 0;
    if(!attributes || !GetText(attributes, kAttrPath, status.path)
       || !GetText(attributes, kAttrInfo, status.info) || !GetText(attributes, kAttrRequest, status.request)
       || !GetText(attributes, kAttrDetail, status.detail)
       || attributes->getInt(kAttrState, code) != kResultOk
       || attributes->getInt(kAttrSerial, serial) != kResultOk)
        return kInvalidArgument;
    status.SetCode(static_cast<int>(code));
    status.serial = static_cast<std::uint32_t>(serial);
    file_ = std::move(status);
    ShowFile();
    return kResultOk;
}

void Controller::ShowFile() {
    for(const OpenEditor& open : editors_)
        open.editor->ShowFileStatus(file_);
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if(!name || std::strcmp(name, ViewType::kEditor) != 0)
        return nullptr;
    EditorCallbacks callbacks;
    callbacks.changed = [this](const EditorSettings& settings) { editor_ = settings; };
    callbacks.settled = [this] { setDirty(true); };
    callbacks.strike = [this](int note) { PlayNote(note); };
    callbacks.release = [this](int note) { ReleaseNote(note); };
    callbacks.load = [this](const std::string& path) { LoadFile(path); };
    // While a file is being read, the window asks how it goes, ten times a second.
    callbacks.poll = [this] {
        if(file_.request_state == RequestState::kLoading || file_.serial == 0)
            RequestFileStatus();
    };
    auto* editor = new Editor(this, editor_, std::move(callbacks));
    editor->ShowFileStatus(file_);
    return editor;
}

void Controller::editorAttached(EditorView* editor) {
    auto* own = dynamic_cast<Editor*>(editor);
    if(!own)
        return;
    for(const OpenEditor& open : editors_)
        if(open.view == editor)
            return;
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
    if(busIndex != 0 || channel < 0 || channel > 15)
        return kResultFalse;
    if(midiControllerNumber == kPitchBend) {
        id = kParamPitchBend;
        return kResultTrue;
    }
    if(midiControllerNumber >= kMidiCcFirstPot && midiControllerNumber < kMidiCcFirstPot + kPotCount) {
        id = kFirstPrimary + static_cast<ParamID>(midiControllerNumber - kMidiCcFirstPot);
        return kResultTrue;
    }
    if(midiControllerNumber == kMidiCcVolume) {
        id = kParamVolume;
        return kResultTrue;
    }
    return kResultFalse;
}

FUnknown* CreateControllerInstance(void*) {
    return Controller::createInstance(nullptr);
}

} // namespace messier
