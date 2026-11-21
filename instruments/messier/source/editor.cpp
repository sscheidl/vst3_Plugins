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
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/tasks.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/cslider.h"
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
// Beta 2: the nebula variants, in the menu that chose the GUI presets; round 3: the painted
// galaxies first.
constexpr std::array kPresetMenuOrder{GuiPreset::kBlueGalaxy, GuiPreset::kOrangeGalaxy, GuiPreset::kRedGalaxy,
                                      GuiPreset::kVioletGalaxy, GuiPreset::kSpiral,     GuiPreset::kBarred,
                                      GuiPreset::kRing,         GuiPreset::kCloud};
static_assert(kPresetMenuOrder.size() == kGuiPresetCount);

// After the last step of a drag, wait this long before telling the host the
// project changed. Hosts may do real work for that message.
constexpr auto kSettleDelay = std::chrono::milliseconds(400);
constexpr int32_t kAccentTag = -2;
constexpr int32_t kWaveTag = -3;
constexpr int32_t kWaveSpan = kWaveMaxPercent - kWaveMinPercent;

constexpr float WaveNormalized(int32_t percent) {
    return static_cast<float>(percent - kWaveMinPercent) / kWaveSpan;
}

constexpr int32_t ClampWaveSetting(int32_t percent) {
    return percent == kPresetWave ? kPresetWave : std::clamp(percent, kWaveMinPercent, kWaveMaxPercent);
}

class AccentSlider : public CSlider {
    static constexpr double kTrackInset = 4.0;

  public:
    AccentSlider(const CRect& rect, IControlListener* listener, std::function<void()> restore)
    // VSTGUI uses a one-pixel handle even without a bitmap.
    : CSlider(rect, listener, kAccentTag, CPoint(kTrackInset + 0.5, 0),
              int32_t(rect.getWidth() - 2 * kTrackInset + 1), nullptr, nullptr),
      restore_(std::move(restore)) {
        setSliderMode(CSliderMode::FreeClick);
        setWheelInc(1.0f / (360 - kAccentBlueHue));
        setTooltipText("Colour of the nebula and the START bar: red, violet, blue. Double-click: the preset's colour.");
    }

    CMouseEventResult onMouseDown(CPoint& point, const CButtonState& buttons) override {
        return Guarded(kMouseEventHandled, [&] {
            if(buttons.isLeftButton() && buttons.isDoubleClick()) {
                restore_();
                return kMouseEventHandled;
            }
            return CSlider::onMouseDown(point, buttons);
        });
    }

    // C6 (0.2.9): VSTGUI's default-value gesture (Ctrl+click) restores the preset's
    // value as the double-click does, instead of the slider's default 0.5.
    void dispatchEvent(Event& event) override {
        if(event.type == EventType::MouseDown
           && CControl::CheckDefaultValueEventFunc(this, castMouseDownEvent(event))) {
            auto& down = castMouseDownEvent(event);
            Guarded([&] { restore_(); });
            down.consumed = true;
            down.ignoreFollowUpMoveAndUpEvents(true);
            return;
        }
        CSlider::dispatchEvent(event);
    }

    void draw(CDrawContext* context) override {
        Guarded([&] {
            const CRect area = getViewSize();
            const double y = area.getCenter().y;
            const double left = area.left + kTrackInset, right = area.right - kTrackInset;
            const double x = left + (right - left) * getValueNormalized();
            context->setDrawMode(kAntiAliasing);
            // Beta 2 (the user's wish): neutral, not a rainbow; the colour shows on the nebula.
            context->setFillColor(CColor(0, 0, 0, 120));
            context->drawRect(CRect(left, y - 2, right, y + 2), kDrawFilled);
            context->setFillColor(CColor(200, 200, 206, 140));
            context->drawRect(CRect(left, y - 1, right, y + 1), kDrawFilled);
            const CRect thumb(x - 4, y - 6, x + 4, y + 6);
            context->setFillColor(CColor(232, 232, 236));
            context->setFrameColor(kBlackCColor);
            context->setLineWidth(1);
            context->drawEllipse(thumb, kDrawFilledAndStroked);
            setDirty(false);
        });
    }

  private:
    std::function<void()> restore_;
};

// WAVE (0.2.6): a slider of HUE's size for the waveform's brightness; its track in the
// groove's colour, filled in the controls' colour up to the value. Double-click restores the preset default.
class WaveSlider : public CSlider {
    static constexpr double kTrackInset = 4.0;

  public:
    WaveSlider(const CRect& rect, IControlListener* listener, std::function<void()> restore)
    : CSlider(rect, listener, kWaveTag, CPoint(kTrackInset + 0.5, 0),
              int32_t(rect.getWidth() - 2 * kTrackInset + 1), nullptr, nullptr),
      restore_(std::move(restore)) {
        setSliderMode(CSliderMode::FreeClick);
        setWheelInc(5.0f / kWaveSpan);
        setTooltipText("Brightness of the waveform (WAVE) or the nebula (GALAXY), 25 to 200 %. "
                       "Double-click: preset default.");
    }
    void SetColours(CColor groove, CColor fill) {
        groove_ = groove;
        fill_ = fill;
        invalid();
    }

    CMouseEventResult onMouseDown(CPoint& point, const CButtonState& buttons) override {
        return Guarded(kMouseEventHandled, [&] {
            if(buttons.isLeftButton() && buttons.isDoubleClick()) {
                restore_();
                return kMouseEventHandled;
            }
            return CSlider::onMouseDown(point, buttons);
        });
    }

