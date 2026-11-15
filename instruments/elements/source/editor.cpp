#include "editor.h"

#include "parameters.h"
#include "version.h"
#include "widgets.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/ctextlabel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace elements_synth {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace {

// Layout in unscaled pixels: two rows of nine knobs.
constexpr CCoord kWidth = kEditorWidth;
constexpr CCoord kMargin = 20;
constexpr CCoord kColumnWidth = (kWidth - 2 * kMargin) / Editor::kKnobsPerRow;
constexpr CCoord kKnobSize = 54;
constexpr CCoord kRowTop[2] = {50, 186};  // the group names of a row
constexpr CCoord kNameOffset = 20;        // from there to the knob names
constexpr CCoord kKnobOffset = 38;
constexpr CCoord kValueOffset = 96;
constexpr CCoord kStatusTop = 314;
constexpr CCoord kKeyboardTop = kEditorTopHeight - 4;
constexpr CCoord kKeyboardBottom = kEditorTopHeight + kKeyboardSectionHeight - 18;
constexpr CCoord kButtonWidth = 52;

// After the last step of a drag, wait this long before telling the host the
// project changed. Hosts may do real work for that message.
constexpr auto kSettleDelay = std::chrono::milliseconds(400);

constexpr CColor kBackground(28, 30, 36);
constexpr CColor kTrack(64, 68, 80);
constexpr CColor kAccent(236, 172, 64);
constexpr CColor kText(232, 232, 236);
constexpr CColor kDimText(150, 156, 168);

// The exciter knobs take their colours from the hardware module, in memory of
// Mutable Instruments: white for the envelope and the bow, magenta for the
// blow, turquoise for the strike. Our own shades, chosen for the dark
// background; only the colours are borrowed, nothing of the panel itself.
constexpr CColor kExciterWhite(226, 228, 234);
constexpr CColor kBlowMagenta(222, 78, 162);
constexpr CColor kStrikeTurquoise(52, 196, 196);

struct KnobSpec {
    ParamID id;
    const char* name;
    bool bipolar;
    CColor ring;
};

// Row by row, left to right.
constexpr KnobSpec kKnobs[Editor::kKnobCount] = {
    {kParamContour, "CONTOUR", false, kExciterWhite},
    {kParamBowLevel, "BOW", false, kExciterWhite},
    {kParamBowTimbre, "TIMBRE", false, kExciterWhite},
    {kParamBlowLevel, "BLOW", false, kBlowMagenta},
    {kParamBlowFlow, "FLOW", false, kBlowMagenta},
    {kParamBlowTimbre, "TIMBRE", false, kBlowMagenta},
    {kParamStrikeLevel, "STRIKE", false, kStrikeTurquoise},
    {kParamStrikeMallet, "MALLET", false, kStrikeTurquoise},
    {kParamStrikeTimbre, "TIMBRE", false, kStrikeTurquoise},
    {kParamGeometry, "GEOMETRY", false, kAccent},
    {kParamBrightness, "BRIGHTNESS", false, kAccent},
    {kParamDamping, "DAMPING", false, kAccent},
    {kParamPosition, "POSITION", false, kAccent},
    {kParamSpace, "SPACE", false, kAccent},
    {kParamVolume, "VOLUME", false, kAccent},
    {kParamCoarse, "COARSE", true, kAccent},
    {kParamFine, "FINE", true, kAccent},
    {kParamVelocity, "VELOCITY", false, kAccent},
};

// The names above groups of knobs: row, first column, number of columns, and
// the colour of their knobs.
struct GroupSpec {
    int row, column, columns;
    const char* name;
    CColor color;
};
constexpr GroupSpec kGroups[] = {
    {0, 0, 1, "ENVELOPE", kExciterWhite},  {0, 1, 2, "BOW", kExciterWhite},
    {0, 3, 3, "BLOW", kBlowMagenta},       {0, 6, 3, "STRIKE", kStrikeTurquoise},
    {1, 0, 4, "RESONATOR", kAccent},       {1, 4, 2, "SPACE", kAccent},
    {1, 6, 3, "PERFORMANCE", kAccent},
};

