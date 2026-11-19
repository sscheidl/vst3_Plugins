// The window's background (0.2.0): the chassis bitmap of the GUI preset, the engraved
// rules, and the fixed engraved texts: the brand, the subtitle, the version and the
// knobs' names. It takes no mouse; the controls lie on top of it. Since 0.2.2 without
// screws, and without the places kept for the audio input (they return with 0.3.0).
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

    enum class LabelKind { kKnob, kSmallKnob };
    // A fixed text, centred in `box`.
    void AddLabel(const VSTGUI::CRect& box, std::string text, LabelKind kind);
    void SetSkin(std::shared_ptr<const Skin> skin);
    // The keyboard section below: its rule.
    void SetKeyboard(bool shown);

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
};

} // namespace messier
