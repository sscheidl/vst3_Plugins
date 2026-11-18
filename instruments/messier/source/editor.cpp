#include "editor.h"

#include "dialog_start.h"
#include "guard.h"
#include "file_drop.h"

#include "file_loader.h"
#include "map_view.h"
#include "parameters.h"
#include "version.h"
#include "widgets.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/ctextlabel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace messier {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace {

// Layout in unscaled pixels: nine columns, the pots and Volume.
constexpr CCoord kWidth = kEditorWidth;
constexpr CCoord kMargin = 20;
constexpr int kColumns = 9;
constexpr CCoord kColumnWidth = (kWidth - 2 * kMargin) / kColumns;
constexpr CCoord kFileTop = 54;
constexpr CCoord kInfoTop = 76;
// The map of the sample (0.1.4, provisional) between the file line and the knobs.
constexpr CCoord kMapTop = 102;
constexpr CCoord kMapBottom = kMapTop + kEditorMapHeight - 8;
constexpr CCoord kNameTop = 104 + kEditorMapHeight;
constexpr CCoord kKnobSize = 60;
constexpr CCoord kKnobTop = 122 + kEditorMapHeight;
constexpr CCoord kValueTop = 186 + kEditorMapHeight;
constexpr CCoord kSmallNameTop = 214 + kEditorMapHeight;
constexpr CCoord kSmallKnobSize = 40;
constexpr CCoord kSmallKnobTop = 230 + kEditorMapHeight;
constexpr CCoord kSmallValueTop = 272 + kEditorMapHeight;
constexpr CCoord kKeyboardTop = kEditorTopHeight - 4;
constexpr CCoord kKeyboardBottom = kEditorTopHeight + kKeyboardSectionHeight - 18;
constexpr CCoord kButtonWidth = 52;
constexpr CCoord kGripSize = 18; // the resize grip (0.1.11)

// After the last step of a drag, wait this long before telling the host the
// project changed. Hosts may do real work for that message.
constexpr auto kSettleDelay = std::chrono::milliseconds(400);

constexpr CColor kBackground(28, 30, 36);
constexpr CColor kTrack(64, 68, 80);
constexpr CColor kText(232, 232, 236);
constexpr CColor kDimText(150, 156, 168);
constexpr CColor kWarning(236, 172, 64);

// The rings of the knobs: Mendelejew's three colours, by what a knob acts on.
constexpr CColor kKnobWhite(226, 228, 234);    // the playhead and the output
constexpr CColor kKnobMagenta(222, 78, 162);   // the grains: how many, how long, what shape
constexpr CColor kKnobTurquoise(52, 196, 196); // the sound of the read: pitch and the mix

struct KnobSpec {
    ParamID id;
    const char* name;
    int column;
    bool small;
    bool bipolar;
    CColor ring;
};

constexpr KnobSpec kKnobs[Editor::kKnobCount] = {
    {kParamStart, "START", 0, false, false, kKnobWhite},
    {kParamSpeed, "SPEED", 1, false, true, kKnobWhite},
    {kParamSize, "SIZE", 2, false, false, kKnobWhite},
    {kParamDensity, "DENSITY", 3, false, false, kKnobMagenta},
    {kParamPitch, "PITCH", 4, false, true, kKnobTurquoise},
    {kParamOverlap, "OVERLAP", 5, false, false, kKnobMagenta},
    {kParamBlend, "BLEND", 6, false, false, kKnobTurquoise},
    {kParamWindow, "WINDOW", 7, false, false, kKnobMagenta},
    {kParamVolume, "VOLUME", 8, false, false, kKnobWhite},
    // The secondaries under their pot; none under Speed and Blend, whose
    // secondaries act on the audio input (0.2.0).
    {kParamSpray, "SPRAY", 0, true, false, kKnobWhite},
    {kParamPanRandom, "PAN RND", 2, true, false, kKnobWhite},
    {kParamDisplacement, "DISPLACE", 3, true, false, kKnobMagenta},
    {kParamPitchRandom, "PITCH RND", 4, true, false, kKnobTurquoise},
    {kParamSizeRandom, "SIZE RND", 5, true, false, kKnobMagenta},
    {kParamMask, "MASK", 7, true, false, kKnobMagenta},
};

