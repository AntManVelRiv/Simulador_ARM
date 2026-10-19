// core/Arm.h — Núcleo del simulador ARMv4: ensamblador de dos pasadas + emulador de CPU.
// Sin dependencias externas: solo la biblioteca estándar de C++17.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <deque>
#include <array>

namespace arm {

// ------------------------------ Constantes --------------------------------
constexpr uint32_t TEXT_BASE  = 0x00001000;
constexpr uint32_t DATA_BASE  = 0x00002000;
constexpr uint32_t STACK_TOP  = 0x20200000;
// Direcciones reservadas para funciones de biblioteca simuladas (printf, scanf...).
// "BL printf" salta aquí; Machine::step() intercepta la ejecución en este rango y
// hace como si la función ya hubiera hecho su efecto y hubiera vuelto con BX LR.
constexpr uint32_t LIB_BASE   = 0xFFFF0000;
// Nombres de las funciones de biblioteca soportadas, en el mismo orden que sus
// direcciones (LIB_BASE, LIB_BASE+4, LIB_BASE+8...). Se usa tanto al ensamblar
// (para resolver "BL printf" sin que el usuario defina la etiqueta) como al
// ejecutar (para saber qué función simular). Ver libraryFunctionName().
extern const std::vector<std::string> LIB_FUNC_NAMES;
const char* libraryFunctionName(uint32_t addr); // nullptr si addr no es una función de biblioteca
constexpr int       MAX_STEPS  = 2'000'000;

enum class Cond : int { EQ=0, NE=1, CS=2, CC=3, MI=4, PL=5, VS=6, VC=7, HI=8, LS=9, GE=10, LT=11, GT=12, LE=13, AL=14 };

struct CondInfo { int code; const char* mn; const char* alt; const char* name; const char* flagsText; };
extern const std::array<CondInfo,15> COND_TABLE;
const char* condName(int code);

struct Flags { bool N=false, Z=false, C=false, V=false; };
bool condPass(int code, const Flags& f);

std::string regLabel(int n);   // "R0".."R12", "SP","LR","PC"
int parseReg(const std::string& s); // -1 si no es un registro válido

std::string hex(uint32_t v, int w = 8);
std::string hx0x(uint32_t v, int w = 8); // con prefijo 0x
std::string bin(uint32_t v, int w);

// ------------------------------ Errores ------------------------------------
struct AsmOptions { uint32_t textBase = TEXT_BASE, dataBase = DATA_BASE, stackTop = STACK_TOP; };

struct AsmError { int line = 0; std::string msg; std::string hint; };

// ------------------------------ Campos (para la UI) ------------------------
struct Field { int hi, lo; std::string name; std::string cls; std::string note; std::string bits; };

// ------------------------------ Instrucción decodificada -------------------
enum class IType { DP, MEM, BR, BX, MUL, MULL, BLK, SWI, NOP, INVALID };

struct Operand2 {
    bool isImm = false;
    uint32_t imm = 0;        // valor inmediato ya calculado (para DP) o registro
    int reg = -1;             // Rm si es registro
    int shiftType = 0;        // 0=LSL 1=LSR 2=ASR 3=ROR (4=RRX se codifica como ROR #0)
    bool shiftIsReg = false;
    int shiftAmount = 0;      // si !shiftIsReg
    int shiftReg = -1;        // si shiftIsReg
    bool rrx = false;
};

struct MemOperand {
    int baseReg = -1;
    bool preIndexed = false;   // [Rn, ...]
    bool postIndexed = false;  // [Rn], ...
    bool writeBack = false;    // '!' en pre-indexado
    bool add = true;           // signo del offset
    bool offsetIsReg = false;
    int offsetReg = -1;
    int offShiftType = 0;
    int offShiftAmount = 0;
    int32_t offsetImm = 0;
};

struct Instruction {
    int line = 0;
    uint32_t addr = 0;
    std::string src;              // texto fuente original (recortado)
    uint32_t word = 0;            // código máquina
    IType t = IType::INVALID;
    std::string mnemonic;         // ADD, LDR, BEQ...
    int cond = 14;
    bool s = false;                // sufijo S
    int rd = -1, rn = -1, rm = -1;
    Operand2 op2{};
    MemOperand mem{};
    // memoria
    bool load = false; int size = 4; bool signedLoad = false;
    // salto
    bool link = false;             // BL
    int32_t branchOffsetWords = 0; // relativo, en palabras (ya calculado tras resolver etiqueta)
    std::string targetLabel;
    // multiplicación larga
    int rdLo = -1, rdHi = -1, rs = -1;
    // bloque (PUSH/POP/LDM/STM)
    uint32_t regList = 0;          // máscara de 16 bits
    std::string blkMode;           // IA/IB/DA/DB (tras traducir FD/FA/ED/EA)
    bool isStack = false;
    // swi
    uint32_t swiNum = 0;
    // formato / info para UI
    std::string format;            // "Procesamiento de datos" | "Acceso a memoria" | "Salto"
    std::string note;
    std::vector<Field> fields;
};

// ------------------------------ Listado / programa --------------------------
struct ListingLine {
    uint32_t addr = 0;
    uint32_t word = 0;
    std::string src;
    int line = 0;
    bool isInstr = true;   // false = dato / literal pool
    Instruction ins;       // válido si isInstr
};

struct Program {
    bool ok = false;
    std::vector<AsmError> errors;
    std::vector<ListingLine> listing;
    std::unordered_map<uint32_t, Instruction> byAddr;
    std::map<std::string, uint32_t> symbols;
    std::vector<std::string> labelNames; // orden de declaración
    // memoria inicial (texto + datos + pool de literales), dirección -> byte
    std::unordered_map<uint32_t, uint8_t> initialMem;
    uint32_t textStart = TEXT_BASE, textEnd = TEXT_BASE;
    uint32_t dataStart = DATA_BASE, dataEnd = DATA_BASE;
    uint32_t poolEnd = DATA_BASE;
    int nInstr = 0;
    AsmOptions opt;
};

uint32_t evalExpr(const std::string& s, const std::map<std::string,uint32_t>& syms, uint32_t dot);
Program assemble(const std::string& source, const AsmOptions& opt = AsmOptions());

// Codifica una única línea (para el codificador en vivo de la UI). syms opcional.
struct EncodeResult {
    bool ok = false;
    std::string error, hint;
    uint32_t word = 0;
    std::string format;
    std::string note;
    std::vector<Field> fields;
};
EncodeResult encodeLine(const std::string& text, uint32_t addr, const std::map<std::string,uint32_t>& syms);

// ------------------------------ Registro de un paso (para trazas/undo) ------
struct RegChange { int r; uint32_t oldVal, newVal; };
struct MemWrite { uint32_t addr; uint8_t oldVal, newVal; };

struct StepRecord {
    uint32_t addr = 0;
    Instruction ins{};
    bool executed = false;     // si la condición se cumplió
    std::string text;          // explicación en lenguaje llano
    std::string sym;           // "R1 <- R2 + R3"
    std::string num;           // "0x5 <- 0x2 + 0x3"
    std::vector<std::string> extra;
    bool fault = false;
    std::vector<RegChange> regs;
    std::vector<MemWrite> mem;
    Flags flagsOld, flagsNew;
    uint32_t pcOld = 0, pcNew = 0;
};

// ------------------------------ Máquina -------------------------------------
class Machine {
public:
    explicit Machine(const Program& prog);

