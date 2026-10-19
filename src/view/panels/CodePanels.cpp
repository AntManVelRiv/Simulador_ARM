// view/panels/CodePanels.cpp — Editor, código máquina, instrucción actual y camino de datos
#include "view/Panels.h"
#include "view/Theme.h"
#include <cmath>
#include <set>
#include <algorithm>
#include <unordered_set>

using namespace arm;

// ------------------------------------ Editor -------------------------------------
EditorPanel::EditorPanel() : Panel(P_EDITOR) { configureLanguage(); }

void EditorPanel::configureLanguage() {
    static TextEditor::LanguageDefinition lang;
    lang = TextEditor::LanguageDefinition();
    lang.mName = "ARM"; lang.mCaseSensitive = false; lang.mAutoIndentation = true;
    lang.mSingleLineComment = "@"; lang.mPreprocChar = '\x01';
    const char* bases[] = {"AND","EOR","SUB","RSB","ADD","ADC","SBC","RSC","TST","TEQ","CMP","CMN","ORR","MOV","BIC","MVN","LSL","LSR","ASR","ROR","RRX",
        "MUL","MLA","SMULL","UMULL","SMLAL","UMLAL","LDR","STR","LDRB","STRB","LDRH","STRH","LDRSB","LDRSH","LDM","STM","PUSH","POP","B","BL","BX","SWI","SVC","NOP"};
    const char* conds[] = {"","EQ","NE","CS","HS","CC","LO","MI","PL","VS","VC","HI","LS","GE","LT","GT","LE","AL"};
    for (auto b : bases) for (auto c : conds) { lang.mKeywords.insert(std::string(b) + c); lang.mKeywords.insert(std::string(b) + c + "S"); }
    for (auto m : {"LDMIA","LDMIB","LDMDA","LDMDB","LDMFD","LDMFA","LDMED","LDMEA","STMIA","STMIB","STMDA","STMDB","STMFD","STMFA","STMED","STMEA"}) lang.mKeywords.insert(m);
    for (int i = 0; i <= 15; i++) { TextEditor::Identifier id; id.mDeclaration = "Registro R" + std::to_string(i); lang.mIdentifiers.insert({"R" + std::to_string(i), id}); }
    for (auto r : {"SP","LR","PC","FP","IP","SL","SB"}) { TextEditor::Identifier id; id.mDeclaration = "Registro"; lang.mIdentifiers.insert({r, id}); }
    for (auto fn : {"printf","scanf","puts","gets","strlen"}) { TextEditor::Identifier id; id.mDeclaration = "Funcion de biblioteca simulada (BL " + std::string(fn) + ")"; lang.mIdentifiers.insert({fn, id}); }
    lang.mTokenRegexStrings.push_back({"\\.[A-Za-z]+", TextEditor::PaletteIndex::Preprocessor});
    lang.mTokenRegexStrings.push_back({"#-?0[xX][0-9a-fA-F]+", TextEditor::PaletteIndex::Number});
    lang.mTokenRegexStrings.push_back({"#-?[0-9]+", TextEditor::PaletteIndex::Number});
    lang.mTokenRegexStrings.push_back({"0[xX][0-9a-fA-F]+", TextEditor::PaletteIndex::Number});
    lang.mTokenRegexStrings.push_back({"[0-9]+", TextEditor::PaletteIndex::Number});
    lang.mTokenRegexStrings.push_back({"[A-Za-z_][A-Za-z0-9_]*", TextEditor::PaletteIndex::Identifier});
    lang.mTokenRegexStrings.push_back({"L?\\\"(\\\\.|[^\\\"])*\\\"", TextEditor::PaletteIndex::String});
    lang.mTokenRegexStrings.push_back({"[\\[\\]\\{\\}\\!\\-\\+\\*\\,\\=\\:]", TextEditor::PaletteIndex::Punctuation});
    ed_.SetLanguageDefinition(lang);
    ed_.SetTabSize(8);
    ed_.SetShowWhitespaces(false);
}