CTextLabel* MakeLabel(const CRect& size, const char* text, CFontRef font, CColor color,
                      CHoriTxtAlign align) {
    auto* label = new CTextLabel(size, text);
    try {
        label->setFont(font);
        label->setFontColor(color);
        label->setBackColor(kTransparentCColor);
        label->setFrameColor(kTransparentCColor);
        label->setStyle(CParamDisplay::kNoFrame);
        label->setHoriAlign(align);
        label->setMouseEnabled(false);
    } catch(...) {
        label->forget(); // MS-R24: not yet the frame's
        throw;
    }
    return label;
}

} // namespace

Editor::Editor(EditController* controller, const EditorSettings& settings,
               EditorCallbacks callbacks)
: VSTGUIEditor(controller), callbacks_(std::move(callbacks)), alive_(std::make_shared<bool>(true)) {
    settings_.zoom = ClampEditorZoom(settings.zoom);
    settings_.keyboard = settings.keyboard;
    settings_.transpose = ClampKeyboardTranspose(settings.transpose);
    setRect(ScaledSize());
}

Editor::~Editor() {
    *alive_ = false; // a file dialog still open calls back into nothing
    if(frame)
        close();
}

tresult PLUGIN_API Editor::queryInterface(const TUID _iid, void** obj) {
    QUERY_INTERFACE(_iid, obj, IPlugViewContentScaleSupport::iid, IPlugViewContentScaleSupport)
    return VSTGUIEditor::queryInterface(_iid, obj);
}

void Editor::ForgetViews() {
    knobs_ = {};
    keyboard_view_ = nullptr;
    keyboard_button_ = nullptr;
    octave_down_ = nullptr;
    octave_up_ = nullptr;
    freeze_button_ = nullptr;
    file_name_ = nullptr;
    file_info_ = nullptr;
    map_view_ = nullptr;
    file_zone_ = nullptr;
    window_shape_ = nullptr;
    latch_button_ = nullptr;
    grip_ = nullptr;
    stop_button_ = nullptr;
}

tresult PLUGIN_API Editor::attached(void* parent, FIDString type) {
    tresult result = kResultFalse;
    try {
        result = VSTGUIEditor::attached(parent, type);
    } catch(...) {
        result = kOutOfMemory;
    }
    if(result == kResultOk && frame)
        return kResultOk;
    // open() failed (the SDK attaches anyway) or something after it threw: nothing
    // half built stays, and the controller forgets this window again (the SDK's
    // removed(): the timer stopped, close(), the editor detached).
    try {
        VSTGUIEditor::removed();
    } catch(...) {
    }
    return result == kResultOk ? kResultFalse : result;
}

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
    try {
        return OpenUnchecked(parent, platformType);
    } catch(...) {
        close(); // the frame and the views it owns; views not yet in it were let go
        return false;
    }
}

