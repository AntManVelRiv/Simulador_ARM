// view/panels/CpuPanels.cpp — selector de procesador, camino de datos (monociclo, multiciclo, segmentado) y panel de ciclos
#include "view/Panels.h"
#include "view/Theme.h"
#include <cmath>
#include <cstring>
#include <set>
#include <map>
#include <unordered_map>
#include <algorithm>

using namespace arm;

// ============================== Selector del tipo de procesador ==============================
void drawCpuSelector(ViewContext& c) {
    const CpuConfig cfg = c.model.cpuConfig();
    static const char* tips[3] = {
        "Monociclo: cada instrucción se ejecuta completa en un único ciclo de reloj (CPI = 1).",
        "Multiciclo: cada instrucción usa los ciclos que necesita (BI, LR, E, M, ER), uno por etapa.",
        "Segmentado: varias instrucciones a la vez, cada una en una etapa distinta (como una cadena de montaje).\n"
        "Con riesgos de datos (anticipación o parada) y de control (vaciado tras un salto)." };
    // coloca el siguiente elemento en la misma línea si cabe; si no, en la siguiente
    auto next = [&](float w) {
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= right) ImGui::SameLine(); };
    const float fsz = ImGui::GetFrameHeight();
    ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Procesador:");
    for (int i = 0; i < 3; i++) {
        next(fsz + 4 + ImGui::CalcTextSize(cpuKindName((CpuKind)i)).x + 8);
        if (ImGui::RadioButton(cpuKindName((CpuKind)i), (int)cfg.kind == i) && (int)cfg.kind != i) c.ctrl.setCpuKind((CpuKind)i);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n\nAl cambiar de procesador se reinicia la simulación.", tips[i]);
    }
    if (cfg.kind == CpuKind::Pipelined) {
        next(ImGui::CalcTextSize("Etapas:  -  10  +").x + 40);
        ImGui::AlignTextToFramePadding(); ImGui::Text("Etapas:"); ImGui::SameLine();
        int n = cfg.clampedStages();
        ImGui::BeginDisabled(n <= PIPE_MIN_STAGES); if (ImGui::SmallButton("-##stg")) c.ctrl.setPipelineStages(n - 1); ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::Text("%d", n); ImGui::SameLine();
        ImGui::BeginDisabled(n >= PIPE_MAX_STAGES); if (ImGui::SmallButton("+##stg")) c.ctrl.setPipelineStages(n + 1); ImGui::EndDisabled();
        if (ImGui::IsItemHovered() || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Número de etapas de la tubería (%d a %d). 5 = BI, LR, E, M, ER.\nMenos etapas: se fusionan. Más: se parten E, M, BI, LR, ER (en ese orden).", PIPE_MIN_STAGES, PIPE_MAX_STAGES);
        next(fsz + 4 + ImGui::CalcTextSize("Anticipación").x + 8); bool f = cfg.forwarding;
        if (ImGui::Checkbox("Anticipación##fwd", &f)) c.ctrl.setForwarding(f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forwarding: el resultado se envía directamente a la instrucción que lo necesita, sin esperar a que se escriba en el banco de registros.\nDesactivada, la instrucción dependiente se detiene hasta que el dato se escribe.");
    }
}

// ================== Camino de datos "de bloques": monociclo y multiciclo ==================
struct Blk { const char* id; float x, y, w, h; const char* label; };
static const Blk BLK[] = {{"PC",.02f,.40f,.08f,.16f,"PC"},{"IM",.14f,.30f,.16f,.36f,"Memoria\ninstruc."},{"IR",.34f,.40f,.07f,.16f,"IR"},{"UC",.34f,.05f,.30f,.16f,"Unidad de control"},
    {"RF",.46f,.30f,.15f,.36f,"Banco de\nregistros"},{"ALU",.66f,.30f,.10f,.36f,"ALU"},{"FL",.66f,.74f,.10f,.14f,"NZCV"},{"DM",.82f,.30f,.15f,.36f,"Memoria\ndatos"}};

// Qué bloques y flechas están activos en una etapa (0 BI, 1 LR, 2 E, 3 M, 4 ER) para una instrucción dada
static void stageVisual(int stage, IType t, bool load, bool ex, const StepRecord* rec, std::set<std::string>& hot, std::vector<std::pair<std::string, std::string>>& arrows, std::string& name) {
    hot.clear(); arrows.clear();
    if (stage == 0) { hot = {"PC","IM","IR"}; arrows = {{"PC","IM"},{"IM","IR"}}; name = "1. Búsqueda (BI): PC -> memoria -> IR"; }
    else if (stage == 1) { hot = {"IR","UC","RF"}; arrows = {{"IR","UC"},{"IR","RF"}}; name = "2. Decodificación y lectura de registros (LR)"; }
    else if (stage == 2) {
        if (!ex) { hot = {"UC"}; name = "3. Condición no cumplida: instrucción anulada"; }
        else { hot = {"RF","ALU"}; arrows = {{"RF","ALU"}}; if (t == IType::DP) hot.insert("FL"); name = (t == IType::BR || t == IType::BX) ? "3. Ejecución (E): cálculo de la dirección de salto" : "3. Ejecución (E) en la ALU"; }
    }
    else if (stage == 3) { hot = {"ALU","DM"}; arrows = {{"ALU","DM"}}; name = "4. Memoria (M): acceso a la memoria de datos"; }
    else if (stage == 4) {
        if (ex && rec && (t == IType::BR || t == IType::BX) && rec->regs.empty()) { hot = {"ALU","PC"}; arrows = {{"ALU","PC"}}; name = "5. El PC recibe la dirección de destino"; }
        else { hot = {"ALU","RF"}; arrows = {{"ALU","RF"}}; if (t == IType::MEM && load) { hot = {"DM","RF"}; arrows = {{"DM","RF"}}; } name = "5. Escritura en registros (ER)"; }
    }
}

static void drawBlockDiagram(ViewContext& c, bool multi) {
    const SimModel& m = c.model; const UiPrefs& ui = c.ui(); Pal pal(ui.col);
    const CpuModel* cpu = m.cpu();
    ImVec2 avail = ImGui::GetContentRegionAvail(); avail.y = std::max(avail.y, 200.0f * ui.fontScale);
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##cpu", avail);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lh = ImGui::GetTextLineHeight();
    const StepRecord* last = m.lastStep();
    const CpuState* S = cpu ? &cpu->state() : nullptr;

    // ---- multiciclo: máquina de estados con las etapas de la instrucción en curso ----
    float strip = 0;
    if (multi && S) {
        strip = lh + 18 * ui.fontScale;
        float x = o.x + 4; float y = o.y + 4; float ch = lh + 8 * ui.fontScale;
        for (int ls = 0; ls < LS_COUNT; ls++) {
            std::string lab = std::string(logicalStageName(ls));
            bool inPlan = std::find(S->mcPlan.begin(), S->mcPlan.end(), ls) != S->mcPlan.end() || !S->mcActive;
            bool cur = ls == S->lastLogical;
            bool done = S->mcActive && ls < S->lastLogical && inPlan;
            ImVec2 ts = ImGui::CalcTextSize(lab.c_str()); float w = ts.x + 22 * ui.fontScale;
            ImVec4 fill = cur ? mixc(pal.panel, pal.good, 0.55f) : done ? mixc(pal.panel, pal.accent, 0.25f) : withA(mixc(pal.panel, pal.bg, 0.3f), 0.9f);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + ch), U32(fill), 6);
            dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + ch), U32(cur ? pal.good : withA(pal.accent, inPlan ? 0.6f : 0.2f)), 6, 0, cur ? 2.5f : 1.2f);
            dl->AddText(ImVec2(x + (w - ts.x) / 2, y + (ch - ts.y) / 2), U32(inPlan ? pal.text : withA(pal.text, 0.4f)), lab.c_str());
            x += w;
            if (ls + 1 < LS_COUNT) { dl->AddLine(ImVec2(x + 3, y + ch / 2), ImVec2(x + 13, y + ch / 2), U32(withA(pal.accent, 0.6f)), 1.5f); x += 16; }
        }
        char buf[96];
        if (S->lastLogical >= 0) std::snprintf(buf, sizeof buf, "   ciclo %d de %d de esta instrucción  ·  ciclo total %ld", S->mcPhase + 1, (int)S->mcPlan.size(), cpu->stats().cycles);
        else std::snprintf(buf, sizeof buf, "   pulsa Paso para empezar");
        dl->AddText(ImVec2(x + 6, y + (ch - lh) / 2), U32(withA(pal.text, 0.8f)), buf);
        o.y += strip; avail.y -= strip;
    }

    auto rect = [&](const char* id, ImVec2& a, ImVec2& b) { for (auto& k : BLK) if (!std::strcmp(k.id, id)) { a = ImVec2(o.x + k.x * avail.x, o.y + k.y * avail.y); b = ImVec2(a.x + k.w * avail.x, a.y + k.h * avail.y); return; } };
    auto center = [&](const char* id) { ImVec2 a, b; rect(id, a, b); return ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2); };
    std::set<std::string> hot; std::vector<std::pair<std::string, std::string>> arrows;
    const float T = 0.6f; std::string stageName = "En reposo";
    const Instruction* irIns = nullptr;
    if (!multi) {
        if (last && ui.anim() && c.vs.stageT < 1e8f) {
            int stage = (int)(c.vs.stageT / T); auto t = last->ins.t; bool ex = last->executed, ld = last->ins.load;
            bool memStage = ex && (t == IType::MEM || t == IType::BLK);
            if (stage == 3 && !memStage) stage = 4;
            bool wr = ex && (!last->regs.empty() || t == IType::BR || t == IType::BX);
            if (stage == 4 && !wr) stage = 5;
            if (stage >= 5) stageName = "Instrucción completada";
            else stageVisual(stage, t, ld, ex, last, hot, arrows, stageName);
            irIns = &last->ins;
        } else if (last) irIns = &last->ins;
    } else if (S && S->lastLogical >= 0 && S->mcIns) {
        const Instruction& I = *S->mcIns; int stage = S->lastLogical;
        bool ex = stage < 2 ? true : (last ? last->executed : true);
        stageVisual(stage, I.t, I.load, ex, last, hot, arrows, stageName);
        if (stage == 3 && I.t == IType::BLK) stageName = "4. Memoria (M): transferencia de un registro";
        if (stage == 2 && S->mcPlan.size() == 3 && ex && I.t != IType::BR && I.t != IType::BX && !analyzeInstruction(I).dst) stageName += " (fin de la instrucción)";
        irIns = stage >= 1 ? &I : nullptr;
    }
    float frac = ui.anim() ? std::fmod(c.vs.stageT, T) / T : 0.0f; if (multi) frac = ui.anim() ? std::min(1.0f, c.vs.stageT / T) : 0.0f;
    static const char* conns[][2] = {{"PC","IM"},{"IM","IR"},{"IR","UC"},{"IR","RF"},{"RF","ALU"},{"ALU","DM"},{"ALU","FL"},{"DM","RF"},{"ALU","PC"}};
    for (auto& cn : conns) dl->AddLine(center(cn[0]), center(cn[1]), U32(withA(pal.accent, 0.22f)), 2.0f);
    for (auto& ar : arrows) {
        ImVec2 a = center(ar.first.c_str()), b = center(ar.second.c_str());
        dl->AddLine(a, b, U32(withA(pal.accent2, 0.9f)), 3.0f);
        ImVec2 pt(a.x + (b.x - a.x) * frac, a.y + (b.y - a.y) * frac);
        dl->AddCircleFilled(pt, 10, U32(withA(pal.accent2, 0.18f))); dl->AddCircleFilled(pt, 5, U32(pal.accent2));
    }
    for (auto& k : BLK) {
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
        if (irIns) { std::snprintf(buf, sizeof buf, "IR=%s", hex(irIns->word).c_str()); rect("IR", a, b); dl->AddText(ImVec2(a.x - 20, b.y + 4), U32(pal.violet()), buf); }
        std::snprintf(buf, sizeof buf, "N%d Z%d C%d V%d", mc->f.N, mc->f.Z, mc->f.C, mc->f.V); rect("FL", a, b); dl->AddText(ImVec2(a.x, b.y + 4), U32(pal.text), buf);
    }
    dl->AddText(ImVec2(o.x + 8, o.y + avail.y - lh - 6), U32(pal.good), stageName.c_str());
}

