#include "controller.h"

#include "editor.h"
#include "message_text.h"
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
#include <atomic>
#include <chrono>
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

    // MS-R11: hosts call these through getParamStringByValue and
    // getParamValueByString, also without a window; nothing may leave them.
    void toString(ParamValue normalized, String128 string) const SMTG_OVERRIDE {
        try {
            Put(string, FormatValue(getInfo().id, normalized));
        } catch(...) {
            string[0] = 0; // no memory for the text: an empty one
        }
    }

    bool fromString(const TChar* string, ParamValue& normalized) const SMTG_OVERRIDE {
        if(!string)
            return false;
        try {
            return ParseValue(getInfo().id, StringConvert::convert(string), normalized);
        } catch(...) {
            return false;
        }
    }

    OBJ_METHODS(DisplayParameter, Parameter)
};

// MS-R28: connection generations of all controllers of this process, so that a
// processor can tell a newer connection from an older one whichever controller made it.
std::atomic<int64> g_generations{0};

} // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    generation_ = ++g_generations;
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

tresult PLUGIN_API Controller::connect(IConnectionPoint* other) {
    const tresult result = EditController::connect(other);
    if(result != kResultOk)
        return result;
    // MS-R17, MS-R21, MS-R28: the processor at the other end may be a new one, whose
    // sequence numbers start anew. A new generation: replies still on their way from
    // the old connection are not taken, and the processor refuses requests of the old
    // one that reach it late.
    generation_ = ++g_generations;
    last_status_serial_ = 0;
    last_reply_ = 0;
    // Parameter 199 as a processor starts it (never 0 after a load), so that a new
    // processor's first value is a change. Nothing is counted here.
    EditController::setParamNormalized(kParamSampleChanges, 0.0);
    ForgetDisplay();
    // MS-R31: the processor let go of the keys of the old connection (E5, 0.2.9).
    ForEachEditor([](Editor& editor) { editor.ForgetHeldKey(); });
    RequestFileStatus();
    return kResultOk;
}

void Controller::ForgetDisplay() noexcept {
    display_ = EngineDisplay{};
    wave_ = nullptr;
    ShowDisplay();
}