bool Editor::OpenUnchecked(void* parent, const PlatformType& platformType) {
    frame = new CFrame(CRect(0, 0, kWidth, EditorBaseHeight(settings_.keyboard)), this);
    frame->setBackgroundColor(kBackground);
    frame->enableTooltips(true);
    applied_zoom_ = 1.0;
    drag_ = DragConstraint{};

    const auto title_font = makeOwned<CFontDesc>("Segoe UI", 20, kBoldFace);
    const auto name_font = makeOwned<CFontDesc>("Segoe UI", 10, kBoldFace);
    const auto small_name_font = makeOwned<CFontDesc>("Segoe UI", 9, kBoldFace);
    const auto value_font = makeOwned<CFontDesc>("Segoe UI", 13);
    const auto small_value_font = makeOwned<CFontDesc>("Segoe UI", 11);
    const auto file_font = makeOwned<CFontDesc>("Segoe UI", 13, kBoldFace);
    const auto info_font = makeOwned<CFontDesc>("Segoe UI", 11);

    Add(MakeLabel(CRect(kMargin, 14, 200, 42), stringPluginName, title_font, kText, kLeftText));
    const CCoord button_left = kWidth - kMargin - kButtonWidth;
    Add(MakeLabel(CRect(kWidth / 2 - 100, 18, button_left - 152, 40),
                  stringCompanyName "  " FULL_VERSION_STR, name_font, kDimText, kRightText));
    keyboard_button_ = Add(new ToggleButton(CRect(button_left, 18, kWidth - kMargin, 40), "KEYS",
                                            settings_.keyboard,
                                            [this](bool on) { SetKeyboardVisible(on); }));
    latch_button_ = Add(new ToggleButton(CRect(button_left - 70, 18, button_left - 8, 40), "LATCH", latch_,
                                         [this](bool on) { SetLatch(on); }));
    latch_button_->setTooltipText("Latch: a struck key stays held; click it again, another key or LATCH to let go");
    {
        const bool stopped = controller && IsOn(controller->getParamNormalized(kParamStop));
        stop_button_ = Add(new ToggleButton(CRect(button_left - 140, 18, button_left - 78, 40), "STOP", stopped,
                                            [this](bool on) { SetStop(on); }));
        stop_button_->setTooltipText("Stop: pauses the sound with a short fade; press again to go on where it stopped");
    }

    // The sample: LOAD, its name, its format or what went wrong.
    auto* load = Add(new ToggleButton(CRect(kMargin, kFileTop, kMargin + 72, kFileTop + 22), "LOAD...",
                                      false, [this](bool) { ChooseFile(); }, ToggleButton::Style::Action));
    load->setTooltipText("Load a WAV file: 16 or 24 bit PCM or 32 bit float, mono or stereo");
    file_name_ = Add(MakeLabel(CRect(kMargin + 84, kFileTop, kWidth - kMargin, kFileTop + 22), "",
                               file_font, kText, kLeftText));
    file_info_ = Add(MakeLabel(CRect(kMargin + 84, kInfoTop, kWidth - kMargin, kInfoTop + 18), "",
                               info_font, kDimText, kLeftText));
    ApplyFileTexts();

    map_view_ = Add(new MapView(CRect(kMargin, kMapTop, kWidth - kMargin, kMapBottom)));
    map_view_->Show(shown_display_, shown_wave_);
    // Since 0.1.8: on the map a click or drag sets START, the wheel SPRAY, as edits the
    // host can record (begin, perform, end), as a knob would.
    {
        MapView::Edits edits;
        edits.start_begin = [this] {
            if(controller)
                controller->beginEdit(kParamStart);
        };
        edits.start = [this](double share) {
            if(!controller)
                return;
            controller->setParamNormalized(kParamStart, share);
            controller->performEdit(kParamStart, controller->getParamNormalized(kParamStart));
        };
        edits.start_end = [this] {
            if(controller)
                controller->endEdit(kParamStart);
        };
        edits.spray = [this](double step) {
            if(!controller)
                return;
            const double value = std::clamp(controller->getParamNormalized(kParamSpray) + step, 0.0, 1.0);
            controller->beginEdit(kParamSpray);
            controller->setParamNormalized(kParamSpray, value);
            controller->performEdit(kParamSpray, value);
            controller->endEdit(kParamSpray);
        };
        map_view_->SetEdits(std::move(edits));
    }
    // Drag and drop (since 0.1.8): a WAV dropped on the file line or the map loads as
    // with LOAD... . The target outlives no editor: it checks alive_ first.
    {
        const std::weak_ptr<bool> alive = alive_;
        auto drop = makeOwned<FileDropTarget>(
            [this, alive](FileDropTarget::Hover state, const std::string& reason) {
                const auto still = alive.lock();
                if(!still || !*still || !map_view_)
                    return;
                map_view_->SetDropState(state == FileDropTarget::Hover::kUsable    ? MapView::DropShown::kUsable
                                        : state == FileDropTarget::Hover::kRefused ? MapView::DropShown::kRefused
                                                                                   : MapView::DropShown::kNone,
                                        reason);
            },
            [this, alive](const std::string& path) {
                const auto still = alive.lock();
                if(still && *still && callbacks_.load)
                    callbacks_.load(path);
            });
        map_view_->setDropTarget(drop);
        file_zone_ = Add(new CView(CRect(kMargin + 84, kFileTop, kWidth - kMargin, kInfoTop + 18)));
        file_zone_->setDropTarget(drop);
        ApplyFileTexts(); // its tooltip
    }

    keyboard_view_ = Add(new KeyboardView(CRect(kMargin + 40, kKeyboardTop, kWidth - kMargin - 40,
                                            kKeyboardBottom),
                                      [this](int note) {
                                          if(callbacks_.strike)
                                              callbacks_.strike(note);
                                      },
                                      [this](int note) {
                                          if(callbacks_.release)
                                              callbacks_.release(note);
                                      }));
    keyboard_view_->SetTranspose(settings_.transpose);
    keyboard_view_->SetLatch(latch_);
    keyboard_view_->setVisible(settings_.keyboard);
    octave_down_ = Add(new ToggleButton(CRect(kMargin, kKeyboardTop + 22, kMargin + 32,
                                          kKeyboardTop + 46),
                                    "<<", false,
                                    [this](bool) { SetKeyboardTranspose(settings_.transpose - 12); },
                                    ToggleButton::Style::Action));
    octave_up_ = Add(new ToggleButton(CRect(kWidth - kMargin - 32, kKeyboardTop + 22, kWidth - kMargin,
                                        kKeyboardTop + 46),
                                  ">>", false,
                                  [this](bool) { SetKeyboardTranspose(settings_.transpose + 12); },
                                  ToggleButton::Style::Action));
    octave_down_->setTooltipText("Keyboard down one octave (lowest: C2 to C4)");
    octave_up_->setTooltipText("Keyboard up one octave (highest: C6 to C8)");

    for(std::size_t i = 0; i < knobs_.size(); ++i) {
        const KnobSpec& spec = kKnobs[i];
        const CCoord left = kMargin + spec.column * kColumnWidth;
        const CCoord size = spec.small ? kSmallKnobSize : kKnobSize;
        const CCoord top = spec.small ? kSmallKnobTop : kKnobTop;
        const CCoord name_top = spec.small ? kSmallNameTop : kNameTop;
        const CCoord value_top = spec.small ? kSmallValueTop : kValueTop;
        const CCoord knob_left = left + (kColumnWidth - size) / 2;

        Add(MakeLabel(CRect(left, name_top, left + kColumnWidth, name_top + 16), spec.name,
                      spec.small ? small_name_font : name_font, kDimText, kCenterText));
        int32_t style = CKnob::kCoronaOutline | CKnob::kCoronaDrawing;
        if(spec.bipolar)
            style |= CKnob::kCoronaFromCenter;
        auto* knob = Add(new CKnob(CRect(knob_left, top, knob_left + size, top + size), this,
                                   static_cast<int32_t>(spec.id), nullptr, nullptr, CPoint(0, 0), style));
        knob->setCoronaColor(spec.ring);
        knob->setColorShadowHandle(kTrack);
        knob->setColorHandle(kText);
        knob->setHandleLineWidth(spec.small ? 3 : 4);
        knob->setCoronaInset(spec.small ? 4 : 5);
        knob->setInsetValue(spec.small ? 10 : 15);
        knob->setDefaultValue(static_cast<float>(kDefaults[static_cast<std::size_t>(Index(spec.id))]));
        knob->setWheelInc(0.01f);
        auto* value = Add(MakeLabel(CRect(left, value_top, left + kColumnWidth, value_top + 18), "",
                                    spec.small ? small_value_font : value_font, kText, kCenterText));
        if(spec.id == kParamWindow) // the mixed window as a symbol (0.1.8)
            window_shape_ = Add(new WindowShapeView(CRect(left + (kColumnWidth - 46) / 2, value_top + 2,
                                                          left + (kColumnWidth + 46) / 2, value_top + 16)));
        knobs_[i] = {spec.id, knob, value};
        const ParamValue normalized = controller ? controller->getParamNormalized(spec.id) : 0.0;
        knob->setValue(static_cast<float>(normalized));
        ShowValue(knobs_[i], normalized);
    }

    // Freeze and Reset under Volume.
    const CCoord column = kMargin + 8 * kColumnWidth;
    const CCoord button_inset = (kColumnWidth - 60) / 2;
    const bool frozen = controller && IsOn(controller->getParamNormalized(kParamFreeze));
    freeze_button_ = Add(new ToggleButton(CRect(column + button_inset, kSmallKnobTop - 6,
                                                column + button_inset + 60, kSmallKnobTop + 16),
                                          "FREEZE", frozen, [this](bool on) { SetFreeze(on); }));
    freeze_button_->setTooltipText("Freeze: the playhead stands still; grains and the vocoder go on");
    auto* reset = Add(new ToggleButton(CRect(column + button_inset, kSmallKnobTop + 26,
                                             column + button_inset + 60, kSmallKnobTop + 48),
                                       "RESET", false, [this](bool) { PressReset(); },
                                       ToggleButton::Style::Action));
    reset->setTooltipText("Reset: playback back to START");
    // The resize grip in the lower right corner (0.1.11), added last so that it is on top;
    // LayOut() puts it into the corner of the height the keyboard leaves.
    grip_ = Add(new ResizeGrip(CRect(0, 0, kGripSize, kGripSize), kDimText, [this] { GripBegin(); },
                               [this](double dx, double dy) { GripDrag(dx, dy); }));

    if(!frame->open(parent, platformType)) {
        frame->forget();
        frame = nullptr;
        ForgetViews();
        return false;
    }
    setIdleRate(static_cast<int32>(kDisplayInterval.count()));
    LayOut(rect.getWidth(), rect.getHeight());
    Resize();
    if(callbacks_.poll)
        callbacks_.poll();
    return true;
}