// ============== Camino de datos SEGMENTADO (figura 7.58 de Harris & Harris, con k etapas) ==============
static std::string firstTok(const std::string& s) { size_t p = s.find('+'); return p == std::string::npos ? s : s.substr(0, p); }
static std::string lastTok(const std::string& s) { size_t p = s.rfind('+'); return p == std::string::npos ? s : s.substr(p + 1); }

// Parte un texto en líneas que caben en 'maxW' píxeles (por palabras)
static std::vector<std::string> wrapLines(const std::string& text, float maxW) {
    std::vector<std::string> out; std::string line, word; size_t i = 0;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && ImGui::CalcTextSize(t.c_str()).x > maxW) { out.push_back(line); line = word; } else line = t;
        word.clear(); };
    for (; i < text.size(); i++) { if (text[i] == ' ') flushWord(); else word += text[i]; }
    flushWord(); if (!line.empty()) out.push_back(line); return out;
}

static void drawPipelineDiagram(ViewContext& c) {
    const SimModel& m = c.model; const UiPrefs& ui = c.ui(); Pal pal(ui.col);
    const CpuModel* cpu = m.cpu(); if (!cpu) return;
    const StageMap& M = cpu->stageMap(); const CpuState& S = cpu->state(); const HazardSignals& g = S.sig; const CpuStats& st = cpu->stats();
    const int k = M.k, eFirst = M.first[LS_E];
    const float fs = ui.fontScale, lh = ImGui::GetTextLineHeight();
    const ImVec4 purple = pal.violet(), dim = withA(pal.text, 0.55f);
    ImVec2 avail = ImGui::GetContentRegionAvail(); const float W = avail.x;

    // ---- señales de la unidad de riesgos ----
    struct Chip { std::string label; bool on; };
    auto code = [](int f) { return f == 1 ? "01" : f == 2 ? "10" : "00"; };
    std::vector<Chip> chips = {
        {"StallF·D", g.stall}, {"FlushD", g.flush}, {"FlushE", g.stall || g.flush},
        {std::string("ForwardAE ") + code(g.forwardA), g.forwardA > 0}, {std::string("ForwardBE ") + code(g.forwardB), g.forwardB > 0},
        {"Match", g.match}, {"RegWriteM", g.regWriteM}, {"RegWriteW", g.regWriteW}, {"MemtoRegE", g.memToRegE}, {"BranchTkE", g.branchTaken || g.pcWrite}};
    const float padC = 8 * fs, gapC = 5 * fs, chipH = lh + 6 * fs, hzPad = 12 * fs;
    const char* hzTitle = "UNIDAD DE RIESGOS · HAZARD UNIT";
    const float titleW = ImGui::CalcTextSize(hzTitle).x, innerW = W - 8 - 2 * hzPad;
    std::vector<ImVec2> chipPos(chips.size()); int hzRows = 1;     // flujo de los chips: se parten en filas si no caben
    { float x = titleW + 2 * hzPad, y = 0; for (size_t i = 0; i < chips.size(); i++) { float w = ImGui::CalcTextSize(chips[i].label.c_str()).x + 2 * padC;
        if (x + w > innerW) { hzRows++; x = 0; y += chipH + 6 * fs; } chipPos[i] = ImVec2(x, y); x += w + gapC; } }

    // ---- mensaje del ciclo (se parte en líneas) ----
    std::string msg; ImVec4 mc = pal.text;
    if (g.stall && g.stallStage >= 0 && S.slots[g.stallStage].ins) { msg = std::string("Parada: ") + S.slots[g.stallStage].ins->src + " espera un dato" + (g.loadUse ? " que un LDR todavía está leyendo de memoria (se inserta una burbuja)." : " que aún no se ha escrito en el banco de registros (se inserta una burbuja)."); mc = pal.warn; }
    else if (g.flush) { msg = g.pcWrite ? "El PC se ha escrito desde ER (MOV PC / LDR PC / POP {PC}): se anulan las instrucciones captadas por el camino equivocado." : "Salto tomado: se anulan las instrucciones captadas por el camino equivocado y se capta desde el destino."; mc = pal.bad; }
    else if (g.forwardA || g.forwardB) { msg = "Anticipación: un operando llega directamente desde la etapa " + std::string((g.forwardA == 2 || g.forwardB == 2) ? "ER (ResultW)" : "M (ALUOutM)") + ", sin esperar al banco de registros."; mc = purple; }
    else if (st.cycles > 0) msg = "Sin riesgos en este ciclo.";
    else msg = "Pulsa Ciclo (F10) para avanzar la tubería un ciclo de reloj.";
    std::vector<std::string> msgLines = wrapLines(msg, W - 16);
    char s1[160], s2[160];
    std::snprintf(s1, sizeof s1, "Ciclo %ld  ·  %ld instrucciones terminadas  ·  CPI %.2f", st.cycles, st.retired, st.cpi());
    std::snprintf(s2, sizeof s2, "paradas %ld  ·  vaciados %ld (%ld anuladas)  ·  anticipaciones %ld", st.stallCycles, st.flushEvents, st.flushedInstrs, st.forwards);

    const float hdrH = lh + 8 * fs, occH = 2 * lh + 10 * fs, hzH = hzRows * (chipH + 6 * fs) + 8 * fs, gapHz = 2 * lh + 10 * fs;
    const float sumH = ((float)msgLines.size() + 2) * lh + 12 * fs;
    const float fixedH = hdrH + 4 * fs + occH + 6 * fs + gapHz + hzH + sumH;
    const float dpH = std::max(avail.y - fixedH, 170.0f * fs);
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##pipe", ImVec2(W, fixedH + dpH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float dpTop = o.y + hdrH + 4 * fs + occH + 6 * fs, dpBot = dpTop + dpH, hzTop = dpBot + gapHz;

    static const float bnd[6] = {0.01f, 0.22f, 0.46f, 0.68f, 0.84f, 0.99f};     // reparto horizontal de las 5 etapas lógicas
    auto PX = [&](float u) { return o.x + u * W; };
    auto P = [&](float u, float v) { return ImVec2(o.x + u * W, dpTop + v * dpH); };
    auto stageU = [&](int s, float& u0, float& u1) {
        int lo = M.logicalLo[s], hi = M.logicalHi[s], sub = M.subIndex[s], cnt = M.subCount[s];
        if (cnt <= 1) { u0 = bnd[lo]; u1 = bnd[hi + 1]; }
        else { float a = bnd[lo], b = bnd[lo + 1]; u0 = a + (b - a) * sub / cnt; u1 = a + (b - a) * (sub + 1) / cnt; }
    };
    // estado de cada etapa física: 0 vacía · 1 activa · 2 retenida · 3 anulada · 4 ruta equivocada
    auto stageState = [&](int s) { const PipeSlot& p = S.slots[s]; if (!p.valid) return 0; if (p.squashed) return 3; if (p.stalled) return 2; if (p.wrongPath) return 4; return 1; };
    auto regionState = [&](int L) { int best = 0; auto rank = [](int s_) { return s_ == 3 ? 5 : s_ == 2 ? 4 : s_ == 1 ? 3 : s_ == 4 ? 2 : 0; };
        for (int s = M.first[L]; s <= M.last[L]; s++) { int x = stageState(s); if (rank(x) > rank(best)) best = x; } return best; };
    auto colOf = [&](int s_) { return s_ == 1 ? pal.good : s_ == 2 ? pal.warn : s_ == 3 ? pal.bad : pal.accent; };

    // ---- bandas de fondo de cada etapa física ----
    for (int s = 0; s < k; s++) {
        float u0, u1; stageU(s, u0, u1); int x = stageState(s);
        ImVec4 tint = x == 2 ? withA(pal.warn, 0.10f) : x == 3 ? withA(pal.bad, 0.12f) : (s & 1) ? withA(pal.accent, 0.07f) : withA(pal.accent, 0.03f);
        dl->AddRectFilled(ImVec2(PX(u0), dpTop), ImVec2(PX(u1), dpBot), U32(tint));
    }
    // ---- cabeceras de las 5 etapas lógicas (como en la figura; abreviadas si no caben) ----
    static const char* LHl[LS_COUNT] = {"FETCH · BI", "DECODE · LR", "EXECUTE · E", "MEMORY · M", "WRITEBACK · ER"};
    for (int L = 0; L < LS_COUNT; L++) {
        ImVec2 a(PX(bnd[L]) + 2, o.y), b(PX(bnd[L + 1]) - 2, o.y + hdrH);
        const char* t = ImGui::CalcTextSize(LHl[L]).x + 10 <= b.x - a.x ? LHl[L] : logicalStageName(L);
        dl->AddRectFilled(a, b, U32(withA(mixc(pal.panel, pal.bg, 0.3f), 0.95f)), 4); dl->AddRect(a, b, U32(withA(pal.accent, 0.45f)), 4);
        ImVec2 ts = ImGui::CalcTextSize(t); dl->PushClipRect(a, b, true);
        dl->AddText(ImVec2((a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2), U32(withA(pal.text, 0.85f)), t); dl->PopClipRect();
    }
    // ---- qué instrucción hay en cada etapa ----
    for (int s = 0; s < k; s++) {
        float u0, u1; stageU(s, u0, u1); const PipeSlot& p = S.slots[s]; int x = stageState(s);
        ImVec2 a(PX(u0) + 2, o.y + hdrH + 4 * fs), b(PX(u1) - 2, a.y + occH);
        ImVec4 base = colOf(x);
        dl->AddRectFilled(a, b, U32(withA(mixc(pal.panel, base, x == 0 ? 0.0f : 0.28f), 0.95f)), 5);
        dl->AddRect(a, b, U32(withA(base, x == 0 ? 0.25f : x == 4 ? 0.4f : 0.9f)), 5, 0, x == 0 ? 1.0f : 1.8f);
        std::string l1 = M.names[s]; if (x == 2) l1 += " · parada"; else if (x == 3) l1 += " · anulada"; else if (x == 4) l1 += " · ruta errónea";
        std::string l2 = x == 0 ? "burbuja" : (p.ins ? p.ins->src : "?");
        dl->PushClipRect(a, b, true);
        dl->AddText(ImVec2(a.x + 5 * fs, a.y + 3 * fs), U32(withA(pal.text, 0.6f)), l1.c_str());
        dl->AddText(ImVec2(a.x + 5 * fs, a.y + 3 * fs + lh), U32(x == 0 ? dim : x == 3 ? withA(pal.bad, 0.9f) : pal.text), l2.c_str());
        if (x == 3) dl->AddLine(ImVec2(a.x + 5 * fs, a.y + 3 * fs + lh * 1.5f), ImVec2(std::min(b.x - 4, a.x + 5 * fs + ImGui::CalcTextSize(l2.c_str()).x), a.y + 3 * fs + lh * 1.5f), U32(pal.bad), 1.5f);
        dl->PopClipRect();
        if (x != 0 && ImGui::IsMouseHoveringRect(a, b) && ImGui::IsWindowHovered()) ImGui::SetTooltip("%s  %s\n%s", hx0x(p.addr).c_str(), p.ins ? p.ins->src.c_str() : "", p.wrongPath && !p.squashed ? "Captada por el camino equivocado: será anulada." : "");
    }

    // ---- cables ----
    auto wireCol = [&](int L, float* thick) {
        int x = regionState(L); *thick = x == 1 ? 2.4f : 1.6f;
        return x == 1 ? withA(pal.accent2, 0.95f) : x == 2 ? withA(pal.warn, 0.8f) : x == 3 ? withA(pal.bad, 0.6f) : withA(pal.accent, 0.26f); };
    auto poly = [&](int L, std::initializer_list<ImVec2> pts) {
        float t; ImVec4 col = wireCol(L, &t);
        const ImVec2* q = pts.begin(); for (size_t i = 0; i + 1 < pts.size(); i++) dl->AddLine(q[i], q[i + 1], U32(col), t); };
    const ImVec4 fwdCol = withA(purple, 0.95f), brCol = withA(pal.warn, 0.95f);
    poly(LS_F, {P(.058f,.46f), P(.075f,.46f)});
    poly(LS_F, {P(.150f,.46f), P(.285f,.46f)});
    poly(LS_D, {P(.20f,.46f), P(.20f,.80f), P(.285f,.80f)});
    poly(LS_F, {P(.064f,.46f), P(.064f,.80f), P(.075f,.80f)});
    poly(LS_F, {P(.105f,.80f), P(.165f,.80f), P(.165f,.20f), P(.012f,.20f), P(.012f,.46f), P(.020f,.46f)});
    poly(LS_D, {P(.385f,.29f), P(.505f,.29f)}); poly(LS_D, {P(.385f,.55f), P(.505f,.55f)});
    poly(LS_E, {P(.525f,.33f), P(.585f,.33f)}); poly(LS_E, {P(.525f,.59f), P(.585f,.59f)});
    poly(LS_D, {P(.360f,.80f), P(.555f,.80f), P(.555f,.59f)});
    poly(LS_E, {P(.650f,.46f), P(.735f,.46f)});
    poly(LS_E, {P(.47f,.55f), P(.47f,.74f), P(.72f,.74f), P(.72f,.58f), P(.735f,.58f)});
    poly(LS_M, {P(.690f,.46f), P(.690f,.70f), P(.875f,.70f), P(.875f,.50f), P(.895f,.50f)});
    poly(LS_M, {P(.810f,.42f), P(.895f,.42f)});
    { bool on = regionState(LS_W) == 1; ImVec4 wc = on ? withA(pal.accent2, 0.95f) : withA(pal.accent, 0.26f); float t = on ? 2.4f : 1.6f;
      ImVec2 q[] = {P(.915f,.46f), P(.97f,.46f), P(.97f,.93f), P(.375f,.93f), P(.375f,.66f)}; for (int i = 0; i < 4; i++) dl->AddLine(q[i], q[i + 1], U32(wc), t); }
    const bool fwdOn = c.model.cpuConfig().forwarding && k > 2;
    const bool fM = g.forwardA == 1 || g.forwardB == 1, fW = g.forwardA == 2 || g.forwardB == 2;
    if (fwdOn) {
        ImVec4 cm = fM ? fwdCol : withA(purple, 0.22f), cw = fW ? fwdCol : withA(purple, 0.22f); float tm = fM ? 2.6f : 1.4f, tw = fW ? 2.6f : 1.4f;
        dl->AddLine(P(.70f,.46f), P(.70f,.84f), U32(cm), tm); dl->AddLine(P(.70f,.84f), P(.494f,.84f), U32(cm), tm); dl->AddLine(P(.494f,.84f), P(.494f,.33f), U32(cm), tm);
        dl->AddLine(P(.494f,.33f), P(.505f,.33f), U32(g.forwardA == 1 ? fwdCol : withA(purple, 0.22f)), 2.2f); dl->AddLine(P(.494f,.59f), P(.505f,.59f), U32(g.forwardB == 1 ? fwdCol : withA(purple, 0.22f)), 2.2f);
        dl->AddLine(P(.375f,.93f), P(.482f,.93f), U32(cw), tw); dl->AddLine(P(.482f,.93f), P(.482f,.37f), U32(cw), tw);
        dl->AddLine(P(.482f,.37f), P(.505f,.37f), U32(g.forwardA == 2 ? fwdCol : withA(purple, 0.22f)), 2.2f); dl->AddLine(P(.482f,.63f), P(.505f,.63f), U32(g.forwardB == 2 ? fwdCol : withA(purple, 0.22f)), 2.2f);
    }
    { bool on = g.branchTaken || g.pcWrite; ImVec4 bc = on ? brCol : withA(pal.accent, 0.26f); float t = on ? 2.8f : 1.6f;
      ImVec2 q[] = {P(.665f,.46f), P(.665f,.03f), P(.012f,.03f), P(.012f,.20f)}; for (int i = 0; i < 3; i++) dl->AddLine(q[i], q[i + 1], U32(bc), t); }

    // ---- bloques (con etiqueta abreviada si la larga no cabe) ----
    auto block = [&](int L, float u0, float v0, float u1, float v1, const char* longL, const char* shortL) {
        int x = regionState(L); ImVec2 a = P(u0, v0), b = P(u1, v1); ImVec4 base = colOf(x);
        if (x == 1) dl->AddRectFilled(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), U32(withA(pal.good, 0.12f)), 8);
        dl->AddRectFilled(a, b, U32(x == 0 || x == 4 ? withA(mixc(pal.panel, pal.bg, 0.3f), 0.95f) : mixc(pal.panel, base, 0.35f)), 5);
        dl->AddRect(a, b, U32(x == 0 || x == 4 ? withA(pal.accent, 0.6f) : base), 5, 0, x == 1 ? 2.2f : 1.4f);
        const char* lb = ImGui::CalcTextSize(longL).x + 8 <= b.x - a.x ? longL : shortL;
        ImVec2 ts = ImGui::CalcTextSize(lb); dl->PushClipRect(a, b, true);
        dl->AddText(ImVec2((a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2), U32(pal.text), lb); dl->PopClipRect(); };
    block(LS_F, .020f, .36f, .058f, .56f, "PC", "PC");
    block(LS_F, .075f, .26f, .150f, .66f, "Memoria de\ninstrucc.", "Mem.\ninstr.");
    block(LS_F, .075f, .74f, .105f, .86f, "+", "+");
    block(LS_D, .285f, .26f, .385f, .66f, "Banco de\nregistros", "Banco\nregs.");
    block(LS_D, .285f, .74f, .360f, .86f, "Extend", "Ext.");
    block(LS_E, .505f, .25f, .525f, .41f, "", ""); block(LS_E, .505f, .51f, .525f, .67f, "", "");
    block(LS_E, .585f, .26f, .650f, .66f, "ALU", "ALU");
    block(LS_M, .735f, .26f, .810f, .66f, "Memoria\nde datos", "Mem.\ndatos");
    block(LS_W, .895f, .36f, .915f, .56f, "", "");

    // ---- registros de segmentación (con su etiqueta) y colocación de textos sin solapes ----
    std::vector<ImVec4> occ;   // rectángulos (x0,y0,x1,y1) ya ocupados por etiquetas
    auto overlaps = [&](const ImVec4& r) { for (auto& q : occ) if (r.x < q.z && r.z > q.x && r.y < q.w && r.w > q.y) return true; return false; };
    auto placeLabel = [&](float u, float v, const std::string& t, const ImVec4& col, float maxShiftPx = 70) {
        ImVec2 p = P(u, v), ts = ImGui::CalcTextSize(t.c_str());
        for (float dx = 0; dx <= maxShiftPx; dx += 6 * fs) for (int sgn = (dx == 0 ? 1 : -1); sgn <= 1; sgn += 2) {
            float x = p.x + sgn * dx; ImVec4 r(x - 2, p.y - 1, x + ts.x + 2, p.y + ts.y + 1);
            if (r.x < o.x || r.z > o.x + W || overlaps(r)) continue;
            dl->AddText(ImVec2(x, p.y), U32(col), t.c_str()); occ.push_back(r); return; } };
    for (int i = 0; i + 1 < k; i++) {
        float u0, u1; stageU(i, u0, u1); float x = PX(u1); ImVec2 a(x - 6 * fs, dpTop + 0.15f * dpH), b(x + 6 * fs, dpTop + 0.90f * dpH);
        bool flushBar = (g.flush && i < M.last[LS_E]) || (g.stall && i == eFirst - 1), stallBar = g.stall && i <= eFirst - 1;
        ImVec4 f = mixc(pal.panel, pal.text, 0.55f); if (flushBar) f = mixc(f, pal.bad, 0.55f); else if (stallBar) f = mixc(f, pal.warn, 0.5f);
        dl->AddRectFilled(a, b, U32(f), 3); dl->AddRect(a, b, U32(withA(pal.text, 0.5f)), 3);
        occ.push_back(ImVec4(a.x - 3, a.y, b.x + 3, b.y));
        std::string l = lastTok(M.letters[i]) + "/" + firstTok(M.letters[i + 1]); ImVec2 ts = ImGui::CalcTextSize(l.c_str());
        ImVec2 ca(x - ts.x / 2 - 3 * fs, dpTop + 0.045f * dpH), cb(x + ts.x / 2 + 3 * fs, ca.y + lh + 2);
        dl->AddRectFilled(ca, cb, U32(withA(pal.bg, 0.85f)), 3); dl->AddRect(ca, cb, U32(withA(pal.text, 0.35f)), 3);
        dl->AddText(ImVec2(ca.x + 3 * fs, ca.y + 1), U32(pal.text), l.c_str());
        occ.push_back(ImVec4(ca.x - 2, ca.y - 2, cb.x + 2, cb.y + 2));
    }
    placeLabel(.30f, .05f, "ALUResultE → PC' (destino del salto)", g.branchTaken || g.pcWrite ? pal.warn : dim, 180);
    placeLabel(.50f, .965f, "ResultW → WD3", dim, 120);
    placeLabel(.545f, .185f, "SrcAE", dim, 40); placeLabel(.545f, .62f, "SrcBE", dim, 40);
    placeLabel(.700f, .16f, "ALUOutM", dim, 40); placeLabel(.895f, .245f, "ResultW", dim, 40);
    if (fwdOn) {
        placeLabel(.478f, .205f, "ForwardAE", g.forwardA ? purple : withA(purple, 0.7f), 50); placeLabel(.478f, .675f, "ForwardBE", g.forwardB ? purple : withA(purple, 0.7f), 50);
        placeLabel(.60f, .855f, "ALUOutM (anticipación)", fM ? purple : dim, 120); placeLabel(.50f, .895f, "ResultW (anticipación)", fW ? purple : dim, 120);
    }

    // ---- unidad de riesgos ----
    {
        ImVec2 a(o.x + 4, hzTop), b(o.x + W - 4, hzTop + hzH);
        dl->AddRectFilled(a, b, U32(withA(mixc(pal.panel, purple, 0.15f), 0.95f)), 8); dl->AddRect(a, b, U32(withA(purple, 0.9f)), 8, 0, 2.0f);
        dl->AddText(ImVec2(a.x + hzPad, a.y + 6 * fs + (chipH - lh) / 2), U32(purple), hzTitle);
        for (size_t i = 0; i < chips.size(); i++) {
            const Chip& ch = chips[i]; ImVec2 ts = ImGui::CalcTextSize(ch.label.c_str()); float w = ts.x + 2 * padC;
            ImVec2 ca(a.x + hzPad + chipPos[i].x, a.y + 6 * fs + chipPos[i].y), cb(ca.x + w, ca.y + chipH);
            dl->AddRectFilled(ca, cb, U32(ch.on ? withA(purple, 0.55f) : withA(pal.bg, 0.5f)), 5); dl->AddRect(ca, cb, U32(withA(purple, ch.on ? 1.0f : 0.5f)), 5, 0, ch.on ? 2.0f : 1.0f);
            dl->AddText(ImVec2(ca.x + padC, ca.y + (chipH - ts.y) / 2), U32(ch.on ? pal.text : withA(pal.text, 0.6f)), ch.label.c_str());
        }
        // líneas de control hacia el PC y los registros de segmentación (como en la figura)
        float u0, u1; stageU(0, u0, u1);
        struct Ctl { float x; std::string t; bool on; bool fromPc; };
        std::vector<Ctl> ctl; ctl.push_back({PX(.039f), "StallF", g.stall, true});
        if (eFirst - 1 > 0) { float v0, v1; stageU(eFirst - 1, v0, v1); ctl.push_back({PX(u1), "StallD·EN FlushD·CLR", g.stall || g.flush, false}); ctl.push_back({PX(v1), "FlushE·CLR", g.stall || g.flush, false}); }
        else ctl.push_back({PX(u1), "StallD·EN Flush·CLR", g.stall || g.flush, false});
        std::vector<ImVec4> lo;   // etiquetas ya colocadas sobre la unidad de riesgos
        for (auto& cl : ctl) {
            ImVec4 col = cl.on ? purple : withA(purple, 0.5f);
            dl->AddLine(ImVec2(cl.x, cl.fromPc ? P(0, .56f).y : dpTop + 0.90f * dpH), ImVec2(cl.x, hzTop), U32(col), cl.on ? 2.4f : 1.4f);
            ImVec2 ts = ImGui::CalcTextSize(cl.t.c_str());
            for (int row = 0; row < 2; row++) {
                ImVec4 r(cl.x - 4 * fs, hzTop - (row + 1) * (lh + 3 * fs), cl.x - 4 * fs + ts.x, hzTop - (row + 1) * (lh + 3 * fs) + lh); bool hit = false;
                for (auto& q : lo) if (r.x < q.z && r.z > q.x && r.y < q.w && r.w > q.y) hit = true;
                if (hit && row == 0) continue;
                if (r.z > o.x + W) { float sh = r.z - (o.x + W); r.x -= sh; r.z -= sh; }
                dl->AddText(ImVec2(r.x, r.y), U32(col), cl.t.c_str()); lo.push_back(r); break;
            }
        }
    }

    // ---- mensaje del ciclo y contadores ----
    {
        float y = hzTop + hzH + 4 * fs;
        for (auto& ln : msgLines) { dl->AddText(ImVec2(o.x + 8, y), U32(mc), ln.c_str()); y += lh; }
        y += 3 * fs; dl->AddText(ImVec2(o.x + 8, y), U32(withA(pal.text, 0.85f)), s1);
        dl->AddText(ImVec2(o.x + 8, y + lh), U32(withA(pal.text, 0.85f)), s2);
    }
}

// ====================================== Panel "Camino de datos" ======================================
void DatapathPanel::content(ViewContext& c) {
    drawCpuSelector(c);                         // (puede reconstruir el modelo de ciclos: se consulta después)
    ImGui::Separator();
    const CpuConfig cfg = c.model.cpuConfig();
    if (!c.model.cpu()) { ImGui::TextDisabled("Ensambla un programa para ver el camino de datos."); return; }
    if (cfg.kind == CpuKind::Pipelined) drawPipelineDiagram(c);
    else drawBlockDiagram(c, cfg.kind == CpuKind::Multi);
}

// ============================ Panel "Ciclos y segmentación" (diagrama de ejecución) ============================
static ImVec4 stageColor(int idx, int count) {
    float h = std::fmod(0.58f + 0.78f * (float)idx / (float)std::max(1, count), 1.0f);
    ImVec4 col; ImGui::ColorConvertHSVtoRGB(h, 0.42f, 0.82f, col.x, col.y, col.z); col.w = 1; return col;
}

void PipelinePanel::content(ViewContext& c) {
    const SimModel& m = c.model; const UiPrefs& ui = c.ui(); Pal pal(ui.col);
    const CpuModel* cpu = m.cpu();
    if (!cpu) { ImGui::TextDisabled("Ensambla un programa para ver los ciclos."); return; }
    const CpuConfig cfg = cpu->config(); const CpuStats& st = cpu->stats(); const StageMap& M = cpu->stageMap();
    const float fs = ui.fontScale, lh = ImGui::GetTextLineHeight();

    ImGui::TextColored(pal.accent2, "%s", cpuConfigText(cfg).c_str());
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    ImGui::Text("ciclos %ld   instrucciones %ld   CPI %.2f", st.cycles, st.retired, st.cpi());
    if (cfg.kind == CpuKind::Pipelined) { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); ImGui::Text("paradas %ld   vaciados %ld (%ld anuladas)   anticipaciones %ld", st.stallCycles, st.flushEvents, st.flushedInstrs, st.forwards); }
    if (cfg.kind == CpuKind::Single) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("Monociclo: cada instrucción se ejecuta en un solo ciclo (CPI = 1). Elige Multiciclo o Segmentado en el camino de datos."); ImGui::PopTextWrapPos(); }

    // ---- leyenda ----
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto chip = [&](const std::string& t, const ImVec4& col) {
            ImVec2 p = ImGui::GetCursorScreenPos(), ts = ImGui::CalcTextSize(t.c_str()); float w = ts.x + 12 * fs;
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + lh + 2), U32(col), 4); dl->AddText(ImVec2(p.x + 6 * fs, p.y + 1), IM_COL32(20, 22, 30, 255), t.c_str());
            ImGui::Dummy(ImVec2(w, lh + 2)); ImGui::SameLine(0, 6 * fs); };
        if (cfg.kind == CpuKind::Pipelined) for (int s = 0; s < M.k; s++) chip(M.names[s], stageColor(s, M.k));
        else if (cfg.kind == CpuKind::Multi) for (int s = 0; s < LS_COUNT; s++) chip(logicalStageName(s), stageColor(s, LS_COUNT));
        if (cfg.kind == CpuKind::Pipelined) { chip("parada", pal.warn); chip("anulada (flush)", pal.bad); chip("ruta errónea", withA(pal.bad, 0.45f)); }
        ImGui::NewLine();
    }

    const auto& tl = cpu->timeline();
    if (tl.empty()) { ImGui::TextDisabled("Aún no se ha ejecutado ningún ciclo. Pulsa Paso (F10) o Ejecutar."); return; }

    // ---- filas (una por instrucción) y columnas (una por ciclo) ----
    struct Row { uint64_t seq; uint32_t addr; const Instruction* ins; };
    std::vector<Row> rows; std::unordered_map<uint64_t, int> rowOf;
    for (auto& cy : tl) for (auto& cell : cy.cells) if (!rowOf.count(cell.seq)) { rowOf[cell.seq] = (int)rows.size(); rows.push_back({cell.seq, cell.addr, cell.ins}); }
    const int maxRows = 64; int skip = std::max(0, (int)rows.size() - maxRows);
    const int nr = (int)rows.size() - skip, nc = (int)tl.size();
    float cw = 38 * fs; if (cfg.kind == CpuKind::Single) cw = ImGui::CalcTextSize("CICLO").x + 14 * fs; for (int s = 0; s < M.k && cfg.kind == CpuKind::Pipelined; s++) cw = std::max(cw, ImGui::CalcTextSize(M.names[s].c_str()).x + 12 * fs);
    const float rowH = lh + 6 * fs, headH = lh + 6 * fs, labW = 250 * fs;

    ImGui::BeginChild("##gantt", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float totalW = labW + nc * cw + 12 * fs, totalH = headH + nr * rowH + 4 * fs;
    ImGui::Dummy(ImVec2(totalW, totalH));
    const float sx = ImGui::GetScrollX(), viewW = ImGui::GetWindowWidth();
    const long firstCycle = tl.front().cycle;

    // cuadrícula
    const float sy = ImGui::GetScrollY(); std::vector<std::pair<float, std::string>> hdr;
    for (int ci = 0; ci < nc; ci++) {
        float x = p0.x + labW + ci * cw;
        if (x + cw < p0.x + sx || x > p0.x + sx + viewW) continue;
        char b[16]; std::snprintf(b, sizeof b, "%ld", firstCycle + ci); ImVec2 ts = ImGui::CalcTextSize(b);
        hdr.push_back({x + (cw - ts.x) / 2, b});
        dl->AddLine(ImVec2(x, p0.y + headH), ImVec2(x, p0.y + totalH), U32(withA(pal.accent, 0.10f)));
        for (auto& cell : tl[ci].cells) {
            auto it = rowOf.find(cell.seq); if (it == rowOf.end() || it->second < skip) continue;
            int r = it->second - skip; ImVec2 a(x + 1, p0.y + headH + r * rowH + 1), bb(x + cw - 1, a.y + rowH - 2);
            std::string label = cpu->cellLabel(cell);
            ImVec4 col = cfg.kind == CpuKind::Single ? withA(pal.accent, 0.6f) : cfg.kind == CpuKind::Multi ? stageColor(cell.stage, LS_COUNT) : stageColor(cell.stage, M.k);
            if (cell.kind == 1) { col = pal.warn; label += "!"; }
            else if (cell.kind == 2) { col = pal.bad; label = "x " + label; }
            else if (cell.kind == 3) col = withA(pal.bad, 0.45f);
            dl->AddRectFilled(a, bb, U32(col), 4);
            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            dl->PushClipRect(a, bb, true); dl->AddText(ImVec2(a.x + (bb.x - a.x - ts.x) / 2, a.y + (bb.y - a.y - ts.y) / 2), IM_COL32(20, 22, 30, 255), label.c_str()); dl->PopClipRect();
        }
    }
    // columna de etiquetas "pegada" a la izquierda
    const ImVec4 labBg = mixc(pal.bg, pal.panel, 0.5f);
    dl->AddRectFilled(ImVec2(p0.x + sx, p0.y), ImVec2(p0.x + sx + labW - 4 * fs, p0.y + totalH), U32(withA(labBg, 1.0f)));
    for (int r = 0; r < nr; r++) {
        const Row& rw = rows[skip + r]; float y = p0.y + headH + r * rowH;
        std::string t = hx0x(rw.addr) + "  " + (rw.ins ? rw.ins->src : "?");
        dl->PushClipRect(ImVec2(p0.x + sx, y), ImVec2(p0.x + sx + labW - 6 * fs, y + rowH), true);
        dl->AddText(ImVec2(p0.x + sx + 4 * fs, y + 3 * fs), U32(pal.text), t.c_str()); dl->PopClipRect();
    }
    // cabecera (números de ciclo) fija arriba al desplazar
    dl->AddRectFilled(ImVec2(p0.x + sx, p0.y + sy), ImVec2(p0.x + sx + viewW, p0.y + sy + headH), U32(withA(labBg, 1.0f)));
    dl->AddLine(ImVec2(p0.x + sx, p0.y + sy + headH), ImVec2(p0.x + sx + viewW, p0.y + sy + headH), U32(withA(pal.accent, 0.35f)));
    for (auto& h : hdr) dl->AddText(ImVec2(h.first, p0.y + sy + 3 * fs), U32(withA(pal.text, 0.7f)), h.second.c_str());
    dl->AddRectFilled(ImVec2(p0.x + sx, p0.y + sy), ImVec2(p0.x + sx + labW - 4 * fs, p0.y + sy + headH), U32(withA(labBg, 1.0f)));
    dl->AddText(ImVec2(p0.x + sx + 4 * fs, p0.y + sy + 3 * fs), U32(withA(pal.text, 0.7f)), "instrucción \\ ciclo");
    if (seenCycles_ != st.cycles) { seenCycles_ = st.cycles; ImGui::SetScrollX(totalW); ImGui::SetScrollY(totalH); }
    ImGui::EndChild();
}
