#include "editor.h"

#include "parameters.h"
#include "version.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/ctextlabel.h"

#include <cmath>
#include <string>

namespace taureon::toy_piano_classic {

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace {

// Layout in unscaled pixels.
constexpr CCoord kWidth = 460;
constexpr CCoord kHeight = 190;
constexpr CCoord kMargin = 20;
constexpr CCoord kColumnWidth = (kWidth - 2 * kMargin) / 4;
constexpr CCoord kKnobSize = 64;
constexpr CCoord kNameTop = 58;
constexpr CCoord kKnobTop = 82;
constexpr CCoord kValueTop = 152;

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

constexpr KnobSpec kKnobs[] = {
    {kParamMasterPitch, "MASTER PITCH", kDefaultMasterPitch, true},
    {kParamAttack, "ATTACK", kDefaultAttack, false},
    {kParamDecay, "DECAY", kDefaultDecay, false},
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

Editor::Editor(EditController* controller) : VSTGUIEditor(controller) {
    ViewRect size(0, 0, static_cast<int32>(kWidth), static_cast<int32>(kHeight));
    setRect(size);
}

tresult PLUGIN_API Editor::queryInterface(const TUID _iid, void** obj) {
    QUERY_INTERFACE(_iid, obj, IPlugViewContentScaleSupport::iid, IPlugViewContentScaleSupport)
    return VSTGUIEditor::queryInterface(_iid, obj);
}

bool PLUGIN_API Editor::open(void* parent, const PlatformType& platformType) {
    frame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
    frame->setBackgroundColor(kBackground);

    const auto title_font = makeOwned<CFontDesc>("Segoe UI", 20, kBoldFace);
    const auto name_font = makeOwned<CFontDesc>("Segoe UI", 11, kBoldFace);
    const auto value_font = makeOwned<CFontDesc>("Segoe UI", 14);

    frame->addView(MakeLabel(CRect(kMargin, 14, kWidth / 2 + 60, 42), stringPluginName,
                             title_font, kText, kLeftText));
    frame->addView(MakeLabel(CRect(kWidth / 2 + 60, 18, kWidth - kMargin, 40),
                             stringCompanyName "  " FULL_VERSION_STR, name_font, kDimText,
                             kRightText));

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
        knob->setInsetValue(16);
        knob->setDefaultValue(static_cast<float>(spec.default_value));
        knob->setWheelInc(spec.id == kParamMasterPitch ? 1.0f / kMasterPitchSteps : 0.01f);
        frame->addView(knob);

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
        knobs_ = {};
        return false;
    }
    frame->setZoom(scale_);
    // `rect` holds the size the host knows. A host that asked for the size
    // before it told us the scale factor made its window too small.
    ViewRect wanted = ScaledSize();
    if(plugFrame && (rect.getWidth() != wanted.getWidth() || rect.getHeight() != wanted.getHeight()))
        plugFrame->resizeView(this, &wanted);
    return true;
}

ViewRect Editor::ScaledSize() const {
    return ViewRect(0, 0, static_cast<int32>(std::lround(kWidth * scale_)),
                    static_cast<int32>(std::lround(kHeight * scale_)));
}

tresult PLUGIN_API Editor::getSize(ViewRect* size) {
    // Before the window exists, answer with the size it will have.
    if(!frame)
        setRect(ScaledSize());
    return VSTGUIEditor::getSize(size);
}

void PLUGIN_API Editor::close() {
    knobs_ = {};
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
    if(id == kParamMasterPitch) // whole semitones only
        normalized = MasterPitchNormalized(MasterPitchSemitones(normalized));
    // The controller reports back through ParameterChanged(), which also puts
    // the knob on the semitone it snapped to.
    controller->setParamNormalized(id, normalized);
    controller->performEdit(id, normalized);
}

void Editor::ParameterChanged(ParamID id, ParamValue normalized) {
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
    if(frame) {
        frame->setZoom(scale_);
        // Ask first, with `rect` still at the old size: hosts compare the two
        // and answer with onSize(), which updates `rect`.
        ViewRect wanted = ScaledSize();
        if(plugFrame)
            plugFrame->resizeView(this, &wanted);
    }
    return kResultOk;
}

} // namespace taureon::toy_piano_classic
