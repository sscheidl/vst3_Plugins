#include "editor.h"

#include "dialog_start.h"
#include "guard.h"
#include "file_drop.h"

#include "file_loader.h"
#include "map_view.h"
#include "panel_view.h"
#include "parameters.h"
#include "version.h"
#include "widgets.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/tasks.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace messier {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;
namespace L = layout;

namespace {

constexpr CCoord kWidth = kEditorWidth;

// After the last step of a drag, wait this long before telling the host the
// project changed. Hosts may do real work for that message.
constexpr auto kSettleDelay = std::chrono::milliseconds(400);

struct KnobSpec {
    ParamID id;
    const char* name;
    int column;
    bool small;
    bool bipolar;
};

// The columns of the window study, version 3: START is the slider under the plate. Since
// 0.2.2 (the user's design round) SPRAY, its secondary, is a large knob at the head of the
// upper row, with LATCH and KEYS under it. The other secondaries stand under their pot;
// the places of the secondaries of Speed and Blend, which act on the audio input, stay
// empty until 0.3.0.
constexpr KnobSpec kKnobs[Editor::kKnobCount] = {
    {kParamSpeed, "SPEED", 1, false, true},
    {kParamSize, "LOOP SIZE", 2, false, false},
    {kParamDensity, "DENSITY", 3, false, false},
    {kParamPitch, "PITCH", 4, false, true},
    {kParamOverlap, "OVERLAP", 5, false, false},
    {kParamWindow, "WINDOW", 6, false, false},
    {kParamBlend, "BLEND", 7, false, false},
    {kParamVolume, "VOLUME", 8, false, false},
    {kParamSpray, "SPRAY", 0, false, false},
    {kParamPanRandom, "PAN RND", 2, true, false},
    {kParamDisplacement, "DISPLACE", 3, true, false},
    {kParamPitchRandom, "PITCH RND", 4, true, false},
    {kParamSizeRandom, "SIZE RND", 5, true, false},
    {kParamMask, "MASK", 6, true, false},
};

CRect Box(double left, double top, double right, double bottom) {
    return CRect(left, top, right, bottom);
}

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
    settings_.preset = IsGuiPreset(static_cast<std::int32_t>(settings.preset)) ? settings.preset : GuiPreset::kSlate;
    setRect(ScaledSize());
}