ViewRect Editor::ScaledSize() const {
    const EditorSize size = EditorSizeFor(scale_ * settings_.zoom, settings_.keyboard);
    return ViewRect(0, 0, size.width, size.height);
}

double Editor::DisplayZoom(int32 width, int32 height) const {
    const ViewRect wanted = ScaledSize();
    if(width == wanted.getWidth() && height == wanted.getHeight())
        return scale_ * settings_.zoom;
    // A window the settings did not ask for: as large as fits into it, also
    // below the 50 % a user can choose, so nothing is cut off. Above 200 %
    // the content stops growing and background fills the rest.
    const double fitting =
        std::min(width / kEditorWidth, height / EditorBaseHeight(settings_.keyboard));
    return std::min(fitting, scale_ * kMaxEditorZoom);
}

void Editor::LayOut(int32 width, int32 height) {
    if(!frame)
        return;
    if(keyboard_view_)
        keyboard_view_->setVisible(settings_.keyboard);
    if(keyboard_button_)
        keyboard_button_->SetOn(settings_.keyboard);
    for(ToggleButton* octave : {octave_down_, octave_up_}) {
        if(!octave)
            continue;
        const int limit = octave == octave_down_ ? -kKeyboardTransposeLimit : kKeyboardTransposeLimit;
        octave->setVisible(settings_.keyboard);
        octave->setMouseEnabled(settings_.transpose != limit);
        octave->invalid();
    }
    if(grip_) {
        const CCoord bottom = EditorBaseHeight(settings_.keyboard);
        grip_->setViewSize(CRect(kWidth - kGripSize - 2, bottom - kGripSize - 2, kWidth - 2, bottom - 2));
        grip_->setMouseableArea(grip_->getViewSize());
        grip_->invalid();
    }
    // Zooming invalidates and repaints the whole window, so only when needed.
    const double total = DisplayZoom(width, height);
    if(total != applied_zoom_) {
        frame->setZoom(total);
        applied_zoom_ = total;
    }
    frame->setSize(width, height);
}

