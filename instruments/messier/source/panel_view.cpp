#include "panel_view.h"

#include "gui_layout.h"
#include "guard.h"
#include "version.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <string>
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
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);

    // Use the fully open layout as the chassis coordinate system. Shorter layouts crop
    // the bottom so decorations stay aligned with the controls in every layout.
    const SkinImage& chassis = skin.Image(SkinImageId::kChassis);
    if(chassis) {
        const CCoord full_height = L::TopHeight(true) + L::kKeyboardSection;
        ConcatClip clip(*context, all);
        DrawSkinImage(context, chassis, CRect(all.left, all.top, all.right, all.top + full_height));
    } else {
        context->setFillColor(c.metal);
        context->drawRect(all, kDrawFilled);
    }

    // Beta 2: screws in three corners, if delivered (the fourth holds the resize grip).
    if(const SkinImage& screw = skin.Image(SkinImageId::kScrew)) {
        for(const CPoint at : {CPoint(all.left + 2, all.top + 2), CPoint(all.right - 16, all.top + 2),
                               CPoint(all.left + 2, all.bottom - 16)})
            DrawSkinImage(context, screw, CRect(at.x, at.y, at.x + 14, at.y + 14));
    }

    // Engraved rules (beta 2: fine ones only): under the header, above the info line, above
    // DETAILS while open, above the keyboard.
    const CCoord x0 = all.left + L::kMargin, x1 = all.right - L::kMargin;
    DrawRule(context, c, CPoint(x0, all.top + L::kHeaderRule), CPoint(x1, all.top + L::kHeaderRule));
    const L::BottomRows rows = L::Bottom();
    DrawRule(context, c, CPoint(x0, all.top + L::kInfoTop - 1), CPoint(x1, all.top + L::kInfoTop - 1));
    const CCoord bar = all.top + L::kInfoTop + L::kInfoHeight / 2;
    if(details_)
        DrawRule(context, c, CPoint(x0, all.top + L::kTopHeight), CPoint(x1, all.top + L::kTopHeight));
    if(keyboard_) {
        const CCoord y = all.top + L::TopHeight(details_) + L::kKeyboardRule;
        DrawRule(context, c, CPoint(x0, y), CPoint(x1, y));
    }

    // The maker and version, at the right end of the DETAILS bar; with a custom skin, how
    // many files and values its folders gave (0.2.7).
    std::string version = stringCompanyName " " DISPLAY_VERSION_STR;
    if(skin.Overrides() > 0)
        version = "CUSTOM SKIN " + std::to_string(skin.Overrides());
    DrawTracked(context, version, CRect(all.left + rows.version_left, bar - 8, x1, bar + 8), kRightText, 1.4,
                skin.Caps(10.5), c.dim, &c);

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
            case LabelKind::kScaleLow:
            case LabelKind::kScaleHigh:
                DrawTracked(context, label.text, box, label.kind == LabelKind::kScaleLow ? kRightText : kLeftText, 0.2,
                            skin.Sans(9.5), WithAlpha(c.dim, 0.85));
                break;
            case LabelKind::kValueBox:
                if(auto path = owned(context->createGraphicsPath())) {
                    path->addRoundRect(CRect(box).inset(0.5, 0.5), 4);
                    context->setFillColor(CColor(0, 0, 0, 70));
                    context->drawGraphicsPath(path, CDrawContext::kPathFilled);
                    context->setLineWidth(1);
                    context->setFrameColor(c.rule_light);
                    context->drawGraphicsPath(path, CDrawContext::kPathStroked);
                }
                break;
        }
    }
}

} // namespace messier
