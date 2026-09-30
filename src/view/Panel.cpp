// view/Panel.cpp
#include "Panel.h"

void Panel::draw(ViewContext& ctx) {
    const UiPrefs& ui = ctx.ui();
    if (!ui.show[id_]) return;
    if (ui.animations) {   // fundido de aparición escalonado
        double t = ImGui::GetTime() - ctx.vs.bootTime - 0.9 - id_ * 0.05;
        ImGui::SetNextWindowBgAlpha(ui.windowAlpha * (float)std::min(1.0, std::max(0.0, t / 0.5)));
    }
    bool open = true;
    if (ImGui::Begin(panelTitle(id_), &open, flags())) content(ctx);
    ImGui::End();
    if (!open) ctx.ctrl.setPanelVisible(id_, false);
}