bool Editor::Resize() {
    if(!frame)
        return false;
    ViewRect wanted = ScaledSize();
    if(rect.getWidth() == wanted.getWidth() && rect.getHeight() == wanted.getHeight()) {
        LayOut(rect.getWidth(), rect.getHeight());
        return true;
    }
    // Only ask. The window itself changes in onSize(), once the host agrees,
    // as the VST3 interface requires. `rect` still holds the old size here:
    // hosts compare the two.
    return plugFrame && plugFrame->resizeView(this, &wanted) == kResultTrue;
}

void Editor::SetKeyboardVisible(bool show) {
    if(show == settings_.keyboard)
        return;
    const EditorSettings before = settings_;
    settings_.keyboard = show;
    if(frame && !Resize()) {
        // The host keeps the window as it is, so the keyboard stays as it was.
        settings_ = before;
        if(keyboard_button_)
            keyboard_button_->SetOn(before.keyboard);
        return;
    }
    UserChanged();
}

void Editor::SetStop(bool on) {
    if(!controller)
        return;
    const ParamValue normalized = on ? 1.0 : 0.0;
    controller->beginEdit(kParamStop);
    controller->setParamNormalized(kParamStop, normalized);
    controller->performEdit(kParamStop, normalized);
    controller->endEdit(kParamStop);
}

void Editor::SetLatch(bool on) {
    latch_ = on;
    if(keyboard_view_)
        keyboard_view_->SetLatch(on);
    if(latch_button_)
        latch_button_->SetOn(on);
}

void Editor::SetKeyboardTranspose(int semitones) {
    const int transpose = ClampKeyboardTranspose(semitones);
    if(transpose == settings_.transpose)
        return;
    if(keyboard_view_)
        keyboard_view_->SetTranspose(transpose);
    settings_.transpose = transpose;
    if(frame)
        LayOut(rect.getWidth(), rect.getHeight());
    UserChanged();
}

void Editor::GripBegin() {
    grip_start_ = {rect.getWidth(), rect.getHeight()};
}