void Controller::AddParameters() {
    const struct {
        const TChar* title;
        ParamID id;
    } pots[] = {{STR16("Start"), kParamStart},     {STR16("Speed"), kParamSpeed},
                {STR16("Loop End"), kParamSize},   {STR16("Density"), kParamDensity}, // round 5: was "Size"
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
    // Since 0.1.8: the host's bypass (it fades the output out; saved) and STOP, a pause
    // of the sound with a fade (not saved).
    parameters.addParameter(new DisplayParameter(STR16("Bypass"), kParamBypass, 1,
                                                 ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass));
    parameters.addParameter(new DisplayParameter(STR16("Stop"), kParamStop, 1,
                                                 ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
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
    SetValue(kParamStop, 0.0); // a project always opens playing
    // The processor has read the file by now; it says how that went. Moving the
    // path allocates nothing.
    file_.path = std::move(saved.path);
    file_.sound = file_.path.empty() ? SoundState::kNone : SoundState::kReady;
    file_.info.clear();
    ShowFile();
    // The processor restarts without notes: no screen key is held any more (E5, 0.2.9).
    ForEachEditor([](Editor& editor) { editor.ForgetHeldKey(); });
    RequestFileStatus();
    return kResultOk;
}

void Controller::SetValue(ParamID tag, ParamValue value) noexcept {
    if(EditController::setParamNormalized(tag, value) == kResultOk)
        ShowParameter(tag);
}

void Controller::ShowParameter(ParamID tag) noexcept {
    // Out of memory while drawing a text: the value is set, the window is late.
    ForEachEditor([&](Editor& editor) { editor.ParameterChanged(tag, getParamNormalized(tag)); });
}

void Controller::ForEachEditor(const std::function<void(Editor&)>& call) noexcept {
    std::vector<IPtr<Editor>> held;
    try {
        held.reserve(editors_.size());
        for(const OpenEditor& open : editors_)
            held.emplace_back(open.editor); // a reference of our own
    } catch(...) {
        return;
    }
    for(const IPtr<Editor>& editor : held) {
        const bool registered = std::any_of(editors_.begin(), editors_.end(),
                                            [&](const OpenEditor& open) { return open.editor == editor.get(); });
        if(!registered)
            continue; // closed by an earlier call
        try {
            call(*editor);
        } catch(...) {
        }
    }
}

tresult PLUGIN_API Controller::setParamNormalized(ParamID tag, ParamValue value) {
    const ParamValue before = getParamNormalized(tag);
    const tresult result = EditController::setParamNormalized(tag, value);
    if(result != kResultOk)
        return result;
    ShowParameter(tag);
    // MS-R09, MS-R12: the processor reports files from the window that became the
    // sound. The count in the value tells the host the project changed, whether or
    // not a status reply follows; the status then says which file it is.
    if(tag == kParamSampleChanges && getParamNormalized(tag) != before) {
        MarkChanged(); // MS-R26: every change marks; nothing is subtracted later
        RequestFileStatus();
    }
    return result;
}

tresult PLUGIN_API Controller::getParamStringByValue(ParamID tag, ParamValue valueNormalized,
                                                     String128 string) {
    try {
        return EditController::getParamStringByValue(tag, valueNormalized, string);
    } catch(...) {
        return kOutOfMemory; // MS-R11
    }
}

tresult PLUGIN_API Controller::getParamValueByString(ParamID tag, TChar* string,
                                                     ParamValue& valueNormalized) {
    try {
        return EditController::getParamValueByString(tag, string, valueNormalized);
    } catch(...) {
        return kOutOfMemory; // MS-R11
    }
}

tresult PLUGIN_API Controller::getState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    if(!stream.writeInt32(kControllerStateVersion) || !stream.writeDouble(editor_.zoom)
       || !stream.writeInt8(editor_.keyboard ? 1 : 0) || !stream.writeInt32(editor_.transpose)
       || !stream.writeInt32(static_cast<int32>(editor_.preset)) || !stream.writeInt8(editor_.details ? 1 : 0)
       || !stream.writeInt32(static_cast<int32>(editor_.accents.size())))
        return kResultFalse;
    for(const int32 accent : editor_.accents)
        if(!stream.writeInt32(accent))
            return kResultFalse;
    // Version 8 (beta 2): GALAXY or WAVE.
    return stream.writeInt32(editor_.wave_percent) && stream.writeInt8(editor_.galaxy ? 1 : 0) ? kResultOk
                                                                                                 : kResultFalse;
}

tresult PLUGIN_API Controller::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    int32 version = 0;
    double zoom = 1.0;
    int8 keyboard = 1;
    int32 transpose = kKeyboardDefaultTranspose;
    int32 preset = static_cast<int32>(GuiPreset::kSpiral); // version 1 (before 0.2.0)
    int8 details = 0;                                     // versions 1 and 2 (before 0.2.4)
    EditorSettings settings;
    if(!stream.readInt32(version) || version < 1 || version > kControllerStateVersion
       || !stream.readDouble(zoom) || !stream.readInt8(keyboard) || !stream.readInt32(transpose)
       || (version >= 2 && !stream.readInt32(preset)) || (version >= 3 && !stream.readInt8(details)))
        return kResultFalse;
    // Every field in its range, or the whole state is refused (MS-R62: the zoom too, which
    // was clamped before).
    if(!std::isfinite(zoom) || zoom < kMinEditorZoom || zoom > kMaxEditorZoom || (keyboard != 0 && keyboard != 1) || (details != 0 && details != 1) || transpose % 12 != 0
       || transpose < kKeyboardLowestTranspose || transpose > kKeyboardHighestTranspose || !IsGuiPreset(preset))
        return kResultFalse;
    if(version >= 4) {
        // Up to V8 one hue for each of the four presets of then; V9 its count first. Hues of
        // presets this version does not know are read and checked, not kept; presets the
        // state has none for keep their own colours.
        int32 count = kLegacyGuiPresetCount;
        if(version >= 9 && (!stream.readInt32(count) || count < 0 || count > kMaxStoredAccents))
            return kResultFalse;
        for(int32 i = 0; i < count; ++i) {
            int32 accent = kPresetAccent;
            if(!stream.readInt32(accent) || !IsAccent(accent))
                return kResultFalse;
            if(i < kGuiPresetCount)
                settings.accents[static_cast<std::size_t>(i)] = accent;
        }
    }
    int32 trail_ms = kTrailDefaultMs; // version 5 only; the trail is fixed since 0.2.6
    if(version == 5 && (!stream.readInt32(trail_ms) || trail_ms < kTrailMinMs || trail_ms > kTrailMaxMs))
        return kResultFalse;
    if(version >= 6) {
        if(!stream.readInt32(settings.wave_percent))
            return kResultFalse;
        const bool uses_preset = version >= 7 && settings.wave_percent == kPresetWave;
        if(!uses_preset && (settings.wave_percent < kWaveMinPercent || settings.wave_percent > kWaveMaxPercent))
            return kResultFalse;
    }
    if(version >= 8) { // beta 2; before it, a project opens in GALAXY
        int8 galaxy = 1;
        if(!stream.readInt8(galaxy) || (galaxy != 0 && galaxy != 1))
            return kResultFalse;
        settings.galaxy = galaxy != 0;
    }
    settings.zoom = zoom;
    settings.keyboard = keyboard != 0;
    settings.transpose = transpose;
    settings.preset = static_cast<GuiPreset>(preset);
    settings.details = details != 0;
    editor_ = settings;
    // The settings are taken; an open window shows them when it can.
    ShareSettings(settings, nullptr);
    return kResultOk;
}

void Controller::ShareSettings(const EditorSettings& settings, const Editor* except) noexcept {
    if(sharing_settings_)
        return; // GD.1 (0.2.10): a window answering the round; its settings are kept (editor_)
    sharing_settings_ = true;
    ForEachEditor([&](Editor& editor) {
        if(&editor != except)
            editor.ApplySettings(settings);
    });
    sharing_settings_ = false;
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
    IPtr<IMessage> message = NewMessage(kNoteMessageId);
    if(!message)
        return false;
    message->getAttributes()->setInt(kNoteMessageNote, note);
    message->getAttributes()->setInt(kNoteMessageVelocity, velocity);
    return sendMessage(message) == kResultOk;
}

IPtr<IMessage> Controller::NewMessage(const char* id) {
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message || !message->getAttributes())
        return nullptr;
    message->setMessageID(id);
    message->getAttributes()->setInt(kAttrGeneration, generation_);
    message->getAttributes()->setInt(kAttrIssued, static_cast<int64>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return message;
}

bool Controller::LoadFile(const std::string& path) {
    try {
        const bool sent = SendLoad(path);
        if(sent) {
            awaiting_ = path; // the window asks until this request has finished
            await_ticks_ = kAwaitTicks;
        }
        return sent;
    } catch(...) {
        return false; // MS-R01: not into the window's callback
    }
}

bool Controller::SendLoad(const std::string& path) {
    if(path.empty() || path.size() > static_cast<std::size_t>(kMaxPathBytes) || !IsValidUtf8(path))
        return false;
    IPtr<IMessage> message = NewMessage(kLoadMessageId);
    if(!message)
        return false;
    SetText(message->getAttributes(), kAttrPath, path);
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
        IPtr<IMessage> message = NewMessage(id);
        if(!message)
            return false;
        return sendMessage(message) == kResultOk;
    } catch(...) {
        return false;
    }
}