Editor::~Editor() {
    *alive_ = false; // a file dialog still open calls back into nothing
    // MS-R44: out of the controller's list before anything here can reach the host; the
    // list's copy would otherwise hold an object that is being destroyed. (EditorView
    // tells the controller again, later; that finds nothing.)
    if(controller)
        Guarded([&] { controller->editorDestroyed(this); });
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
    panel_ = nullptr;
    size_button_ = nullptr;
    preset_button_ = nullptr;
    catalogue_ = nullptr;
    catalogue_shown_ = ~std::uint64_t(0);
    skin_.reset();
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
    frame->setBackgroundColor(kBlackCColor);
    frame->enableTooltips(true);
    applied_zoom_ = 1.0;
    drag_ = DragConstraint{};
    ++window_serial_;
    skin_ = std::make_shared<const Skin>(settings_.preset);
    const Skin& skin = *skin_;
    const SkinColours& colours = skin.Colours();

    // The background first: chassis, rules, the fixed texts.
    panel_ = Add(new PanelView(CRect(0, 0, kWidth, EditorBaseHeight(settings_.keyboard))));

    // The header: STOP and the size and preset menus, as one group (0.2.2).
    {
        const bool stopped = controller && IsOn(controller->getParamNormalized(kParamStop));
        stop_button_ = Add(new ToggleButton(Box(L::kStopLeft, L::kButtonTop, L::kStopRight, L::kButtonTop + L::kButtonHeight),
                                            "STOP", stopped, [this](bool on) { SetStop(on); }));
        stop_button_->setTooltipText("Stop: pauses the sound with a short fade; press again to go on where it stopped");
    }
    size_button_ = Add(new ToggleButton(Box(L::kSizeLeft, L::kButtonTop, L::kSizeRight, L::kButtonTop + L::kButtonHeight),
                                        "", false, [this](bool) { ShowSizeMenu(); }, ToggleButton::Style::Action));
    size_button_->SetIcon(ToggleButton::Icon::kSize);
    size_button_->setTooltipText("Window size");
    preset_button_ = Add(new ToggleButton(Box(L::kPresetLeft, L::kButtonTop, L::kPresetRight, L::kButtonTop + L::kButtonHeight),
                                          "", false, [this](bool) { ShowPresetMenu(); }, ToggleButton::Style::Action));
    preset_button_->SetIcon(ToggleButton::Icon::kPreset);
    preset_button_->setTooltipText("GUI preset: the look of the window");

    // The sample: LOAD, its name, its format or what went wrong, its catalogue number.
    auto* load = Add(new ToggleButton(Box(L::kLoadLeft, L::kLoadTop, L::kLoadRight, L::kLoadBottom), "LOAD...",
                                      false, [this](bool) { ChooseFile(); }, ToggleButton::Style::Action));
    load->setTooltipText("Load a WAV file: 16 or 24 bit PCM or 32 bit float, mono or stereo");
    load->SetSkin(skin_);
    file_name_ = Add(MakeLabel(Box(L::kFileLeft, L::kFileNameTop, L::kFileRight, L::kFileNameBottom), "",
                               skin.Italic(19), colours.text, kLeftText));
    file_info_ = Add(MakeLabel(Box(L::kFileLeft, L::kFileNameBottom, L::kFileRight, L::kFileInfoBottom), "",
                               skin.Mono(11), colours.dim, kLeftText));
    catalogue_ = Add(MakeLabel(Box(L::kFileRight, 52, kWidth - 24, 76), "", skin.Caps(15), colours.value,
                               kRightText));
    catalogue_->setTooltipText("Catalogue number: the how-manyth file this instance has loaded");
    ApplyFileTexts();

    map_view_ = Add(new MapView(Box(L::kMapLeft, L::kMapTop, L::kMapRight, L::kMapBottom), L::kBezelEdge));
    map_view_->Show(shown_display_, shown_wave_);
    if(controller) {
        map_view_->SetStart(controller->getParamNormalized(kParamStart));
        map_view_->SetSize(controller->getParamNormalized(kParamSize));
        map_view_->SetBlend(controller->getParamNormalized(kParamBlend));
        map_view_->SetPitch(PitchOctaves(controller->getParamNormalized(kParamPitch)));
        map_view_->SetStopped(IsOn(controller->getParamNormalized(kParamStop)));
    }
    // Since 0.1.8: on the map a click or drag sets START, the wheel SPRAY, as edits the
    // host can record (begin, perform, end), as a knob would. Since 0.2.0 the map holds
    // the START slider too.
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
        edits.freeze_off = [this] { SetFreeze(false); }; // a click on the snowflake (0.2.1)
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
        file_zone_ = Add(new CView(Box(L::kFileLeft, L::kFileNameTop, L::kFileRight, L::kFileInfoBottom)));
        file_zone_->setDropTarget(drop);
        ApplyFileTexts(); // its tooltip
    }

    // The keyboard section: the octave keys and the keys.
    keyboard_view_ = Add(new KeyboardView(Box(L::kKeyboardLeft, L::kPianoTop, L::kKeyboardLeft + L::kKeyboardWidth,
                                              L::kPianoTop + L::kPianoHeight),
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
    // The octave keys (0.2.2): as large as LATCH, KEYS, FREEZE and RESET, labelled.
    const CCoord keys_right = L::kKeyboardLeft + L::kKeyboardWidth;
    const CCoord down_left = L::kKeyboardLeft - L::kOctaveGap - L::kButtonWidth;
    octave_down_ = Add(new ToggleButton(Box(down_left, L::kKeyButtonTop, down_left + L::kButtonWidth,
                                            L::kKeyButtonTop + L::kButtonHeight),
                                        "\xE2\x88\x92 OCT", false,
                                        [this](bool) { SetKeyboardTranspose(settings_.transpose - 12); },
                                        ToggleButton::Style::Action));
    octave_up_ = Add(new ToggleButton(Box(keys_right + L::kOctaveGap, L::kKeyButtonTop,
                                          keys_right + L::kOctaveGap + L::kButtonWidth,
                                          L::kKeyButtonTop + L::kButtonHeight),
                                      "OCT +", false,
                                      [this](bool) { SetKeyboardTranspose(settings_.transpose + 12); },
                                      ToggleButton::Style::Action));
    octave_down_->SetValueInk(true);
    octave_up_->SetValueInk(true);
    octave_down_->setTooltipText("Keyboard down one octave (lowest: C2 to C4)");
    octave_up_->setTooltipText("Keyboard up one octave (highest: C6 to C8)");

    // The knobs, their names (on the panel) and values.
    const CFontRef value_font = skin.Mono(12.5);
    const CFontRef small_value_font = skin.Mono(11.5);
    for(std::size_t i = 0; i < knobs_.size(); ++i) {
        const KnobSpec& spec = kKnobs[i];
        const CCoord left = L::ColumnLeft(spec.column);
        const CCoord right = left + L::kColumnWidth;
        const CCoord size = spec.small ? L::kSmallKnobSize : L::kKnobSize;
        const CCoord top = spec.small ? L::kSmallKnobTop : L::kKnobTop;
        const CCoord name_top = spec.small ? L::kSmallNameTop : L::kNameTop;
        const CCoord value_top = spec.small ? L::kSmallValueTop : L::kValueTop;
        const CCoord knob_left = left + (L::kColumnWidth - size) / 2;

        panel_->AddLabel(Box(left, name_top, right, name_top + (spec.small ? 14 : 16)), spec.name,
                         spec.small ? PanelView::LabelKind::kSmallKnob : PanelView::LabelKind::kKnob);
        auto* knob = Add(new MetalKnob(Box(knob_left, top, knob_left + size, top + size), this,
                                       static_cast<int32_t>(spec.id), spec.small, spec.bipolar));
        knob->setDefaultValue(static_cast<float>(kDefaults[static_cast<std::size_t>(Index(spec.id))]));
        knob->setWheelInc(0.01f);
        auto* value = Add(MakeLabel(Box(left, value_top, right, value_top + (spec.small ? 14 : 16)), "",
                                    spec.small ? small_value_font : value_font, colours.value, kCenterText));
        if(spec.id == kParamWindow) // the mixed window as a symbol (0.1.8)
            window_shape_ = Add(new WindowShapeView(Box(left + (L::kColumnWidth - 46) / 2, value_top + 1,
                                                        left + (L::kColumnWidth + 46) / 2, value_top + 15)));
        knobs_[i] = {spec.id, knob, value};
        const ParamValue normalized = controller ? controller->getParamNormalized(spec.id) : 0.0;
        knob->setValue(static_cast<float>(normalized));
        ShowValue(knobs_[i], normalized);
    }
    // LATCH and KEYS under SPRAY (0.2.2), as FREEZE and RESET stand under Volume. LATCH
    // stays in its place while the keyboard is hidden, dimmed and without the mouse.
    const CCoord first = L::ColumnLeft(0) + (L::kColumnWidth - L::kButtonWidth) / 2;
    latch_button_ = Add(new ToggleButton(Box(first, L::kLatchTop, first + L::kButtonWidth,
                                             L::kLatchTop + L::kButtonHeight),
                                         "LATCH", latch_, [this](bool on) { SetLatch(on); }));
    latch_button_->setTooltipText("Latch: a struck key stays held; click it again, another key or LATCH to let go");
    keyboard_button_ = Add(new ToggleButton(Box(first, L::kKeysTop, first + L::kButtonWidth,
                                                L::kKeysTop + L::kButtonHeight),
                                            "KEYS", settings_.keyboard, [this](bool on) { SetKeyboardVisible(on); }));
    keyboard_button_->setTooltipText("Keys: shows or hides the keyboard");

    // Freeze and Reset under Volume.
    const CCoord column = L::ColumnLeft(8) + (L::kColumnWidth - L::kButtonWidth) / 2;
    const bool frozen = controller && IsOn(controller->getParamNormalized(kParamFreeze));
    freeze_button_ = Add(new ToggleButton(Box(column, L::kFreezeTop, column + L::kButtonWidth,
                                              L::kFreezeTop + L::kButtonHeight),
                                          "FREEZE", frozen, [this](bool on) { SetFreeze(on); }));
    freeze_button_->setTooltipText("Freeze: the playhead stands still; grains and the vocoder go on");
    auto* reset = Add(new ToggleButton(Box(column, L::kResetTop, column + L::kButtonWidth,
                                           L::kResetTop + L::kButtonHeight),
                                       "RESET", false, [this](bool) { PressReset(); },
                                       ToggleButton::Style::Action));
    reset->setTooltipText("Reset: all knobs and START to their defaults, Freeze off, playback back to START");
    // The resize grip in the lower right corner (0.1.11), added last so that it is on top;
    // LayOut() puts it into the corner of the height the keyboard leaves.
    grip_ = Add(new ResizeGrip(Box(0, 0, L::kGripSize, L::kGripSize), colours.dim, [this] { GripBegin(); },
                               [this](double dx, double dy) { GripDrag(dx, dy); }));

    ApplySkin();
    if(!frame->open(parent, platformType)) {
        frame->forget();
        frame = nullptr;
        ForgetViews();
        return false;
    }
    setIdleRate(static_cast<int32>(kDisplayInterval.count()));
    LayOut(rect.getWidth(), rect.getHeight());
    Resize();
    // MS-R48: the first request goes the delayed way too, after the SDK has finished
    // opening the window; a reply may make the host close it.
    AnimateLater();
    return true;
}

void Editor::ApplySkin() noexcept {
    if(!frame || !skin_)
        return;
    const SkinColours colours = skin_->Colours();
    // Every view of the frame that draws with the skin gets it; each on its own (MS-R41).
    Guarded([&] {
        frame->forEachChild([&](CView* view) {
            Guarded([&] {
                if(auto* button = dynamic_cast<ToggleButton*>(view))
                    button->SetSkin(skin_);
                else if(auto* knob = dynamic_cast<MetalKnob*>(view))
                    knob->SetSkin(skin_);
            });
        });
    });
    Guarded([&] {
        if(panel_)
            panel_->SetSkin(skin_);
    });
    if(map_view_)
        map_view_->SetSkin(skin_);
    Guarded([&] {
        if(keyboard_view_)
            keyboard_view_->SetSkin(skin_);
    });
    Guarded([&] {
        if(window_shape_)
            window_shape_->SetColour(colours.control);
    });
    Guarded([&] {
        if(grip_)
            grip_->SetColour(colours.dim);
    });
    for(Knob& knob : knobs_)
        Guarded([&] {
            if(knob.value)
                knob.value->setFontColor(colours.value);
        });
    Guarded([&] {
        if(file_name_)
            file_name_->setFontColor(colours.text);
    });
    Guarded([&] {
        if(catalogue_)
            catalogue_->setFontColor(colours.value);
    });
    Guarded([&] { ApplyFileInfoColour(); }); // on its own, not behind the texts (MS-R41)
    Guarded([&] { frame->invalid(); });
}

void Editor::Animate() noexcept {
    const auto now = std::chrono::steady_clock::now();
    // MS-R44: only an open window asks the processor.
    if(frame && now - last_poll_ >= kDisplayInterval * 4 / 5) {
        // The timer comes every kDisplayInterval; edits in between do not ask more often.
        last_poll_ = now;
        const std::weak_ptr<bool> alive = alive_;
        Guarded([&] {
            if(callbacks_.poll)
                callbacks_.poll();
        });
        // The request may have reached the host, which may have closed or let go of
        // this window on the way.
        const auto still = alive.lock();
        if(!still || !*still)
            return;
    }
    if(map_view_)
        map_view_->Tick();
}

void Editor::AnimateLater() noexcept {
    if(map_view_)
        map_view_->Tick(); // draws only; no call leaves the window
    if(!frame || animate_window_ == window_serial_)
        return;
    Guarded([&] {
        const std::weak_ptr<bool> alive = alive_;
        const std::uint64_t window = window_serial_;
        Tasks::schedule(Tasks::mainQueue(), [this, alive, window] {
            // A task cannot be taken back: it checks that this editor and this window
            // are still there.
            const auto still = alive.lock();
            if(!still || !*still)
                return;
            if(animate_window_ == window)
                animate_window_ = 0;
            if(frame && window == window_serial_)
                Animate();
        });
        animate_window_ = window;
    });
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
    const CCoord bottom = EditorBaseHeight(settings_.keyboard);
    if(panel_) {
        panel_->setViewSize(CRect(0, 0, kWidth, bottom));
        panel_->SetKeyboard(settings_.keyboard);
    }
    if(keyboard_view_)
        keyboard_view_->setVisible(settings_.keyboard);
    if(keyboard_button_)
        keyboard_button_->SetOn(settings_.keyboard);
    if(latch_button_) {
        // 0.2.2: in its place without the keyboard, dimmed and without the mouse.
        latch_button_->setMouseEnabled(settings_.keyboard);
        latch_button_->invalid();
    }
    for(ToggleButton* octave : {octave_down_, octave_up_}) {
        if(!octave)
            continue;
        const int limit = octave == octave_down_ ? -kKeyboardTransposeLimit : kKeyboardTransposeLimit;
        octave->setVisible(settings_.keyboard);
        octave->setMouseEnabled(settings_.transpose != limit);
        octave->invalid();
    }
    if(grip_) {
        grip_->setViewSize(CRect(kWidth - L::kGripSize - 2, bottom - L::kGripSize - 2, kWidth - 2, bottom - 2));
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

void Editor::SetZoom(double zoom) {
    zoom = ClampEditorZoom(zoom);
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

void Editor::SetGuiPreset(GuiPreset preset) {
    if(!IsGuiPreset(static_cast<std::int32_t>(preset)))
        return;
    // The same preset again only redraws if its look is not what the window shows (a
    // failed change before, MS-R41).
    if(preset == settings_.preset && (!frame || (skin_ && skin_->Preset() == ShownPreset(preset))))
        return;
    if(frame) {
        // The new look first: if it cannot be made, nothing changes. Then every view gets
        // it; only then the setting (MS-R41).
        auto skin = std::make_shared<const Skin>(preset);
        skin_ = std::move(skin);
        ApplySkin();
    }
    settings_.preset = preset;
    UserChanged();
}

SharedPointer<COptionMenu> MakePresetMenu(GuiPreset current) {
    auto menu = makeOwned<COptionMenu>(CRect(), nullptr, -1, nullptr, nullptr, COptionMenu::kMultipleCheckStyle);
    for(std::int32_t i = 0; i < kGuiPresetCount; ++i) {
        const GuiPresetInfo& info = PresetInfo(static_cast<GuiPreset>(i));
        std::string title = info.name;
        int32_t flags = CMenuItem::kNoFlags;
        if(!info.delivered) {
            title += "  (bitmaps to come)";
            flags |= CMenuItem::kDisabled;
        }
        if(static_cast<GuiPreset>(i) == current)
            flags |= CMenuItem::kChecked;
        menu->addEntry(UTF8String(title), -1, flags);
    }
    return menu;
}

SharedPointer<COptionMenu> MakeSizeMenu(double zoom) {
    auto menu = makeOwned<COptionMenu>(CRect(), nullptr, -1, nullptr, nullptr, COptionMenu::kMultipleCheckStyle);
    for(const double size : kMenuZooms) {
        char title[16];
        std::snprintf(title, sizeof title, "%d %%", static_cast<int>(std::lround(size * 100)));
        const bool current = std::abs(size - zoom) < 0.005;
        menu->addEntry(UTF8String(title), -1, current ? CMenuItem::kChecked : CMenuItem::kNoFlags);
    }
    return menu;
}

bool PopUpMenu(CFrame* frame, COptionMenu* menu, CPoint where, std::function<void(int32_t)> chosen) {
    if(!frame || !menu)
        return false;
    // A reference of our own for the whole call: the frame adds the menu as a child and
    // removes it again only in the answer.
    const SharedPointer<COptionMenu> hold(menu);
    auto answered = std::make_shared<bool>(false);
    auto take_out = [&] {
        // A child of the frame, also if popup stopped half way through adding it.
        if(!*answered && frame->isChild(menu, false))
            Guarded([&] { frame->removeView(menu, false); });
    };
    bool shown = false;
    try {
        shown = menu->popup(frame, where, [answered, chosen](COptionMenu* answer) {
            *answered = true;
            Guarded([&] {
                if(chosen)
                    chosen(answer ? answer->getLastResult() : -1);
            });
        });
    } catch(...) {
        take_out();
        throw;
    }
    take_out(); // shown without an answer: the platform could not make the menu
    return shown && *answered;
}

void Editor::ShowPresetMenu() {
    if(!frame || !preset_button_)
        return;
    const CRect at = preset_button_->getViewSize();
    const std::weak_ptr<bool> alive = alive_;
    const std::uint64_t window = window_serial_;
    PopUpMenu(frame, MakePresetMenu(settings_.preset), CPoint(at.left, at.bottom), [this, alive, window](int32_t index) {
        const auto still = alive.lock();
        if(!still || !*still || !frame || window != window_serial_)
            return; // the editor or that window is gone
        if(IsGuiPreset(index) && PresetInfo(static_cast<GuiPreset>(index)).delivered)
            SetGuiPreset(static_cast<GuiPreset>(index));
    });
}

void Editor::ShowSizeMenu() {
    if(!frame || !size_button_)
        return;
    const CRect at = size_button_->getViewSize();
    const std::weak_ptr<bool> alive = alive_;
    const std::uint64_t window = window_serial_;
    PopUpMenu(frame, MakeSizeMenu(settings_.zoom), CPoint(at.left, at.bottom), [this, alive, window](int32_t index) {
        const auto still = alive.lock();
        if(!still || !*still || !frame || window != window_serial_)
            return;
        if(index >= 0 && index < static_cast<int32_t>(std::size(kMenuZooms)))
            SetZoom(kMenuZooms[index]);
    });
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
        Animate();
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
    const GuiPreset preset =
        IsGuiPreset(static_cast<std::int32_t>(settings.preset)) ? settings.preset : GuiPreset::kSlate;
    if(preset != settings_.preset) {
        // A loaded project brings its look. If it cannot be made, the old one stays on
        // screen, the setting is the project's, and the rest below is taken all the same
        // (MS-R41); choosing the preset again retries.
        settings_.preset = preset;
        if(frame)
            Guarded([&] {
                auto skin = std::make_shared<const Skin>(preset);
                skin_ = std::move(skin);
                ApplySkin();
            });
    }
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
    Animate(); // 0.2.1: the plate moves on while the host holds back the timer
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
    // 0.2.1 (the user's wish): every knob and the START slider back to its default,
    // Freeze off, as edits the host records.
    if(controller) {
        auto to_default = [&](ParamID id) {
            const ParamValue value = kDefaults[static_cast<std::size_t>(Index(id))];
            if(controller->getParamNormalized(id) == value)
                return;
            controller->beginEdit(id);
            controller->setParamNormalized(id, value);
            controller->performEdit(id, controller->getParamNormalized(id));
            controller->endEdit(id);
        };
        to_default(kParamStart);
        for(const KnobSpec& spec : kKnobs)
            to_default(spec.id);
        if(IsOn(controller->getParamNormalized(kParamFreeze)))
            SetFreeze(false);
    }
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
    // The catalogue number of the file the core reads (0.2.0), only when it changes.
    const std::uint64_t number = display.frames > 0 ? display.sequence : 0;
    if(catalogue_ && number != catalogue_shown_) {
        Guarded([&] {
            char text[32] = "";
            if(number > 0)
                std::snprintf(text, sizeof text, "No. %llu", static_cast<unsigned long long>(number));
            catalogue_->setText(text);
            catalogue_->invalid();
            catalogue_shown_ = number;
        });
    }
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
        Guarded([&] { file_name_->setText(file_name_text_.c_str()); });
        Guarded([&] { file_name_->setTooltipText(file_tooltip_.c_str()); });
        Guarded([&] { file_name_->invalid(); });
    }
    if(file_zone_)
        Guarded([&] { file_zone_->setTooltipText(file_tooltip_.c_str()); });
    if(file_info_)
        Guarded([&] { file_info_->setText(file_info_text_.c_str()); });
    Guarded([&] { ApplyFileInfoColour(); });
}

void Editor::ApplyFileInfoColour() {
    if(!file_info_)
        return;
    if(skin_)
        file_info_->setFontColor(file_warning_ ? skin_->Colours().warning : skin_->Colours().dim);
    file_info_->invalid();
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
        if(map_view_)
            map_view_->SetStopped(IsOn(normalized)); // the nebula's drift comes to rest
        return;
    }
    if(id == kParamFreeze) {
        if(freeze_button_)
            freeze_button_->SetOn(IsOn(normalized));
        return;
    }
    if(id == kParamStart && map_view_)
        map_view_->SetStart(normalized); // the slider under the plate (0.2.0)
    if(id == kParamSize && map_view_)
        map_view_->SetSize(normalized); // the loop at once (0.2.1)
    if(id == kParamBlend && map_view_)
        map_view_->SetBlend(normalized);
    if(id == kParamPitch && map_view_)
        map_view_->SetPitch(PitchOctaves(normalized)); // the grains' tint (0.2.3)
    // 0.2.1: also while the host automates and holds back the timer; MS-R44: not from
    // inside the controller's call.
    AnimateLater();
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
    // 0.2.1: drawn now. setText only marks the label for the window's timer, which some
    // hosts hold back while a knob is dragged.
    knob.value->invalid();
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