void EditorPanel::content(ViewContext& c) {
    const SimModel& m = c.model;
    // ---- el modelo manda: si el texto se cargó desde fuera (archivo, ejemplo), se recarga el editor
    if (c.vs.seenExtRev != m.externalSourceRevision()) { c.vs.seenExtRev = m.externalSourceRevision(); ed_.SetText(m.source()); }
    if (seenSettings_ != m.settingsRevision()) { seenSettings_ = m.settingsRevision(); ed_.SetPalette(makeEditorPalette(c.ui())); }
    if (seenState_ != m.stateSerial()) {   // sincroniza marcadores de errores y puntos de parada
        seenState_ = m.stateSerial();
        TextEditor::ErrorMarkers em; for (auto& e : m.program().errors) if (!em.count(e.line)) em[e.line] = e.msg;
        ed_.SetErrorMarkers(em);
        ed_.SetBreakpoints(TextEditor::Breakpoints(m.breakpoints().begin(), m.breakpoints().end()));
    }
    if (c.vs.editorJumpLine >= 0) { ed_.SetCursorPosition(TextEditor::Coordinates(c.vs.editorJumpLine, 0)); c.vs.editorJumpLine = -1; }

    auto cp = ed_.GetCursorPosition(); c.vs.editorCursorLine = cp.mLine + 1;
    ImGui::TextDisabled("Lín %d, Col %d  |  %d líneas  |  %s%s", cp.mLine + 1, cp.mColumn + 1, ed_.GetTotalLines(),
        m.settings().lastFile.empty() ? "(sin archivo)" : m.settings().lastFile.c_str(), m.isDirty() ? "  *modificado" : "");
    const auto& errs = m.program().errors;
    float errH = errs.empty() ? 0.0f : std::min(110.0f * c.ui().fontScale, ImGui::GetTextLineHeightWithSpacing() * (1 + (float)errs.size()) + 8);
    ed_.Render("##code", ImVec2(0, -errH - 2), false);
    if (ed_.IsTextChanged()) c.ctrl.editorChanged(ed_.GetText());     // la vista informa; el controlador actualiza el modelo
    if (!errs.empty()) {
        Pal p(c.ui().col);
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::BeginChild("##errs", ImVec2(0, errH), true);
        for (auto& e : errs) {
            std::string s = "Línea " + std::to_string(e.line) + ": " + e.msg;
            if (ImGui::Selectable(s.c_str())) ed_.SetCursorPosition(TextEditor::Coordinates(std::max(0, e.line - 1), 0));
            if (!e.hint.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.hint.c_str());
        }
        ImGui::EndChild(); ImGui::PopStyleColor();
    }
}

// ------------------------- Campos de bits de una instrucción -------------------------
static ImVec4 fieldColor(const std::string& cls, const Pal& p) {
    if (cls == "cond") return p.violet();
    if (cls == "cmd") return p.warn;
    if (cls == "rn") return p.accent;
    if (cls == "rd") return p.good;
    if (cls == "src") return ImVec4(1.0f, 0.45f, 0.7f, 1);
    if (cls == "flag") return ImVec4(0.55f, 0.7f, 0.95f, 1);
    return mixc(p.text, p.bg, 0.5f);
}