void Editor::GripDrag(double dx, double dy) {
    // As a corner drag of the window: the allowed size nearest to the asked one, with
    // the aspect ratio and the zoom limits kept (ConstrainEditorSize).
    const EditorSize asked{static_cast<int>(std::lround(grip_start_.width + dx)),
                           static_cast<int>(std::lround(grip_start_.height + dy))};
    const EditorSize allowed = ConstrainEditorSize(asked, grip_start_, scale_, settings_.keyboard, true);
    const double zoom = EditorZoomForSize(allowed.width, allowed.height, scale_, settings_.keyboard);
    if(zoom == settings_.zoom)
        return;
    const EditorSettings before = settings_;
    settings_.zoom = zoom;
    if(frame && !Resize()) {
        settings_ = before; // the host keeps the size
        return;
    }
    UserChanged();
}

void Editor::UserChanged() {
    if(callbacks_.changed)
        callbacks_.changed(settings_);
    settle_pending_ = true;
    last_change_ = std::chrono::steady_clock::now();
}

// MS-R37: every call of the host, VSTGUI or the system into the editor is an
// exception boundary (guard.h); the window catches up on a later call.
CMessageResult Editor::notify(CBaseObject* sender, const char* message) {
    const CMessageResult result =
        Guarded(kMessageUnknown, [&] { return VSTGUIEditor::notify(sender, message); });
    if(message == CVSTGUITimer::kMsgTimer && settle_pending_
       && std::chrono::steady_clock::now() - last_change_ >= kSettleDelay) {
        settle_pending_ = false;
        Guarded([&] {
            if(callbacks_.settled)
                callbacks_.settled();
        });
    }
    // The controller asks the processor for the file status while a load runs.
    if(message == CVSTGUITimer::kMsgTimer)
        Guarded([&] {
            if(callbacks_.poll)
                callbacks_.poll();
        });
    return result;
}

tresult PLUGIN_API Editor::canResize() {
    return kResultTrue;
}

tresult PLUGIN_API Editor::checkSizeConstraint(ViewRect* size) {
    return Guarded(static_cast<tresult>(kResultFalse), [&] { return CheckSizeUnchecked(size); });
}

tresult Editor::CheckSizeUnchecked(ViewRect* size) {
    if(!size)
        return kInvalidArgument;
    const EditorSize allowed =
        drag_.Apply({size->getWidth(), size->getHeight()}, {rect.getWidth(), rect.getHeight()},
                    scale_, settings_.keyboard, DragConstraint::Clock::now());
    size->right = size->left + allowed.width;
    size->bottom = size->top + allowed.height;
    return kResultTrue;
}

tresult PLUGIN_API Editor::onSize(ViewRect* newSize) {
    return Guarded(static_cast<tresult>(kOutOfMemory), [&] { return OnSizeUnchecked(newSize); });
}

tresult Editor::OnSizeUnchecked(ViewRect* newSize) {
    if(!newSize)
        return kInvalidArgument;
    const int32 width = newSize->getWidth();
    const int32 height = newSize->getHeight();
    if(width <= 0 || height <= 0)
        return kResultFalse;
    const ViewRect current = ScaledSize();
    if(width != current.getWidth() || height != current.getHeight()) {
        // The user resized the window, or the host chose a size. The zoom to
        // remember stays within the limits; the display may go below them.
        const double zoom = EditorZoomForSize(width, height, scale_, settings_.keyboard);
        if(zoom != settings_.zoom) {
            settings_.zoom = zoom;
            UserChanged();
        }
    }
    LayOut(width, height);
    setRect(*newSize);
    return kResultOk;
}

void Editor::ApplySettings(const EditorSettings& settings) {
    const double zoom = ClampEditorZoom(settings.zoom);
    const int transpose = ClampKeyboardTranspose(settings.transpose);
    if(zoom == settings_.zoom && settings.keyboard == settings_.keyboard
       && transpose == settings_.transpose)
        return;
    settings_.zoom = zoom;
    settings_.keyboard = settings.keyboard;
    settings_.transpose = transpose;
    if(keyboard_view_)
        keyboard_view_->SetTranspose(transpose);
    // A loaded project decides. If the host keeps the window size, the
    // content fits into it as it is.
    if(frame && !Resize())
        LayOut(rect.getWidth(), rect.getHeight());
}

tresult PLUGIN_API Editor::getSize(ViewRect* size) {
    return Guarded(static_cast<tresult>(kResultFalse), [&] {
        // Before the window exists, answer with the size it will have.
        if(!frame)
            setRect(ScaledSize());
        return VSTGUIEditor::getSize(size);
    });
}

