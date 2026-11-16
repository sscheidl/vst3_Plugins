#include "editor.h"

#include "keyboard_view.h"
#include "parameters.h"
#include "version.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/ctextlabel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace taureon::toy_piano_generative {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace {

// Layout in unscaled pixels.
constexpr CCoord kWidth = kEditorWidth;
constexpr CCoord kMargin = 20;
// Two rows of knobs: the sound above, the sequencer below.
struct Row {
    int columns;
    CCoord knob_size, name_top, knob_top, value_top;
};
constexpr Row kRows[] = {{5, 64, 58, 82, 152}, {8, 48, 216, 234, 286}};
constexpr CCoord kSectionTop = 189; // the line that names the sequencer row
constexpr CCoord kKeyboardTop = kEditorTopHeight - 4;
constexpr CCoord kKeyboardBottom = kEditorTopHeight + kKeyboardSectionHeight - 18;
constexpr CCoord kButtonWidth = 52;
constexpr CCoord kTransposeWidth = 88;
constexpr CCoord kOctaveButtonWidth = 30;

// After the last step of a drag, wait this long before telling the host the
// project changed. Hosts may do real work for that message.
constexpr auto kSettleDelay = std::chrono::milliseconds(400);

constexpr CColor kBackground(28, 30, 36);
constexpr CColor kTrack(64, 68, 80);
constexpr CColor kAccent(236, 172, 64);
constexpr CColor kText(232, 232, 236);
constexpr CColor kDimText(150, 156, 168);

struct KnobSpec {
    ParamID id;
    const char* name;
    double default_value;
    bool bipolar;
    int row, column;
};

