// view/panels/ConfigPanels.cpp — mapa de memoria / estado inicial y ajustes de personalización.
// Trabajan sobre una COPIA de los ajustes y la envían al controlador cuando el usuario cambia algo.
#include "view/Panels.h"

using namespace arm;

void MemoryMapPanel::content(ViewContext& c) {
    Settings s = c.model.settings(); bool ch = false;
    const UiPrefs& ui = c.ui();
    auto hexIn = [&](const char* lbl, uint32_t& v) -> bool {
        char b[16]; std::snprintf(b, sizeof b, "%08X", v); ImGui::SetNextItemWidth(120 * ui.fontScale);
        if (ImGui::InputText(lbl, b, sizeof b, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase)) { v = (uint32_t)std::strtoul(b, nullptr, 16); return true; }
        return false;
    };
    ImGui::SeparatorText("Mapa de memoria (dónde se ensambla cada zona)");
    ch |= hexIn("Inicio del código (.text)", s.sim.opt.textBase);
    ch |= hexIn("Inicio de los datos (.data)", s.sim.opt.dataBase);
    ch |= hexIn("Base de la pila (SP inicial)", s.sim.opt.stackTop);
    ImGui::TextDisabled("También puedes usar  .org DIRECCIÓN  dentro del programa para colocar datos o código en una dirección concreta.");
    ImGui::SeparatorText("Valores iniciales de registros");
    for (int i = 0; i < 16; i++) {
        ImGui::PushID(i); ch |= ImGui::Checkbox("##on", &s.sim.initRegSet[i]); ImGui::SameLine(); ImGui::Text("%-3s", regLabel(i).c_str()); ImGui::SameLine();
        ImGui::BeginDisabled(!s.sim.initRegSet[i]); ch |= hexIn("##v", s.sim.initRegs[i]); ImGui::EndDisabled(); ImGui::PopID();
        if (i % 2 == 0 && i < 15) ImGui::SameLine();
    }
    ImGui::SeparatorText("Precarga de memoria (se aplica al reiniciar)");
    int del = -1;
    for (size_t i = 0; i < s.sim.preloads.size(); i++) {
        auto& pl = s.sim.preloads[i]; ImGui::PushID((int)i);
        ch |= ImGui::Checkbox("##en", &pl.enabled); ImGui::SameLine(); ch |= hexIn("##ad", pl.addr); ImGui::SameLine();
        char b[256]; std::snprintf(b, sizeof b, "%s", pl.words.c_str()); ImGui::SetNextItemWidth(180 * ui.fontScale);
        if (ImGui::InputText("##w", b, sizeof b)) { pl.words = b; ch = true; }
        ImGui::SameLine(); ch |= ImGui::Checkbox("bytes", &pl.bytes); ImGui::SameLine(); if (ImGui::SmallButton("X")) del = (int)i; ImGui::PopID();
    }
    if (del >= 0) { s.sim.preloads.erase(s.sim.preloads.begin() + del); ch = true; }
    if (ImGui::Button("Añadir bloque de memoria")) { s.sim.preloads.push_back(Preload()); ch = true; }
    if (ch) c.ctrl.updateSettings(s);
    ImGui::Separator();
    if (ImGui::Button("Aplicar y reensamblar")) c.ctrl.assemble();
}

void SettingsPanel::content(ViewContext& c) {
    Settings s = c.model.settings(); UiPrefs& u = s.ui; bool ch = false;
    ImGui::SeparatorText("Tema de color");
    const auto& presets = themePresets();
    for (int i = 0; i < (int)presets.size(); i++) { if (ImGui::Button(presets[i].name)) { c.ctrl.applyThemePreset(i); return; } if (i + 1 < (int)presets.size()) ImGui::SameLine(); }
    auto col = [&](const char* n, Color& cc) { float v[3] = {cc.r, cc.g, cc.b}; if (ImGui::ColorEdit3(n, v)) { cc.r = v[0]; cc.g = v[1]; cc.b = v[2]; ch = true; } };
    col("Fondo", u.col.bg); col("Paneles", u.col.panel); col("Acento principal", u.col.accent); col("Acento secundario", u.col.accent2);
    col("Texto", u.col.text); col("Correcto / registros", u.col.good); col("Aviso", u.col.warn); col("Error", u.col.bad);
    ImGui::SeparatorText("Texto y forma");
    ch |= ImGui::SliderFloat("Tamaño del texto", &u.fontScale, 0.6f, 3.0f, "%.2fx");
    ch |= ImGui::SliderFloat("Redondeo", &u.rounding, 0.0f, 16.0f, "%.0f");
    ch |= ImGui::SliderFloat("Brillo de bordes", &u.glow, 0.0f, 1.0f);
    ch |= ImGui::SliderFloat("Opacidad de ventanas", &u.windowAlpha, 0.3f, 1.0f);
    ImGui::SeparatorText("Animaciones");
    ch |= ImGui::Checkbox("Animaciones activadas", &u.animations); ch |= ImGui::SliderFloat("Velocidad de animación", &u.animSpeed, 0.2f, 3.0f, "%.1fx");
    ch |= ImGui::Checkbox("Rejilla de fondo", &u.bgGrid); ImGui::SameLine(); ch |= ImGui::Checkbox("Pulsos de circuito", &u.bgPulses); ImGui::SameLine(); ch |= ImGui::Checkbox("Líneas de barrido (CRT)", &u.scanlines);
    ImGui::SeparatorText("Comportamiento");
    ch |= ImGui::Checkbox("Seguir la ejecución en el editor", &u.followExec); ImGui::SameLine(); ch |= ImGui::Checkbox("Mostrar FPS", &u.showFps);
    const char* fm[] = {"Hex", "Dec con signo", "Dec sin signo", "Binario"}; ch |= ImGui::Combo("Formato de registros", &u.regFormat, fm, 4);
    if (ch) c.ctrl.updateSettings(s);
    ImGui::Separator();
    if (ImGui::Button("Restablecer todo")) { c.ctrl.resetSettings(); c.vs.resetLayout = true; }
    ImGui::SameLine(); ImGui::TextDisabled("Los ajustes se guardan solos al cerrar.");
}