bool Controller::RequestDisplay() noexcept {
    try {
        IPtr<IMessage> message = NewMessage(kDisplayRequestId);
        if(!message)
            return false;
        // The overview the controller holds, so the processor sends one only when the
        // file changed.
        message->getAttributes()->setInt(kAttrHaveWave,
                                         wave_ ? static_cast<int64>(wave_->sequence) : 0);
        return sendMessage(message) == kResultOk;
    } catch(...) {
        return false;
    }
}

tresult Controller::ReadDisplay(IMessage* message) noexcept {
    // MS-R23: a host may make the attribute list on first access; without memory the
    // reply is dropped and the last valid display stays.
    try {
        return ReadDisplayUnchecked(message);
    } catch(...) {
        return kOutOfMemory;
    }
}

tresult Controller::ReadDisplayUnchecked(IMessage* message) {
    IAttributeList* attributes = message->getAttributes();
    if(!attributes)
        return kInvalidArgument;
    int64 generation = 0, reply = 0;
    if(attributes->getInt(kAttrGeneration, generation) != kResultOk
       || attributes->getInt(kAttrReply, reply) != kResultOk || reply <= 0)
        return kInvalidArgument;
    // MS-R21: a reply of an earlier connection, or one overtaken by a newer reply of
    // this one, is not taken.
    if(generation != generation_ || static_cast<std::uint64_t>(reply) <= last_reply_)
        return kResultFalse;
    const void* data = nullptr;
    uint32 size = 0;
    EngineDisplay display;
    if(attributes->getBinary(kAttrFrame, data, size) != kResultOk
       || !ReadDisplayBytes(data, size, display))
        return kInvalidArgument;
    std::shared_ptr<const WaveOverview> wave;
    if(attributes->getBinary(kAttrWave, data, size) == kResultOk)
        wave = ReadOverviewBytes(data, size, display);
    // Everything read: taken together.
    last_reply_ = static_cast<std::uint64_t>(reply);
    display_ = display;
    if(wave)
        wave_ = std::move(wave);
    ShowDisplay();
    return kResultOk;
}

