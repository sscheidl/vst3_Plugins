#include "panel_view.h"

#include "gui_layout.h"
#include "guard.h"
#include "version.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <utility>

namespace messier {

using namespace VSTGUI;
namespace L = layout;

PanelView::PanelView(const CRect& size) : CView(size) {
    setMouseEnabled(false);
}

void PanelView::AddLabel(const CRect& box, std::string text, LabelKind kind) {
    labels_.push_back({box, std::move(text), kind});
}

void PanelView::SetSkin(std::shared_ptr<const Skin> skin) {
    skin_ = std::move(skin);
    invalid();
}

void PanelView::SetKeyboard(bool shown) {
    keyboard_ = shown;
    invalid();
}

void PanelView::draw(CDrawContext* context) {
    Guarded([&] { DrawUnchecked(context); }); // MS-R37
}

void PanelView::DrawUnchecked(CDrawContext* context) {
    setDirty(false);
    if(!skin_)
        return;
    const Skin& skin = *skin_;
    const SkinColours& c = skin.Colours();
    const CRect all = getViewSize();
    const CCoord height = all.getHeight();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);

    // The chassis: its upper part without the keyboard, all of it with.
    const SkinImage& chassis = skin.Image(SkinImageId::kChassis);
    if(chassis) {
        const CCoord shown = std::min<CCoord>(height, chassis.size.y);
        DrawSkinImagePart(context, chassis, CRect(0, 0, chassis.size.x, shown),
                          CRect(all.left, all.top, all.right, all.top + shown));
    } else {
        context->setFillColor(c.metal);
        context->drawRect(all, kDrawFilled);
    }

    // No screws since 0.2.2 (the user's design round): the slate runs through to the
    // corners; the resize grip keeps its corner.

    // Engraved rules: under the header, above the knobs, between their rows, beside
    // Volume's column, above the keyboard.
    const CCoord x0 = all.left + L::kMargin, x1 = all.right - L::kMargin;
    DrawRule(context, c, CPoint(x0, all.top + L::kHeaderRule), CPoint(x1, all.top + L::kHeaderRule));
    DrawRule(context, c, CPoint(x0, all.top + L::kRuleAboveKnobs), CPoint(x1, all.top + L::kRuleAboveKnobs));
    DrawRule(context, c, CPoint(x0, all.top + L::kRuleBetweenRows),
             CPoint(all.left + L::kColumnRuleX - 4, all.top + L::kRuleBetweenRows));
    DrawRule(context, c, CPoint(all.left + L::kColumnRuleX, all.top + L::kNameTop),
             CPoint(all.left + L::kColumnRuleX, all.top + L::kSmallValueTop + 14));
    if(keyboard_)
        DrawRule(context, c, CPoint(x0, all.top + L::kKeyboardRule), CPoint(x1, all.top + L::kKeyboardRule));

    // The header: brand, an accent stroke, the subtitle; the maker and version. Only when
    // the header is redrawn (the texts cost more than the plate's frame).
    CRect header;
    context->getClipRect(header);
    if(header.top < all.top + L::kHeaderRule) {
        const CFontRef brand_font = skin.Caps(30);
        const CCoord brand_width = TrackedWidth(context, "MESSIER", 9, brand_font);
        const CCoord brand_left = all.left + 40;
        DrawTracked(context, "MESSIER", CRect(brand_left, all.top + 5, brand_left + brand_width + 4, all.top + 41),
                    kLeftText, 9, brand_font, c.text, &c);
        const CCoord stroke = brand_left + brand_width + 14;
        context->setFillColor(c.control);
        context->drawRect(CRect(stroke, all.top + 12, stroke + 2, all.top + 36), kDrawFilled);
        DrawTracked(context, "granular looper & phase vocoder",
                    CRect(stroke + 14, all.top + 10, all.left + L::kVersionRight - 120, all.top + 36), kLeftText, 0.8,
                    skin.Italic(16.5), c.text, &c);
        DrawTracked(context, stringCompanyName " " FULL_VERSION_STR,
                    CRect(all.left + L::kVersionRight - 160, all.top + 14, all.left + L::kVersionRight, all.top + 36),
                    kRightText, 1.6, skin.Caps(12), c.text, &c);
    }

    // The file line's stroke between LOAD... and the name.
    context->setFillColor(c.control);
    context->drawRect(CRect(all.left + 122, all.top + L::kLoadTop, all.left + 124, all.top + L::kLoadBottom),
                      kDrawFilled);

    // The knobs' names in the controls' sans serif (0.2.1, the user's wish; since 0.2.2
    // regular and spaced wider). Only those the redrawn area touches: the plate is drawn
    // about 40 times a second.
    CRect clip;
    context->getClipRect(clip);
    for(const Label& label : labels_) {
        const CRect box = CRect(label.box).offset(all.left, all.top);
        if(!CRect(box).extend(4, 4).rectOverlap(clip))
            continue;
        switch(label.kind) {
            case LabelKind::kKnob:
                DrawTracked(context, label.text, box, kCenterText, 1.9, skin.Sans(13.5), c.text, &c);
                break;
            case LabelKind::kSmallKnob:
                DrawTracked(context, label.text, box, kCenterText, 1.5, skin.Sans(12), c.text, &c);
                break;
        }
    }
}

} // namespace messier
