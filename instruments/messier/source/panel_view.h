// The window's background (0.2.0): the chassis bitmap of the GUI preset, the engraved
// rules, and the fixed engraved texts: the brand, the maker and version, the knobs'
// names. It takes no mouse; the controls lie on top of it. The secondary knobs' names
// only while DETAILS is open. Beta 2: fine rules only (under the header, above the info
// line, above DETAILS and the keyboard), the scales' ends and the boxes of the values.
#pragma once

#include "gui_skin.h"

#include "vstgui/lib/cview.h"

#include <memory>
#include <string>
#include <vector>

namespace messier {

class PanelView : public VSTGUI::CView {
  public:
    explicit PanelView(const VSTGUI::CRect& size);

    // Beta 2: also the scale's ends beside a knob (low: right-aligned, high: left-aligned)
    // and the box behind a knob's value (no text).
    enum class LabelKind { kKnob, kSmallKnob, kScaleLow, kScaleHigh, kValueBox };
    // A fixed text, centred in `box`.
    void AddLabel(const VSTGUI::CRect& box, std::string text, LabelKind kind);
    void SetSkin(std::shared_ptr<const Skin> skin);
    // Which sections are shown (0.2.4): the keyboard, DETAILS' secondary knobs.
    void SetSections(bool keyboard, bool details);

    void draw(VSTGUI::CDrawContext* context) override;
    void DrawUnchecked(VSTGUI::CDrawContext* context);

  private:
    struct Label {
        VSTGUI::CRect box;
        std::string text;
        LabelKind kind;
    };
    std::vector<Label> labels_;
    std::shared_ptr<const Skin> skin_;
    bool keyboard_ = false;
    bool details_ = false;
};

} // namespace messier