void Controller::ShowDisplay() noexcept {
    ForEachEditor([&](Editor& editor) { editor.ShowDisplay(display_, ShownWave()); });
}

tresult PLUGIN_API Controller::notify(IMessage* message) {
    if(message && message->getMessageID()
       && std::strcmp(message->getMessageID(), kDisplayMessageId) == 0)
        return ReadDisplay(message);
    if(!message || !message->getMessageID()
       || std::strcmp(message->getMessageID(), kStatusMessageId) != 0)
        return EditController::notify(message);
    try {
        IAttributeList* attributes = message->getAttributes();
        FileStatus status;
        int64 code = 0, serial = 0, user_loads = 0, processor = 0;
        // The four texts may be empty, and an empty one may be missing (0.2.7).
        if(!attributes || !GetOptionalText(attributes, kAttrPath, status.path)
           || !GetOptionalText(attributes, kAttrInfo, status.info)
           || !GetOptionalText(attributes, kAttrRequest, status.request)
           || !GetOptionalText(attributes, kAttrDetail, status.detail)
           || attributes->getInt(kAttrState, code) != kResultOk
           || attributes->getInt(kAttrSerial, serial) != kResultOk
           || attributes->getInt(kAttrUserLoads, user_loads) != kResultOk
           || attributes->getInt(kAttrProcessor, processor) != kResultOk || processor == 0
           || user_loads < 0 || user_loads > 0xFFFFFFFFll
           || serial < 0 || serial > 0xFFFFFFFFll // MS-R59: not cut to 32 bits
           || !FileStatus::IsCode(code))
            return kInvalidArgument;
        status.SetCode(static_cast<int>(code));
        if(!status.HasItsTexts()) // MS-R58: a text lost on the way
            return kInvalidArgument;
        // MS-R20, MS-R21: only a status of the current connection, and not one older
        // than the newest taken (the loader's serial grows with every change).
        int64 generation = 0;
        if(attributes->getInt(kAttrGeneration, generation) != kResultOk)
            return kInvalidArgument;
        if(generation != generation_ || static_cast<std::uint32_t>(serial) < last_status_serial_)
            return kResultFalse;
        last_status_serial_ = static_cast<std::uint32_t>(serial);
        status.serial = static_cast<std::uint32_t>(serial);
        status.user_loads = static_cast<std::uint32_t>(user_loads);
        // MS-R04, MS-R09: files from the window became the sound. The project's saved
        // state has changed though no parameter did; tell the host once for each such
        // load since the last status. Restoring a project and repeated status replies
        // leave the count alone.
        const std::uint32_t loads = status.user_loads;
        if(await_ticks_ > 0 && status.request == awaiting_ && status.request_state != RequestState::kLoading)
            await_ticks_ = 0; // that request has finished, loaded or failed
        file_ = std::move(status);
        CountFromStatus(static_cast<std::uint64_t>(processor), loads);
        ShowFile();
        return kResultOk;
    } catch(...) {
        return kOutOfMemory;
    }
}