CTextLabel* MakeLabel(const CRect& size, const char* text, CFontRef font, CColor color,
                      CHoriTxtAlign align) {
    auto* label = new CTextLabel(size, text);
    label->setFont(font);
    label->setFontColor(color);
    label->setBackColor(kTransparentCColor);
    label->setFrameColor(kTransparentCColor);
    label->setStyle(CParamDisplay::kNoFrame);
    label->setHoriAlign(align);
    label->setMouseEnabled(false);
    return label;
}

} // namespace

std::string StatusText(const EngineStatus& status) {
    if(!status.known)
        return "Engine 48 kHz native";
    char text[160];
    if(status.host_rate == status.engine_rate)
        std::snprintf(text, sizeof(text), "Engine %.4g kHz native  |  Host %.4g kHz, no conversion",
                      status.engine_rate / 1000.0, status.host_rate / 1000.0);
    else
        std::snprintf(text, sizeof(text),
                      "Engine %.4g kHz native  |  Host %.4g kHz, converted, latency %d samples",
                      status.engine_rate / 1000.0, status.host_rate / 1000.0, status.latency);
    return text;
}

Editor::Editor(EditController* controller, const EditorSettings& settings,
               const EngineStatus& status, EditorCallbacks callbacks)
: VSTGUIEditor(controller), status_(status), callbacks_(std::move(callbacks)) {
    settings_.zoom = ClampEditorZoom(settings.zoom);
    settings_.keyboard = settings.keyboard;
    settings_.transpose = ClampKeyboardTranspose(settings.transpose);
    settings_.panel = settings.panel;
    setRect(ScaledSize());
}

