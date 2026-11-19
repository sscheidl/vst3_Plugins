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

void PanelView::SetSections(bool keyboard, bool details) {
    keyboard_ = keyboard;
    details_ = details;
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

    // The chassis: as much of it as the window shows; a window taller than the bitmap
    // (DETAILS and the keyboard both open) gets it stretched, a plain material.
    const SkinImage& chassis = skin.Image(SkinImageId::kChassis);
    if(chassis) {
        const CCoord shown = std::min<CCoord>(height, chassis.size.y);
        DrawSkinImagePart(context, chassis, CRect(0, 0, chassis.size.x, shown), all);
    } else {
        context->setFillColor(c.metal);
        context->drawRect(all, kDrawFilled);
    }

    // Engraved rules (0.2.4): under the header, above the knobs, between the three groups,
    // on both sides of DETAILS, above the keyboard.
    const CCoord x0 = all.left + L::kMargin, x1 = all.right - L::kMargin;
    DrawRule(context, c, CPoint(x0, all.top + L::kHeaderRule), CPoint(x1, all.top + L::kHeaderRule));
    DrawRule(context, c, CPoint(x0, all.top + L::kRuleAboveKnobs), CPoint(x1, all.top + L::kRuleAboveKnobs));
    for(const CCoord x : {L::kGroupRule1, L::kGroupRule2})
        DrawRule(context, c, CPoint(all.left + x, all.top + L::kNameTop), CPoint(all.left + x, all.top + L::kValueTop + 16));
    const CCoord bar = all.top + L::kDetailsBarTop + L::kDetailsBarHeight / 2;
    DrawRule(context, c, CPoint(x0, bar), CPoint(all.left + L::kDetailsLeft - 8, bar));
    DrawRule(context, c, CPoint(all.left + L::kDetailsRight + 8, bar), CPoint(x1 - 120, bar));
    if(keyboard_) {
        const CCoord y = all.top + L::TopHeight(details_) + L::kKeyboardRule;
        DrawRule(context, c, CPoint(x0, y), CPoint(x1, y));
    }

    // The maker and version, at the right end of the DETAILS bar.
    DrawTracked(context, stringCompanyName " " FULL_VERSION_STR,
                CRect(x1 - 112, bar - 8, x1, bar + 8), kRightText, 1.4, skin.Caps(10.5), c.dim, &c);

    // The header: the brand and an accent stroke before LOAD. Only when the header is
    // redrawn (the texts cost more than the plate's frame).
    CRect header;
    context->getClipRect(header);
    if(header.top < all.top + L::kHeaderRule) {
        const CFontRef brand_font = skin.Sans(25); // the template: a clear sans serif
        const CCoord brand_left = all.left + L::kBrandLeft;
        DrawTracked(context, "MESSIER", CRect(brand_left, all.top + 8, all.left + L::kBrandStroke - 8, all.top + 44),
                    kLeftText, 6, brand_font, c.text, &c);
        context->setFillColor(c.control);
        context->drawRect(CRect(all.left + L::kBrandStroke, all.top + 14, all.left + L::kBrandStroke + 2, all.top + 38),
                          kDrawFilled);
    }

    // The knobs' names in the controls' sans serif (0.2.1, the user's wish; since 0.2.2
    // regular and spaced wider). Only those the redrawn area touches: the plate is drawn
    // about 40 times a second.
    CRect clip;
    context->getClipRect(clip);
    for(const Label& label : labels_) {
        const CRect box = CRect(label.box).offset(all.left, all.top);
        if(!CRect(box).extend(4, 4).rectOverlap(clip) || (label.kind == LabelKind::kSmallKnob && !details_))
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
