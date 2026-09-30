// view/Draw.h — pequeñas utilidades de color para la vista
#pragma once
#include "imgui.h"
#include "model/Settings.h"

inline ImVec4 toIm(const Color& c) { return ImVec4(c.r, c.g, c.b, c.a); }
inline ImVec4 mixc(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
inline ImVec4 withA(ImVec4 c, float a) { c.w = a; return c; }
inline ImU32 U32(const ImVec4& c) { return ImGui::GetColorU32(c); }

struct Pal {  // paleta ya convertida a ImVec4
    ImVec4 bg, panel, accent, accent2, text, good, warn, bad;
    explicit Pal(const Palette& p) : bg(toIm(p.bg)), panel(toIm(p.panel)), accent(toIm(p.accent)), accent2(toIm(p.accent2)), text(toIm(p.text)), good(toIm(p.good)), warn(toIm(p.warn)), bad(toIm(p.bad)) {}
    ImVec4 violet() const { return ImVec4(0.75f, 0.45f, 1.0f, 1); }
};