Editor::~Editor() {
    // A host should call removed() first, which closes the window. If it does
    // not, close it here, while this object still exists.
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
    freeze_button_ = nullptr;
    octave_down_ = nullptr;
    octave_up_ = nullptr;
    panel_buttons_ = {};
    for(auto& views : panel_views_)
        views.clear();
    status_label_ = nullptr;
}

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
    frame = new CFrame(CRect(0, 0, kWidth, EditorBaseHeight(settings_.keyboard)), this);
    frame->setBackgroundColor(kBackground);
    frame->enableTooltips(true);
    applied_zoom_ = 1.0;
    drag_ = DragConstraint{};

    const auto title_font = makeOwned<CFontDesc>("Segoe UI", 20, kBoldFace);
    const auto name_font = makeOwned<CFontDesc>("Segoe UI", 10, kBoldFace);
    const auto value_font = makeOwned<CFontDesc>("Segoe UI", 13);

    frame->addView(MakeLabel(CRect(kMargin, 14, 230, 42), stringPluginName, title_font,
                             kText, kLeftText));
    const CCoord button_left = kWidth - kMargin - 5 * kButtonWidth - 24;
    frame->addView(MakeLabel(CRect(238, 18, button_left - 12, 40),
                             stringCompanyName "  " FULL_VERSION_STR, name_font, kDimText,
                             kRightText));
    const CCoord keys_left = button_left + 4 * (kButtonWidth + 6);
    keyboard_button_ = new ToggleButton(CRect(keys_left, 18, keys_left + kButtonWidth, 40), "KEYS",
                                        settings_.keyboard,
                                        [this](bool on) { SetKeyboardVisible(on); });
    frame->addView(keyboard_button_);
    keyboard_button_->setTooltipText("Show or hide keyboard in any panel");
    const char* panel_names[] = {"ENG", "MOD", "POLY", "GEN"};
    const char* panel_tips[] = {"Engine", "Modulation",
        "Planned: cycle 1 / 2 / 4 / 8 voices, following the Kekule findings", "Generative sequences"};
    for(int i = 0; i < 4; ++i) {
        const CCoord left = button_left + i * (kButtonWidth + 6);
        const bool poly = i == 2;
        // POLY will cycle the voices, not switch panels: no panel callback.
        // ENG, MOD and GEN are the panels 0, 1 and 2.
        const int panel = i < 2 ? i : i - 1;
        std::function<void(bool)> clicked;
        if(!poly)
            clicked = [this, panel](bool) { SetPanel(static_cast<EditorPanel>(panel)); };
        auto* button = new ToggleButton(CRect(left, 18, left + kButtonWidth, 40),
                                         panel_names[i], false, std::move(clicked));
        button->setTooltipText(panel_tips[i]);
        if(poly)
            button->setMouseEnabled(false); // enable with the actual voice allocator
        else
            panel_buttons_[panel] = button;
        frame->addView(button);
    }

    keyboard_view_ = new KeyboardView(CRect(kMargin + 40, kKeyboardTop, kWidth - kMargin - 40,
                                            kKeyboardBottom),
                                      [this](int note) {
                                          if(callbacks_.strike)
                                              callbacks_.strike(note);
                                      },
                                      [this](int note) {
                                          if(callbacks_.release)
                                              callbacks_.release(note);
                                      });
    keyboard_view_->SetTranspose(settings_.transpose);
    keyboard_view_->setVisible(settings_.keyboard);
    frame->addView(keyboard_view_);
    octave_down_ = new ToggleButton(CRect(kMargin, kKeyboardTop + 22, kMargin + 32,
                                          kKeyboardTop + 46), "<<", false,
                                    [this](bool) { SetKeyboardTranspose(settings_.transpose - 12); },
                                    ToggleButton::Style::Action);
    octave_up_ = new ToggleButton(CRect(kWidth - kMargin - 32, kKeyboardTop + 22,
                                        kWidth - kMargin, kKeyboardTop + 46), ">>", false,
                                  [this](bool) { SetKeyboardTranspose(settings_.transpose + 12); },
                                  ToggleButton::Style::Action);
    octave_down_->setTooltipText("Keyboard down one octave (minimum -24 semitones)");
    octave_up_->setTooltipText("Keyboard up one octave (maximum +24 semitones)");
    frame->addView(octave_down_);
    frame->addView(octave_up_);

    const auto first_engine_view = frame->getNbViews();

    const auto group_font = makeOwned<CFontDesc>("Segoe UI", 10, kBoldFace);
    for(const GroupSpec& group : kGroups) {
        const CCoord left = kMargin + group.column * kColumnWidth;
        frame->addView(MakeLabel(CRect(left, kRowTop[group.row],
                                       left + group.columns * kColumnWidth,
                                       kRowTop[group.row] + 16),
                                 group.name, group_font, group.color, kCenterText));
    }

    for(std::size_t i = 0; i < knobs_.size(); ++i) {
        const KnobSpec& spec = kKnobs[i];
        const CCoord top = kRowTop[i / kKnobsPerRow];
        const CCoord left = kMargin + static_cast<CCoord>(i % kKnobsPerRow) * kColumnWidth;
        const CCoord knob_left = left + (kColumnWidth - kKnobSize) / 2;
        const CCoord knob_top = top + kKnobOffset;

        // SPACE and its freeze icon share one label row, centred as a group.
        const CCoord name_right = left + kColumnWidth - (spec.id == kParamSpace ? 28 : 0);
        frame->addView(MakeLabel(CRect(left, top + kNameOffset, name_right,
                                       top + kNameOffset + 16),
                                 spec.name, name_font, kDimText, kCenterText));

        int32_t style = CKnob::kCoronaOutline | CKnob::kCoronaDrawing;
        if(spec.bipolar)
            style |= CKnob::kCoronaFromCenter;
        auto* knob = new CKnob(CRect(knob_left, knob_top, knob_left + kKnobSize,
                                     knob_top + kKnobSize),
                               this, static_cast<int32_t>(spec.id), nullptr, nullptr, CPoint(0, 0),
                               style);
        knob->setCoronaColor(spec.ring);
        knob->setColorShadowHandle(kTrack);
        knob->setColorHandle(kText);
        knob->setHandleLineWidth(4);
        knob->setCoronaInset(5);
        knob->setInsetValue(14);
        knob->setDefaultValue(static_cast<float>(DefaultOf(Index(static_cast<ParamId>(spec.id)))));
        // One semitone per wheel step for Coarse, one hundredth for the others.
        knob->setWheelInc(spec.id == kParamCoarse ? static_cast<float>(1.0 / kCoarseSteps) : 0.01f);
        frame->addView(knob);
        if(spec.id == kParamSpace) {
            // Keep the freeze circle clear of both the corona and the value.
            const bool frozen = controller && IsOn(controller->getParamNormalized(kParamFreeze));
            freeze_button_ = new ToggleButton(
                CRect(left + kColumnWidth - 27, top + kNameOffset - 2,
                      left + kColumnWidth - 7, top + kNameOffset + 18),
                "", frozen, [this](bool on) { SetFreeze(on); }, ToggleButton::Style::Snowflake);
            freeze_button_->setTooltipText("Freeze reverb");
            frame->addView(freeze_button_);
        }

        auto* value = MakeLabel(CRect(left, top + kValueOffset, left + kColumnWidth,
                                      top + kValueOffset + 20),
                                "", value_font, kText, kCenterText);
        frame->addView(value);

        knobs_[i] = {spec.id, knob, value};
        const ParamValue normalized = controller ? controller->getParamNormalized(spec.id) : 0.0;
        knob->setValue(static_cast<float>(normalized));
        ShowValue(knobs_[i], normalized);
    }

    for(auto i = first_engine_view; i < frame->getNbViews(); ++i)
        panel_views_[0].push_back(frame->getView(i));
    const char* panel_titles[] = {"", "MODULATION", "GENERATIVE"};
    const char* panel_descriptions[] = {"", "Planned: modulation matrix and LFOs.",
        "Planned: melodic MIDI sequences and probability."};
    for(int i = 1; i < 3; ++i) {
        auto* title = MakeLabel(CRect(kMargin, 112, kWidth - kMargin, 146), panel_titles[i],
                                title_font, kText, kCenterText);
        auto* description = MakeLabel(CRect(kMargin, 164, kWidth - kMargin, 190),
                                      panel_descriptions[i], value_font, kDimText, kCenterText);
        frame->addView(title);
        frame->addView(description);
        panel_views_[i] = {title, description};
    }
    ShowPanel();

    status_label_ = MakeLabel(CRect(kMargin, kStatusTop, kWidth - kMargin, kStatusTop + 16),
                              StatusText(status_).c_str(), name_font, kDimText, kLeftText);
    frame->addView(status_label_);

    if(!frame->open(parent, platformType)) {
        frame->forget();
        frame = nullptr;
        ForgetViews();
        return false;
    }
    // `rect` is the size the host gave the window. Fill it, then ask for the
    // size the settings want: a host that asked for the size before it told us
    // the scale factor made the window too small.
    LayOut(rect.getWidth(), rect.getHeight());
    Resize();
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
    if(keyboard_view_) {
        if(!settings_.keyboard && keyboard_view_->isVisible())
            keyboard_view_->ReleaseHeld();
        keyboard_view_->setVisible(settings_.keyboard);
    }
    if(keyboard_button_)
        keyboard_button_->SetOn(settings_.keyboard);
    if(octave_down_) {
        octave_down_->setVisible(settings_.keyboard);
        octave_down_->setMouseEnabled(settings_.transpose > -kKeyboardTransposeLimit);
        octave_down_->invalid();
    }
    if(octave_up_) {
        octave_up_->setVisible(settings_.keyboard);
        octave_up_->setMouseEnabled(settings_.transpose < kKeyboardTransposeLimit);
        octave_up_->invalid();
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

void Editor::UserChanged() {
    if(callbacks_.changed)
        callbacks_.changed(settings_);
    settle_pending_ = true;
    last_change_ = std::chrono::steady_clock::now();
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

void Editor::ShowPanel() {
    // Also covers a panel restored by ApplySettings while a wheel edit is
    // pending. Hidden controls must not leave the host's gesture open.
    if(settings_.panel != EditorPanel::Engine)
        for(const Knob& knob : knobs_)
            while(knob.knob && knob.knob->isEditing())
                knob.knob->endEdit();
    for(std::size_t i = 0; i < panel_views_.size(); ++i) {
        const bool selected = i == static_cast<std::size_t>(settings_.panel);
        for(auto* view : panel_views_[i])
            view->setVisible(selected);
        if(panel_buttons_[i])
            panel_buttons_[i]->SetOn(selected);
    }
}

void Editor::SetPanel(EditorPanel panel) {
    if(static_cast<int>(panel) < 0 || static_cast<int>(panel) > 2)
        return;
    if(panel == settings_.panel) {
        ShowPanel(); // clicking the selected tab must not turn it off
        return;
    }
    settings_.panel = panel;
    ShowPanel();
    UserChanged();
}

CMessageResult Editor::notify(CBaseObject* sender, const char* message) {
    const CMessageResult result = VSTGUIEditor::notify(sender, message);
    if(message == CVSTGUITimer::kMsgTimer && settle_pending_
       && std::chrono::steady_clock::now() - last_change_ >= kSettleDelay) {
        settle_pending_ = false;
        if(callbacks_.settled)
            callbacks_.settled();
    }
    return result;
}

tresult PLUGIN_API Editor::canResize() {
    return kResultTrue;
}

tresult PLUGIN_API Editor::checkSizeConstraint(ViewRect* size) {
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
       && transpose == settings_.transpose && settings.panel == settings_.panel)
        return;
    settings_.zoom = zoom;
    settings_.keyboard = settings.keyboard;
    settings_.transpose = transpose;
    settings_.panel = settings.panel;
    ShowPanel();
    if(keyboard_view_)
        keyboard_view_->SetTranspose(transpose);
    // A loaded project decides. If the host keeps the window size, the
    // content fits into it as it is.
    if(frame && !Resize())
        LayOut(rect.getWidth(), rect.getHeight());
}

tresult PLUGIN_API Editor::getSize(ViewRect* size) {
    // Before the window exists, answer with the size it will have.
    if(!frame)
        setRect(ScaledSize());
    return VSTGUIEditor::getSize(size);
}

void PLUGIN_API Editor::close() {
    if(settle_pending_) { // a change made just before closing still counts
        settle_pending_ = false;
        if(callbacks_.settled)
            callbacks_.settled();
    }
    // A knob still held by the mouse, or within the pause that follows a turn
    // of the wheel, has told the host that an edit began. Its view is about
    // to go without a mouse-up; finish the edit here, once, while the frame
    // can still pass it on. The same for a screen key still held: the host
    // gets its note-off.
    if(frame)
        for(const Knob& knob : knobs_)
            while(knob.knob && knob.knob->isEditing())
                knob.knob->endEdit();
    if(keyboard_view_)
        keyboard_view_->ReleaseHeld();
    ForgetViews();
    if(frame) {
        frame->removeAll(true);
        if(frame->getNbReference() == 1)
            frame->close(); // also releases the frame
        else
            frame->forget();
        frame = nullptr;
    }
}

void Editor::valueChanged(CControl* control) {
    if(!controller || !control)
        return;
    const auto id = static_cast<ParamID>(control->getTag());
    ParamValue normalized = control->getValueNormalized();
    // Coarse moves in whole semitones.
    if(id == kParamCoarse)
        normalized = CoarseNormalized(CoarseSemitones(normalized));
    // The controller reports back through ParameterChanged(), which also puts
    // the Coarse knob on the semitone it landed on.
    controller->setParamNormalized(id, normalized);
    controller->performEdit(id, normalized);
}

void Editor::SetFreeze(bool freeze) {
    if(!controller)
        return;
    const ParamValue normalized = freeze ? 1.0 : 0.0;
    controller->beginEdit(kParamFreeze);
    controller->setParamNormalized(kParamFreeze, normalized);
    controller->performEdit(kParamFreeze, normalized);
    controller->endEdit(kParamFreeze);
}

void Editor::ShowStatus(const EngineStatus& status) {
    status_ = status;
    if(status_label_) {
        status_label_->setText(StatusText(status_).c_str());
        status_label_->invalid();
    }
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
    knob.value->setText(display.c_str());
}

tresult PLUGIN_API Editor::setContentScaleFactor(ScaleFactor factor) {
    if(!(factor > 0.0f) || !std::isfinite(factor))
        return kInvalidArgument;
    scale_ = factor;
    if(frame && !Resize())
        LayOut(rect.getWidth(), rect.getHeight());
    return kResultOk;
}

} // namespace elements_synth