    // C6 (0.2.9): VSTGUI's default-value gesture (Ctrl+click) restores the preset's
    // value as the double-click does, instead of the slider's default 0.5.
    void dispatchEvent(Event& event) override {
        if(event.type == EventType::MouseDown
           && CControl::CheckDefaultValueEventFunc(this, castMouseDownEvent(event))) {
            auto& down = castMouseDownEvent(event);
            Guarded([&] { restore_(); });
            down.consumed = true;
            down.ignoreFollowUpMoveAndUpEvents(true);
            return;
        }
        CSlider::dispatchEvent(event);
    }

    void draw(CDrawContext* context) override {
        Guarded([&] {
            const CRect area = getViewSize();
            const double y = area.getCenter().y;
            const double left = area.left + kTrackInset, right = area.right - kTrackInset;
            const double x = left + (right - left) * getValueNormalized();
            context->setDrawMode(kAntiAliasing);
            context->setFillColor(groove_);
            context->drawRect(CRect(left, y - 2, right, y + 2), kDrawFilled);
            context->setFillColor(fill_);
            context->drawRect(CRect(left, y - 1.5, x, y + 1.5), kDrawFilled);
            const CRect thumb(x - 4, y - 6, x + 4, y + 6);
            context->setFillColor(kWhiteCColor);
            context->setFrameColor(kBlackCColor);
            context->setLineWidth(1);
            context->drawEllipse(thumb, kDrawFilledAndStroked);
            setDirty(false);
        });
    }

  private:
    std::function<void()> restore_;
    CColor groove_ = CColor(0, 0, 0, 120), fill_ = kWhiteCColor;
};

struct KnobSpec {
    ParamID id;
    const char* name;
    int place; // a primary: its slot (gui_layout.h, kPrimary); a secondary: its column
    bool small;
    bool bipolar;
};

// Beta 2 (the user's round window): the primary knobs on the arcs beside the glass, left
// PITCH, SPEED, LOOP END, DENSITY, right SPRAY, OVERLAP, WINDOW, VOLUME, BLEND below it;
// START is the bar under them. The secondaries under DETAILS, in columns of their own. The
// secondaries of Speed and Blend, which act on the audio input, come with 0.3.0.
constexpr KnobSpec kKnobs[Editor::kKnobCount] = {
    {kParamSpeed, "SPEED", 1, false, true},
    {kParamSize, "LOOP END", 2, false, false},
    {kParamDensity, "DENSITY", 3, false, false},
    {kParamPitch, "PITCH", 0, false, true},
    {kParamOverlap, "OVERLAP", 5, false, false},
    {kParamWindow, "WINDOW", 6, false, false},
    {kParamBlend, "BLEND", 8, false, false},
    {kParamVolume, "VOLUME", 7, false, false},
    {kParamSpray, "SPRAY", 4, false, false},
    {kParamPanRandom, "PAN RND", 0, true, false},
    {kParamDisplacement, "DISPLACE", 1, true, false},
    {kParamPitchRandom, "PITCH RND", 2, true, false},
    {kParamSizeRandom, "SIZE RND", 3, true, false},
    {kParamMask, "MASK", 4, true, false},
};

CRect Box(double left, double top, double right, double bottom) {
    return CRect(left, top, right, bottom);
}

CRect Box(const L::Box& box) {
    return CRect(box.left, box.top, box.right, box.bottom);
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

// Beta 2: where the window's knobs rest (the user's wish): SPEED at 0x, +1x and -1x within
// a band of 0.06x, PITCH on every 50 cents.
ParamValue SnappedByKnob(ParamID id, ParamValue normalized) {
    if(id == kParamSpeed) {
        const double factor = SpeedFactor(normalized);
        for(const double rest : {-1.0, 0.0, 1.0})
            if(std::abs(factor - rest) < 0.06)
                return SpeedNormalized(rest);
    } else if(id == kParamPitch) {
        const double octaves = std::round(PitchOctaves(normalized) * 24.0) / 24.0;
        return PitchNormalized(octaves);
    }
    return normalized;
}

// A parameter's value as the host shows it, with its unit.
std::string ParamText(EditController* controller, ParamID id, ParamValue normalized) {
    String128 text{};
    controller->getParamStringByValue(id, normalized, text);
    std::string display = StringConvert::convert(text);
    ParameterInfo info{};
    const int32 count = controller->getParameterCount();
    for(int32 i = 0; i < count; ++i) {
        if(controller->getParameterInfo(i, info) == kResultOk && info.id == id) {
            const std::string units = StringConvert::convert(info.units);
            if(!units.empty())
                display += " " + units;
            break;
        }
    }
    return display;
}

} // namespace

Editor::Editor(EditController* controller, const EditorSettings& settings,
               EditorCallbacks callbacks)