constexpr KnobSpec kKnobs[] = {
    {kParamMasterPitch, "MASTER PITCH", kDefaultMasterPitch, true, 0, 0},
    {kParamTuning, "TUNING", kDefaultTuning, false, 0, 1},
    {kParamAttack, "ATTACK", kDefaultAttack, false, 0, 2},
    {kParamDecay, "DECAY", kDefaultDecay, false, 0, 3},
    {kParamVolume, "VOLUME", kDefaultVolume, false, 0, 4},
    {kParamAlgorithm, "ALGORITHM", kDefaultAlgorithm, false, 1, 0},
    {kParamMutation, "MUTATION", kDefaultMutation, false, 1, 1},
    {kParamNoteProbability, "NOTES", kDefaultNoteProbability, false, 1, 2},
    {kParamScale, "SCALE", kDefaultScale, false, 1, 3},
    {kParamTonic, "TONIC", kDefaultTonic, false, 1, 4},
    {kParamOctave, "OCTAVE", kDefaultOctave, false, 1, 5},
    {kParamLength, "LENGTH", kDefaultLength, false, 1, 6},
    {kParamClockDivide, "DIVIDE", kDefaultClockDivide, false, 1, 7},
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

Editor::Editor(EditController* controller, const EditorSettings& settings,
               EditorCallbacks callbacks)
: VSTGUIEditor(controller), callbacks_(std::move(callbacks)) {
    settings_.zoom = ClampEditorZoom(settings.zoom);
    settings_.keyboard = settings.keyboard;
    settings_.keyboard_shift = ClampKeyboardShift(settings.keyboard_shift);
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

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
    frame = new CFrame(CRect(0, 0, kWidth, EditorBaseHeight(settings_.keyboard)), this);
    frame->setBackgroundColor(kBackground);
    applied_zoom_ = 1.0;
    drag_ = DragConstraint{};

    const auto title_font = makeOwned<CFontDesc>("Segoe UI", 20, kBoldFace);
    const auto name_font = makeOwned<CFontDesc>("Segoe UI", 11, kBoldFace);
    const auto value_font = makeOwned<CFontDesc>("Segoe UI", 14);

    const auto value_of = [this](ParamID id) {
        return controller ? controller->getParamNormalized(id) : 0.0;
    };

    frame->addView(MakeLabel(CRect(kMargin, 14, kWidth / 2, 42), stringPluginName, title_font,
                             kText, kLeftText));
    const CCoord button_left = kWidth - kMargin - kButtonWidth;
    const CCoord transpose_left = button_left - 8 - kTransposeWidth;
    frame->addView(MakeLabel(CRect(kWidth / 2, 18, transpose_left - 12, 40),
                             stringCompanyName "  " FULL_VERSION_STR, name_font, kDimText,
                             kRightText));
    // Keys and MIDI notes transpose the sequence while this is on; they play
    // notes while it is off.
    transpose_button_ =
        new ToggleButton(CRect(transpose_left, 18, button_left - 8, 40), "TRANSPOSE",
                         IsOn(value_of(kParamKeysTranspose)),
                         [this](bool on) { EditSwitch(kParamKeysTranspose, on); });
    frame->addView(transpose_button_);
    keyboard_button_ = new ToggleButton(CRect(button_left, 18, kWidth - kMargin, 40), "KEYS",
                                        settings_.keyboard,
                                        [this](bool on) { SetKeyboardVisible(on); });
    frame->addView(keyboard_button_);

    frame->addView(MakeLabel(CRect(kMargin, kSectionTop, kMargin + 90, kSectionTop + 18),
                             "SEQUENCER", name_font, kDimText, kLeftText));
    run_button_ = new ToggleButton(CRect(kMargin + 92, kSectionTop - 1, kMargin + 92 + kButtonWidth,
                                         kSectionTop + 19),
                                   "RUN", IsOn(value_of(kParamRun)),
                                   [this](bool on) { EditSwitch(kParamRun, on); });
    frame->addView(run_button_);

    // The octave buttons at both ends of the keyboard. They are push buttons:
    // each press moves, and the button goes out again.
    const CCoord keys_left = kMargin + kOctaveButtonWidth + 6;
    const CCoord keys_right = kWidth - kMargin - kOctaveButtonWidth - 6;
    octave_down_ = new ToggleButton(CRect(kMargin, kKeyboardTop + 20, kMargin + kOctaveButtonWidth,
                                          kKeyboardTop + 44),
                                    "<<", false, [this](bool) {
                                        octave_down_->SetOn(false);
                                        ShiftKeyboard(-1);
                                    });
    octave_up_ = new ToggleButton(CRect(kWidth - kMargin - kOctaveButtonWidth, kKeyboardTop + 20,
                                        kWidth - kMargin, kKeyboardTop + 44),
                                  ">>", false, [this](bool) {
                                      octave_up_->SetOn(false);
                                      ShiftKeyboard(1);
                                  });
    octave_down_->setVisible(settings_.keyboard);
    octave_up_->setVisible(settings_.keyboard);
    frame->addView(octave_down_);
    frame->addView(octave_up_);
    keyboard_view_ = new KeyboardView(CRect(keys_left, kKeyboardTop, keys_right, kKeyboardBottom),
                                      [this](int note) {
                                          if(callbacks_.strike)
                                              callbacks_.strike(note);
                                      });
    keyboard_view_->SetShift(settings_.keyboard_shift);
    keyboard_view_->setVisible(settings_.keyboard);
    frame->addView(keyboard_view_);
    UpdateMarkedKey();

    for(std::size_t i = 0; i < knobs_.size(); ++i) {
        const KnobSpec& spec = kKnobs[i];
        const Row& row = kRows[spec.row];
        const CCoord column_width = (kWidth - 2 * kMargin) / row.columns;
        const CCoord knob_size = row.knob_size;
        const CCoord name_top = row.name_top, knob_top = row.knob_top, value_top = row.value_top;
        const CCoord left = kMargin + static_cast<CCoord>(spec.column) * column_width;
        const CCoord knob_left = left + (column_width - knob_size) / 2;

        frame->addView(MakeLabel(CRect(left, name_top, left + column_width, name_top + 16),
                                 spec.name, name_font, kDimText, kCenterText));

        int32_t style = CKnob::kCoronaOutline | CKnob::kCoronaDrawing;
        if(spec.bipolar)
            style |= CKnob::kCoronaFromCenter;
        auto* knob = new CKnob(CRect(knob_left, knob_top, knob_left + knob_size,
                                     knob_top + knob_size),
                               this, static_cast<int32_t>(spec.id), nullptr, nullptr, CPoint(0, 0),
                               style);
        knob->setCoronaColor(kAccent);
        knob->setColorShadowHandle(kTrack);
        knob->setColorHandle(kText);
        knob->setHandleLineWidth(spec.row == 0 ? 4 : 3);
        knob->setCoronaInset(spec.row == 0 ? 5 : 4);
        knob->setInsetValue(spec.row == 0 ? 16 : 12);
        knob->setDefaultValue(static_cast<float>(spec.default_value));
        // The wheel moves a stepped knob by one step.
        const int steps = ParamSteps(spec.id);
        knob->setWheelInc(steps > 0 ? 1.0f / static_cast<float>(steps) : 0.01f);
        frame->addView(knob);

        auto* value = MakeLabel(CRect(left, value_top, left + column_width, value_top + 20), "",
                                value_font, kText, kCenterText);
        frame->addView(value);

        knobs_[i] = {spec.id, knob, value};
        const ParamValue normalized = controller ? controller->getParamNormalized(spec.id) : 0.0;
        knob->setValue(static_cast<float>(normalized));
        ShowValue(knobs_[i], normalized);
    }

    if(!frame->open(parent, platformType)) {
        frame->forget();
        frame = nullptr;
        knobs_ = {};
        keyboard_view_ = nullptr;
        keyboard_button_ = transpose_button_ = run_button_ = nullptr;
        octave_down_ = octave_up_ = nullptr;
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
        keyboard_view_->setVisible(settings_.keyboard);
        keyboard_view_->SetShift(settings_.keyboard_shift);
    }
    for(ToggleButton* button : {octave_down_, octave_up_})
        if(button)
            button->setVisible(settings_.keyboard);
    if(keyboard_button_)
        keyboard_button_->SetOn(settings_.keyboard);
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

void Editor::ShiftKeyboard(int octaves) {
    const int shift = ClampKeyboardShift(settings_.keyboard_shift + 12 * octaves);
    if(shift != settings_.keyboard_shift) {
        settings_.keyboard_shift = shift;
        if(keyboard_view_)
            keyboard_view_->SetShift(shift);
        UserChanged();
    }
    // The sequence moves along, as far as its own octave range goes.
    if(callbacks_.octave)
        callbacks_.octave(octaves);
}

void Editor::EditSwitch(ParamID id, bool on) {
    if(!controller)
        return;
    controller->beginEdit(id);
    controller->setParamNormalized(id, on ? 1.0 : 0.0);
    controller->performEdit(id, on ? 1.0 : 0.0);
    controller->endEdit(id);
}

void Editor::UpdateMarkedKey() {
    if(!keyboard_view_ || !controller)
        return;
    const int key = KeyForTransposition(
        StepIndex(controller->getParamNormalized(kParamTonic), kTonicSteps),
        StepIndex(controller->getParamNormalized(kParamOctave), kOctaveSteps));
    keyboard_view_->SetMarked(IsOn(controller->getParamNormalized(kParamKeysTranspose)) ? key
                                                                                         : -1);
}

void Editor::UserChanged() {
    if(callbacks_.changed)
        callbacks_.changed(settings_);
    settle_pending_ = true;
    last_change_ = std::chrono::steady_clock::now();
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
    const int shift = ClampKeyboardShift(settings.keyboard_shift);
    if(zoom == settings_.zoom && settings.keyboard == settings_.keyboard
       && shift == settings_.keyboard_shift)
        return;
    settings_.zoom = zoom;
    settings_.keyboard = settings.keyboard;
    settings_.keyboard_shift = shift;
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
    knobs_ = {};
    keyboard_view_ = nullptr;
    keyboard_button_ = transpose_button_ = run_button_ = nullptr;
    octave_down_ = octave_up_ = nullptr;
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
    // Stepped parameters take whole steps only.
    const ParamValue normalized = SnapToStep(id, control->getValueNormalized());
    // The controller reports back through ParameterChanged(), which also puts
    // the knob on the step it snapped to.
    controller->setParamNormalized(id, normalized);
    controller->performEdit(id, normalized);
}

// A drag of a knob is one edit for the host: it starts and ends here.
void Editor::controlBeginEdit(CControl* control) {
    if(controller && control)
        controller->beginEdit(static_cast<ParamID>(control->getTag()));
}

void Editor::controlEndEdit(CControl* control) {
    if(controller && control)
        controller->endEdit(static_cast<ParamID>(control->getTag()));
}

void Editor::ParameterChanged(ParamID id, ParamValue normalized) {
    if(id == kParamRun && run_button_)
        run_button_->SetOn(IsOn(normalized));
    if(id == kParamKeysTranspose && transpose_button_)
        transpose_button_->SetOn(IsOn(normalized));
    if(id == kParamKeysTranspose || id == kParamTonic || id == kParamOctave)
        UpdateMarkedKey();
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
    if(!(factor > 0.0f))
        return kInvalidArgument;
    scale_ = factor;
    if(frame && !Resize())
        LayOut(rect.getWidth(), rect.getHeight());
    return kResultOk;
}

} // namespace taureon::toy_piano_generative
