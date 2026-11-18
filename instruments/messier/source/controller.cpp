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
    try {
        AddParameters();
    } catch(...) {
        return kOutOfMemory; // MS-R06: nothing leaves a host call as an exception
    }
    return kResultOk;
}

void Controller::AddParameters() {
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
    // Since 0.1.2 (MS-R09): the processor's report that a file from the window has
    // become the sound. Read-only and hidden; not for automation.
    parameters.addParameter(new DisplayParameter(STR16("Sample Changes"), kParamSampleChanges, 0,
                                                 ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden));
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    // MS-R06: the whole call is guarded. A project that cannot be read is refused; a
    // window that cannot show it does not make it unreadable.
    SavedState saved;
    try {
        if(!ReadSavedState(state, saved))
            return kResultFalse;
    } catch(...) {
        return kOutOfMemory;
    }
    for(int i = 0; i < kSavedParamCount; ++i)
        SetValue(kParamIds[static_cast<std::size_t>(kSavedIndex[static_cast<std::size_t>(i)])],
                 saved.values[static_cast<std::size_t>(i)]);
    SetValue(kParamReset, 0.0);
    SetValue(kParamPitchBend, kDefaultPitchBend);
    // The processor has read the file by now; it says how that went. Moving the
    // path allocates nothing.
    file_.path = std::move(saved.path);
    file_.sound = file_.path.empty() ? SoundState::kNone : SoundState::kReady;
    file_.info.clear();
    ShowFile();
    RequestFileStatus();
    return kResultOk;
}

void Controller::SetValue(ParamID tag, ParamValue value) noexcept {
    if(EditController::setParamNormalized(tag, value) == kResultOk)
        ShowParameter(tag);
}

void Controller::ShowParameter(ParamID tag) noexcept {
    try {
        for(const OpenEditor& open : editors_)
            open.editor->ParameterChanged(tag, getParamNormalized(tag));
    } catch(...) {
        // Out of memory while drawing a text: the value is set, the window is late.
    }
}

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value) {
    const ParamValue before = getParamNormalized(tag);
    const tresult result = EditController::setParamNormalized(tag, value);
    if(result != kResultOk)
        return result;
    ShowParameter(tag);
    // MS-R09: the processor reports a file from the window that became the sound;
    // the status says which, and that tells the host the project changed.
    if(tag == kParamSampleChanges && getParamNormalized(tag) != before)
        RequestFileStatus();
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
    try {
        for(const OpenEditor& open : editors_)
            open.editor->ApplySettings(editor_);
    } catch(...) {
        // The settings are taken; an open window shows them when it can.
    }
    return kResultOk;
}

bool Controller::PlayNote(int note) {
    return SendKey(note, kKeyboardVelocity);
}

bool Controller::ReleaseNote(int note) {
    return SendKey(note, 0);
}

bool Controller::SendKey(int note, int velocity) noexcept {
    try {
        return SendKeyUnchecked(note, velocity);
    } catch(...) {
        return false;
    }
}

bool Controller::SendKeyUnchecked(int note, int velocity) {
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
    try {
        return SendLoad(path);
    } catch(...) {
        return false; // MS-R01: not into the window's callback
    }
}

bool Controller::SendLoad(const std::string& path) {
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

bool Controller::RequestFileStatus() noexcept {
    return SendSimple(kStatusRequestId);
}

bool Controller::PressReset() noexcept {
    // MS-R08: each press as a message; the processor acts on every press that comes.
    return SendSimple(kResetMessageId);
}

bool Controller::SendSimple(const char* id) noexcept {
    try {
        IPtr<IMessage> message = owned(allocateMessage());
        if(!message)
            return false;
        message->setMessageID(id);
        return sendMessage(message) == kResultOk;
    } catch(...) {
        return false;
    }
}

tresult PLUGIN_API Controller::notify(IMessage* message) {
    if(!message || !message->getMessageID()
       || std::strcmp(message->getMessageID(), kStatusMessageId) != 0)
        return EditController::notify(message);
    try {
        IAttributeList* attributes = message->getAttributes();
        FileStatus status;
        int64 code = 0, serial = 0, user_loads = 0;
        if(!attributes || !GetText(attributes, kAttrPath, status.path)
           || !GetText(attributes, kAttrInfo, status.info) || !GetText(attributes, kAttrRequest, status.request)
           || !GetText(attributes, kAttrDetail, status.detail)
           || attributes->getInt(kAttrState, code) != kResultOk
           || attributes->getInt(kAttrSerial, serial) != kResultOk
           || attributes->getInt(kAttrUserLoads, user_loads) != kResultOk)
            return kInvalidArgument;
        status.SetCode(static_cast<int>(code));
        status.serial = static_cast<std::uint32_t>(serial);
        status.user_loads = static_cast<std::uint32_t>(user_loads);
        // MS-R04, MS-R09: files from the window became the sound. The project's saved
        // state has changed though no parameter did; tell the host once for each such
        // load since the last status. Restoring a project and repeated status replies
        // leave the count alone.
        const std::uint32_t added =
            status.user_loads > file_.user_loads ? status.user_loads - file_.user_loads : 0;
        file_ = std::move(status);
        for(std::uint32_t i = 0; i < added && i < 1024u; ++i)
            setDirty(true);
        ShowFile();
        return kResultOk;
    } catch(...) {
        return kOutOfMemory;
    }
}

void Controller::ShowFile() noexcept {
    try {
        for(const OpenEditor& open : editors_)
            open.editor->ShowFileStatus(file_);
    } catch(...) {
        // The window shows the status when it next can.
    }
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if(!name || std::strcmp(name, ViewType::kEditor) != 0)
        return nullptr;
    try {
        return MakeEditor();
    } catch(...) {
        return nullptr;
    }
}

Editor* Controller::MakeEditor() {
    EditorCallbacks callbacks;
    callbacks.changed = [this](const EditorSettings& settings) { editor_ = settings; };
    callbacks.settled = [this] { setDirty(true); };
    callbacks.strike = [this](int note) { PlayNote(note); };
    callbacks.release = [this](int note) { ReleaseNote(note); };
    callbacks.load = [this](const std::string& path) { LoadFile(path); };
    callbacks.reset = [this] { PressReset(); };
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
    try {
        editors_.push_back({editor, own});
    } catch(...) {
        // Without memory for the list this window gets no updates.
    }
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
