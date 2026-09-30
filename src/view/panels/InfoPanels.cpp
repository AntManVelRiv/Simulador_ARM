// view/panels/InfoPanels.cpp — Traza, salida, codificador, referencia y acerca de
#include "view/Panels.h"

using namespace arm;

void TracePanel::content(ViewContext& c) {
    const auto& trace = c.model.trace(); Pal p(c.ui().col);
    if (ImGui::SmallButton("Limpiar")) c.ctrl.clearTrace();
    ImGui::SameLine(); ImGui::TextDisabled("%zu pasos guardados", trace.size());
    if (ImGui::BeginTable("##trace", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#"); ImGui::TableSetupColumn("PC"); ImGui::TableSetupColumn("Instrucción"); ImGui::TableSetupColumn("Efecto", ImGuiTableColumnFlags_WidthStretch); ImGui::TableHeadersRow();
        ImGuiListClipper clip; clip.Begin((int)trace.size());
        while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
            const StepRecord& r = trace[i]; ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled("%d", i + 1);
            ImGui::TableNextColumn(); ImGui::TextColored(p.accent2, "%s", hex(r.addr, 4).c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(r.executed ? p.accent : withA(p.text, 0.4f), "%s", r.ins.src.c_str());
            ImGui::TableNextColumn(); if (r.executed) ImGui::TextUnformatted(r.sym.c_str()); else ImGui::TextDisabled("no se ejecuta (%s)", condName(r.ins.cond));
        }
        if (c.model.running() || c.vs.stageT < 0.2f) ImGui::SetScrollHereY(1.0f);
        ImGui::EndTable();
    }
}

void OutputPanel::content(ViewContext& c) {
    const Machine* m = c.model.machine(); Pal p(c.ui().col);
    if (!m) { ImGui::TextDisabled("Sin máquina."); return; }
    bool waiting = c.model.waitingForInput();
    if (waiting) { ImGui::PushStyleColor(ImGuiCol_Text, p.warn); ImGui::TextWrapped("%s", m->haltReason.c_str()); ImGui::PopStyleColor(); }
    else if (m->halted) ImGui::TextColored(m->haltKind == "error" ? p.bad : p.warn, "%s", m->haltReason.c_str());
    else ImGui::TextDisabled("El programa puede leer y escribir por esta consola (ver SWI 0x20-0x32 en la Referencia).");
    ImGui::Separator();
    ImGui::BeginChild("##consoleout", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), false);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(m->output.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::Separator();
    static char buf[256] = "";
    if (waiting) { ImGui::PushStyleColor(ImGuiCol_FrameBg, withA(p.warn, 0.20f)); ImGui::PushStyleColor(ImGuiCol_Border, p.warn); }
    ImGui::SetNextItemWidth(-90 * c.ui().fontScale);
    bool enter = ImGui::InputText("##consolein", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (waiting) ImGui::PopStyleColor(2);
    ImGui::SameLine();
    bool send = ImGui::Button(waiting ? "Enviar *" : "Enviar");
    if ((enter || send) && buf[0] != '\0') { c.ctrl.submitInput(buf); buf[0] = '\0'; }
    if (waiting) { ImGui::SameLine(); ImGui::TextColored(p.warn, "esperando..."); }
}

void EncoderPanel::content(ViewContext& c) {
    Pal p(c.ui().col);
    ImGui::TextWrapped("Escribe una instrucción y mira su codificación en 32 bits, campo a campo.");
    ImGui::SetNextItemWidth(-1); ImGui::InputText("##enc", c.vs.encBuf, sizeof c.vs.encBuf);
    auto r = encodeLine(c.vs.encBuf, 0x1000, c.model.program().symbols);
    if (!r.ok) { ImGui::TextColored(p.bad, "%s", r.error.c_str()); if (!r.hint.empty()) ImGui::TextDisabled("%s", r.hint.c_str()); return; }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f); ImGui::TextColored(p.accent, "0x%s", hex(r.word).c_str()); ImGui::PopFont();
    ImGui::TextDisabled("Formato: %s", r.format.c_str());
    drawFieldRow(c, r.fields, ImGui::GetContentRegionAvail().x);
    if (!r.note.empty()) ImGui::TextWrapped("%s", r.note.c_str());
}

void ReferencePanel::content(ViewContext& c) {
    Pal p(c.ui().col);
    if (ImGui::CollapsingHeader("Códigos de condición", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("##cond", 3, ImGuiTableFlags_RowBg)) {
        for (auto& cd : COND_TABLE) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(p.accent, "%s%s%s", cd.mn, cd.alt[0] ? "/" : "", cd.alt); ImGui::TableNextColumn(); ImGui::TextUnformatted(cd.name); ImGui::TableNextColumn(); ImGui::TextDisabled("%s", cd.flagsText); }
        ImGui::EndTable();
    }
    struct I { const char* m; const char* s; const char* d; };
    static const I list[] = {
        {"MOV / MVN","Rd, op2","Copia (o copia negado) op2 en Rd"},{"ADD / SUB / RSB","Rd, Rn, op2","Suma, resta, resta inversa"},{"ADC / SBC / RSC","Rd, Rn, op2","Con acarreo (carry)"},
        {"AND / ORR / EOR / BIC","Rd, Rn, op2","Lógicas bit a bit (BIC borra bits)"},{"LSL / LSR / ASR / ROR / RRX","Rd, Rm, #n|Rs","Desplazamientos y rotaciones"},
        {"CMP / CMN / TST / TEQ","Rn, op2","Solo actualizan flags"},{"MUL / MLA","Rd, Rm, Rs[, Rn]","Multiplicación de 32 bits"},{"SMULL / UMULL / SMLAL / UMLAL","RdLo, RdHi, Rm, Rs","Multiplicación de 64 bits"},
        {"LDR / STR","Rd, [Rn, ...]","Palabra. Modos: [Rn,#o] [Rn,#o]! [Rn],#o [Rn,Rm,LSL #n]"},{"LDRB / STRB / LDRSB","Rd, [Rn, ...]","Byte (sin/con signo)"},{"LDRH / STRH / LDRSH","Rd, [Rn, ...]","Media palabra"},
        {"LDM / STM (IA IB DA DB FD ED FA EA)","Rn, {lista}","Carga/almacenamiento múltiple"},{"PUSH / POP","{lista}","Apilar / desapilar"},{"B / BL / BX","etiqueta | Rm","Saltos (BL guarda el retorno en LR)"},
        {"SWI","#n","Llamada al sistema: ver la tabla de abajo"},{"LDR Rd, =valor","","Pseudo-instrucción: carga cualquier constante o dirección"},
        {".text .data .word .byte .space .ascii .asciz .equ .org .end","","Directivas"}};
    if (ImGui::CollapsingHeader("Instrucciones y directivas", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("##ins", 3, ImGuiTableFlags_RowBg)) {
        for (auto& i : list) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(p.accent, "%s", i.m); ImGui::TableNextColumn(); ImGui::TextUnformatted(i.s); ImGui::TableNextColumn(); ImGui::TextDisabled("%s", i.d); }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Todas las instrucciones admiten sufijo de condición (ADDEQ, MOVNE...) y S (ADDS, SUBS...).");

    struct S { const char* n; const char* d; };
    static const S swis[] = {
        {"0x11 / 0x18","Termina el programa"},
        {"0x20 (o 0x00)","Escribe el carácter de R0"},{"0x21","Escribe R0 como entero con signo"},{"0x22","Escribe R0 como entero sin signo"},
        {"0x23","Escribe R0 en hexadecimal"},{"0x24","Escribe la cadena (terminada en cero) apuntada por R0"},{"0x25","Escribe un salto de línea"},
        {"0x30","Lee una línea y la interpreta como entero -> R0 (bloquea hasta tener una línea)"},
        {"0x31","Lee un carácter -> R0 (bloquea hasta tener un carácter)"},
        {"0x32","Lee una línea en el buffer [R1], máximo R2 bytes; longitud -> R0 (bloquea hasta tener una línea)"},
        {"0x6B","(compatibilidad) escribe R1 como entero con signo"},
    };
    if (ImGui::CollapsingHeader("Entrada / salida por consola (SWI)", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("##swi", 2, ImGuiTableFlags_RowBg)) {
        for (auto& x : swis) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(p.accent, "%s", x.n); ImGui::TableNextColumn(); ImGui::TextUnformatted(x.d); }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Las llamadas de lectura detienen el programa (\"esperando entrada\") hasta que escribes algo en la consola y pulsas Enviar o Intro.");
}

void AboutPanel::content(ViewContext& c) {
    Pal p(c.ui().col);
    ImGui::TextColored(p.accent, "ARM SIM  -  Simulador de ensamblador ARMv4");
    ImGui::TextWrapped("Ensamblador de dos pasadas y emulador de CPU con codificación binaria campo a campo, paso atrás, puntos de parada, memoria editable, pila, camino de datos animado y temas personalizables.");
    ImGui::Separator();
    ImGui::TextDisabled("Arquitectura MVC: modelo (src/model), vista (src/view) y controlador (src/controller).");
    ImGui::TextDisabled("Atajos: F5 ejecutar/pausar  F10 paso  Alt+Izq atrás  Mayús+F5 reiniciar  F7 ensamblar  F9 punto de parada  Ctrl +/- tamaño del texto");
    ImGui::TextDisabled("Construido con Dear ImGui, SDL2 e ImGuiColorTextEdit. Fuente: JetBrains Mono (OFL).");
}