void drawFieldRow(const ViewContext& c, const std::vector<Field>& fields, float width) {
    if (fields.empty()) return;
    Pal pal(c.ui().col);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float cw = ImGui::CalcTextSize("0").x + 2, lh = ImGui::GetTextLineHeight();
    float x = 0, y = 0, rowH = lh * 2 + 12; const Field* hovered = nullptr;
    ImVec2 mp = ImGui::GetMousePos();
    for (auto& f : fields) {
        int n = f.hi - f.lo + 1;
        float w = std::max(n * cw + 6, ImGui::CalcTextSize(f.name.c_str()).x + 8);
        if (x + w > width && x > 0) { x = 0; y += rowH + 4; }
        ImVec2 a(origin.x + x, origin.y + y + lh * 0.9f), b(a.x + w, a.y + lh + 6);
        ImVec4 col = fieldColor(f.cls, pal);
        bool hov = mp.x >= a.x && mp.x <= b.x && mp.y >= origin.y + y && mp.y <= b.y + lh;
        dl->AddRectFilled(a, b, U32(withA(col, hov ? 0.40f : 0.18f)), 3);
        dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y), U32(col), 2.0f);
        char pos[16]; if (f.hi == f.lo) std::snprintf(pos, sizeof pos, "%d", f.hi); else std::snprintf(pos, sizeof pos, "%d:%d", f.hi, f.lo);
        dl->AddText(ImVec2(a.x + (w - ImGui::CalcTextSize(pos).x) / 2, origin.y + y), U32(withA(pal.text, 0.45f)), pos);
        dl->AddText(ImVec2(a.x + (w - ImGui::CalcTextSize(f.bits.c_str()).x) / 2, a.y + 3), U32(col), f.bits.c_str());
        dl->AddText(ImVec2(a.x + (w - ImGui::CalcTextSize(f.name.c_str()).x) / 2, b.y + 2), U32(col), f.name.c_str());
        if (hov) hovered = &f;
        x += w + 4;
    }
    ImGui::Dummy(ImVec2(width, y + rowH));
    if (hovered) { ImGui::BeginTooltip(); ImGui::TextColored(fieldColor(hovered->cls, pal), "%s [%d:%d] = %s", hovered->name.c_str(), hovered->hi, hovered->lo, hovered->bits.c_str()); ImGui::TextUnformatted(hovered->note.c_str()); ImGui::EndTooltip(); }
}