void PLUGIN_API Editor::close() {
    // Each step on its own: whatever one of them meets, the frame is let go.
    Guarded([&] { FinishEdits(); });
    ForgetViews();
    if(frame) {
        Guarded([&] { frame->removeAll(true); });
        if(frame->getNbReference() == 1)
            frame->close(); // also releases the frame
        else
            frame->forget();
        frame = nullptr;
    }
}

void Editor::FinishEdits() {
    // A change made just before closing still counts.
    Guarded([&] {
        if(settle_pending_) {
            settle_pending_ = false;
            if(callbacks_.settled)
                callbacks_.settled();
        }
    });
    // A knob still held by the mouse, or within the pause that follows a turn
    // of the wheel, has told the host that an edit began. Its view is about
    // to go without a mouse-up; finish the edit here, once, while the frame
    // can still pass it on. The same for a screen key still held: the host
    // gets its note-off. Each on its own (MS-R37).
    if(frame)
        for(const Knob& knob : knobs_)
            Guarded([&] {
                while(knob.knob && knob.knob->isEditing())
                    knob.knob->endEdit();
            });
    Guarded([&] {
        if(keyboard_view_)
            keyboard_view_->ReleaseHeld();
    });
    Guarded([&] {
        if(map_view_)
            map_view_->Release(); // a START drag still going: its edit ends here (0.1.8)
    });
}

void Editor::valueChanged(CControl* control) {
    Guarded([&] {
        if(!controller || !control)
            return;
        const auto id = static_cast<ParamID>(control->getTag());
        const ParamValue normalized = control->getValueNormalized();
        controller->setParamNormalized(id, normalized);
        controller->performEdit(id, normalized);
    });
}

void Editor::SetFreeze(bool on) {
    if(!controller)
        return;
    const ParamValue normalized = on ? 1.0 : 0.0;
    controller->beginEdit(kParamFreeze);
    controller->setParamNormalized(kParamFreeze, normalized);
    controller->performEdit(kParamFreeze, normalized);
    controller->endEdit(kParamFreeze);
}

void Editor::PressReset() {
    // MS-R08: each press goes to the processor as a message, through the controller;
    // no edit of parameter 121, which a host may merge or the window may outlive.
    if(callbacks_.reset)
        callbacks_.reset();
}

void Editor::ChooseFile() {
    if(!frame || dialog_open_)
        return;
    // MS-R39: owned from its creation, so that a failure while setting it up frees it.
    const SharedPointer<CNewFileSelector> selector(
        CNewFileSelector::create(frame, CNewFileSelector::kSelectFile), false); // owned
    if(!selector)
        return;
    selector->setTitle("Load a WAV file");
    // Since 0.1.8: start in the folder of the file playing, also one dropped onto the
    // window (Windows remembers only what was chosen through the dialog).
    {
        const std::size_t slash = file_tooltip_.find_last_of("/\\");
        if(slash != std::string::npos)
            selector->setInitialDirectory(file_tooltip_.substr(0, slash).c_str());
    }
    selector->addFileExtension(CFileExtension("WAVE", "wav"));
    // The dialog may call back after this window has gone: then nothing happens.
    std::shared_ptr<bool> alive = alive_;
    auto answered = std::make_shared<bool>(false);
    VSTGUI::CNewFileSelector::CallbackFunc answer = [this, alive, answered](CNewFileSelector* chosen) {
        *answered = true;
        if(!*alive)
            return;
        dialog_open_ = false;
        // MS-R37: making the path's string may fail; not into the dialog's caller.
        Guarded([&] {
            if(chosen && chosen->getNumSelectedFiles() > 0 && callbacks_.load)
                if(const char* path = chosen->getSelectedFile(0))
                    callbacks_.load(path);
        });
    };
    // MS-R39: if the system cannot open the dialog, LOAD... works again at once.
    // VSTGUI's run() keeps one reference for the answer it then never gives; that one
    // is given back here (and only if run() took it).
    const auto references = selector->getNbReference();
    StartDialog(
        dialog_open_, answered, [&] { return selector->run(std::move(answer)); },
        [&] {
            if(selector->getNbReference() > references)
                selector->forget();
        });
}

void Editor::ShowDisplay(const EngineDisplay& display, std::shared_ptr<const WaveOverview> wave) noexcept {
    shown_display_ = display;
    shown_wave_ = std::move(wave);
    if(map_view_)
        map_view_->Show(shown_display_, shown_wave_);
}

