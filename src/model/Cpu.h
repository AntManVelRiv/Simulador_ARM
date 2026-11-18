// model/Cpu.h — MODELO DE PROCESADOR: monociclo, multiciclo y segmentado (con k etapas configurables).
//
// El emulador funcional (arm::Machine) sigue siendo la única fuente de verdad del resultado de un programa:
// ejecuta una instrucción por llamada a step(). Este modelo decide CUÁNDO ocurre cada cosa (ciclo a ciclo)
// y qué instrucción ocupa cada etapa, de modo que el resultado de un programa es idéntico en los tres
// procesadores y solo cambian los ciclos empleados (CPI) y lo que se ve en el camino de datos.
//
//   * Monociclo : 1 ciclo por instrucción (CPI = 1).
//   * Multiciclo: cada instrucción pasa por las etapas que necesita (BI, LR, E, M, ER) y cada etapa es un ciclo.
//   * Segmentado: tubería en orden de k etapas (2..10, por defecto 5 = BI, LR, E, M, ER como en la figura 7.58 de
//                 Harris & Harris) con unidad de riesgos: anticipación (forwarding), parada por dependencia de
//                 un LDR con la instrucción siguiente y vaciado (flush) de las instrucciones captadas tras un
//                 salto tomado.
//
// Convenio de ejecución: la instrucción modifica registros, flags y memoria cuando pasa por la ÚLTIMA etapa de
// ejecución (E). Es el punto en el que el resultado de la ALU, los flags y la decisión de un salto están
// disponibles. (En la etapa de escritura, ER, solo se modela el momento en que el resultado llegaría al banco.)
//
// No depende de ninguna biblioteca gráfica.
#pragma once
#include "Arm.h"
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace arm {

enum class CpuKind : int { Single = 0, Multi = 1, Pipelined = 2 };

constexpr int PIPE_MIN_STAGES = 2, PIPE_MAX_STAGES = 10, PIPE_DEFAULT_STAGES = 5;

struct CpuConfig {
    CpuKind kind = CpuKind::Single;          // por defecto: monociclo
    int stages = PIPE_DEFAULT_STAGES;        // solo segmentado: nº de etapas (2..10)
    bool forwarding = true;                  // solo segmentado: anticipación de resultados (forwarding)
    int clampedStages() const { return stages < PIPE_MIN_STAGES ? PIPE_MIN_STAGES : stages > PIPE_MAX_STAGES ? PIPE_MAX_STAGES : stages; }
    bool operator==(const CpuConfig& o) const { return kind == o.kind && stages == o.stages && forwarding == o.forwarding; }
    bool operator!=(const CpuConfig& o) const { return !(*this == o); }
};
const char* cpuKindName(CpuKind k);            // "Monociclo" | "Multiciclo" | "Segmentado"
std::string cpuConfigText(const CpuConfig& c); // p. ej. "Segmentado (5 etapas, con anticipación)"

// Etapas lógicas de una instrucción (las de la figura del libro)
enum LStage { LS_F = 0, LS_D, LS_E, LS_M, LS_W, LS_COUNT };
const char* logicalStageName(int ls);     // "BI", "LR", "E", "M", "ER"
const char* logicalStageLetter(int ls);   // "F", "D", "E", "M", "W"
const char* logicalStageTitle(int ls);    // "Búsqueda (fetch)", ...

// Cómo se reparten las 5 etapas lógicas entre las k etapas físicas de la tubería:
//   k < 5 : se fusionan (2: BI | LR+E+M+ER ; 3: BI | LR | E+M+ER ; 4: BI | LR | E | M+ER)
//   k = 5 : BI | LR | E | M | ER
//   k > 5 : se parten en subetapas, por este orden: E, M, BI, LR, ER (6: E1 E2 ; 7: +M1 M2 ; 8: +BI1 BI2 ; ...)
struct StageMap {
    int k = 5;
    int first[LS_COUNT] = {0, 1, 2, 3, 4};    // primera etapa física que contiene cada etapa lógica
    int last[LS_COUNT]  = {0, 1, 2, 3, 4};    // última
    std::vector<std::string> names;           // nombre de cada etapa física: "BI", "E1", "LR+E+M+ER"...
    std::vector<std::string> letters;         // igual con letras del libro: "F", "D", "E1", "D+E+M+W"...
    std::vector<int> logicalLo, logicalHi;    // etapas lógicas que contiene cada etapa física (rango)
    std::vector<int> subIndex, subCount;      // si la etapa física es una subetapa de una lógica: j-ésima de n
    static StageMap make(int k);
};

// Qué lee y qué escribe una instrucción (para detectar riesgos). Se excluye R15 (el PC se trata aparte).
struct InstrInfo {
    uint16_t srcA = 0, srcB = 0;   // registros que lee: A = primer operando (Rn / base), B = el resto (operando 2, dato a guardar...)
    uint16_t dst = 0;              // registros que escribe
    bool isLoad = false;           // el dato llega de la memoria (disponible al final de M)
    bool isBranch = false;         // B / BL / BX: el destino se conoce al final de E
    bool writesPc = false;         // escribe el PC sin ser un salto (MOV PC,LR; LDR PC,...; POP {...,PC})
    uint16_t src() const { return (uint16_t)(srcA | srcB); }
};
InstrInfo analyzeInstruction(const Instruction& ins);

// ------------------------------- Estado de la tubería --------------------------------
struct PipeSlot {
    bool valid = false;
    uint64_t seq = 0;                    // identificador único de cada instrucción captada
    uint32_t addr = 0;
    const Instruction* ins = nullptr;
    InstrInfo info;
    bool executed = false;               // la máquina ya la ha ejecutado (pasó por la última etapa de E)
    bool wrongPath = false;              // captada por la ruta equivocada: no se ejecutará y será anulada
    bool squashed = false;               // anulada (flush) en este ciclo
    bool condOk = true;                  // su condición se cumplió (conocido tras ejecutarla)
    bool taken = false;                  // ha cambiado el PC (salto tomado)
    bool redirected = false;             // su salto ya ha sido resuelto
    bool stalled = false;                // retenida en este ciclo por un riesgo de datos
    bool fwdCounted = false;
    bool retired = false;                // ya contada como terminada (ha completado su ciclo en la última etapa)
    uint32_t target = 0;                 // dirección a la que salta
};

// Señales de la unidad de riesgos en el ciclo actual (las de la figura 7.58)
struct HazardSignals {
    bool stall = false;                  // StallF + StallD + FlushE: se detiene la parte inicial y se inserta una burbuja
    bool loadUse = false;                // la causa es un LDR (MemtoRegE) cuyo dato aún no está listo
    bool match = false;                  // hay dependencia de datos con una instrucción más avanzada (Match)
    bool flush = false;                  // FlushD + FlushE: se anulan las instrucciones captadas tras un salto
    bool branchTaken = false;            // BranchTakenE
    bool pcWrite = false;                // escritura del PC por una instrucción que no es un salto (ResultW -> PC')
    int forwardA = 0, forwardB = 0;      // 0 = ninguna, 1 = desde M (ALUOutM), 2 = desde ER (ResultW)
    int stallStage = -1;                 // etapa cuya instrucción se retiene
    bool regWriteM = false, regWriteW = false, memToRegE = false;
};

struct CpuStats {
    long cycles = 0;          // ciclos de reloj transcurridos
    long executed = 0;        // instrucciones ejecutadas (resultado ya aplicado)
    long retired = 0;         // instrucciones terminadas (han salido de la última etapa)
    long stallCycles = 0;     // ciclos con parada por riesgo de datos
    long flushEvents = 0;     // saltos tomados que han vaciado la tubería
    long flushedInstrs = 0;   // instrucciones anuladas
    long forwards = 0;        // operandos anticipados
    double cpi() const { return retired > 0 ? (double)cycles / (double)retired : 0.0; }
};

struct CpuState {
    CpuStats st;
    uint64_t nextSeq = 1;
    int lastLogical = -1;                // última etapa lógica ejecutada (multiciclo; -1 = ninguna / monociclo)
    // monociclo / multiciclo
    bool mcActive = false;
    uint32_t mcAddr = 0; const Instruction* mcIns = nullptr; uint64_t mcSeq = 0;
    std::vector<int> mcPlan;             // etapas lógicas que le tocan a la instrucción en curso
    int mcPhase = 0;                     // índice (dentro de mcPlan) de la etapa del último ciclo
    // segmentado
    std::vector<PipeSlot> slots;         // una por etapa física
    uint32_t fetchPc = 0;
    bool fetching = true;
    bool pendingPc = false;              // una instrucción que escribe el PC (sin ser salto) ya se ejecutó pero aún no ha llegado a ER: lo captado después es ruta equivocada
    HazardSignals sig;
    uint64_t lastFetchedSeq = 0;         // instrucción captada en el último ciclo (0 = ninguna)
    const Instruction* lastFetched = nullptr;
};

// Una celda del diagrama de ejecución: qué instrucción estaba en qué etapa en un ciclo dado
struct TlCell {
    uint64_t seq = 0; uint32_t addr = 0; const Instruction* ins = nullptr;
    int stage = 0;                       // etapa física (segmentado) o lógica (multiciclo); -1 = ciclo completo (monociclo)
    uint8_t kind = 0;                    // 0 normal · 1 retenida (parada) · 2 anulada (flush) · 3 ruta equivocada
};
struct TlCycle { long cycle = 0; std::vector<TlCell> cells; };

class CpuModel {
public:
    CpuModel(const Program& prog, const CpuConfig& cfg);

    const CpuConfig& config() const { return cfg_; }
    const StageMap& stageMap() const { return map_; }
    const CpuState& state() const { return s_; }
    const CpuStats& stats() const { return s_.st; }
    const std::deque<TlCycle>& timeline() const { return tl_; }

    struct ClockResult {
        bool ok = false;                       // false = no se ha consumido ningún ciclo (terminado o esperando entrada)
        bool executed = false;                 // en este ciclo se ha ejecutado una instrucción
        std::optional<StepRecord> rec;         // lo que ha hecho la instrucción ejecutada
    };
    // Avanza un ciclo de reloj. En el monociclo, un ciclo = una instrucción.
    ClockResult clock(Machine& m);

    struct BackResult { bool ok = false; bool undidInstruction = false; };
    BackResult back(Machine& m);               // deshace el último ciclo (y la instrucción que ejecutó, si la hubo)
    bool canBack() const { return !hist_.empty(); }
    void clearHistory() { hist_.clear(); }

    // Vacía la tubería y empieza de nuevo desde el PC de la máquina (se usa al reiniciar y al editar el PC)
    void reset(const Machine& m);
    void resync(const Machine& m);             // como reset pero conserva estadísticas y diagrama

    bool busy() const;                         // quedan instrucciones a medias (multiciclo o segmentado)
    bool atInstructionBoundary() const { return !s_.mcActive; }   // monociclo/multiciclo: ¿empieza una instrucción nueva?
    const Instruction* fetchedLastCycle() const { return s_.lastFetched; }   // segmentado: instrucción captada en el último ciclo

    // Nombre de la etapa de una celda del diagrama ("BI", "E2", "CICLO"...)
    std::string cellLabel(const TlCell& c) const;

private:
    ClockResult clockSingle(Machine& m);
    ClockResult clockMulti(Machine& m);
    ClockResult clockPipe(Machine& m);
    void pushHistory(const CpuState& before, bool executed);
    void pushTimeline(const std::vector<TlCell>& cells);

    const Program* prog_;
    CpuConfig cfg_;
    StageMap map_;
    CpuState s_;
    struct Hist { CpuState before; bool executed; };
    std::deque<Hist> hist_;
    std::deque<TlCycle> tl_;
};

} // namespace arm