// ---------------------------------- Código máquina ----------------------------------
void ListingPanel::content(ViewContext& c) {
    const SimModel& m = c.model; const Program& prog = m.program(); const Machine* mach = m.machine();
    Pal p(c.ui().col); const UiPrefs& ui = c.ui();
    if (!prog.ok || !mach) { ImGui::TextDisabled("Ensambla el programa (F7) para ver el código máquina."); return; }
    if (ImGui::BeginTable("##list", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 22 * ui.fontScale);
        ImGui::TableSetupColumn("Dirección"); ImGui::TableSetupColumn("Código"); ImGui::TableSetupColumn("Instrucción", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Formato");
        ImGui::TableHeadersRow();
        bool follow = (m.running() || c.vs.stageT < 0.2f) && ui.followExec;
        const StepRecord* last = m.lastStep();
        for (auto& l : prog.listing) {
            ImGui::TableNextRow();
            bool isPC = l.isInstr && l.addr == mach->pc && !mach->halted;
            bool isLast = last && l.addr == last->addr;
            bool bp = l.isInstr && std::find(m.breakpoints().begin(), m.breakpoints().end(), l.line) != m.breakpoints().end();
            if (isPC) { float a = ui.animations ? 0.25f + 0.10f * std::sin((float)ImGui::GetTime() * 5) : 0.3f; ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, U32(withA(p.good, a))); if (follow) ImGui::SetScrollHereY(0.5f); }
            else if (isLast) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, U32(withA(p.accent, 0.15f)));
            ImGui::TableNextColumn(); ImGui::PushID((int)l.addr);
            if (l.isInstr) {
                ImVec2 pos = ImGui::GetCursorScreenPos(); float r = 5 * ui.fontScale;
                if (ImGui::InvisibleButton("##bp", ImVec2(r * 3, ImGui::GetTextLineHeight()))) c.ctrl.toggleBreakpoint(l.line);
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + r * 1.2f, pos.y + ImGui::GetTextLineHeight() / 2), r, bp ? U32(p.bad) : U32(withA(p.text, ImGui::IsItemHovered() ? 0.5f : 0.15f)));
            }
            ImGui::TableNextColumn(); ImGui::TextColored(p.accent2, "%s", hx0x(l.addr).c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(p.violet(), "%s", hex(l.word).c_str());
            ImGui::TableNextColumn();
            if (l.isInstr) { if (ImGui::Selectable(l.src.c_str(), c.vs.selInstrAddr == (int)l.addr, ImGuiSelectableFlags_SpanAllColumns)) { c.vs.selInstrAddr = (int)l.addr; c.vs.editorJumpLine = l.line - 1; } }
            else ImGui::TextDisabled("%s  (pool de literales)", l.src.c_str());
            ImGui::TableNextColumn(); if (l.isInstr) ImGui::TextDisabled("%s", l.ins.format.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ------------------------------- Instrucción actual --------------------------------
void InstructionPanel::content(ViewContext& c) {
    const SimModel& m = c.model; Pal p(c.ui().col);
    const Instruction* ins = nullptr;
    if (m.program().ok && m.machine()) { uint32_t a = c.vs.selInstrAddr >= 0 ? (uint32_t)c.vs.selInstrAddr : m.machine()->pc; auto it = m.program().byAddr.find(a); if (it != m.program().byAddr.end()) ins = &it->second; }
    if (ins) {
        ImGui::TextColored(p.accent2, "%s", hx0x(ins->addr).c_str()); ImGui::SameLine();
        ImGui::TextColored(p.violet(), "%s", hex(ins->word).c_str()); ImGui::SameLine();
        ImGui::TextColored(p.accent, "%s", ins->src.c_str());
        ImGui::TextDisabled("%s  -  %s", ins->format.c_str(), c.vs.selInstrAddr >= 0 ? "seleccionada" : "siguiente a ejecutar (PC)");
        if (c.vs.selInstrAddr >= 0) { ImGui::SameLine(); if (ImGui::SmallButton("Ver la del PC")) c.vs.selInstrAddr = -1; }
        drawFieldRow(c, ins->fields, ImGui::GetContentRegionAvail().x);
        if (!ins->note.empty()) ImGui::TextWrapped("%s", ins->note.c_str());
    } else ImGui::TextDisabled("No hay instrucción cargada.");
    if (const StepRecord* last = m.lastStep()) {
        ImGui::SeparatorText("Último paso");
        ImGui::TextColored(p.accent, "%s", last->ins.src.c_str());
        if (last->executed) { ImGui::TextColored(p.good, "%s", last->sym.c_str()); ImGui::TextDisabled("%s", last->num.c_str()); }
        for (auto& e : last->extra) ImGui::TextWrapped("%s", e.c_str());
        ImGui::TextWrapped("%s", last->text.c_str());
    }
}

// -------------------------------- Camino de datos ---------------------------------
void DatapathPanel::content(ViewContext& c) {
    const SimModel& m = c.model; const UiPrefs& ui = c.ui(); Pal pal(ui.col);
    ImVec2 avail = ImGui::GetContentRegionAvail(); avail.y = std::max(avail.y, 200.0f * ui.fontScale);
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##cpu", avail);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    struct Blk { const char* id; float x, y, w, h; const char* label; };
    static const Blk B[] = {{"PC",.02f,.40f,.08f,.16f,"PC"},{"IM",.14f,.30f,.16f,.36f,"Memoria\ninstruc."},{"IR",.34f,.40f,.07f,.16f,"IR"},{"UC",.34f,.05f,.30f,.16f,"Unidad de control"},
        {"RF",.46f,.30f,.15f,.36f,"Banco de\nregistros"},{"ALU",.66f,.30f,.10f,.36f,"ALU"},{"FL",.66f,.74f,.10f,.14f,"NZCV"},{"DM",.82f,.30f,.15f,.36f,"Memoria\ndatos"}};
    auto rect = [&](const char* id, ImVec2& a, ImVec2& b) { for (auto& k : B) if (!std::strcmp(k.id, id)) { a = ImVec2(o.x + k.x * avail.x, o.y + k.y * avail.y); b = ImVec2(a.x + k.w * avail.x, a.y + k.h * avail.y); return; } };
    auto center = [&](const char* id) { ImVec2 a, b; rect(id, a, b); return ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2); };
    std::set<std::string> hot; std::vector<std::pair<std::string, std::string>> arrows;
    const float T = 0.6f; std::string stageName = "En reposo";
    const StepRecord* last = m.lastStep();
    if (last && ui.animations && c.vs.stageT < 1e8f) {
        int stage = (int)(c.vs.stageT / T); float frac = std::fmod(c.vs.stageT, T) / T; (void)frac;
        auto t = last->ins.t; bool ex = last->executed;
        if (stage == 0) { hot = {"PC","IM","IR"}; arrows = {{"PC","IM"},{"IM","IR"}}; stageName = "1. Búsqueda (fetch): PC -> memoria -> IR"; }
        else if (stage == 1) { hot = {"IR","UC","RF"}; arrows = {{"IR","UC"},{"IR","RF"}}; stageName = "2. Decodificación: la unidad de control lee IR"; }
        else if (stage == 2) { if (!ex) { hot = {"UC"}; stageName = "3. Condición no cumplida: instrucción anulada"; } else { hot = {"RF","ALU"}; arrows = {{"RF","ALU"}}; if (t == IType::DP) hot.insert("FL"); stageName = (t == IType::BR || t == IType::BX) ? "3. Cálculo de la dirección de salto" : "3. Ejecución en la ALU"; } }
        else if (stage == 3) { if (ex && (t == IType::MEM || t == IType::BLK)) { hot = {"ALU","DM"}; arrows = {{"ALU","DM"}}; stageName = "4. Acceso a memoria de datos"; } else stage = 4; }
        if (stage == 4) { if (ex && !last->regs.empty()) { hot = {"ALU","RF"}; arrows = {{"ALU","RF"}}; if (t == IType::MEM && last->ins.load) { hot = {"DM","RF"}; arrows = {{"DM","RF"}}; } stageName = "5. Escritura del resultado en registros"; } else if (ex && (t == IType::BR || t == IType::BX)) { hot = {"ALU","PC"}; arrows = {{"ALU","PC"}}; stageName = "5. PC recibe la dirección de destino"; } else stage = 5; }
        if (stage >= 5) { hot.clear(); arrows.clear(); stageName = "Instrucción completada"; }
    }
    float frac = std::fmod(c.vs.stageT, T) / T;
    static const char* conns[][2] = {{"PC","IM"},{"IM","IR"},{"IR","UC"},{"IR","RF"},{"RF","ALU"},{"ALU","DM"},{"ALU","FL"},{"DM","RF"},{"ALU","PC"}};
    for (auto& cn : conns) dl->AddLine(center(cn[0]), center(cn[1]), U32(withA(pal.accent, 0.22f)), 2.0f);
    for (auto& ar : arrows) {
        ImVec2 a = center(ar.first.c_str()), b = center(ar.second.c_str());
        dl->AddLine(a, b, U32(withA(pal.accent2, 0.9f)), 3.0f);
        ImVec2 pt(a.x + (b.x - a.x) * frac, a.y + (b.y - a.y) * frac);
        dl->AddCircleFilled(pt, 10, U32(withA(pal.accent2, 0.18f))); dl->AddCircleFilled(pt, 5, U32(pal.accent2));
    }
    for (auto& k : B) {
        ImVec2 a, b; rect(k.id, a, b); bool h = hot.count(k.id) > 0;
        if (h) dl->AddRectFilled(ImVec2(a.x - 4, a.y - 4), ImVec2(b.x + 4, b.y + 4), U32(withA(pal.good, 0.12f)), 8);
        dl->AddRectFilled(a, b, U32(h ? mixc(pal.panel, pal.good, 0.35f) : withA(mixc(pal.panel, pal.bg, 0.3f), 0.95f)), 6);
        dl->AddRect(a, b, U32(h ? pal.good : withA(pal.accent, 0.6f)), 6, 0, h ? 2.5f : 1.5f);
        ImVec2 ts = ImGui::CalcTextSize(k.label);
        dl->AddText(ImVec2((a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2), U32(pal.text), k.label);
    }
    if (const Machine* mc = m.machine()) {
        char buf[64]; ImVec2 a, b;
        std::snprintf(buf, sizeof buf, "PC=%s", hx0x(mc->pc).c_str()); rect("PC", a, b); dl->AddText(ImVec2(a.x, b.y + 4), U32(pal.accent2), buf);
        if (last) { std::snprintf(buf, sizeof buf, "IR=%s", hex(last->ins.word).c_str()); rect("IR", a, b); dl->AddText(ImVec2(a.x - 20, b.y + 4), U32(pal.violet()), buf); }
        std::snprintf(buf, sizeof buf, "N%d Z%d C%d V%d", mc->f.N, mc->f.Z, mc->f.C, mc->f.V); rect("FL", a, b); dl->AddText(ImVec2(a.x, b.y + 4), U32(pal.text), buf);
    }
    dl->AddText(ImVec2(o.x + 8, o.y + avail.y - ImGui::GetTextLineHeight() - 6), U32(pal.good), stageName.c_str());
}
