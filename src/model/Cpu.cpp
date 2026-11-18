// model/Cpu.cpp — modelo de procesador: monociclo, multiciclo y segmentado de k etapas.
#include "Cpu.h"
#include <algorithm>

namespace arm {

// ------------------------------------ Nombres ------------------------------------
const char* cpuKindName(CpuKind k) {
    switch (k) { case CpuKind::Single: return "Monociclo"; case CpuKind::Multi: return "Multiciclo"; default: return "Segmentado"; }
}
std::string cpuConfigText(const CpuConfig& c) {
    if (c.kind != CpuKind::Pipelined) return cpuKindName(c.kind);
    return std::string("Segmentado (") + std::to_string(c.clampedStages()) + " etapas, " + (c.forwarding ? "con" : "sin") + " anticipación)";
}
static const char* LNAME[LS_COUNT]   = {"BI", "LR", "E", "M", "ER"};
static const char* LLETTER[LS_COUNT] = {"F", "D", "E", "M", "W"};
static const char* LTITLE[LS_COUNT]  = {"Búsqueda de instrucción (BI)", "Lectura de registros (LR)", "Ejecución (E)", "Memoria (M)", "Escritura en registros (ER)"};
const char* logicalStageName(int ls)   { return (ls >= 0 && ls < LS_COUNT) ? LNAME[ls] : "?"; }
const char* logicalStageLetter(int ls) { return (ls >= 0 && ls < LS_COUNT) ? LLETTER[ls] : "?"; }
const char* logicalStageTitle(int ls)  { return (ls >= 0 && ls < LS_COUNT) ? LTITLE[ls] : "?"; }

// ---------------------------------- Mapa de etapas ----------------------------------
StageMap StageMap::make(int k) {
    StageMap m; k = std::max(PIPE_MIN_STAGES, std::min(PIPE_MAX_STAGES, k)); m.k = k;
    auto add = [&](int lo, int hi, int sub, int cnt) {
        int idx = (int)m.names.size();
        std::string n, l;
        for (int s = lo; s <= hi; s++) { if (s > lo) { n += "+"; l += "+"; } n += LNAME[s]; l += LLETTER[s]; }
        if (cnt > 1) { n += std::to_string(sub + 1); l += std::to_string(sub + 1); }
        m.names.push_back(n); m.letters.push_back(l);
        m.logicalLo.push_back(lo); m.logicalHi.push_back(hi); m.subIndex.push_back(sub); m.subCount.push_back(cnt);
        for (int s = lo; s <= hi; s++) { if (sub == 0) m.first[s] = idx; m.last[s] = idx; }
    };
    if (k < 5) {
        if (k == 2) { add(LS_F, LS_F, 0, 1); add(LS_D, LS_W, 0, 1); }
        else if (k == 3) { add(LS_F, LS_F, 0, 1); add(LS_D, LS_D, 0, 1); add(LS_E, LS_W, 0, 1); }
        else { add(LS_F, LS_F, 0, 1); add(LS_D, LS_D, 0, 1); add(LS_E, LS_E, 0, 1); add(LS_M, LS_W, 0, 1); }
    } else {
        int n[LS_COUNT] = {1, 1, 1, 1, 1};
        static const int order[5] = {LS_E, LS_M, LS_F, LS_D, LS_W};
        for (int i = 0; i < k - 5; i++) n[order[i % 5]]++;
        for (int s = 0; s < LS_COUNT; s++) for (int j = 0; j < n[s]; j++) add(s, s, j, n[s]);
    }
    return m;
}

// --------------------------- Análisis de registros de una instrucción ---------------------------
static uint16_t rbit(int r) { return (r >= 0 && r < 15) ? (uint16_t)(1u << r) : (uint16_t)0; }

InstrInfo analyzeInstruction(const Instruction& I) {
    InstrInfo x;
    switch (I.t) {
    case IType::DP: {
        bool test = I.mnemonic == "CMP" || I.mnemonic == "CMN" || I.mnemonic == "TST" || I.mnemonic == "TEQ";
        x.srcA = rbit(I.rn);
        if (!I.op2.isImm) { x.srcB |= rbit(I.op2.reg); if (I.op2.shiftIsReg) x.srcB |= rbit(I.op2.shiftReg); }
        if (!test) { x.dst = rbit(I.rd); if (I.rd == 15) x.writesPc = true; }
        break; }
    case IType::MUL:
        x.srcA = rbit(I.rm); x.srcB = rbit(I.rs); if (I.mnemonic == "MLA") x.srcB |= rbit(I.rn);
        x.dst = rbit(I.rd); break;
    case IType::MULL:
        x.srcA = rbit(I.rm); x.srcB = rbit(I.rs);
        if (I.mnemonic.size() >= 5 && I.mnemonic.compare(1, 4, "MLAL") == 0) x.srcB |= rbit(I.rdLo) | rbit(I.rdHi);
        x.dst = rbit(I.rdLo) | rbit(I.rdHi); break;
    case IType::MEM:
        x.srcA = rbit(I.mem.baseReg);
        if (I.mem.offsetIsReg) x.srcB |= rbit(I.mem.offsetReg);
        if (I.load) { x.dst = rbit(I.rd); x.isLoad = true; if (I.rd == 15) x.writesPc = true; }
        else x.srcB |= rbit(I.rd);
        if (I.mem.writeBack || I.mem.postIndexed) x.dst |= rbit(I.mem.baseReg);
        break;
    case IType::BLK:
        x.srcA = rbit(I.blkBase);
        if (I.load) { x.dst = (uint16_t)(I.regList & 0x7FFF); x.isLoad = true; if (I.regList & 0x8000) x.writesPc = true; }
        else x.srcB = (uint16_t)(I.regList & 0x7FFF);
        if (I.blkWb) x.dst |= rbit(I.blkBase);
        break;
    case IType::BR:
        x.isBranch = true; if (I.link) x.dst = rbit(14);
        if (libraryFunctionName((uint32_t)((int32_t)(I.addr + 8) + I.branchOffsetWords * 4))) { x.srcA = 0x000F; x.dst |= rbit(0); }   // printf(R0..R3) -> R0
        break;
    case IType::BX: x.isBranch = true; x.srcA = rbit(I.rm); break;
    case IType::SWI: {                                  // lo que lee/escribe cada servicio simulado
        uint32_t n = I.swiNum;
        if (n == 0x00 || (n >= 0x20 && n <= 0x24)) x.srcA = rbit(0);          // imprimir R0
        else if (n == 0x6B) x.srcA = rbit(1);                                  // imprimir R1
        else if (n == 0x30 || n == 0x31) x.dst = rbit(0);                      // leer un entero / carácter -> R0
        else if (n == 0x32) { x.srcA = rbit(1); x.srcB = rbit(2); x.dst = rbit(0); }   // leer línea en [R1], máx. R2 -> R0
        break; }
    default: break;
    }
    return x;
}

// ------------------------------------- CpuModel -------------------------------------
CpuModel::CpuModel(const Program& prog, const CpuConfig& cfg) : prog_(&prog), cfg_(cfg), map_(StageMap::make(cfg.clampedStages())) {
    cfg_.stages = cfg_.clampedStages();
    s_.slots.assign(map_.k, PipeSlot());
}

void CpuModel::reset(const Machine& m) {
    s_ = CpuState(); s_.slots.assign(map_.k, PipeSlot());
    s_.fetchPc = m.pc; s_.fetching = !m.halted;
    hist_.clear(); tl_.clear();
}

void CpuModel::resync(const Machine& m) {
    CpuStats st = s_.st; uint64_t seq = s_.nextSeq;
    s_ = CpuState(); s_.st = st; s_.nextSeq = seq; s_.slots.assign(map_.k, PipeSlot());
    s_.fetchPc = m.pc; s_.fetching = !m.halted;
    hist_.clear();
}

bool CpuModel::busy() const {
    if (cfg_.kind == CpuKind::Multi) return s_.mcActive;
    if (cfg_.kind == CpuKind::Pipelined) {
        // la instrucción de la última etapa ya ha terminado si no se va a captar nada más (fin del programa)
        for (size_t i = 0; i < s_.slots.size(); i++) { const PipeSlot& sl = s_.slots[i]; if (sl.valid && !sl.squashed && (s_.fetching || i + 1 < s_.slots.size())) return true; }
    }
    return false;
}

std::string CpuModel::cellLabel(const TlCell& c) const {
    if (cfg_.kind == CpuKind::Single || c.stage < 0) return "CICLO";
    if (cfg_.kind == CpuKind::Multi) return logicalStageName(c.stage);
    return c.stage < (int)map_.names.size() ? map_.names[c.stage] : "?";
}

void CpuModel::pushHistory(const CpuState& before, bool executed) {
    hist_.push_back({before, executed});
    if (hist_.size() > 4000) hist_.pop_front();
}
void CpuModel::pushTimeline(const std::vector<TlCell>& cells) {
    TlCycle c; c.cycle = s_.st.cycles; c.cells = cells; tl_.push_back(std::move(c));
    if (tl_.size() > 600) tl_.pop_front();
}

CpuModel::BackResult CpuModel::back(Machine& m) {
    BackResult r;
    if (hist_.empty()) return r;
    if (hist_.back().executed && !m.canBack()) return r;
    Hist h = std::move(hist_.back()); hist_.pop_back();
    if (h.executed) m.back();
    s_ = std::move(h.before);
    if (!tl_.empty()) tl_.pop_back();
    r.ok = true; r.undidInstruction = h.executed;
    return r;
}

CpuModel::ClockResult CpuModel::clock(Machine& m) {
    switch (cfg_.kind) {
        case CpuKind::Single: return clockSingle(m);
        case CpuKind::Multi:  return clockMulti(m);
        default:              return clockPipe(m);
    }
}

// ------------------------------------ Monociclo ------------------------------------
CpuModel::ClockResult CpuModel::clockSingle(Machine& m) {
    ClockResult res;
    if (m.halted) return res;
    CpuState before = s_;
    auto rec = m.step();
    if (!rec) return res;
    s_.st.cycles++; s_.st.executed++; s_.st.retired++; s_.lastLogical = -1;
    TlCell c; c.seq = s_.nextSeq++; c.addr = rec->addr; c.stage = -1;
    auto it = prog_->byAddr.find(rec->addr); c.ins = it != prog_->byAddr.end() ? &it->second : nullptr;
    pushTimeline({c});
    if (m.keepHistory) pushHistory(before, true);
    res.ok = true; res.executed = true; res.rec = std::move(rec);
    return res;
}

// ------------------------------------ Multiciclo ------------------------------------
CpuModel::ClockResult CpuModel::clockMulti(Machine& m) {
    ClockResult res;
    if (m.halted && (m.haltKind == "input" || !s_.mcActive)) return res;
    CpuState before = s_;
    if (!s_.mcActive) {
        auto it = prog_->byAddr.find(m.pc);
        if (it == prog_->byAddr.end()) { m.step(); return res; }   // la máquina informa del error
        s_.mcActive = true; s_.mcAddr = m.pc; s_.mcIns = &it->second; s_.mcSeq = s_.nextSeq++;
        s_.mcPlan = {LS_F, LS_D, LS_E}; s_.mcPhase = 0;
    } else s_.mcPhase++;
    const int ls = s_.mcPlan[s_.mcPhase];
    std::optional<StepRecord> rec;
    if (ls == LS_E) {
        rec = m.step();
        if (!rec) { s_ = before; return res; }                     // esperando entrada (o error): no se consume el ciclo
        s_.st.executed++;
        if (rec->executed) {
            const Instruction& I = *s_.mcIns; InstrInfo inf = analyzeInstruction(I);
            if (I.t == IType::MEM) s_.mcPlan.push_back(LS_M);
            else if (I.t == IType::BLK) { int n = 0; for (int i = 0; i < 16; i++) if (I.regList & (1u << i)) n++; for (int i = 0; i < n; i++) s_.mcPlan.push_back(LS_M); }
            bool wr = (I.t == IType::SWI) ? !rec->regs.empty() : (inf.dst || inf.writesPc);
            if (wr) s_.mcPlan.push_back(LS_W);
        }
    }
    s_.st.cycles++; s_.lastLogical = ls;
    TlCell c; c.seq = s_.mcSeq; c.addr = s_.mcAddr; c.ins = s_.mcIns; c.stage = ls;
    pushTimeline({c});
    if (s_.mcPhase + 1 >= (int)s_.mcPlan.size()) { s_.mcActive = false; s_.st.retired++; }
    if (m.keepHistory) pushHistory(before, rec.has_value());
    res.ok = true; res.executed = rec.has_value(); res.rec = std::move(rec);
    return res;
}

// ------------------------------------ Segmentado ------------------------------------
CpuModel::ClockResult CpuModel::clockPipe(Machine& m) {
    ClockResult res;
    if (m.halted && (m.haltKind == "input" || !busy())) return res;
    CpuState before = s_;
    CpuState& S = s_;
    const int k = map_.k, eFirst = map_.first[LS_E], eEnd = map_.last[LS_E], mEnd = map_.last[LS_M], wFirst = map_.first[LS_W];
    const bool fwd = cfg_.forwarding;
    S.st.cycles++;

    // ---- A: el reloj avanza las instrucciones (de atrás hacia delante) y se capta una nueva ----
    std::vector<PipeSlot> nxt(k);
    int hold = (S.sig.stall && S.sig.stallStage >= 0) ? S.sig.stallStage : -1;
    for (int s = k - 1; s >= 0; s--) {
        const PipeSlot& c = S.slots[s];
        if (!c.valid || c.squashed) continue;                    // las anuladas desaparecen: queda una burbuja
        if (s == k - 1) continue;                                 // ya terminó en el ciclo anterior
        PipeSlot n = c; n.stalled = false;
        if (s <= hold) nxt[s] = n; else nxt[s + 1] = n;
    }
    S.lastFetched = nullptr; S.lastFetchedSeq = 0;
    if (!nxt[0].valid && S.fetching) {
        auto it = prog_->byAddr.find(S.fetchPc);
        if (it != prog_->byAddr.end()) {
            PipeSlot f; f.valid = true; f.seq = S.nextSeq++; f.addr = S.fetchPc; f.ins = &it->second; f.info = analyzeInstruction(it->second); f.wrongPath = S.pendingPc;
            nxt[0] = f; S.fetchPc += 4; S.lastFetched = f.ins; S.lastFetchedSeq = f.seq;
        }
    }
    S.slots = std::move(nxt);

    // ---- B: la instrucción que está en la última etapa de E se ejecuta (se aplica su resultado) ----
    std::optional<StepRecord> rec;
    {
        PipeSlot& e = S.slots[eEnd];
        if (e.valid && !e.executed && !e.wrongPath && !m.halted) {
            if (e.addr != m.pc) e.wrongPath = true;              // captada tras un salto/escritura de PC aún sin resolver
            else {
                rec = m.step();
                if (!rec) { S = before; return res; }            // esperando entrada (o error): no se consume el ciclo
                e.executed = true; e.condOk = rec->executed; e.taken = rec->pcNew != rec->addr + 4; e.target = rec->pcNew;
                S.st.executed++;
                if (e.taken && !e.info.isBranch) {               // escritura del PC en ER: todo lo posterior es ruta equivocada
                    S.pendingPc = true;
                    for (int s = 0; s < eEnd; s++) if (S.slots[s].valid) S.slots[s].wrongPath = true;
                }
                if (m.halted) {                                   // fin: las posteriores no llegarán a ejecutarse
                    S.fetching = false;
                    for (int s = 0; s < eEnd; s++) if (S.slots[s].valid && !S.slots[s].squashed) { S.slots[s].squashed = true; S.st.flushedInstrs++; }
                }
            }
        }
    }

    { PipeSlot& L = S.slots[k - 1]; if (L.valid && L.executed && !L.retired) { L.retired = true; S.st.retired++; } }   // termina al completar su ciclo en ER

    // ---- C: resolución de saltos / escrituras de PC: se anulan las instrucciones captadas por la ruta equivocada ----
    bool flushNow = false, branchNow = false, pcwNow = false;
    for (int s = 0; s < k; s++) {
        PipeSlot& c = S.slots[s];
        if (!(c.valid && c.executed && c.taken && !c.redirected && !c.squashed)) continue;
        int R = c.info.isBranch ? eEnd : k - 1;                  // salto: al final de E · otras escrituras de PC: en ER (ResultW -> PC')
        if (s != R) continue;
        c.redirected = true; int n = 0;
        for (int t = 0; t < s; t++) if (S.slots[t].valid && !S.slots[t].squashed) { S.slots[t].squashed = true; n++; }
        S.fetchPc = c.target; S.fetching = !m.halted; if (!c.info.isBranch) S.pendingPc = false;
        S.st.flushEvents++; S.st.flushedInstrs += n; flushNow = true;
        if (c.info.isBranch) branchNow = true; else pcwNow = true;
    }

    // ---- D: señales de la unidad de riesgos para este ciclo (rigen el avance del siguiente) ----
    HazardSignals g;
    {
        int chk = fwd ? eFirst - 1 : map_.last[LS_D];           // etapa donde se decide si la instrucción puede avanzar
        PipeSlot& ci = S.slots[chk];
        if (ci.valid && !ci.squashed) {
            for (int t = chk + 1; t < k; t++) {
                const PipeSlot& j = S.slots[t];
                if (!j.valid || j.squashed || !(j.info.dst & ci.info.src())) continue;
                g.match = true;
                int avail = fwd ? (j.info.isLoad ? mEnd : eEnd) : wFirst;   // etapa en la que su resultado ya se puede usar
                if (t < avail) { g.stall = true; if (j.info.isLoad && fwd) g.loadUse = true; }
            }
            if (g.stall) { g.stallStage = chk; ci.stalled = true; }
        }
        PipeSlot& ce = S.slots[eFirst];
        if (fwd && ce.valid && !ce.squashed) {
            auto find = [&](uint16_t mask) { if (!mask) return 0; for (int t = eFirst + 1; t < k; t++) { const PipeSlot& j = S.slots[t]; if (j.valid && !j.squashed && (j.info.dst & mask)) return t < wFirst ? 1 : 2; } return 0; };
            g.forwardA = find(ce.info.srcA); g.forwardB = find(ce.info.srcB);
            if ((g.forwardA || g.forwardB) && !ce.fwdCounted) { ce.fwdCounted = true; S.st.forwards += (g.forwardA ? 1 : 0) + (g.forwardB ? 1 : 0); }
        }
        const PipeSlot& sm = S.slots[mEnd]; const PipeSlot& sw = S.slots[k - 1]; const PipeSlot& se = S.slots[eEnd];
        g.regWriteM = sm.valid && !sm.squashed && sm.info.dst; g.regWriteW = sw.valid && !sw.squashed && sw.info.dst;
        g.memToRegE = se.valid && !se.squashed && se.info.isLoad;
        g.flush = flushNow; g.branchTaken = branchNow; g.pcWrite = pcwNow;
        if (g.stall) S.st.stallCycles++;
    }
    S.sig = g;

    std::vector<TlCell> cells;
    for (int s = 0; s < k; s++) {
        const PipeSlot& c = S.slots[s]; if (!c.valid) continue;
        TlCell t; t.seq = c.seq; t.addr = c.addr; t.ins = c.ins; t.stage = s;
        t.kind = c.squashed ? 2 : c.stalled ? 1 : c.wrongPath ? 3 : 0;
        cells.push_back(t);
    }
    pushTimeline(cells);
    if (m.keepHistory) pushHistory(before, rec.has_value());
    res.ok = true; res.executed = rec.has_value(); res.rec = std::move(rec);
    return res;
}

} // namespace arm