void Editor::ShowFileStatus(const FileStatus& status) noexcept {
    try {
        ShowFileStatusUnchecked(status);
    } catch(...) {
        // No memory for the texts (MS-R15): the window keeps what it shows.
    }
}

void Editor::ShowFileStatusUnchecked(const FileStatus& status) {
    file_warning_ = false;
    file_tooltip_ = status.path;
    if(status.path.empty()) {
        file_name_text_ = "No sample";
    } else {
        file_name_text_ = FileName(status.path);
    }
    switch(status.sound) {
        case SoundState::kNone: file_info_text_ = "Load a WAV file to play"; break;
        case SoundState::kReady: file_info_text_ = status.info; break;
        case SoundState::kMissing:
            file_info_text_ = "File not found, silent: " + status.path;
            file_warning_ = true;
            break;
        default:
            file_info_text_ = "Silent, " + status.info;
            file_warning_ = true;
            break;
    }
    if(status.request_state == RequestState::kLoading) {
        file_info_text_ = "Loading " + FileName(status.request) + " ...";
        file_warning_ = false;
    } else if(status.request_state == RequestState::kFailed) {
        file_info_text_ = "Could not load " + FileName(status.request) + ": " + status.detail;
        file_warning_ = true;
    }
    ApplyFileTexts();
}

void Editor::ApplyFileTexts() {
    if(file_name_) {
        file_name_->setText(file_name_text_.c_str());
        file_name_->setTooltipText(file_tooltip_.c_str());
    }
    if(file_zone_)
        file_zone_->setTooltipText(file_tooltip_.c_str());
    if(file_info_) {
        file_info_->setText(file_info_text_.c_str());
        file_info_->setFontColor(file_warning_ ? kWarning : kDimText);
    }
}

std::string Editor::ShownFileName() const {
    return file_name_ ? file_name_text_ : std::string();
}

std::string Editor::ShownFileInfo() const {
    return file_info_ ? file_info_text_ : std::string();
}

int Editor::ShownFreeze() const {
    if(!freeze_button_)
        return -1;
    return controller && IsOn(controller->getParamNormalized(kParamFreeze)) ? 1 : 0;
}

CKnob* Editor::KnobOf(ParamID id) const {
    for(const Knob& knob : knobs_)
        if(knob.id == id)
            return knob.knob;
    return nullptr;
}

float Editor::ShownKnobValue(ParamID id) const {
    for(const Knob& knob : knobs_)
        if(knob.id == id && knob.knob)
            return knob.knob->getValue();
    return -1.0f;
}

void Editor::ParameterChanged(ParamID id, ParamValue normalized) {
    if(id == kParamStop) {
        if(stop_button_)
            stop_button_->SetOn(IsOn(normalized));
        return;
    }
    if(id == kParamFreeze) {
        if(freeze_button_)
            freeze_button_->SetOn(IsOn(normalized));
        return;
    }
    for(Knob& knob : knobs_) {
        if(knob.id != id || !knob.knob)
            continue;
        const auto value = static_cast<float>(normalized);
        if(knob.knob->getValue() != value) {
            knob.knob->setValue(value);
            knob.knob->invalid();
        }
        ShowValue(knob, normalized);
    }
}

void Editor::ShowValue(Knob& knob, ParamValue normalized) {
    if(!controller || !knob.value)
        return;
    String128 text{};
    controller->getParamStringByValue(knob.id, normalized, text);
    std::string display = StringConvert::convert(text);
    ParameterInfo info{};
    const int32 count = controller->getParameterCount();
    for(int32 i = 0; i < count; ++i) {
        if(controller->getParameterInfo(i, info) == kResultOk && info.id == knob.id) {
            const std::string units = StringConvert::convert(info.units);
            if(!units.empty())
                display += " " + units;
            break;
        }
    }
    if(knob.id == kParamWindow && window_shape_) {
        // The curve says it; the name of the nearest shape is the knob's tooltip.
        window_shape_->SetValue(PotOf(kParamWindow, normalized));
        knob.value->setText("");
        if(knob.knob)
            knob.knob->setTooltipText(display.c_str());
        return;
    }
    knob.value->setText(display.c_str());
}

tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
    if(!(factor > 0.0f) || !std::isfinite(factor))
        return kInvalidArgument;
    scale_ = factor;
    return Guarded(static_cast<tresult>(kOutOfMemory), [&] {
        if(frame && !Resize())
            LayOut(rect.getWidth(), rect.getHeight());
        return static_cast<tresult>(kResultOk);
    });
}

} // namespace messier