    std::array<uint32_t,16> r{};   // r[15] no se usa directamente como PC "vivo"; usamos pc
    uint32_t pc = TEXT_BASE;        // dirección de la próxima instrucción a ejecutar
    Flags f;

    bool halted = false;
    std::string haltReason;
    std::string haltKind;           // "exit" | "error" | "limit" | "end"
    long steps = 0;

    std::string output;             // salida generada por las llamadas SWI de escritura

    // memoria dispersa
    std::unordered_map<uint32_t, uint8_t> mem;
    std::unordered_map<uint32_t,bool> touched; // direcciones escritas por el programa (no la carga inicial)

    std::optional<StepRecord> step();     // ejecuta una instrucción; nullopt si ya está detenida (o esperando entrada)
    bool back();                           // deshace la última instrucción ejecutada; false si no hay historial
    bool canBack() const { return !history.empty(); }
    void run(int maxSteps, const std::vector<uint32_t>* breakpoints = nullptr);
    void reset();
    void clearHistory() { history.clear(); }

    // ------------------------- Entrada de consola (para SWI de lectura) -------------------------
    // El programa puede quedar detenido con haltKind == "input" esperando datos: la interfaz
    // debe llamar a feedInput() con lo que el usuario escriba (se le añade un salto de línea)
    // y la máquina reintentará automáticamente la misma instrucción.
    void feedInput(const std::string& text);
    bool hasPendingChar() const { return inputPos_ < input_.size(); }
    bool hasPendingLine() const { return input_.find('\n', inputPos_) != std::string::npos; }
    std::string peekPendingInput() const { return input_.substr(inputPos_); }

    // Bandera informativa (no de error): true tras ejecutar una llamada a una
    // función de biblioteca simulada en el último step(). Útil para la interfaz.
    bool lastWasLibraryCall = false;

    uint8_t  readByte(uint32_t a) const;
    uint32_t readWord(uint32_t a) const;
    void writeByte(uint32_t a, uint8_t v, StepRecord* rec = nullptr);
    void writeWord(uint32_t a, uint32_t v, StepRecord* rec = nullptr);

    const Program& program() const { return prog_; }
    const StepRecord* lastRecord() const { return history.empty() ? nullptr : &history.back().after; }

private:
    std::string input_; size_t inputPos_ = 0;
    char takeChar();
    std::string takeLine();
    std::string readCString(uint32_t addr, size_t maxLen = 8192) const;
    void runLibraryCall(const std::string& fn, StepRecord& rec);
    struct Snapshot {
        std::array<uint32_t,16> r; uint32_t pc; Flags f;
        std::vector<MemWrite> memUndo; // aplicar en orden inverso para deshacer
        long outLenBefore = 0;
        StepRecord after;
    };
    const Program& prog_;
    std::deque<Snapshot> history;

    void halt(const std::string& reason, const std::string& kind);
    uint32_t regRead(int n) const { return n == 15 ? pc + 8 : r[n]; }
};

} // namespace arm