: VSTGUIEditor(controller), callbacks_(std::move(callbacks)), alive_(std::make_shared<bool>(true)) {
    settings_.zoom = ClampEditorZoom(settings.zoom);
    settings_.keyboard = settings.keyboard;
    settings_.details = settings.details;
    settings_.transpose = ClampKeyboardTranspose(settings.transpose);
    settings_.preset = IsGuiPreset(static_cast<std::int32_t>(settings.preset)) ? settings.preset : GuiPreset::kSpiral;
    settings_.accents = settings.accents;
    settings_.wave_percent = ClampWaveSetting(settings.wave_percent);
    settings_.galaxy = settings.galaxy;
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
    details_button_ = nullptr;
    accent_slider_ = nullptr;
    wave_slider_ = nullptr;
    wave_value_ = nullptr;
    octave_down_ = nullptr;
    octave_up_ = nullptr;
    freeze_button_ = nullptr;
    file_name_ = nullptr;
    file_info_ = nullptr;
    map_view_ = nullptr;
    start_bar_ = nullptr;
    view_switch_ = nullptr;
    status_ = nullptr;
    file_zone_ = nullptr;
    window_shape_ = nullptr;
    latch_button_ = nullptr;
    grip_ = nullptr;
    stop_button_ = nullptr;
    panel_ = nullptr;
    size_button_ = nullptr;
    preset_button_ = nullptr;
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
    if(callbacks_.current)
        ApplySettings(callbacks_.current()); // F3 (0.2.9); without a frame it only takes them
    const L::BottomRows rows = L::Bottom();
    frame = new CFrame(CRect(0, 0, kWidth, EditorBaseHeight(settings_.keyboard, settings_.details)), this);
    frame->setBackgroundColor(kBlackCColor);
    frame->enableTooltips(true);
    applied_zoom_ = 1.0;
    drag_ = DragConstraint{};
    ++window_serial_;
    skin_ = std::make_shared<Skin>(settings_.preset);
    skin_->SetAccent(settings_.Accent());
    const Skin& skin = *skin_;
    const SkinColours& colours = skin.Colours();

    // The background first: chassis, rules, the fixed texts.
    panel_ = Add(new PanelView(CRect(0, 0, kWidth, EditorBaseHeight(settings_.keyboard, settings_.details))));

    // The header: KEYS, STOP and the size and preset menus, as one group (0.2.4).
    keyboard_button_ = Add(new ToggleButton(Box(L::kKeysLeft, L::kButtonTop, L::kKeysRight, L::kButtonTop + L::kButtonHeight),
                                            "KEYS", settings_.keyboard, [this](bool on) { SetKeyboardVisible(on); }));
    keyboard_button_->SetIcon(ToggleButton::Icon::kKeyboard);
    keyboard_button_->setTooltipText("Keys: shows or hides the keyboard");
    {
        const bool stopped = controller && IsOn(controller->getParamNormalized(kParamStop));
        stop_button_ = Add(new ToggleButton(Box(L::kStopLeft, L::kButtonTop, L::kStopRight, L::kButtonTop + L::kButtonHeight),
                                            "STOP", stopped, [this](bool on) { SetStop(on); }));
        stop_button_->setTooltipText("Stop: pauses the sound with a short fade; press again to go on where it stopped");
    }
    size_button_ = Add(new ToggleButton(Box(L::kSizeLeft, L::kButtonTop, L::kSizeRight, L::kButtonTop + L::kButtonHeight),
                                        "", false, [this](bool) { Later([this] { ShowSizeMenu(); }); },
                                        ToggleButton::Style::Action));
    size_button_->SetIcon(ToggleButton::Icon::kSize);
    size_button_->setTooltipText("Window size");
    preset_button_ = Add(new ToggleButton(Box(L::kPresetLeft, L::kButtonTop, L::kPresetRight, L::kButtonTop + L::kButtonHeight),
                                          "", false, [this](bool) { Later([this] { ShowPresetMenu(); }); },
                                          ToggleButton::Style::Action));
    preset_button_->SetIcon(ToggleButton::Icon::kPreset);
    preset_button_->setTooltipText("Nebula: the object in the glass");

    // The sample: LOAD, its name, its format or what went wrong (beta 2, the user's test:
    // no catalogue number; the loader's sequence number stays internal).
    auto* load = Add(new ToggleButton(Box(L::kLoadLeft, L::kLoadTop, L::kLoadRight, L::kLoadBottom), "LOAD...",
                                      false, [this](bool) { Later([this] { ChooseFile(); }); }, ToggleButton::Style::Action));
    load->setTooltipText("Load a WAV file: 16 or 24 bit PCM or 32 bit float, mono or stereo");
    load->SetSkin(skin_);
    // The name upright like the rest of the header (0.2.7), as it is spelt; a name too long
    // for its place ends in "..", the tooltip has the whole path. Beta 2: the format, or
    // what went wrong, in the info line.
    file_name_ = Add(MakeLabel(Box(L::kFileLeft, L::kFileNameTop, L::kFileRight, L::kFileNameBottom), "",
                               skin.Sans(18), colours.text, kLeftText));
    file_name_->setTextTruncateMode(CTextLabel::kTruncateTail);
    file_info_ = Add(MakeLabel(Box(rows.file_info), "",
                               skin.Mono(11), colours.dim, kLeftText));
    file_info_->setTextTruncateMode(CTextLabel::kTruncateTail);
    status_ = Add(MakeLabel(Box(rows.status), "",
                            skin.Mono(11), colours.dim, kRightText));
    ApplyFileTexts();

    map_view_ = Add(new MapView(Box(L::kMapLeft, L::kMapTop, L::kMapRight, L::kMapBottom)));
    map_view_->SetGalaxy(settings_.galaxy);
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
        // Beta 2: the START bar sets START the same way.
        StartBar::Edits bar;
        bar.begin = edits.start_begin;
        bar.start = edits.start;
        bar.end = edits.start_end;
        // Round 2: its end thumb sets LOOP END, as the knob does.
        bar.size_begin = [this] {
            if(controller)
                controller->beginEdit(kParamSize);
        };
        bar.size = [this](double size) {
            if(!controller)
                return;
            controller->setParamNormalized(kParamSize, size);
            controller->performEdit(kParamSize, controller->getParamNormalized(kParamSize));
        };
        bar.size_end = [this] {
            if(controller)
                controller->endEdit(kParamSize);
        };
        map_view_->SetEdits(std::move(edits));
        start_bar_ = Add(new StartBar(Box(rows.start_bar), rows.track_left, rows.track_right));
        start_bar_->SetEdits(std::move(bar));
        if(controller) {
            start_bar_->SetStart(controller->getParamNormalized(kParamStart));
            start_bar_->SetSize(controller->getParamNormalized(kParamSize));
        }
        start_bar_->setTooltipText("Start: where the loop begins in the file; click or drag. "
                                   "The right thumb is the loop's end: drag it for LOOP END");
    }
    // GALAXY / WAVE (beta 2).
    // Round 5: round the BLEND knob, behind it (added before the knobs).
    view_switch_ = Add(new ViewSwitch(Box(L::ArcSwitchBox()),
                                      [this](bool galaxy) { SetGalaxy(galaxy); }));
    view_switch_->SetGalaxy(settings_.galaxy);
    view_switch_->setTooltipText("The glass: the spiral nebula (GALAXY) or the waveform (WAVE)");
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
        file_zone_ = Add(new CView(Box(L::kFileLeft, L::kFileNameTop, L::kFileRight, L::kFileNameBottom)));
        file_zone_->setDropTarget(drop);
        ApplyFileTexts(); // its tooltip
    }

    // The keyboard section: the octave keys and the keys.
    // The keyboard section: LATCH, the octave keys and the keys, in one row; LayOut()
    // puts them below DETAILS.
    const CCoord section = L::TopHeight(settings_.details);
    keyboard_view_ = Add(new KeyboardView(Box(L::kKeyboardLeft, section + L::kPianoTop,
                                              L::kKeyboardLeft + L::kKeyboardWidth,
                                              section + L::kPianoTop + L::kPianoHeight),
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
    // The octave keys (0.2.2): as large as LATCH, FREEZE and RESET, labelled.
    const CCoord key_top = section + L::kKeyButtonTop;
    octave_down_ = Add(new ToggleButton(Box(L::kOctaveDownLeft, key_top, L::kOctaveDownLeft + L::kButtonWidth,
                                            key_top + L::kButtonHeight),
                                        "\xE2\x88\x92 OCT", false,
                                        [this](bool) { SetKeyboardTranspose(settings_.transpose - 12); },
                                        ToggleButton::Style::Action));
    octave_up_ = Add(new ToggleButton(Box(L::kOctaveUpLeft, key_top, L::kOctaveUpLeft + L::kButtonWidth,
                                          key_top + L::kButtonHeight),
                                      "OCT +", false,
                                      [this](bool) { SetKeyboardTranspose(settings_.transpose + 12); },
                                      ToggleButton::Style::Action));
    octave_down_->SetValueInk(true);
    octave_up_->SetValueInk(true);
    // B2B.4: the range of the one-octave keybed at its lowest and highest place.
    {
        const auto c = [](int note) { return "C" + std::to_string(note / 12 - 1); };
        const std::string lowest = c(kKeyboardLowNote + kKeyboardLowestTranspose) + " to "
                                   + c(kKeyboardHighNote + kKeyboardLowestTranspose);
        const std::string highest = c(kKeyboardLowNote + kKeyboardHighestTranspose) + " to "
                                    + c(kKeyboardHighNote + kKeyboardHighestTranspose);
        octave_down_->setTooltipText(("Keyboard down one octave (lowest: " + lowest + ")").c_str());
        octave_up_->setTooltipText(("Keyboard up one octave (highest: " + highest + ")").c_str());
    }

    // The knobs, their names (on the panel) and values.
    const CFontRef value_font = skin.Mono(12.5);
    const CFontRef small_value_font = skin.Mono(11.5);
    for(std::size_t i = 0; i < knobs_.size(); ++i) {
        const KnobSpec& spec = kKnobs[i];
        CRect knob_box, name_box, value_box;
        if(spec.small) {
            const CCoord left = L::SmallColumnLeft(spec.place), right = left + L::kColumnWidth;
            const CCoord knob_left = left + (L::kColumnWidth - L::kSmallKnobSize) / 2;
            knob_box = Box(knob_left, L::kSmallKnobTop, knob_left + L::kSmallKnobSize, L::kSmallKnobTop + L::kSmallKnobSize);
            name_box = Box(left - 4, L::kSmallNameTop, right + 4, L::kSmallNameTop + 14);
            value_box = Box(left, L::kSmallValueTop, right, L::kSmallValueTop + 14);
        } else {
            // Beta 2: on the arcs beside the glass, with the scale's ends and the value in a box.
            knob_box = Box(L::KnobBox(spec.place));
            name_box = Box(L::NameBox(spec.place));
            value_box = Box(L::ValueBox(spec.place));
            panel_->AddLabel(value_box, "", PanelView::LabelKind::kValueBox);
            if(controller && spec.id != kParamWindow)
                for(const bool high : {false, true})
                    panel_->AddLabel(Box(L::ScaleBox(spec.place, high)), ParamText(controller, spec.id, high ? 1.0 : 0.0),
                                     high ? PanelView::LabelKind::kScaleHigh : PanelView::LabelKind::kScaleLow);
        }
        panel_->AddLabel(name_box, spec.name, spec.small ? PanelView::LabelKind::kSmallKnob : PanelView::LabelKind::kKnob);
        auto* knob = Add(new MetalKnob(knob_box, this, static_cast<int32_t>(spec.id), spec.small, spec.bipolar));
        knob->setDefaultValue(static_cast<float>(kDefaults[static_cast<std::size_t>(Index(spec.id))]));
        knob->setWheelInc(0.01f);
        auto* value = Add(MakeLabel(value_box, "", spec.small ? small_value_font : value_font, colours.value, kCenterText));
        if(spec.id == kParamWindow) // the mixed window as a symbol (0.1.8)
            window_shape_ = Add(new WindowShapeView(Box(value_box.getCenter().x - 23, value_box.top + 1,
                                                        value_box.getCenter().x + 23, value_box.top + 15)));
        knobs_[i] = {spec.id, knob, value};
        const ParamValue normalized = controller ? controller->getParamNormalized(spec.id) : 0.0;
        knob->setValue(static_cast<float>(normalized));
        ShowValue(knobs_[i], normalized);
    }
    // LATCH at the head of the keyboard row (0.2.4); it goes with the keyboard.
    latch_button_ = Add(new ToggleButton(Box(L::kLatchLeft, key_top, L::kLatchLeft + L::kButtonWidth,
                                             key_top + L::kButtonHeight),
                                         "LATCH", latch_, [this](bool on) { SetLatch(on); }));
    latch_button_->setTooltipText("Latch: a struck key stays held; click it again, another key or LATCH to let go");
    // DETAILS (0.2.4): opens and closes the row of the secondary knobs; their values and
    // automation stay as they are.
    // Round 4: a key of the common size (the user's wish); round 5 in the top left corner.
    details_button_ = Add(new ToggleButton(Box(rows.details), "DETAILS", settings_.details,
                                           [this](bool on) { SetDetailsVisible(on); }, ToggleButton::Style::Toggle));
    details_button_->setTooltipText("Details: the secondary knobs (pan, displacement, pitch and size random, mask)");

    panel_->AddLabel(Box(L::kAccentLeft, L::kSmallNameTop, L::kAccentRight, L::kSmallKnobTop),
                     "HUE", PanelView::LabelKind::kSmallKnob);
    accent_slider_ = Add(new AccentSlider(Box(L::kAccentLeft, L::kSmallKnobTop, L::kAccentRight,
                                             L::kSmallKnobTop + L::kSmallKnobSize),
                                         this, [this] { SetAccent(kPresetAccent); }));
    accent_slider_->setVisible(settings_.details);

    // WAVE (0.2.6) in TRAIL's place: the waveform's brightness (round 2: in GALAXY the
    // nebula's); the trail is fixed.
    panel_->AddLabel(Box(L::kWaveLeft, L::kSmallNameTop, L::kWaveRight, L::kSmallKnobTop),
                     "WAVE", PanelView::LabelKind::kSmallKnob);
    wave_slider_ = Add(new WaveSlider(Box(L::kWaveLeft, L::kSmallKnobTop, L::kWaveRight,
                                          L::kSmallKnobTop + L::kSmallKnobSize),
                                      this, [this] { SetWave(kPresetWave); }));
    wave_value_ = Add(MakeLabel(Box(L::kWaveLeft, L::kSmallValueTop, L::kWaveRight, L::kSmallValueTop + 14),
                                "", small_value_font, colours.value, kCenterText));
    wave_slider_->setVisible(settings_.details);
    wave_value_->setVisible(settings_.details);
    ApplyWave();

    // Freeze and Reset (beta 2), round 5 above the START row on the right, below VOLUME.
    const bool frozen = controller && IsOn(controller->getParamNormalized(kParamFreeze));
    freeze_button_ = Add(new ToggleButton(Box(rows.freeze), "FREEZE", frozen, [this](bool on) { SetFreeze(on); }));
    freeze_button_->SetIcon(ToggleButton::Icon::kFreeze);
    freeze_button_->setTooltipText("Freeze: the playhead stands still; grains and the vocoder go on");
    auto* reset = Add(new ToggleButton(Box(rows.reset), "RESET", false, [this](bool) { PressReset(); },
                                       ToggleButton::Style::Action));
    reset->SetIcon(ToggleButton::Icon::kReset);
    reset->setTooltipText("Reset: all knobs and START to their defaults, Freeze off, playback back to START");
    // The resize grip in the lower right corner (0.1.11), added last so that it is on top;
    // LayOut() puts it into the corner of the height the keyboard leaves.
    grip_ = Add(new ResizeGrip(Box(0, 0, L::kGripSize, L::kGripSize), colours.dim, [this] { GripBegin(); },
                               [this](double dx, double dy) { GripDrag(dx, dy); }));

    ApplySkin();
    ShowDisplay(shown_display_, shown_wave_); // the START bar's file and the info line
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
    Guarded([&] {
        if(accent_slider_) {
            // Beta 2: the place of HUE, or of the preset's own nebula colour.
            std::int32_t hue = settings_.Accent();
            if(hue == kPresetAccent) {
                double h = 0, saturation = 0, lightness = 0;
                skin_->BaseColours().nebula_a.toHSL(h, saturation, lightness);
                hue = static_cast<std::int32_t>(std::lround(h)) % 360;
            }
            accent_slider_->setValueNormalized(static_cast<float>(AccentSliderValue(hue)));
            accent_slider_->invalid();
        }
    });
    // Every view of the frame that draws with the skin gets it; each on its own (MS-R41).
    Guarded([&] {
        frame->forEachChild([&](CView* view) {
            Guarded([&] {
                if(auto* button = dynamic_cast<ToggleButton*>(view))
                    button->SetSkin(skin_);
                else if(auto* bar = dynamic_cast<StartBar*>(view))
                    bar->SetSkin(skin_);
                else if(auto* view_switch = dynamic_cast<ViewSwitch*>(view))
                    view_switch->SetSkin(skin_);
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
        if(wave_value_)
            wave_value_->setFontColor(colours.value);
        if(auto* wave = dynamic_cast<WaveSlider*>(wave_slider_))
            wave->SetColours(colours.groove, colours.control);
    });
    Guarded([&] {
        if(file_name_)
            file_name_->setFontColor(colours.text);
    });
    Guarded([&] {
        if(status_)
            status_->setFontColor(colours.dim);
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

void Editor::Later(std::function<void()> action) noexcept {
    // C1 of the Fable audit (0.2.9): the file dialog and the menus run their own message
    // loop, in which the host may close the window. Started from the main queue instead
    // of from inside the click, no button and no VSTGUI event dispatch is on the stack
    // then. The task checks that this editor and this window are still there.
    if(!frame || !action)
        return;
    Guarded([&] {
        const std::weak_ptr<bool> alive = alive_;
        const std::uint64_t window = window_serial_;
        Tasks::schedule(Tasks::mainQueue(), [this, alive, window, action = std::move(action)] {
            const auto still = alive.lock();
            if(!still || !*still || !frame || window != window_serial_)
                return;
            Guarded([&] { action(); });
        });
    });
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
    const EditorSize size = EditorSizeFor(scale_ * settings_.zoom, settings_.keyboard, settings_.details);
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
        std::min(width / kEditorWidth, height / EditorBaseHeight(settings_.keyboard, settings_.details));
    return std::min(fitting, scale_ * kMaxEditorZoom);
}

void Editor::LayOut(int32 width, int32 height) {
    if(!frame)
        return;
    const CCoord bottom = EditorBaseHeight(settings_.keyboard, settings_.details);
    if(panel_) {
        panel_->setViewSize(CRect(0, 0, kWidth, bottom));
        panel_->setMouseableArea(panel_->getViewSize());
        panel_->SetSections(settings_.keyboard, settings_.details);
    }
    // 0.2.4: the secondary knobs only while DETAILS is open; the keyboard row below it.
    for(Knob& knob : knobs_) {
        if(!knob.knob || !kKnobs[static_cast<std::size_t>(&knob - knobs_.data())].small)
            continue;
        // C5 (0.2.9): a hidden knob keeps no keyboard focus, or the arrow keys would turn it.
        if(!settings_.details && frame->getFocusView() == knob.knob)
            frame->setFocusView(nullptr);
        knob.knob->setVisible(settings_.details);
        if(knob.value)
            knob.value->setVisible(settings_.details);
    }
    const std::array<CView*, 3> display_controls{accent_slider_, wave_slider_, wave_value_};
    for(CView* view : display_controls) {
        if(!view)
            continue;
        if(!settings_.details && frame->getFocusView() == view)
            frame->setFocusView(nullptr);
        view->setVisible(settings_.details);
    }
    if(details_button_)
        details_button_->SetOn(settings_.details);
    const CCoord section = L::TopHeight(settings_.details);
    const auto place = [&](CView* view, CCoord left, CCoord top, CCoord width, CCoord height) {
        if(!view)
            return;
        view->setViewSize(CRect(left, section + top, left + width, section + top + height));
        view->setMouseableArea(view->getViewSize());
        view->setVisible(settings_.keyboard);
    };
    place(keyboard_view_, L::kKeyboardLeft, L::kPianoTop, L::kKeyboardWidth, L::kPianoHeight);
    place(latch_button_, L::kLatchLeft, L::kKeyButtonTop, L::kButtonWidth, L::kButtonHeight);
    place(octave_down_, L::kOctaveDownLeft, L::kKeyButtonTop, L::kButtonWidth, L::kButtonHeight);
    place(octave_up_, L::kOctaveUpLeft, L::kKeyButtonTop, L::kButtonWidth, L::kButtonHeight);
    if(keyboard_button_)
        keyboard_button_->SetOn(settings_.keyboard);
    for(ToggleButton* octave : {octave_down_, octave_up_}) {
        if(!octave)
            continue;
        const int limit = octave == octave_down_ ? kKeyboardLowestTranspose : kKeyboardHighestTranspose;
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
    settings_.keyboard = show;
    // Round 4 (the user's test: KEYS no longer hid the keyboard): the choice holds even when
    // the host keeps the window as it is or answers later; the content is laid out into the
    // window as it is now, and again when the host changes its size (onSize).
    ShowSections();
    // E5/C10 (0.2.9): a key held by LATCH is let go with the keyboard, which takes LATCH
    // out of sight; nothing would be left to end it.
    if(!show && keyboard_view_)
        Guarded([&] { keyboard_view_->ReleaseHeld(); });
    UserChanged();
}

void Editor::ShowSections() {
    if(!frame)
        return;
    if(!Resize())
        LayOut(rect.getWidth(), rect.getHeight()); // the host keeps the window: fitted into it
    else if(rect.getWidth() != ScaledSize().getWidth() || rect.getHeight() != ScaledSize().getHeight())
        LayOut(rect.getWidth(), rect.getHeight()); // asked; until the host answers, fitted
}

void Editor::SetDetailsVisible(bool show) {
    if(show == settings_.details)
        return;
    settings_.details = show;
    ShowSections(); // round 4: as KEYS
    UserChanged();
}

void Editor::SetGalaxy(bool galaxy) {
    if(galaxy == settings_.galaxy)
        return;
    settings_.galaxy = galaxy;
    if(map_view_)
        map_view_->SetGalaxy(galaxy);
    if(view_switch_)
        view_switch_->SetGalaxy(galaxy);
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
        auto skin = std::make_shared<Skin>(preset);
        skin->SetAccent(settings_.accents[static_cast<std::size_t>(preset)]);
        skin_ = std::move(skin);
        ApplySkin();
    }
    settings_.preset = preset;
    ApplyWave();
    UserChanged();
}

void Editor::SetAccent(std::int32_t hue) {
    if(!IsAccent(hue) || hue == settings_.Accent())
        return;
    settings_.accents[static_cast<std::size_t>(settings_.preset)] = hue;
    if(frame && skin_ && skin_->Preset() == ShownPreset(settings_.preset)) {
        skin_->SetAccent(hue);
        ApplySkin();
    }
    UserChanged();
}

void Editor::SetWave(std::int32_t percent) {
    percent = ClampWaveSetting(percent);
    if(percent == settings_.wave_percent)
        return;
    settings_.wave_percent = percent;
    ApplyWave();
    UserChanged();
}

void Editor::ApplyWave() {
    const auto percent = settings_.WavePercent();
    if(map_view_)
        map_view_->SetWave(percent);
    if(wave_slider_) {
        wave_slider_->setValueNormalized(WaveNormalized(percent));
        wave_slider_->invalid();
    }
    if(wave_value_) {
        char text[16];
        std::snprintf(text, sizeof text, "%d %%", static_cast<int>(percent));
        wave_value_->setText(text);
    }
}

SharedPointer<COptionMenu> MakePresetMenu(GuiPreset current) {
    auto menu = makeOwned<COptionMenu>(CRect(), nullptr, -1, nullptr, nullptr, COptionMenu::kMultipleCheckStyle);
    for(const GuiPreset preset : kPresetMenuOrder) {
        const GuiPresetInfo& info = PresetInfo(preset);
        std::string title = info.name;
        int32_t flags = CMenuItem::kNoFlags;
        if(!info.delivered) {
            title += "  (bitmaps to come)";
            flags |= CMenuItem::kDisabled;
        }
        if(preset == current)
            flags |= CMenuItem::kChecked;
        menu->addEntry(UTF8String(title), -1, flags);
    }
    // Custom skins (0.2.7; a test build's in 0.2.6): read the folders again, or open the
    // user's.
    menu->addSeparator();
    menu->addEntry(UTF8String("Reload skin folders"));
    menu->addEntry(UTF8String("Open skin folder"));
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
        // A child of the frame, also if popup stopped half way through adding it. Through
        // the menu's own frame, never `frame`: the host may have closed the window during
        // the menu's loop, and a frame that let the menu go leaves it without one (GA.1 of
        // the GPT review, 0.2.10).
        if(*answered)
            return;
        if(CFrame* owner = menu->getFrame(); owner && owner->isChild(menu, false))
            Guarded([&] { owner->removeView(menu, false); });
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
        if(index >= 0 && index < std::ssize(kPresetMenuOrder)) {
            const GuiPreset preset = kPresetMenuOrder[static_cast<std::size_t>(index)];
            if(PresetInfo(preset).delivered)
                SetGuiPreset(preset);
        } else if(index == std::ssize(kPresetMenuOrder) + 1) {
            ReloadSkin();
        } else if(index == std::ssize(kPresetMenuOrder) + 2) {
            OpenUserSkinFolder();
        }
    });
}

void Editor::ReloadSkin() {
    if(!frame)
        return;
    auto skin = std::make_shared<Skin>(settings_.preset);
    skin->SetAccent(settings_.Accent());
    skin_ = std::move(skin);
    ApplySkin();
    Guarded([&] { frame->invalid(); });
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
    const EditorSize allowed =
        ConstrainEditorSize(asked, grip_start_, scale_, settings_.keyboard, settings_.details, true);
    const double zoom = EditorZoomForSize(allowed.width, allowed.height, scale_, settings_.keyboard, settings_.details);
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
                    scale_, settings_.keyboard, settings_.details, DragConstraint::Clock::now());
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
        const double zoom = EditorZoomForSize(width, height, scale_, settings_.keyboard, settings_.details);
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
        IsGuiPreset(static_cast<std::int32_t>(settings.preset)) ? settings.preset : GuiPreset::kSpiral;
    const std::int32_t old_accent = settings_.Accent();
    const std::int32_t old_wave = settings_.WavePercent();
    settings_.accents = settings.accents;
    if(preset != settings_.preset) {
        // A loaded project brings its look. If it cannot be made, the old one stays on
        // screen, the setting is the project's, and the rest below is taken all the same
        // (MS-R41); choosing the preset again retries.
        settings_.preset = preset;
        if(frame)
            Guarded([&] {
                auto skin = std::make_shared<Skin>(preset);
                skin->SetAccent(settings_.Accent());
                skin_ = std::move(skin);
                ApplySkin();
            });
    } else if(frame && skin_ && skin_->Preset() == ShownPreset(preset) && old_accent != settings_.Accent()) {
        skin_->SetAccent(settings_.Accent());
        ApplySkin();
    }
    settings_.wave_percent = ClampWaveSetting(settings.wave_percent);
    if(settings_.WavePercent() != old_wave)
        Guarded([&] { ApplyWave(); });
    // Beta 2: GALAXY or WAVE, without a change of size.
    settings_.galaxy = settings.galaxy;
    if(map_view_)
        map_view_->SetGalaxy(settings_.galaxy);
    if(view_switch_)
        Guarded([&] { view_switch_->SetGalaxy(settings_.galaxy); });
    if(zoom == settings_.zoom && settings.keyboard == settings_.keyboard
       && settings.details == settings_.details && transpose == settings_.transpose)
        return;
    settings_.zoom = zoom;
    // B2B.1 (the GUI review of beta 2): a key held by LATCH is let go with the keyboard, as
    // in SetKeyboardVisible; nothing would be left in sight to end it.
    if(settings_.keyboard && !settings.keyboard && keyboard_view_)
        Guarded([&] { keyboard_view_->ReleaseHeld(); });
    settings_.keyboard = settings.keyboard;
    settings_.details = settings.details;
    settings_.transpose = transpose;
    if(keyboard_view_)
        keyboard_view_->SetTranspose(transpose);
    // A loaded project decides. If the host keeps the window size, or agrees and answers
    // later (B2B.2: the cause of round 4's KEYS fault), the content fits into the window
    // as it is meanwhile.
    ShowSections();
}

tresult PLUGIN_API Editor::getSize(ViewRect* size) {
    return Guarded(static_cast<tresult>(kResultFalse), [&] {
        // Before the window exists, answer with the size it will have: that of the
        // controller's settings (F3, 0.2.9).
        if(!frame) {
            if(callbacks_.current)
                ApplySettings(callbacks_.current());
            setRect(ScaledSize());
        }
        return VSTGUIEditor::getSize(size);
    });
}

void PLUGIN_API Editor::close() {
    // Each step on its own: whatever one of them meets, the frame is let go.
    Guarded([&] { FinishEdits(); });
    latch_ = false; // a window opens unlatched, also a view the host opens again (C10, 0.2.9)
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
    Guarded([&] {
        if(start_bar_)
            start_bar_->Release(); // the same for the START bar (beta 2)
    });
}

void Editor::valueChanged(CControl* control) {
    Guarded([&] {
        if(!control)
            return;
        if(control == accent_slider_) {
            SetAccent(AccentOfSlider(control->getValueNormalized())); // beta 2: red, violet, blue
            return;
        }
        if(control == wave_slider_) {
            SetWave(kWaveMinPercent + static_cast<int32_t>(std::lround(control->getValueNormalized() * kWaveSpan)));
            return;
        }
        if(!controller)
            return;
        const auto id = static_cast<ParamID>(control->getTag());
        ParamValue normalized = control->getValueNormalized();
        // Beta 2 (the user's DAW test): the knob rests at 0x, +1x and -1x and turns PITCH in
        // 50-cent steps. Only in the window: automation and MIDI stay as they come.
        const ParamValue snapped = SnappedByKnob(id, normalized);
        if(snapped != normalized) {
            normalized = snapped;
            control->setValueNormalized(static_cast<float>(snapped));
        }
        controller->setParamNormalized(id, normalized);
        controller->performEdit(id, normalized);
    });
    Animate(); // 0.2.1: the plate moves on while the host holds back the timer
}

// Only parameters go to the host: not HUE and WAVE (window settings) and not the menus,
// whose controls have the tag -1 (C3 of the Fable audit, 0.2.9).
void Editor::beginEdit(int32_t tag) {
    if(tag >= 0)
        VSTGUIEditor::beginEdit(tag);
}

void Editor::endEdit(int32_t tag) {
    if(tag >= 0)
        VSTGUIEditor::endEdit(tag);
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
        dialog_open_, alive, answered, [&] { return selector->run(std::move(answer)); },
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
    if(start_bar_) {
        const bool known = shown_wave_ && shown_wave_->sequence == display.sequence;
        start_bar_->SetFile(display.frames, known ? shown_wave_->rate : 0.0);
    }
    ShowStatus();
}

void Editor::ShowStatus() noexcept {
    if(!status_)
        return;
    Guarded([&] {
        // Beta 2: where the playhead is, and what BLEND makes of the sound.
        std::string text;
        const EngineDisplay& d = shown_display_;
        const bool known = shown_wave_ && shown_wave_->sequence == d.sequence && shown_wave_->rate > 0;
        if(d.frames > 0 && known) {
            char position[48];
            std::snprintf(position, sizeof position, "POS %.2f s", d.playhead / shown_wave_->rate);
            text = position;
            const double blend = controller ? controller->getParamNormalized(kParamBlend) : 0.0;
            text += blend < 0.005 ? "   VOCODER" : blend > 0.995 ? "   GRANULAR" : "   MIX";
        }
        if(text == status_text_)
            return;
        status_text_ = text;
        status_->setText(text.c_str());
        status_->invalid();
    });
}

void Editor::ForgetHeldKey() noexcept {
    if(keyboard_view_)
        Guarded([&] { keyboard_view_->ForgetHeld(); });
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
        map_view_->SetStart(normalized); // the loop's preview (0.2.1)
    if(id == kParamStart && start_bar_)
        start_bar_->SetStart(normalized); // the START bar (beta 2)
    if(id == kParamSize && map_view_)
        map_view_->SetSize(normalized); // the loop at once (0.2.1)
    if(id == kParamSize && start_bar_)
        start_bar_->SetSize(normalized); // the START bar's end thumb (round 2)
    if(id == kParamBlend && map_view_)
        map_view_->SetBlend(normalized);
    if(id == kParamBlend)
        ShowStatus(); // the mode in the info line (beta 2)
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
    const std::string display = ParamText(controller, knob.id, normalized);
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
