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
#include <cstdlib>
#include <string>
#include <utility>

namespace torus_classic {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace {

// Layout in unscaled pixels.
constexpr CCoord kWidth = kEditorWidth;
constexpr CCoord kMargin = 20;
constexpr CCoord kColumnWidth = (kWidth - 2 * kMargin) / Editor::kKnobCount;
constexpr CCoord kSelectorTop = 54;
constexpr CCoord kSelectorBottom = 110;
constexpr CCoord kKnobSize = 60;
constexpr CCoord kNameTop = 124;
constexpr CCoord kKnobTop = 146;
constexpr CCoord kValueTop = 212;
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

struct KnobSpec {
    ParamID id;
    const char* name;
    double default_value;
    bool bipolar;
};

constexpr KnobSpec kKnobs[Editor::kKnobCount] = {
    {kParamTune, "TUNE", kDefaultTune, true},
    {kParamStructure, "STRUCTURE", kDefaultStructure, false},
    {kParamBrightness, "BRIGHTNESS", kDefaultBrightness, false},
    {kParamDamping, "DAMPING", kDefaultDamping, false},
    {kParamPosition, "POSITION", kDefaultPosition, false},
    {kParamExcitation, "EXCITATION", kDefaultExcitation, false},
    {kParamFilter, "FILTER", kDefaultFilter, false},
    {kParamVolume, "VOLUME", kDefaultVolume, false},
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
    tune_free_button_ = nullptr;
    model_selector_ = nullptr;
}

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
    frame = new CFrame(CRect(0, 0, kWidth, EditorBaseHeight(settings_.keyboard)), this);
    frame->setBackgroundColor(kBackground);
    applied_zoom_ = 1.0;
    drag_ = DragConstraint{};

    const auto title_font = makeOwned<CFontDesc>("Segoe UI", 20, kBoldFace);
    const auto name_font = makeOwned<CFontDesc>("Segoe UI", 10, kBoldFace);
    const auto value_font = makeOwned<CFontDesc>("Segoe UI", 13);

    frame->addView(MakeLabel(CRect(kMargin, 14, kWidth / 2, 42), stringPluginName, title_font,
                             kText, kLeftText));
    const CCoord button_left = kWidth - kMargin - kButtonWidth;
    frame->addView(MakeLabel(CRect(kWidth / 2, 18, button_left - 12, 40),
                             stringCompanyName "  " FULL_VERSION_STR, name_font, kDimText,
                             kRightText));
    keyboard_button_ = new ToggleButton(CRect(button_left, 18, kWidth - kMargin, 40), "KEYS",
                                        settings_.keyboard,
                                        [this](bool on) { SetKeyboardVisible(on); });
    frame->addView(keyboard_button_);

    const ParamValue model = controller ? controller->getParamNormalized(kParamModel) : 0.0;
    model_selector_ = new ModelSelector(
        CRect(kMargin, kSelectorTop, kWidth - kMargin, kSelectorBottom), ModelIndex(model),
        [this](int chosen) { ChooseModel(chosen); });
    frame->addView(model_selector_);

    keyboard_view_ = new KeyboardView(CRect(kMargin, kKeyboardTop, kWidth - kMargin,
                                            kKeyboardBottom),
                                      [this](int note) {
                                          if(callbacks_.strike)
                                              callbacks_.strike(note);
                                      },
                                      [this](int note) {
                                          if(callbacks_.release)
                                              callbacks_.release(note);
                                      });
    keyboard_view_->setVisible(settings_.keyboard);
    frame->addView(keyboard_view_);

    for(std::size_t i = 0; i < knobs_.size(); ++i) {
        const KnobSpec& spec = kKnobs[i];
        const CCoord left = kMargin + static_cast<CCoord>(i) * kColumnWidth;
        const CCoord knob_left = left + (kColumnWidth - kKnobSize) / 2;

        frame->addView(MakeLabel(CRect(left, kNameTop, left + kColumnWidth, kNameTop + 16),
                                 spec.name, name_font, kDimText, kCenterText));

        int32_t style = CKnob::kCoronaOutline | CKnob::kCoronaDrawing;
        if(spec.bipolar)
            style |= CKnob::kCoronaFromCenter;
        auto* knob = new CKnob(CRect(knob_left, kKnobTop, knob_left + kKnobSize,
                                     kKnobTop + kKnobSize),
                               this, static_cast<int32_t>(spec.id), nullptr, nullptr, CPoint(0, 0),
                               style);
        knob->setCoronaColor(kAccent);
        knob->setColorShadowHandle(kTrack);
        knob->setColorHandle(kText);
        knob->setHandleLineWidth(4);
        knob->setCoronaInset(5);
        knob->setInsetValue(15);
        knob->setDefaultValue(static_cast<float>(spec.default_value));
        // One semitone per wheel step for Tune, one hundredth for the others.
        knob->setWheelInc(spec.id == kParamTune ? static_cast<float>(0.5 / kTuneRange) : 0.01f);
        frame->addView(knob);
        if(spec.id == kParamTune) {
            // A small switch at the knob's lower right: free tuning on or off.
            const bool free = controller && IsOn(controller->getParamNormalized(kParamTuneFree));
            tune_free_button_ = new ToggleButton(
                CRect(left + kColumnWidth - 32, kKnobTop + kKnobSize - 8, left + kColumnWidth + 2,
                      kKnobTop + kKnobSize + 6),
                "FREE", free, [this](bool on) { SetTuneFree(on); });
            frame->addView(tune_free_button_);
        }

        auto* value = MakeLabel(CRect(left, kValueTop, left + kColumnWidth, kValueTop + 20), "",
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
    if(keyboard_view_)
        keyboard_view_->setVisible(settings_.keyboard);
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
    if(zoom == settings_.zoom && settings.keyboard == settings_.keyboard)
        return;
    settings_.zoom = zoom;
    settings_.keyboard = settings.keyboard;
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
    // Tune moves in whole semitones unless free tuning is on.
    if(id == kParamTune)
        normalized = TuneSounding(normalized, IsOn(controller->getParamNormalized(kParamTuneFree)));
    // The controller reports back through ParameterChanged(), which also puts
    // the knob on the semitone it landed on.
    controller->setParamNormalized(id, normalized);
    controller->performEdit(id, normalized);
}

void Editor::ChooseModel(int model) {
    if(!controller || model < 0 || model >= kModelCount)
        return;
    const ParamValue normalized = ModelNormalized(model);
    controller->beginEdit(kParamModel);
    controller->setParamNormalized(kParamModel, normalized);
    controller->performEdit(kParamModel, normalized);
    controller->endEdit(kParamModel);
}

void Editor::SetTuneFree(bool free) {
    if(!controller)
        return;
    const ParamValue normalized = free ? 1.0 : 0.0;
    controller->beginEdit(kParamTuneFree);
    controller->setParamNormalized(kParamTuneFree, normalized);
    controller->performEdit(kParamTuneFree, normalized);
    controller->endEdit(kParamTuneFree);
}

int Editor::ShownModel() const {
    return model_selector_ ? model_selector_->Model() : -1;
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
    if(id == kParamModel) {
        if(model_selector_)
            model_selector_->SetModel(ModelIndex(normalized));
        return;
    }
    if(id == kParamTuneFree) {
        if(tune_free_button_)
            tune_free_button_->SetOn(IsOn(normalized));
        // Tune reads in whole semitones or with decimals accordingly.
        for(Knob& knob : knobs_)
            if(knob.id == kParamTune && knob.knob && controller)
                ShowValue(knob, controller->getParamNormalized(kParamTune));
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

} // namespace torus_classic
