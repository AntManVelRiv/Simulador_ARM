// view/panels/CodePanels.cpp — Editor, código máquina e instrucción actual
#include "view/Panels.h"
#include "view/Theme.h"
#include "imgui_internal.h"
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

// ---- Edición con el teclado del móvil -------------------------------------------------------
// El teclado en pantalla no tiene Ctrl, así que copiar/pegar se hace con estos botones (y con el menú «Editar»).
void EditorPanel::applyEditCommand(EditCmd cmd) {
    switch (cmd) {
        case EditCmd::Undo: ed_.Undo(); break;
        case EditCmd::Redo: ed_.Redo(); break;
        case EditCmd::Cut: ed_.Cut(); break;
        case EditCmd::Copy: ed_.Copy(); break;
        case EditCmd::Paste: ed_.Paste(); break;
        case EditCmd::SelectAll: ed_.SelectAll(); break;
        case EditCmd::None: break;
    }
}

void EditorPanel::editToolbar(ViewContext& c) {
    struct Item { const char* label; EditCmd cmd; bool enabled; };
    const Item items[] = {
        {"Copiar", EditCmd::Copy, ed_.HasSelection()}, {"Cortar", EditCmd::Cut, ed_.HasSelection()},
        {"Pegar", EditCmd::Paste, true}, {"Todo", EditCmd::SelectAll, true},
        {"Deshacer", EditCmd::Undo, ed_.CanUndo()}, {"Rehacer", EditCmd::Redo, ed_.CanRedo()},
    };
    bool first = true;
    for (const Item& it : items) {
        if (!first) ImGui::SameLine();
        first = false;
        ImGui::BeginDisabled(!it.enabled);
        if (ImGui::Button(it.label)) c.vs.editCmd = it.cmd;
        ImGui::EndDisabled();
    }
}

// ImGui solo avisa a SDL de que muestre el teclado cuando un widget marca PlatformImeData.WantVisible; el editor de
// código (ImGuiColorTextEdit) no es un InputText, así que lo marcamos nosotros mientras el usuario lo esté usando.
void EditorPanel::requestSoftKeyboard() {
    ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
    ime.WantVisible = ime.WantTextInput = true;
    ime.ViewportId = ImGui::GetMainViewport()->ID;
    ime.InputPos = ImGui::GetWindowPos();               // fijo: así SDL_StartTextInput se llama solo al cambiar de estado
    ime.InputLineHeight = ImGui::GetTextLineHeight();
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
    if (platform::isAndroid()) editToolbar(c);
    const auto& errs = m.program().errors;
    float errH = errs.empty() ? 0.0f : std::min(110.0f * c.ui().fontScale, ImGui::GetTextLineHeightWithSpacing() * (1 + (float)errs.size()) + 8);

    // Al aparecer/desaparecer el teclado el editor cambia de tamaño: se vuelve a mostrar la línea del cursor
    if (std::fabs(c.vs.kbInset - prevKbInset_) > 1.0f) {
        prevKbInset_ = c.vs.kbInset;
        TextEditor::Coordinates cur = ed_.GetCursorPosition();
        ed_.SetCursorPosition(TextEditor::Coordinates(0, 0)); ed_.SetCursorPosition(cur);   // (solo mueve el scroll si cambia la posición)
    }
    if (c.vs.refocusEditor) { c.vs.refocusEditor = false; ImGui::SetNextWindowFocus(); }   // tras un botón/menú: el foco vuelve al editor
    const ImVec2 e0 = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    const ImVec2 e1(e0.x + avail.x, e0.y + std::max(avail.y - errH - 2, 0.0f));

    ed_.Render("##code", ImVec2(0, -errH - 2), false);

    // Tocar el editor muestra el teclado (también si se había cerrado con «Atrás»)
    if (platform::isAndroid()) {
        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(e0, e1) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) { kbWanted_ = true; platform::showSoftKeyboard(); }
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) kbWanted_ = false;       // otro panel tiene el foco: fuera teclado
        if (kbWanted_) requestSoftKeyboard();
    }
    // Órdenes de edición pendientes (se aplican DESPUÉS de dibujar para que el controlador se entere del cambio de texto)
    if (c.vs.editCmd != EditCmd::None) { applyEditCommand(c.vs.editCmd); c.vs.editCmd = EditCmd::None; c.vs.refocusEditor = true; }
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
            if (isPC) { float a = ui.anim() ? 0.25f + 0.10f * std::sin((float)ImGui::GetTime() * 5) : 0.3f; ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, U32(withA(p.good, a))); if (follow) ImGui::SetScrollHereY(0.5f); }
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
// (El camino de datos y el panel de ciclos están en CpuPanels.cpp)
