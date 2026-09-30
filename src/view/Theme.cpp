// view/Theme.cpp
#include "Theme.h"
#include "Draw.h"

void applyTheme(const UiPrefs& ui) {
    ImGuiStyle& s = ImGui::GetStyle();
    Pal c(ui.col);
    s.WindowRounding = s.ChildRounding = s.PopupRounding = s.ScrollbarRounding = ui.rounding;
    s.FrameRounding = s.GrabRounding = s.TabRounding = ui.rounding * 0.7f;
    s.WindowBorderSize = 1; s.FrameBorderSize = 0; s.WindowPadding = ImVec2(10, 8); s.FramePadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(8, 6); s.ScrollbarSize = 12; s.TabBarBorderSize = 1;
    s.FontScaleMain = ui.fontScale;
    bool light = (c.bg.x + c.bg.y + c.bg.z) > 1.5f;
    ImVec4 frame = mixc(c.panel, c.accent, light ? 0.10f : 0.14f);
    ImVec4* k = s.Colors;
    k[ImGuiCol_Text] = c.text; k[ImGuiCol_TextDisabled] = mixc(c.text, c.bg, 0.55f);
    k[ImGuiCol_WindowBg] = withA(c.panel, ui.windowAlpha); k[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    k[ImGuiCol_PopupBg] = withA(mixc(c.panel, c.bg, 0.3f), 0.98f);
    k[ImGuiCol_Border] = withA(c.accent, 0.35f * (0.4f + ui.glow)); k[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    k[ImGuiCol_FrameBg] = frame; k[ImGuiCol_FrameBgHovered] = mixc(frame, c.accent, 0.25f); k[ImGuiCol_FrameBgActive] = mixc(frame, c.accent, 0.4f);
    k[ImGuiCol_TitleBg] = mixc(c.bg, c.panel, 0.6f); k[ImGuiCol_TitleBgActive] = mixc(c.panel, c.accent, 0.22f); k[ImGuiCol_TitleBgCollapsed] = c.bg;
    k[ImGuiCol_MenuBarBg] = mixc(c.bg, c.panel, 0.7f);
    k[ImGuiCol_ScrollbarBg] = withA(c.bg, 0.4f); k[ImGuiCol_ScrollbarGrab] = withA(c.accent, 0.5f);
    k[ImGuiCol_ScrollbarGrabHovered] = withA(c.accent, 0.8f); k[ImGuiCol_ScrollbarGrabActive] = c.accent;
    k[ImGuiCol_CheckMark] = c.accent; k[ImGuiCol_SliderGrab] = c.accent; k[ImGuiCol_SliderGrabActive] = c.accent2;
    k[ImGuiCol_Button] = mixc(c.panel, c.accent, 0.22f); k[ImGuiCol_ButtonHovered] = mixc(c.panel, c.accent, 0.5f); k[ImGuiCol_ButtonActive] = mixc(c.panel, c.accent, 0.75f);
    k[ImGuiCol_Header] = mixc(c.panel, c.accent, 0.25f); k[ImGuiCol_HeaderHovered] = mixc(c.panel, c.accent, 0.45f); k[ImGuiCol_HeaderActive] = mixc(c.panel, c.accent, 0.6f);
    k[ImGuiCol_Separator] = withA(c.accent, 0.3f); k[ImGuiCol_SeparatorHovered] = c.accent; k[ImGuiCol_SeparatorActive] = c.accent2;
    k[ImGuiCol_ResizeGrip] = withA(c.accent, 0.25f); k[ImGuiCol_ResizeGripHovered] = withA(c.accent, 0.6f); k[ImGuiCol_ResizeGripActive] = c.accent;
    k[ImGuiCol_Tab] = mixc(c.bg, c.panel, 0.7f); k[ImGuiCol_TabHovered] = mixc(c.panel, c.accent, 0.5f); k[ImGuiCol_TabSelected] = mixc(c.panel, c.accent, 0.35f);
    k[ImGuiCol_TabDimmed] = mixc(c.bg, c.panel, 0.5f); k[ImGuiCol_TabDimmedSelected] = mixc(c.panel, c.accent, 0.2f);
    k[ImGuiCol_DockingPreview] = withA(c.accent, 0.5f); k[ImGuiCol_DockingEmptyBg] = c.bg;
    k[ImGuiCol_TableHeaderBg] = mixc(c.panel, c.accent, 0.2f); k[ImGuiCol_TableBorderStrong] = withA(c.accent, 0.3f); k[ImGuiCol_TableBorderLight] = withA(c.accent, 0.12f);
    k[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0); k[ImGuiCol_TableRowBgAlt] = withA(c.accent, 0.04f);
    k[ImGuiCol_TextSelectedBg] = withA(c.accent, 0.35f); k[ImGuiCol_NavCursor] = c.accent2;
}

TextEditor::Palette makeEditorPalette(const UiPrefs& ui) {
    Pal c(ui.col);
    TextEditor::Palette p = TextEditor::GetDarkPalette();
    using PI = TextEditor::PaletteIndex;
    auto S = [&](PI i, ImVec4 v) { p[(int)i] = ImGui::GetColorU32(v); };
    S(PI::Default, c.text); S(PI::Keyword, c.accent); S(PI::Number, c.accent2); S(PI::String, c.good); S(PI::CharLiteral, c.good);
    S(PI::Punctuation, mixc(c.text, c.bg, 0.4f)); S(PI::Preprocessor, c.violet()); S(PI::Identifier, c.text);
    S(PI::KnownIdentifier, c.good); S(PI::PreprocIdentifier, c.violet());
    S(PI::Comment, mixc(c.text, c.bg, 0.6f)); S(PI::MultiLineComment, mixc(c.text, c.bg, 0.6f));
    S(PI::Background, withA(mixc(c.bg, c.panel, 0.4f), 0.55f)); S(PI::Cursor, c.accent);
    S(PI::Selection, withA(c.accent, 0.30f)); S(PI::ErrorMarker, withA(c.bad, 0.45f)); S(PI::Breakpoint, withA(c.bad, 0.40f));
    S(PI::LineNumber, mixc(c.text, c.bg, 0.5f)); S(PI::CurrentLineFill, withA(c.accent, 0.10f));
    S(PI::CurrentLineFillInactive, withA(c.accent, 0.05f)); S(PI::CurrentLineEdge, withA(c.accent, 0.35f));
    return p;
}