void Controller::CountFromStatus(std::uint64_t processor, std::uint32_t count) noexcept {
    // MS-R17, MS-R27, MS-R32: a count higher than the last one seen of the same
    // processor means new files; a processor never seen has new files if its count is
    // above zero. A lower or equal count is an old reply or nothing new. The counts of
    // the last kKnownProcessors processors are kept, so A -> B -> A marks nothing.
    if(processor != status_from_) {
        if(status_from_ != 0)
            ForgetDisplay(); // another processor: its sequence numbers are its own
        status_from_ = processor;
    }
    Known* entry = nullptr;
    for(Known& k : known_)
        if(k.processor == processor)
            entry = &k;
    bool more = false;
    if(entry) {
        more = count > entry->loads;
        if(more)
            entry->loads = count;
    } else {
        more = count > 0;
        known_[known_next_] = Known{processor, count};
        known_next_ = (known_next_ + 1) % kKnownProcessors;
    }
    if(more)
        MarkChanged();
}

void Controller::MarkChanged() noexcept {
    setDirty(true);
}

void Controller::ShowFile() noexcept {
    // The window shows the status when it next can.
    ForEachEditor([&](Editor& editor) { editor.ShowFileStatus(file_); });
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
    // F3 (0.2.9): a change in one window reaches the others of this instance; a closed
    // view takes the settings when it opens again (callbacks.current).
    auto self = std::make_shared<const Editor*>(nullptr);
    callbacks.changed = [this, self](const EditorSettings& settings) {
        editor_ = settings;
        ShareSettings(settings, *self);
    };
    callbacks.current = [this] { return editor_; };
    callbacks.settled = [this] { setDirty(true); };
    callbacks.strike = [this](int note) { PlayNote(note); };
    callbacks.release = [this](int note) { ReleaseNote(note); };
    callbacks.load = [this](const std::string& path) { LoadFile(path); };
    callbacks.reset = [this] { PressReset(); };
    // While a file is being read, the window asks how it goes on every timer tick.
    // On every tick it asks for what it shows (since 0.1.4).
    callbacks.poll = [this] {
        // Several windows of this instance together ask no more often than one (Codex'
        // note on 0.2.1): every display goes to all of them.
        const auto now = std::chrono::steady_clock::now();
        if(now - last_poll_ < kPollSpacing)
            return;
        last_poll_ = now;
        ++poll_ticks_;
        if(await_ticks_ > 0)
            --await_ticks_;
        if(file_.request_state == RequestState::kLoading || file_.serial == 0 || await_ticks_ > 0
           || poll_ticks_ % kStatusEveryTicks == 0)
            RequestFileStatus();
        RequestDisplay();
    };
    auto* editor = new Editor(this, editor_, std::move(callbacks));
    *self = editor;
    editor->ShowFileStatus(file_); // never throws (MS-R15)
    editor->ShowDisplay(display_, ShownWave()); // never throws
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
