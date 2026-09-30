// view/panels/StatePanels.cpp — Registros, memoria, pila y símbolos.
// Estos paneles LEEN el estado de la máquina; toda modificación se pide al controlador.
#include "view/Panels.h"
#include <cmath>
#include <algorithm>
#include <cstring>

using namespace arm;

static const char* REG_ROLE[16] = {"argumento 1 / resultado","argumento 2","argumento 3","argumento 4","a preservar","a preservar","a preservar","a preservar","a preservar","a preservar","a preservar","a preservar (FP)","temporal (IP)","puntero de pila","registro de enlace (retorno)","contador de programa"};

static std::string fmtVal(uint32_t v, int f) {
    char b[48];
    switch (f) { case 0: return hx0x(v); case 1: std::snprintf(b, sizeof b, "%d", (int32_t)v); return b; case 2: std::snprintf(b, sizeof b, "%u", v); return b; default: return bin(v, 32); }
}

// ------------------------------------ Registros ------------------------------------
void RegistersPanel::content(ViewContext& c) {
    const Machine* mach = c.model.machine(); const UiPrefs& ui = c.ui(); Pal p(ui.col);
    if (!mach) { ImGui::TextDisabled("Sin máquina: ensambla un programa válido."); return; }
    const char* fmts[] = {"Hex", "Dec (con signo)", "Dec (sin signo)", "Binario"};
    int fmt = ui.regFormat;
    ImGui::SetNextItemWidth(160 * ui.fontScale);
    if (ImGui::Combo("Formato", &fmt, fmts, 4)) { Settings s = c.model.settings(); s.ui.regFormat = fmt; c.ctrl.updateSettings(s); }
    if (ImGui::BeginTable("##regs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Reg"); ImGui::TableSetupColumn("Valor", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Uso");
        for (int i = 0; i < 16; i++) {
            ImGui::TableNextRow();
            uint32_t v = (i == 15) ? mach->pc : mach->r[i];
            float fl = c.vs.regFlash[i];
            if (fl > 0) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, U32(withA(p.accent2, 0.45f * fl)));
            ImGui::TableNextColumn(); ImGui::TextColored(i >= 13 ? p.accent2 : p.accent, "%s", regLabel(i).c_str());
            ImGui::TableNextColumn();
            if (editReg_ == i) {
                ImGui::SetNextItemWidth(-1); ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##e", buf_, sizeof buf_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    uint32_t nv; if (c.ctrl.evalNumber(buf_, 0, nv)) { c.ctrl.setRegister(i, nv); c.vs.regFlash[i] = 1; } else c.ctrl.notifyStatus("Valor no válido para el registro", StatusKind::Error);
                    editReg_ = -1;
                } else if (ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) editReg_ = -1;
            } else {
                std::string s = fmtVal(v, ui.regFormat);
                ImGui::PushStyleColor(ImGuiCol_Text, mixc(p.text, p.good, fl));
                ImGui::Selectable((s + "##v" + std::to_string(i)).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { editReg_ = i; std::snprintf(buf_, sizeof buf_, "0x%X", v); }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::Text("hex %s\ndec %d\nsin signo %u\nbin %s\n(doble clic para editar)", hx0x(v).c_str(), (int32_t)v, v, bin(v, 32).c_str()); ImGui::EndTooltip(); }
            }
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", REG_ROLE[i]);
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Flags (CPSR)");
    struct F { char n; bool v; const char* d; } fl[4] = {{'N', mach->f.N, "Negativo"}, {'Z', mach->f.Z, "Cero"}, {'C', mach->f.C, "Carry"}, {'V', mach->f.V, "Overflow"}};
    for (int i = 0; i < 4; i++) {
        ImGui::PushStyleColor(ImGuiCol_Button, fl[i].v ? mixc(p.panel, p.good, 0.6f) : mixc(p.panel, p.bg, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_Text, fl[i].v ? p.bg : mixc(p.text, p.bg, 0.4f));
        std::string lbl = std::string(1, fl[i].n) + " " + (fl[i].v ? "1" : "0");
        if (ImGui::Button(lbl.c_str(), ImVec2(52 * ui.fontScale, 0))) c.ctrl.toggleFlag(fl[i].n);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (clic para cambiar)", fl[i].d);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::TextDisabled("Condiciones que se cumplen ahora:");
    for (int i = 0; i < 14; i++) {
        bool ok = condPass(i, mach->f);
        ImGui::TextColored(ok ? p.good : withA(p.text, 0.25f), "%s", COND_TABLE[i].mn);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s)", COND_TABLE[i].name, COND_TABLE[i].flagsText);
        if (i % 7 != 6 && i != 13) ImGui::SameLine();
    }
}

// ------------------------------------- Memoria -------------------------------------
void MemoryPanel::content(ViewContext& c) {
    const Machine* mach = c.model.machine(); const Program& prog = c.model.program(); const UiPrefs& ui = c.ui(); Pal p(ui.col);
    ViewState& vs = c.vs;
    if (!mach) { ImGui::TextDisabled("Sin máquina."); return; }
    ImGui::SetNextItemWidth(120 * ui.fontScale);
    if (ImGui::InputText("Ir a", vs.memGoto, sizeof vs.memGoto, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase)) vs.memBase = (uint32_t)std::strtoul(vs.memGoto, nullptr, 16) & ~3u;
    ImGui::SameLine(); if (ImGui::SmallButton("PC")) c.goMem(mach->pc);
    ImGui::SameLine(); if (ImGui::SmallButton("SP")) c.goMem(mach->r[13]);
    ImGui::SameLine(); if (ImGui::SmallButton("Código")) c.goMem(prog.textStart);
    ImGui::SameLine(); if (ImGui::SmallButton("Datos")) c.goMem(prog.dataStart);
    ImGui::SameLine(); if (ImGui::SmallButton("<<")) vs.memBase -= ui.memCols * 16;
    ImGui::SameLine(); if (ImGui::SmallButton(">>")) vs.memBase += ui.memCols * 16;
    ImGui::SameLine(); ImGui::SetNextItemWidth(70 * ui.fontScale);
    static const char* colLabels[] = {"4", "8", "16", "32"}; static const int colVals[] = {4, 8, 16, 32};
    int ci = ui.memCols == 4 ? 0 : ui.memCols == 8 ? 1 : ui.memCols == 16 ? 2 : 3;
    bool changed = ImGui::Combo("cols", &ci, colLabels, 4);
    bool ascii = ui.memAscii; ImGui::SameLine(); changed |= ImGui::Checkbox("ASCII", &ascii);
    if (changed) { Settings s = c.model.settings(); s.ui.memCols = colVals[ci]; s.ui.memAscii = ascii; c.ctrl.updateSettings(s); }

    // escribir un valor en cualquier dirección
    ImGui::SetNextItemWidth(100 * ui.fontScale); ImGui::InputText("##wa", wa_, sizeof wa_, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine(); ImGui::SetNextItemWidth(130 * ui.fontScale); ImGui::InputText("##wv", wv_, sizeof wv_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90 * ui.fontScale); const char* szs[] = {"byte", "media", "palabra"}; ImGui::Combo("##ws", &wsz_, szs, 3);
    ImGui::SameLine();
    if (ImGui::Button("Escribir en memoria")) {
        uint32_t a = (uint32_t)std::strtoul(wa_, nullptr, 16), v;
        if (c.ctrl.evalNumber(wv_, a, v)) { c.ctrl.writeMemory(a, v, wsz_ == 0 ? 1 : wsz_ == 1 ? 2 : 4); c.goMem(a); for (int i = 0; i < 4; i++) vs.memFlash[(a + i) & ~3u] = 1; }
        else c.ctrl.notifyStatus("Valor no válido", StatusKind::Error);
    }
    ImGui::Separator();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float insH = ImGui::GetTextLineHeightWithSpacing() * 4;
    ImVec2 o = ImGui::GetCursorScreenPos(); float lh = ImGui::GetTextLineHeight() + 2;
    int rows = std::max(1, (int)((avail.y - insH) / lh));
    ImGui::InvisibleButton("##memgrid", ImVec2(avail.x, rows * lh));
    bool hovered = ImGui::IsItemHovered();
    if (hovered) { float wh = ImGui::GetIO().MouseWheel; if (wh != 0) vs.memBase -= (int)(wh * 3) * ui.memCols; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float chw = ImGui::CalcTextSize("0").x, addrW = chw * 11, cell = chw * 3;
    int cols = ui.memCols; ImVec2 mp = ImGui::GetMousePos();
    std::map<uint32_t, std::string> labels; for (auto& kv : prog.symbols) labels[kv.second & ~(uint32_t)(cols - 1)] += kv.first + " ";
    uint32_t sp = mach->r[13], pc = mach->pc;
    int selRow = -1, selCol = -1;
    if (hovered && mp.y >= o.y) { selRow = (int)((mp.y - o.y) / lh); selCol = (int)((mp.x - o.x - addrW) / cell); }
    for (int r = 0; r < rows; r++) {
        uint32_t ra = vs.memBase + (uint32_t)(r * cols); float y = o.y + r * lh;
        dl->AddText(ImVec2(o.x, y), U32(p.accent2), hex(ra).c_str());
        for (int ci2 = 0; ci2 < cols; ci2++) {
            uint32_t a = ra + ci2; uint8_t b = mach->readByte(a); float x = o.x + addrW + ci2 * cell;
            ImVec4 col = b ? p.text : withA(p.text, 0.3f);
            auto fl = vs.memFlash.find(a & ~3u);
            if (fl != vs.memFlash.end()) dl->AddRectFilled(ImVec2(x - 2, y), ImVec2(x + cell - 2, y + lh), U32(withA(p.accent2, 0.5f * fl->second)));
            else if (mach->touched.count(a)) col = p.accent;
            if (a >= sp && a < sp + 4) dl->AddRect(ImVec2(x - 2, y), ImVec2(x + cell - 2, y + lh), U32(p.good));
            if (a >= pc && a < pc + 4) dl->AddRect(ImVec2(x - 2, y), ImVec2(x + cell - 2, y + lh), U32(p.warn));
            if (hovered && r == selRow && ci2 == selCol) { dl->AddRectFilled(ImVec2(x - 2, y), ImVec2(x + cell - 2, y + lh), U32(withA(p.accent, 0.25f))); vs.memSelAddr = a; }
            if (editAddr_ == (int)a) continue;
            char t[4]; std::snprintf(t, sizeof t, "%02X", b); dl->AddText(ImVec2(x, y), U32(col), t);
        }
        if (ui.memAscii) {
            float ax = o.x + addrW + cols * cell + chw;
            std::string s; for (int ci2 = 0; ci2 < cols; ci2++) { uint8_t b = mach->readByte(ra + ci2); s += (b >= 32 && b < 127) ? (char)b : '.'; }
            dl->AddText(ImVec2(ax, y), U32(withA(p.text, 0.6f)), s.c_str());
            auto lb = labels.find(ra); if (lb != labels.end()) dl->AddText(ImVec2(ax + chw * (cols + 2), y), U32(p.warn), lb->second.c_str());
        }
    }
    if (hovered && selRow >= 0 && selRow < rows && selCol >= 0 && selCol < cols && ImGui::IsMouseDoubleClicked(0)) {
        editAddr_ = (int)(vs.memBase + selRow * cols + selCol); std::snprintf(ebuf_, sizeof ebuf_, "%02X", mach->readByte(editAddr_));
    }
    if (editAddr_ >= 0) {
        int r = ((uint32_t)editAddr_ - vs.memBase) / cols, cc = ((uint32_t)editAddr_ - vs.memBase) % cols;
        if (r >= 0 && r < rows) {
            ImGui::SetCursorScreenPos(ImVec2(o.x + addrW + cc * cell - 2, o.y + r * lh - 2)); ImGui::SetNextItemWidth(cell + 8); ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##edit", ebuf_, sizeof ebuf_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_AutoSelectAll)) {
                c.ctrl.writeMemory((uint32_t)editAddr_, (uint32_t)std::strtoul(ebuf_, nullptr, 16), 1); vs.memFlash[(uint32_t)editAddr_ & ~3u] = 1; editAddr_ = -1;
            } else if (ImGui::IsItemDeactivated()) editAddr_ = -1;
        } else editAddr_ = -1;
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows * lh + 4));
    uint32_t a = vs.memSelAddr; uint32_t w4 = mach->readWord(a & ~3u);
    ImGui::TextDisabled("Inspector @ %s :", hx0x(a).c_str()); ImGui::SameLine();
    ImGui::Text("u8=%u  s8=%d  u16=%u  s16=%d  u32=%u  s32=%d", mach->readByte(a), (int8_t)mach->readByte(a), (unsigned)(mach->readByte(a) | (mach->readByte(a + 1) << 8)), (int16_t)(mach->readByte(a) | (mach->readByte(a + 1) << 8)), mach->readWord(a), (int32_t)mach->readWord(a));
    float fv; uint32_t wv2 = mach->readWord(a); std::memcpy(&fv, &wv2, 4);
    ImGui::Text("palabra alineada %s = %s   float=%g   (doble clic en un byte para editarlo)", hx0x(a & ~3u).c_str(), hx0x(w4).c_str(), fv);
}

// --------------------------------------- Pila ---------------------------------------
void StackPanel::content(ViewContext& c) {
    const Machine* mach = c.model.machine(); const Program& prog = c.model.program(); Pal p(c.ui().col);
    if (!mach) { ImGui::TextDisabled("Sin máquina."); return; }
    uint32_t top = prog.opt.stackTop, sp = mach->r[13];
    ImGui::Text("SP = %s   (%s)", hx0x(sp).c_str(), sp >= top ? "pila vacía" : (std::to_string((top - sp) / 4) + " palabras apiladas").c_str());
    if (ImGui::BeginTable("##stack", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Dirección"); ImGui::TableSetupColumn("Valor"); ImGui::TableSetupColumn("Pista", ImGuiTableColumnFlags_WidthStretch); ImGui::TableHeadersRow();
        uint32_t lo = std::min(sp, top - 16) & ~3u;
        for (uint32_t a = top - 4; ; a -= 4) {
            ImGui::TableNextRow(); uint32_t v = mach->readWord(a);
            if (a == sp) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, U32(withA(p.good, 0.3f)));
            ImGui::TableNextColumn(); ImGui::TextColored(p.accent2, "%s%s", hx0x(a).c_str(), a == sp ? " <- SP" : "");
            ImGui::TableNextColumn(); ImGui::Text("%s", hx0x(v).c_str());
            ImGui::TableNextColumn(); auto it = prog.byAddr.find(v - 4);
            if (a >= sp && v >= prog.textStart && v <= prog.textEnd && it != prog.byAddr.end()) ImGui::TextDisabled("dirección de retorno (tras %s)", it->second.src.c_str());
            if (a <= lo || top - a > 4 * 96) break;
        }
        ImGui::EndTable();
    }
}

// ------------------------------------- Símbolos -------------------------------------
void SymbolsPanel::content(ViewContext& c) {
    const Program& prog = c.model.program(); Pal p(c.ui().col);
    if (ImGui::BeginTable("##syms", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Símbolo"); ImGui::TableSetupColumn("Dirección"); ImGui::TableSetupColumn("Zona"); ImGui::TableHeadersRow();
        for (auto& kv : prog.symbols) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(p.warn, "%s", kv.first.c_str());
            ImGui::TableNextColumn(); if (ImGui::Selectable(hx0x(kv.second).c_str())) c.goMem(kv.second);
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", (kv.second >= prog.textStart && kv.second < prog.textEnd) ? "código" : (kv.second >= prog.dataStart && kv.second < prog.dataEnd) ? "datos" : "constante");
        }
        ImGui::EndTable();
    }
}
