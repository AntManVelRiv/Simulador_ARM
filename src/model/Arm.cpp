// core/Arm.cpp — implementación del ensamblador y el emulador ARMv4.
#include "Arm.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <functional>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace arm {

// ============================================================================
// Utilidades básicas
// ============================================================================
static std::string upper(std::string s) { for (auto& c : s) c = (char)std::toupper((unsigned char)c); return s; }
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static uint32_t rotr(uint32_t v, int n) { n &= 31; return n ? ((v >> n) | (v << (32 - n))) : v; }

std::string hex(uint32_t v, int w) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*X", w, (unsigned)v);
    return std::string(buf);
}
std::string hx0x(uint32_t v, int w) { return "0x" + hex(v, w); }
std::string bin(uint32_t v, int w) {
    std::string s(w, '0');
    for (int i = 0; i < w; i++) s[w - 1 - i] = char('0' + ((v >> i) & 1));
    return s;
}

const std::array<CondInfo,15> COND_TABLE = {{
    {0,"EQ","","Igual","Z = 1"},
    {1,"NE","","Distinto","Z = 0"},
    {2,"CS","HS","Carry activo / sin signo mayor o igual","C = 1"},
    {3,"CC","LO","Carry inactivo / sin signo menor","C = 0"},
    {4,"MI","","Negativo","N = 1"},
    {5,"PL","","Positivo o cero","N = 0"},
    {6,"VS","","Desbordamiento","V = 1"},
    {7,"VC","","Sin desbordamiento","V = 0"},
    {8,"HI","","Sin signo mayor","C = 1 y Z = 0"},
    {9,"LS","","Sin signo menor o igual","C = 0 o Z = 1"},
    {10,"GE","","Con signo mayor o igual","N = V"},
    {11,"LT","","Con signo menor","N != V"},
    {12,"GT","","Con signo mayor","Z = 0 y N = V"},
    {13,"LE","","Con signo menor o igual","Z = 1 o N != V"},
    {14,"AL","","Siempre (incondicional)","sin condicion"},
}};
static const char* COND_NAMES[15] = {"EQ","NE","CS","CC","MI","PL","VS","VC","HI","LS","GE","LT","GT","LE","AL"};
const char* condName(int code) { return (code >= 0 && code <= 14) ? COND_NAMES[code] : "?"; }

bool condPass(int c, const Flags& f) {
    switch (c) {
        case 0: return f.Z;         case 1: return !f.Z;
        case 2: return f.C;         case 3: return !f.C;
        case 4: return f.N;         case 5: return !f.N;
        case 6: return f.V;         case 7: return !f.V;
        case 8: return f.C && !f.Z; case 9: return !f.C || f.Z;
        case 10: return f.N == f.V; case 11: return f.N != f.V;
        case 12: return !f.Z && f.N == f.V; case 13: return f.Z || f.N != f.V;
        default: return true;
    }
}

std::string regLabel(int n) {
    if (n == 13) return "SP"; if (n == 14) return "LR"; if (n == 15) return "PC";
    return "R" + std::to_string(n);
}
int parseReg(const std::string& sIn) {
    std::string t = upper(trim(sIn));
    if (t.size() >= 2 && t.size() <= 3 && t[0] == 'R') {
        bool digits = true; for (size_t i=1;i<t.size();i++) if (!std::isdigit((unsigned char)t[i])) digits = false;
        if (digits) { int n = std::atoi(t.c_str()+1); if (n >= 0 && n <= 15) return n; }
    }
    if (t == "SP") return 13; if (t == "LR") return 14; if (t == "PC") return 15;
    if (t == "FP") return 11; if (t == "IP") return 12; if (t == "SL") return 10; if (t == "SB") return 9;
    if (t.size() == 2 && t[0] == 'A' && t[1] >= '1' && t[1] <= '4') return (t[1]-'0') - 1;
    if (t.size() == 2 && t[0] == 'V' && t[1] >= '1' && t[1] <= '8') return (t[1]-'0') + 3;
    return -1;
}

// ============================================================================
// Funciones de biblioteca simuladas (printf, scanf, puts, gets, strlen)
// ============================================================================
const std::vector<std::string> LIB_FUNC_NAMES = {"printf", "scanf", "puts", "gets", "strlen", "putchar", "getchar", "atoi", "exit",
    // anadidas para poder ejecutar programas compilados con gcc (ver docs/C_CON_GCC.md)
    "sprintf", "strcpy", "strncpy", "strcmp", "strncmp", "strcat", "strchr", "memcpy", "memmove", "memset", "memcmp",
    "abs", "toupper", "tolower", "isdigit", "isalpha", "isalnum", "isspace", "isupper", "islower",
    "malloc", "calloc", "free", "rand", "srand",
    "__aeabi_idiv", "__aeabi_uidiv", "__aeabi_idivmod", "__aeabi_uidivmod",
    "fgets", "fputs", "fflush", "fprintf", "snprintf", "time", "__aeabi_ldivmod", "__aeabi_uldivmod"};
// Nombres alternativos que genera el compilador para la misma funcion
static const std::vector<std::pair<std::string,std::string>> LIB_ALIASES = {
    {"__isoc99_scanf","scanf"}, {"__isoc23_scanf","scanf"}, {"__printf_chk","printf"}, {"__aeabi_memcpy","memcpy"}, {"__aeabi_memcpy4","memcpy"}, {"__aeabi_memcpy8","memcpy"},
    {"__aeabi_memmove","memmove"}, {"__aeabi_memset","memset"}, {"__sprintf_chk","sprintf"}, {"__aeabi_idiv","__aeabi_idiv"}};
const char* libraryFunctionName(uint32_t addr) {
    if (addr < LIB_BASE) return nullptr;
    uint32_t idx = (addr - LIB_BASE) / 4;
    if (idx >= LIB_FUNC_NAMES.size()) return nullptr;
    return LIB_FUNC_NAMES[idx].c_str();
}

// ============================================================================
// AsmException — se usa internamente para propagar errores con línea/hint
// ============================================================================
struct AsmException : std::runtime_error {
    std::string hint;
    AsmException(std::string msg, std::string h = "") : std::runtime_error(std::move(msg)), hint(std::move(h)) {}
};

// divide por comas de primer nivel respetando [] {} () y comillas
static std::vector<std::string> splitTop(const std::string& s) {
    std::vector<std::string> out; int depth = 0; std::string cur; char q = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (q) { cur += ch; if (ch == '\\' && i+1<s.size()) cur += s[++i]; else if (ch == q) q = 0; continue; }
        if (ch == '"' || ch == '\'') { q = ch; cur += ch; continue; }
        if (ch=='['||ch=='{'||ch=='(') depth++;
        if (ch==']'||ch=='}'||ch==')') depth--;
        if (ch == ',' && depth == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
        cur += ch;
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
    return out;
}

// ============================================================================
// Evaluador de expresiones
// ============================================================================
namespace {
struct ExprEval {
    const std::string& str;
    const std::map<std::string,uint32_t>* syms;
    uint32_t dot;
    std::vector<std::string> toks;
    size_t i = 0;

    ExprEval(const std::string& s, const std::map<std::string,uint32_t>* sy, uint32_t d) : str(s), syms(sy), dot(d) { tokenize(); }

    void tokenize() {
        size_t pos = 0; std::string s = str;
        auto isIdentStart = [](char c){ return std::isalpha((unsigned char)c) || c=='_' || c=='.' || c=='$'; };
        auto isIdentCont  = [](char c){ return std::isalnum((unsigned char)c) || c=='_' || c=='.' || c=='$'; };
        while (pos < s.size()) {
            char c = s[pos];
            if (std::isspace((unsigned char)c)) { pos++; continue; }
            if (c=='0' && pos+1<s.size() && (s[pos+1]=='x'||s[pos+1]=='X')) {
                size_t st=pos; pos+=2; while (pos<s.size() && std::isxdigit((unsigned char)s[pos])) pos++;
                toks.push_back(s.substr(st,pos-st)); continue;
            }
            if (c=='0' && pos+1<s.size() && (s[pos+1]=='b'||s[pos+1]=='B')) {
                size_t st=pos; pos+=2; while (pos<s.size() && (s[pos]=='0'||s[pos]=='1')) pos++;
                toks.push_back(s.substr(st,pos-st)); continue;
            }
            if (std::isdigit((unsigned char)c)) {
                size_t st=pos; while (pos<s.size() && std::isdigit((unsigned char)s[pos])) pos++;
                toks.push_back(s.substr(st,pos-st)); continue;
            }
            if (c=='\'') {
                size_t st=pos; pos++; if (pos<s.size() && s[pos]=='\\') pos++; if (pos<s.size()) pos++;
                if (pos<s.size() && s[pos]=='\'') pos++;
                toks.push_back(s.substr(st,pos-st)); continue;
            }
            if (isIdentStart(c)) {
                size_t st=pos; pos++; while (pos<s.size() && isIdentCont(s[pos])) pos++;
                toks.push_back(s.substr(st,pos-st)); continue;
            }
            if (c=='<' && pos+1<s.size() && s[pos+1]=='<') { toks.push_back("<<"); pos+=2; continue; }
            if (c=='>' && pos+1<s.size() && s[pos+1]=='>') { toks.push_back(">>"); pos+=2; continue; }
            if (std::strchr("-+*/%&|^~()", c)) { toks.push_back(std::string(1,c)); pos++; continue; }
            throw AsmException("Expresion no valida: " + str);
        }
    }
    const std::string* peek() const { return i < toks.size() ? &toks[i] : nullptr; }
    std::string eat() { if (i>=toks.size()) throw AsmException("Expresion incompleta: " + str); return toks[i++]; }

    long long primary() {
        std::string t = eat();
        if (t == "(") { long long v = orExpr(); if (eat()!=")") throw AsmException("Falta un parentesis de cierre"); return v; }
        if (t.size()>=2 && t[0]=='0' && (t[1]=='x'||t[1]=='X')) return (long long)std::strtoull(t.c_str()+2,nullptr,16);
        if (t.size()>=2 && t[0]=='0' && (t[1]=='b'||t[1]=='B')) return (long long)std::strtoull(t.c_str()+2,nullptr,2);
        if (std::isdigit((unsigned char)t[0])) return std::atoll(t.c_str());
        if (t[0]=='\'') {
            if (t.size()>=3 && t[1]=='\\') {
                char c2 = t[2]; char v;
                switch (c2) { case 'n': v=10; break; case 't': v=9; break; case 'r': v=13; break; case '0': v=0; break; case '\\': v=92; break; case '\'': v=39; break; default: v=c2; }
                return (unsigned char)v;
            }
            return (unsigned char)t[1];
        }
        if (t == ".") return (long long)dot;
        if (std::isalpha((unsigned char)t[0]) || t[0]=='_' || t[0]=='.' || t[0]=='$') {
            if (syms) {
                auto it = syms->find(t); if (it != syms->end()) return (long long)(uint32_t)it->second;
                std::string low = upper(t);
                for (auto& kv : *syms) if (upper(kv.first) == low) return (long long)(uint32_t)kv.second;
            }
            throw AsmException("Simbolo desconocido: " + t, "Comprueba que la etiqueta existe y esta bien escrita.");
        }
        throw AsmException("No entiendo '" + t + "' en la expresion " + str);
    }
    long long unary() {
        const std::string* t = peek();
        if (t && *t=="-") { eat(); return -unary(); }
        if (t && *t=="+") { eat(); return unary(); }
        if (t && *t=="~") { eat(); return -unary()-1; }
        return primary();
    }
    long long mulExpr() { long long v = unary(); for(;;){ const std::string* t=peek(); if(!t||(*t!="*"&&*t!="/"&&*t!="%")) break; std::string o=eat(); long long r=unary(); v = o=="*"?v*r : o=="/"?(r? v/r : 0) : (r? v%r : 0);} return v; }
    long long addExpr() { long long v = mulExpr(); for(;;){ const std::string* t=peek(); if(!t||(*t!="+"&&*t!="-")) break; std::string o=eat(); long long r=mulExpr(); v = o=="+"?v+r:v-r;} return v; }
    long long shfExpr() { long long v = addExpr(); for(;;){ const std::string* t=peek(); if(!t||(*t!="<<"&&*t!=">>")) break; std::string o=eat(); long long r=addExpr(); v = o=="<<"? (long long)((uint64_t)v << r) : (long long)(v >> r);} return v; }
    long long andExpr() { long long v = shfExpr(); while(peek() && *peek()=="&"){ eat(); v = (uint32_t)v & (uint32_t)shfExpr(); } return v; }
    long long xorExpr() { long long v = andExpr(); while(peek() && *peek()=="^"){ eat(); v = (uint32_t)v ^ (uint32_t)andExpr(); } return v; }
    long long orExpr()  { long long v = xorExpr(); while(peek() && *peek()=="|"){ eat(); v = (uint32_t)v | (uint32_t)xorExpr(); } return v; }

    uint32_t eval() { long long v = orExpr(); if (i < toks.size()) throw AsmException("Sobra algo en la expresion " + str); return (uint32_t)v; }
};
} // namespace

uint32_t evalExpr(const std::string& s, const std::map<std::string,uint32_t>& syms, uint32_t dot) {
    ExprEval e(s, &syms, dot); return e.eval();
}

// ============================================================================
// Inmediatos rotados (imm8 + rot de 4 bits) -- formato de datos inmediato ARM
// ============================================================================
struct ImmEnc { bool ok; int rot; uint32_t imm8; };
static ImmEnc encodeImm(uint32_t v) {
    for (int rot = 0; rot < 16; rot++) {
        uint32_t r = rotr(v, 32 - 2*rot);
        if (r <= 0xFF) { uint32_t back = rotr(r, 2*rot); if (back == v) return {true, rot, r}; }
    }
    return {false, 0, 0};
}

// ============================================================================
// Analisis de mnemonicos
// ============================================================================
static const std::vector<std::pair<std::string,int>> DP_OPS = {
    {"AND",0},{"EOR",1},{"SUB",2},{"RSB",3},{"ADD",4},{"ADC",5},{"SBC",6},{"RSC",7},
    {"TST",8},{"TEQ",9},{"CMP",10},{"CMN",11},{"ORR",12},{"MOV",13},{"BIC",14},{"MVN",15}
};
static bool isTestOp(const std::string& b){ return b=="TST"||b=="TEQ"||b=="CMP"||b=="CMN"; }
static bool isMonoOp(const std::string& b){ return b=="MOV"||b=="MVN"; }
static int dpOpcode(const std::string& b){ for (auto&p:DP_OPS) if (p.first==b) return p.second; return -1; }

static int condCodeFromStr(const std::string& s){
    static const std::pair<const char*,int> M[] = {{"EQ",0},{"NE",1},{"CS",2},{"HS",2},{"CC",3},{"LO",3},{"MI",4},{"PL",5},{"VS",6},{"VC",7},{"HI",8},{"LS",9},{"GE",10},{"LT",11},{"GT",12},{"LE",13},{"AL",14}};
    for (auto& m : M) if (s==m.first) return m.second;
    return -1;
}
struct Suffix { bool ok=false; std::string cond, flag; };
static Suffix trySuffix(const std::string& rest, const std::vector<std::string>& flagsAllowed) {
    auto matchCond = [&](const std::string& s, size_t& pos)->std::string {
        if (pos+2<=s.size()) { std::string c = s.substr(pos,2); if (condCodeFromStr(c)>=0) { pos+=2; return c; } }
        return "";
    };
    auto matchFlag = [&](const std::string& s, size_t& pos)->std::string {
        for (auto& fl : flagsAllowed) if (s.compare(pos, fl.size(), fl)==0) { pos += fl.size(); return fl; }
        return "";
    };
    { size_t pos=0; std::string c=matchCond(rest,pos); std::string fl=matchFlag(rest,pos); if (pos==rest.size()) return {true,c,fl}; }
    { size_t pos=0; std::string fl=matchFlag(rest,pos); std::string c=matchCond(rest,pos); if (pos==rest.size()) return {true,c,fl}; }
    if (rest.empty()) return {true,"",""};
    return {false,"",""};
}

struct ParsedMnemonic {
    bool ok=false;
    std::string kind;
    std::string base;
    int cond=14; std::string condText;
    std::string flag;
    bool s=false;
    bool load=false; int size=4; bool signedLoad=false;
    std::string mode; bool stack=false;
    bool link=false;
};
static ParsedMnemonic parseMnemonic(const std::string& tokIn) {
    std::string t = upper(tokIn);
    ParsedMnemonic pm;
    auto setCond=[&](const std::string& c){ pm.condText=c; pm.cond = c.empty()?14:condCodeFromStr(c); };

    for (auto& op : DP_OPS) {
        const std::string& b = op.first;
        if (t.compare(0,b.size(),b)==0) {
            std::string rest = t.substr(b.size());
            Suffix sf = trySuffix(rest, {"S"});
            if (sf.ok) { pm.ok=true; pm.kind="dp"; pm.base=b; setCond(sf.cond); pm.flag=sf.flag; pm.s = (sf.flag=="S") || isTestOp(b); return pm; }
        }
    }
    for (std::string b : {std::string("LSL"),std::string("LSR"),std::string("ASR"),std::string("ROR")}) {
        if (t.compare(0,3,b)==0) {
            Suffix sf = trySuffix(t.substr(3), {"S"});
            if (sf.ok) { pm.ok=true; pm.kind="shift"; pm.base=b; setCond(sf.cond); pm.s=(sf.flag=="S"); return pm; }
        }
    }
    if (t.compare(0,3,"RRX")==0) {
        Suffix sf = trySuffix(t.substr(3), {"S"});
        if (sf.ok) { pm.ok=true; pm.kind="shift"; pm.base="RRX"; setCond(sf.cond); pm.s=(sf.flag=="S"); return pm; }
    }
    for (std::string b : {std::string("MUL"),std::string("MLA")}) {
        if (t.compare(0,3,b)==0) {
            Suffix sf = trySuffix(t.substr(3), {"S"});
            if (sf.ok) { pm.ok=true; pm.kind="mul"; pm.base=b; setCond(sf.cond); pm.s=(sf.flag=="S"); return pm; }
        }
    }
    for (std::string b : {std::string("SMULL"),std::string("UMULL"),std::string("SMLAL"),std::string("UMLAL")}) {
        if (t.compare(0,5,b)==0) {
            Suffix sf = trySuffix(t.substr(5), {"S"});
            if (sf.ok) { pm.ok=true; pm.kind="mull"; pm.base=b; setCond(sf.cond); pm.s=(sf.flag=="S"); return pm; }
        }
    }
    for (std::string b : {std::string("LDR"),std::string("STR")}) {
        if (t.compare(0,3,b)==0) {
            std::vector<std::string> flags = (b=="LDR") ? std::vector<std::string>{"SB","SH","B","H"} : std::vector<std::string>{"B","H"};
            Suffix sf = trySuffix(t.substr(3), flags);
            if (sf.ok) {
                pm.ok=true; pm.kind="mem"; pm.base=b; setCond(sf.cond); pm.load=(b=="LDR");
                if (sf.flag=="B"||sf.flag=="SB") pm.size=1; else if (sf.flag=="H"||sf.flag=="SH") pm.size=2; else pm.size=4;
                pm.signedLoad = (!sf.flag.empty() && sf.flag[0]=='S');
                return pm;
            }
        }
    }
    static const std::vector<std::string> LDM_MODES = {"IA","IB","DA","DB","FD","FA","ED","EA"};
    for (std::string b : {std::string("LDM"),std::string("STM")}) {
        if (t.compare(0,3,b)==0) {
            Suffix sf = trySuffix(t.substr(3), LDM_MODES);
            if (sf.ok) { pm.ok=true; pm.kind="blk"; pm.base=b; setCond(sf.cond); pm.load=(b=="LDM"); pm.mode = sf.flag.empty()?"IA":sf.flag; return pm; }
        }
    }
    for (std::string b : {std::string("PUSH"),std::string("POP")}) {
        if (t.compare(0,b.size(),b)==0) {
            Suffix sf = trySuffix(t.substr(b.size()), {});
            if (sf.ok) { pm.ok=true; pm.kind="blk"; pm.base=b; setCond(sf.cond); pm.load=(b=="POP"); pm.mode = (b=="POP")?"IA":"DB"; pm.stack=true; return pm; }
        }
    }
    for (std::string b : {std::string("BX"),std::string("BL"),std::string("B")}) {
        if (t.compare(0,b.size(),b)==0) {
            Suffix sf = trySuffix(t.substr(b.size()), {});
            if (sf.ok) { pm.ok=true; pm.kind=(b=="BX")?"bx":"br"; pm.base=b; setCond(sf.cond); pm.link=(b=="BL"); return pm; }
        }
    }
    for (std::string b : {std::string("SWI"),std::string("SVC")}) {
        if (t.compare(0,3,b)==0) {
            Suffix sf = trySuffix(t.substr(3), {});
            if (sf.ok) { pm.ok=true; pm.kind="swi"; pm.base=b; setCond(sf.cond); return pm; }
        }
    }
    if (t.compare(0,3,"NOP")==0) {
        Suffix sf = trySuffix(t.substr(3), {});
        if (sf.ok) { pm.ok=true; pm.kind="nop"; pm.base="NOP"; setCond(sf.cond); return pm; }
    }
    return pm;
}

// ============================================================================
// Reg-list {R0,R1,R4-R6,LR}
// ============================================================================
static uint32_t parseRegList(const std::string& sIn) {
    std::string s = trim(sIn);
    if (s.empty() || s.front()!='{' || s.back()!='}') throw AsmException("Se esperaba una lista de registros entre llaves, p.ej. {R4, R5, LR}");
    s = s.substr(1, s.size()-2);
    uint32_t mask = 0;
    for (auto& part0 : splitTop(s)) {
        std::string part = trim(part0);
        if (part.empty()) continue;
        auto dash = part.find('-');
        if (dash != std::string::npos) {
            int a = parseReg(trim(part.substr(0,dash)));
            int b = parseReg(trim(part.substr(dash+1)));
            if (a<0||b<0) throw AsmException("Rango de registros no valido: " + part);
            if (a>b) std::swap(a,b);
            for (int i=a;i<=b;i++) mask |= (1u<<i);
        } else {
            int r = parseReg(part);
            if (r<0) throw AsmException("Se esperaba un registro y aparece " + part);
            mask |= (1u<<r);
        }
    }
    return mask;
}

// ============================================================================
// Parseo de operando2 y de operando de memoria
// ============================================================================
static void parseShiftSuffix(const std::string& sfxIn, Operand2& op) {
    std::string sfx = trim(sfxIn);
    if (sfx.empty()) return;
    std::istringstream iss(sfx);
    std::string kind; iss >> kind; kind = upper(kind);
    std::string rest; std::getline(iss, rest); rest = trim(rest);
    if (kind == "RRX") { op.rrx = true; op.shiftType = 3; op.shiftIsReg=false; op.shiftAmount=0; return; }
    int st = (kind=="LSL")?0:(kind=="LSR")?1:(kind=="ASR")?2:(kind=="ROR")?3:-1;
    if (st < 0) throw AsmException("Desplazamiento no reconocido: " + kind);
    op.shiftType = st;
    if (!rest.empty() && rest[0]=='#') {
        op.shiftIsReg=false; op.shiftAmount = (int)evalExpr(rest.substr(1), {}, 0);
    } else {
        int r = parseReg(rest);
        if (r<0) throw AsmException("Se esperaba un registro o #cantidad tras " + kind);
        op.shiftIsReg=true; op.shiftReg = r;
    }
}

static Operand2 parseOperand2(const std::vector<std::string>& toks, size_t start) {
    Operand2 op;
    if (start >= toks.size()) throw AsmException("Falta el segundo operando");
    std::string first = toks[start];
    if (!first.empty() && first[0]=='#') {
        op.isImm = true; op.imm = evalExpr(first.substr(1), {}, 0);
        return op;
    }
    int r = parseReg(first);
    if (r < 0) throw AsmException("Se esperaba un registro o un inmediato (#) y aparece " + first);
    op.isImm=false; op.reg = r;
    if (start+1 < toks.size()) parseShiftSuffix(toks[start+1], op);
    return op;
}

static MemOperand parseMemOperand(const std::vector<std::string>& toks, size_t start) {
    MemOperand m;
    if (start >= toks.size()) throw AsmException("Falta el operando de memoria entre corchetes");
    std::string joined; for (size_t i=start;i<toks.size();i++){ if(i>start) joined += ", "; joined += toks[i]; }
    joined = trim(joined);
    if (joined.empty() || joined[0] != '[') throw AsmException("Se esperaba [ para el operando de memoria");
    size_t close = joined.find(']');
    if (close == std::string::npos) throw AsmException("Falta ] en el operando de memoria");
    std::string inside = joined.substr(1, close-1);
    std::string after = trim(joined.substr(close+1));
    bool bang = false;
    if (!after.empty() && after[0]=='!') { bang=true; after = trim(after.substr(1)); }
    if (!after.empty() && after[0]==',') after = trim(after.substr(1));

    auto insideToks = splitTop(inside);
    if (insideToks.empty()) throw AsmException("Operando de memoria vacio");
    int rn = parseReg(insideToks[0]);
    if (rn < 0) throw AsmException("Se esperaba un registro base entre corchetes");
    m.baseReg = rn;

    if (!after.empty()) {
        m.postIndexed = true; m.preIndexed = false; m.writeBack = true;
        if (after[0]=='#') { m.offsetIsReg=false; long long v = (long long)evalExpr(after.substr(1), {}, 0); m.add = v>=0; m.offsetImm = (int32_t)std::llabs(v); }
        else if (after[0]=='-' && after.size()>1 && after[1]=='#') { m.offsetIsReg=false; m.add=false; m.offsetImm=(int32_t)evalExpr(after.substr(2), {}, 0); }
        else {
            std::string sign = after; bool neg=false;
            if (sign[0]=='-') { neg=true; sign = trim(sign.substr(1)); }
            auto pieces = splitTop(sign);
            int rm = parseReg(pieces[0]);
            if (rm<0) throw AsmException("Desplazamiento de memoria no valido: " + after);
            m.offsetIsReg=true; m.offsetReg=rm; m.add=!neg;
            if (pieces.size()>1) { Operand2 tmp; parseShiftSuffix(pieces[1], tmp); m.offShiftType=tmp.shiftType; m.offShiftAmount=tmp.shiftAmount; }
        }
        return m;
    }
    m.preIndexed = true; m.postIndexed = false; m.writeBack = bang;
    if (insideToks.size() == 1) { m.offsetIsReg=false; m.offsetImm=0; m.add=true; return m; }
    std::string off = insideToks[1];
    bool neg=false;
    if (!off.empty() && off[0]=='-') { neg=true; off = trim(off.substr(1)); }
    if (!off.empty() && off[0]=='#') { m.offsetIsReg=false; m.offsetImm=(int32_t)evalExpr(off.substr(1), {}, 0); m.add=!neg; }
    else {
        int rm = parseReg(off);
        if (rm<0) throw AsmException("Desplazamiento de memoria no valido: " + off);
        m.offsetIsReg=true; m.offsetReg=rm; m.add=!neg;
        if (insideToks.size()>2) { Operand2 tmp; parseShiftSuffix(insideToks[2], tmp); m.offShiftType=tmp.shiftType; m.offShiftAmount=tmp.shiftAmount; }
    }
    return m;
}


// LDM/STM: "Rn[!], {lista}"  ->  base, writeback y lista (PUSH/POP: SP! implicito)
static void parseBlockOperands(Instruction& ins, const std::vector<std::string>& toks, bool stackForm) {
    ins.blkBase = 13; ins.blkWb = true;
    size_t first = 0;
    if (!stackForm) {
        if (toks.size() < 2) throw AsmException(ins.mnemonic + " espera: Rn[!], {lista de registros}");
        std::string b = trim(toks[0]); bool wb = false;
        if (!b.empty() && b.back()=='!') { wb = true; b = trim(b.substr(0, b.size()-1)); }
        int rn = parseReg(b);
        if (rn < 0 || rn == 15) throw AsmException("Se esperaba un registro base (no PC) y aparece " + toks[0]);
        ins.blkBase = rn; ins.blkWb = wb; first = 1;
    }
    std::string joined; for (size_t i=first;i<toks.size();i++){ if(i>first) joined+=", "; joined+=toks[i]; }
    ins.regList = joined.empty()? 0 : parseRegList(joined);
    if (ins.regList==0) throw AsmException("La lista de registros no puede estar vacia");
}

// ============================================================================
// Construccion de campos (para mostrar en la UI)
// ============================================================================
static Field mkField(int hi, int lo, std::string name, std::string cls, uint32_t word, std::string note="") {
    int w = hi-lo+1; uint32_t mask = (w>=32)?0xFFFFFFFFu:((1u<<w)-1u);
    return Field{hi, lo, std::move(name), std::move(cls), std::move(note), bin((word>>lo)&mask, w)};
}

static std::string condNoteStr(int c) { return std::string("Condicion ") + condName(c) + ": " + (c>=0&&c<=14? std::string(COND_TABLE[c].name) : std::string("")); }

static void encodeDP(Instruction& ins) {
    uint32_t op = (uint32_t)dpOpcode(ins.mnemonic);
    uint32_t I = ins.op2.isImm ? 1 : 0;
    uint32_t S = ins.s ? 1 : 0;
    uint32_t rn = (isMonoOp(ins.mnemonic)) ? 0 : (uint32_t)ins.rn;
    uint32_t rd = isTestOp(ins.mnemonic) ? 0 : (uint32_t)ins.rd;
    uint32_t op2bits = 0;
    if (ins.op2.isImm) {
        ImmEnc e = encodeImm(ins.op2.imm);
        if (!e.ok) throw AsmException("El inmediato " + hx0x(ins.op2.imm) + " no se puede codificar (no es un byte rotado un numero par de posiciones)",
            "Prueba con un valor que se pueda formar rotando un byte (0-255) un numero par de bits.");
        op2bits = ((uint32_t)e.rot << 8) | e.imm8;
    } else {
        uint32_t shiftField;
        if (ins.op2.rrx) shiftField = (3u<<5) | (0u<<4);
        else if (ins.op2.shiftIsReg) shiftField = ((uint32_t)ins.op2.shiftType<<5) | (1u<<4) | ((uint32_t)ins.op2.shiftReg<<8);
        else shiftField = ((uint32_t)(ins.op2.shiftAmount & 31)<<7) | ((uint32_t)ins.op2.shiftType<<5);
        op2bits = shiftField | (uint32_t)ins.op2.reg;
    }
    ins.word = ((uint32_t)ins.cond<<28) | (0u<<26) | (I<<25) | (op<<21) | (S<<20) | (rn<<16) | (rd<<12) | op2bits;
    ins.format = "Procesamiento de datos";
    auto& F = ins.fields;
    F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
    F.push_back(mkField(27,26,"tipo","fix", ins.word, "00: instruccion de procesamiento de datos"));
    F.push_back(mkField(25,25,"I","flag", ins.word, I? "operando2 es un inmediato":"operando2 es un registro"));
    F.push_back(mkField(24,21,"cmd","cmd", ins.word, "Codigo de operacion: " + ins.mnemonic));
    F.push_back(mkField(20,20,"S","flag", ins.word, S? "actualiza los flags NZCV":"no actualiza los flags"));
    F.push_back(mkField(19,16,"Rn","rn", ins.word, isMonoOp(ins.mnemonic)? "no se usa en "+ins.mnemonic : "primer operando: "+regLabel(ins.rn)));
    F.push_back(mkField(15,12,"Rd","rd", ins.word, isTestOp(ins.mnemonic)? "no se usa en "+ins.mnemonic : "registro destino: "+regLabel(ins.rd)));
    F.push_back(mkField(11,0,"operando2","src", ins.word, ins.op2.isImm? "inmediato rotado" : "registro "+regLabel(ins.op2.reg)+" con desplazamiento"));
}

static void encodeMem(Instruction& ins) {
    uint32_t I = ins.mem.offsetIsReg ? 1 : 0;
    uint32_t P = ins.mem.preIndexed ? 1 : 0;
    uint32_t U = ins.mem.add ? 1 : 0;
    uint32_t B = (ins.size==1) ? 1 : 0;
    uint32_t W = ins.mem.writeBack ? 1 : 0;
    uint32_t L = ins.load ? 1 : 0;
    bool halfOrSigned = (ins.size==2) || ins.signedLoad;
    uint32_t rn = (uint32_t)ins.mem.baseReg, rd = (uint32_t)ins.rd;
    if (!halfOrSigned) {
        uint32_t off12;
        if (I) off12 = ((uint32_t)ins.mem.offShiftAmount<<7) | ((uint32_t)ins.mem.offShiftType<<5) | (uint32_t)ins.mem.offsetReg;
        else off12 = (uint32_t)ins.mem.offsetImm & 0xFFF;
        ins.word = ((uint32_t)ins.cond<<28) | (1u<<26) | (I<<25) | (P<<24) | (U<<23) | (B<<22) | (W<<21) | (L<<20) | (rn<<16) | (rd<<12) | off12;
        ins.format = "Acceso a memoria";
        auto& F = ins.fields;
        F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
        F.push_back(mkField(27,26,"tipo","fix", ins.word, "01: acceso a memoria (LDR/STR)"));
        F.push_back(mkField(25,25,"I","flag", ins.word, I? "el desplazamiento es un registro":"el desplazamiento es un inmediato"));
        F.push_back(mkField(24,24,"P","flag", ins.word, P? "pre-indexado":"post-indexado"));
        F.push_back(mkField(23,23,"U","flag", ins.word, U? "suma el desplazamiento":"resta el desplazamiento"));
        F.push_back(mkField(22,22,"B","flag", ins.word, B? "acceso a un byte":"acceso a una palabra"));
        F.push_back(mkField(21,21,"W","flag", ins.word, W? "actualiza la base":"no actualiza la base"));
        F.push_back(mkField(20,20,"L","flag", ins.word, L? "load (LDR)":"store (STR)"));
        F.push_back(mkField(19,16,"Rn","rn", ins.word, "registro base: "+regLabel(ins.mem.baseReg)));
        F.push_back(mkField(15,12,"Rd","rd", ins.word, (L?"destino: ":"origen: ")+regLabel(ins.rd)));
        F.push_back(mkField(11,0,"offset","src", ins.word, I? "registro "+regLabel(ins.mem.offsetReg):"inmediato de 12 bits"));
    } else {
        uint32_t immHi=0, immLo=0, rm=0;
        if (!I) { uint32_t v = (uint32_t)ins.mem.offsetImm & 0xFF; immHi=(v>>4)&0xF; immLo=v&0xF; }
        else rm = (uint32_t)ins.mem.offsetReg;
        uint32_t SH;
        if (ins.size==1 && ins.signedLoad) SH = 2; else if (ins.size==2 && ins.signedLoad) SH = 3; else SH = 1;
        uint32_t bits = (immHi<<8) | (1u<<7) | (SH<<5) | (1u<<4) | (I? rm : immLo);
        ins.word = ((uint32_t)ins.cond<<28) | (0u<<26) | (P<<24) | (U<<23) | ((I?1u:0u)<<22) | (W<<21) | (L<<20) | (rn<<16) | (rd<<12) | bits;
        ins.format = "Acceso a memoria";
        auto& F = ins.fields;
        F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
        F.push_back(mkField(24,24,"P","flag", ins.word, P?"pre-indexado":"post-indexado"));
        F.push_back(mkField(23,23,"U","flag", ins.word, U?"suma":"resta"));
        F.push_back(mkField(21,21,"W","flag", ins.word, W?"actualiza la base":"no actualiza la base"));
        F.push_back(mkField(20,20,"L","flag", ins.word, L?"load":"store"));
        F.push_back(mkField(19,16,"Rn","rn", ins.word, "base: "+regLabel(ins.mem.baseReg)));
        F.push_back(mkField(15,12,"Rd","rd", ins.word, regLabel(ins.rd)));
        F.push_back(mkField(7,4,"SH","cmd", ins.word, "medio-palabra / con signo"));
        F.push_back(mkField(3,0,"offset/Rm","src", ins.word, "parte baja del desplazamiento o Rm"));
    }
}

static void encodeBranch(Instruction& ins) {
    uint32_t L = ins.link?1:0;
    uint32_t off24 = (uint32_t)ins.branchOffsetWords & 0xFFFFFF;
    ins.word = ((uint32_t)ins.cond<<28) | (5u<<25) | (L<<24) | off24;
    ins.format = "Salto";
    auto& F = ins.fields;
    F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
    F.push_back(mkField(27,25,"tipo","fix", ins.word, "101: instruccion de salto"));
    F.push_back(mkField(24,24,"L","flag", ins.word, L? "enlaza: guarda la direccion de retorno en LR":"no enlaza"));
    F.push_back(mkField(23,0,"offset","src", ins.word, "desplazamiento con signo, en palabras, relativo a PC+8"));
}
static void encodeBX(Instruction& ins) {
    ins.word = ((uint32_t)ins.cond<<28) | 0x012FFF10u | (uint32_t)ins.rm;
    ins.format = "Salto";
    auto& F = ins.fields;
    F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
    F.push_back(mkField(27,4,"fijo","fix", ins.word, "patron fijo de BX"));
    F.push_back(mkField(3,0,"Rm","rn", ins.word, "salta a la direccion contenida en "+regLabel(ins.rm)));
}
static void encodeMul(Instruction& ins) {
    uint32_t S = ins.s?1:0;
    bool mla = ins.mnemonic=="MLA";
    uint32_t rn = mla? (uint32_t)ins.rn : 0;
    ins.word = ((uint32_t)ins.cond<<28) | ((mla?1u:0u)<<21) | (S<<20) | ((uint32_t)ins.rd<<16) | (rn<<12) | ((uint32_t)ins.rs<<8) | (9u<<4) | (uint32_t)ins.rm;
    ins.format = "Procesamiento de datos";
    auto& F = ins.fields;
    F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
    F.push_back(mkField(21,21,"A","flag", ins.word, mla? "acumula (MLA)":"no acumula (MUL)"));
    F.push_back(mkField(20,20,"S","flag", ins.word, S?"actualiza flags":"no actualiza flags"));
    F.push_back(mkField(19,16,"Rd","rd", ins.word, regLabel(ins.rd)));
    F.push_back(mkField(11,8,"Rs","src", ins.word, regLabel(ins.rs)));
    F.push_back(mkField(3,0,"Rm","rn", ins.word, regLabel(ins.rm)));
}
static void encodeMull(Instruction& ins) {
    uint32_t S = ins.s?1:0;
    bool uns = ins.mnemonic[0]=='U';
    bool acc = ins.mnemonic.find("LAL")!=std::string::npos;
    ins.word = ((uint32_t)ins.cond<<28) | (1u<<23) | ((uns?0u:1u)<<22) | ((acc?1u:0u)<<21) | (S<<20) | ((uint32_t)ins.rdHi<<16) | ((uint32_t)ins.rdLo<<12) | ((uint32_t)ins.rs<<8) | (9u<<4) | (uint32_t)ins.rm;
    ins.format = "Procesamiento de datos";
    auto& F = ins.fields;
    F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
    F.push_back(mkField(22,22,"U","flag", ins.word, uns? "sin signo":"con signo"));
    F.push_back(mkField(19,16,"RdHi","rd", ins.word, regLabel(ins.rdHi)));
    F.push_back(mkField(15,12,"RdLo","rd", ins.word, regLabel(ins.rdLo)));
    F.push_back(mkField(11,8,"Rs","src", ins.word, regLabel(ins.rs)));
    F.push_back(mkField(3,0,"Rm","rn", ins.word, regLabel(ins.rm)));
}
static void translateBlockMode(const std::string& mode, bool load, bool& P, bool& U) {
    std::string m = mode;
    if (m=="FD") m = load? "IA" : "DB";
    else if (m=="FA") m = load? "DA" : "IB";
    else if (m=="ED") m = load? "IB" : "DA";
    else if (m=="EA") m = load? "DB" : "IA";
    P = (m=="IB"||m=="DB"); U = (m=="IA"||m=="IB");
}
static void encodeBlock(Instruction& ins) {
    bool P,U; translateBlockMode(ins.blkMode, ins.load, P, U);
    uint32_t L = ins.load?1:0;
    ins.word = ((uint32_t)ins.cond<<28) | (1u<<27) | ((P?1u:0u)<<24) | ((U?1u:0u)<<23) | (0u<<22) | ((ins.blkWb?1u:0u)<<21) | (L<<20) | ((uint32_t)ins.blkBase<<16) | ins.regList;
    ins.format = "Acceso a memoria";
    auto& F = ins.fields;
    F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
    F.push_back(mkField(24,24,"P","flag", ins.word, P?"antes":"despues"));
    F.push_back(mkField(23,23,"U","flag", ins.word, U?"incrementa":"decrementa"));
    F.push_back(mkField(20,20,"L","flag", ins.word, L?"LDM":"STM"));
    F.push_back(mkField(19,16,"Rn","rn", ins.word, "base: "+regLabel(ins.blkBase)));
    F.push_back(mkField(15,0,"lista de registros","src", ins.word, "un bit por registro (bit n = Rn)"));
}
static void encodeSwi(Instruction& ins) {
    ins.word = ((uint32_t)ins.cond<<28) | (0xFu<<24) | (ins.swiNum & 0xFFFFFF);
    ins.format = "Salto";
    auto& F = ins.fields;
    F.push_back(mkField(31,28,"cond","cond", ins.word, condNoteStr(ins.cond)));
    F.push_back(mkField(27,24,"tipo","fix", ins.word, "1111: llamada al sistema (SWI/SVC)"));
    F.push_back(mkField(23,0,"num","src", ins.word, "numero de llamada al sistema"));
}

// ============================================================================
// Ensamblador de dos pasadas
// ============================================================================
struct RawLine { int lineNo; std::string text; };

static std::string stripComment(const std::string& lineIn) {
    char q = 0;
    for (size_t i = 0; i < lineIn.size(); i++) {
        char c = lineIn[i];
        if (q) { if (c=='\\') i++; else if (c==q) q=0; continue; }
        if (c=='"') { q=c; continue; }
        if (c=='\'' && i+2<lineIn.size() && lineIn[i+2]=='\'') { i+=2; continue; }
        if (c=='@' || c==';') return lineIn.substr(0,i);
        if (c=='/' && i+1<lineIn.size() && lineIn[i+1]=='/') return lineIn.substr(0,i);
    }
    return lineIn;
}
static std::string parseStringLit(const std::string& sIn) {
    std::string s = trim(sIn);
    if (s.size()<2 || s.front()!='"' || s.back()!='"') throw AsmException("Se esperaba una cadena entre comillas dobles");
    std::string inner = s.substr(1, s.size()-2), out;
    for (size_t i=0;i<inner.size();i++) {
        if (inner[i]=='\\' && i+1<inner.size()) {
            char c = inner[++i];
            if (c >= '0' && c <= '7') {          // escape octal (gcc escribe "\012", "\000"...): hasta 3 cifras
                int v = c - '0'; int k = 1;
                while (k < 3 && i+1 < inner.size() && inner[i+1] >= '0' && inner[i+1] <= '7') { v = v*8 + (inner[++i] - '0'); k++; }
                out += (char)v; continue;
            }
            if (c == 'x' && i+1 < inner.size() && std::isxdigit((unsigned char)inner[i+1])) {   // \xHH
                int v = 0, k = 0;
                while (k < 2 && i+1 < inner.size() && std::isxdigit((unsigned char)inner[i+1])) { char h = inner[++i]; v = v*16 + (std::isdigit((unsigned char)h) ? h-'0' : (std::tolower(h)-'a'+10)); k++; }
                out += (char)v; continue;
            }
            switch(c){case 'n':out+='\n';break;case 't':out+='\t';break;case 'r':out+='\r';break;case 'a':out+='\a';break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'v':out+='\v';break;default:out+=c;}
        } else out += inner[i];
    }
    return out;
}

struct RawItem {
    int line;
    std::string section;
    std::string kind;
    std::string text;
    std::vector<std::string> values;
    int spaceLen = 0;
    uint32_t addr = 0;
};

// ============================================================================
// Macros: ".macro NOMBRE p1, p2, ..." ... cuerpo ... ".endm" / ".endmacro"
// Sintaxis estilo GNU as: dentro del cuerpo, "\p1" se sustituye por el argumento
// correspondiente en cada invocacion, y "\@" por un numero unico de invocacion
// (util para etiquetas locales dentro de una macro que se usa varias veces).
// ============================================================================
struct MacroDef { std::string name; std::vector<std::string> params; std::vector<std::string> body; int defLine = 0; };

// Sustituye "\param" (y "\@") en una linea del cuerpo de una macro por los
// argumentos de esta invocacion concreta. Los nombres de parametro mas largos
// se comprueban antes que los mas cortos para que, p.ej., "\ab" no se confunda
// con "\a" seguido de "b".
static std::string substituteMacroParams(const std::string& lineIn, const std::vector<std::string>& params, const std::vector<std::string>& args, long invocationId) {
    std::vector<size_t> order(params.size()); for (size_t i=0;i<order.size();i++) order[i]=i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b){ return params[a].size() > params[b].size(); });
    std::string out; 
    for (size_t i=0;i<lineIn.size();i++) {
        if (lineIn[i] != '\\') { out += lineIn[i]; continue; }
        if (i+1<lineIn.size() && lineIn[i+1]=='@') { out += std::to_string(invocationId); i++; continue; }
        bool matched = false;
        for (size_t oi : order) {
            const std::string& p = params[oi];
            if (!p.empty() && lineIn.compare(i+1, p.size(), p) == 0) {
                // que no siga otro caracter de identificador (para no matchear un prefijo)
                size_t after = i+1+p.size();
                bool boundary = after>=lineIn.size() || !(std::isalnum((unsigned char)lineIn[after]) || lineIn[after]=='_');
                if (boundary) { out += (oi < args.size() ? args[oi] : ""); i += p.size(); matched = true; break; }
            }
        }
        if (!matched) out += lineIn[i]; // backslash suelto o parametro no reconocido: se deja tal cual
    }
    return out;
}

static long g_macroInvocationCounter = 0;

// Expande recursivamente (hasta una profundidad razonable) las invocaciones de
// macro dentro de "in", devolviendo la lista de lineas ya expandida. Los errores
// (macro desconocida, numero de argumentos erroneo...) se anotan en out_errors.
static void expandMacroInvocation(const RawLine& rl, const std::map<std::string, MacroDef>& macros, int depth, std::vector<RawLine>& out, std::vector<AsmError>& errs) {
    if (depth > 25) { errs.push_back({rl.lineNo, "Demasiados niveles de macros anidadas (¿una macro se invoca a si misma?)", ""}); return; }
    std::string text = stripComment(rl.text);
    std::string working = text;
    // Preserva etiquetas "NOMBRE:" al principio de la linea (pueden ir antes de
    // una invocacion de macro en la misma linea) tal cual, sin tocarlas.
    std::string labelPrefix;
    for (;;) {
        std::string t = trim(working);
        size_t p = 0; while (p<t.size() && (std::isalnum((unsigned char)t[p])||t[p]=='_'||t[p]=='.'||t[p]=='$')) p++;
        if (p==0 || p>=t.size() || t[p]!=':') break;
        std::string name = t.substr(0,p);
        if (!std::isalpha((unsigned char)name[0]) && name[0]!='_' && name[0]!='.' && name[0]!='$') break;
        labelPrefix += name + ": ";
        working = t.substr(p+1);
    }
    std::string trimmed = trim(working);
    if (trimmed.empty()) { if (!labelPrefix.empty()) out.push_back({rl.lineNo, labelPrefix}); else out.push_back(rl); return; }
    size_t sp = trimmed.find_first_of(" \t");
    std::string firstTok = sp==std::string::npos ? trimmed : trimmed.substr(0,sp);
    auto mit = macros.find(upper(firstTok));
    if (mit == macros.end()) { out.push_back({rl.lineNo, labelPrefix + trimmed}); return; }
    const MacroDef& m = mit->second;
    if (!labelPrefix.empty()) out.push_back({rl.lineNo, labelPrefix});
    std::string rest = sp==std::string::npos ? "" : trim(trimmed.substr(sp+1));
    std::vector<std::string> args = rest.empty() ? std::vector<std::string>() : splitTop(rest);
    if (args.size() != m.params.size()) {
        errs.push_back({rl.lineNo, "La macro \"" + m.name + "\" espera " + std::to_string(m.params.size()) + " argumento(s) y se le han pasado " + std::to_string(args.size()), "Definida en la linea " + std::to_string(m.defLine) + " como: .macro " + m.name + (m.params.empty()?"":" " ) + [&]{std::string s; for(size_t i=0;i<m.params.size();i++){ if(i) s+=", "; s+=m.params[i];} return s;}()});
        return;
    }
    long invId = ++g_macroInvocationCounter;
    for (const std::string& bodyLine : m.body) {
        std::string expanded = substituteMacroParams(bodyLine, m.params, args, invId);
        expandMacroInvocation(RawLine{rl.lineNo, expanded}, macros, depth+1, out, errs);
    }
}

static std::vector<RawLine> expandMacros(const std::vector<RawLine>& in, std::vector<AsmError>& errs) {
    std::map<std::string, MacroDef> macros;
    std::vector<RawLine> withoutDefs;
    bool inMacro = false; MacroDef cur;
    for (auto& rl : in) {
        std::string text = stripComment(rl.text); std::string t = trim(text);
        if (!inMacro) {
            std::string up = upper(t);
            bool isMacroStart = (up == ".MACRO") || (up.size() > 6 && up.compare(0, 6, ".MACRO") == 0 && std::isspace((unsigned char)up[6]));
            if (isMacroStart) {
                std::string rest = trim(t.substr(6));
                // El nombre termina en un espacio o en una coma: ".macro myPOP, a" y ".macro myPOP a" son validos en GNU as
                size_t ne = rest.find_first_of(" \t,"); std::string name = rest.substr(0, ne);
                std::istringstream iss(ne == std::string::npos ? std::string() : rest.substr(ne));
                if (name.empty()) { errs.push_back({rl.lineNo, ".macro necesita un nombre: .macro NOMBRE [param1, param2...]", ""}); continue; }
                std::string restParams; std::getline(iss, restParams); restParams = trim(restParams);
                if (!restParams.empty() && restParams[0]==',') restParams = trim(restParams.substr(1));
                cur = MacroDef{}; cur.name = name; cur.defLine = rl.lineNo;
                cur.params = restParams.empty() ? std::vector<std::string>() : splitTop(restParams);
                inMacro = true;
                continue;
            }
            withoutDefs.push_back(rl);
        } else {
            std::string up = upper(t);
            if (up==".ENDM" || up==".ENDMACRO") {
                macros[upper(cur.name)] = cur;
                inMacro = false;
                continue;
            }
            bool isNestedMacroStart = (up == ".MACRO") || (up.size() > 6 && up.compare(0, 6, ".MACRO") == 0 && std::isspace((unsigned char)up[6]));
            if (isNestedMacroStart) { errs.push_back({rl.lineNo, "No se permiten macros anidadas en su definicion (cierra la anterior con .endm primero)", ""}); continue; }
            cur.body.push_back(rl.text); // se guarda sin recortar: stripComment se aplica al expandir
        }
    }
    if (inMacro) errs.push_back({cur.defLine, "La macro \"" + cur.name + "\" no tiene un .endm que la cierre", ""});
    if (!errs.empty()) return {};
    if (macros.empty()) return in; // atajo: nada que expandir, se conserva el texto tal cual (incluye comentarios)

    std::vector<RawLine> out;
    for (auto& rl : withoutDefs) expandMacroInvocation(rl, macros, 0, out, errs);
    return out;
}

Program assemble(const std::string& source, const AsmOptions& opt) {
    Program prog; prog.opt = opt;
    std::vector<RawLine> lines;
    {
        std::string norm; norm.reserve(source.size());
        for (size_t i=0;i<source.size();i++) { if (source[i]=='\r') { if (i+1<source.size() && source[i+1]=='\n') continue; norm+='\n'; } else norm+=source[i]; }
        int ln=1; size_t start=0;
        for (size_t i=0;i<=norm.size();i++) {
            if (i==norm.size() || norm[i]=='\n') { lines.push_back({ln, norm.substr(start,i-start)}); ln++; start=i+1; }
        }
    }

    // Expande ".macro ... .endm" ANTES de todo lo demas: a partir de aqui, el resto
    // del ensamblador no sabe ni le importa que una linea venga de una macro.
    lines = expandMacros(lines, prog.errors);
    if (!prog.errors.empty()) { prog.ok = false; return prog; }

    std::map<std::string,uint32_t> symTmp;
    std::vector<RawItem> items;
    std::string sec = "text";
    uint32_t tOff = 0, dOff = 0;
    bool ended = false;

    auto err = [&](int line, const AsmException& e) { prog.errors.push_back({line, e.what(), e.hint}); };

    // Posicion actual y avance segun la seccion: los datos escritos dentro de .text (p. ej. el "pool de
    // literales" que genera gcc: ".L9: .word .LC0") se colocan en linea con el codigo, de modo que la
    // etiqueta y el dato coinciden. Antes iban siempre al area de datos aunque la etiqueta estuviera en .text.
    auto cur = [&]() -> uint32_t { return sec=="text" ? opt.textBase + tOff : opt.dataBase + dOff; };
    auto adv = [&](uint32_t n) { if (sec=="text") tOff += n; else dOff += n; };

    auto defineLabel = [&](const std::string& name, int) {
        uint32_t addr = (sec=="text") ? (opt.textBase + tOff) : (opt.dataBase + dOff);
        if (symTmp.count(name)) throw AsmException("La etiqueta " + name + " ya esta definida");
        symTmp[name] = addr;
        prog.labelNames.push_back(name);
    };

    for (auto& rl : lines) {
        if (ended) break;
        std::string text = stripComment(rl.text);
        if (trim(text).empty()) continue;
        text = trim(text);
        try {
            for (;;) {
                size_t p = 0; while (p<text.size() && (std::isalnum((unsigned char)text[p])||text[p]=='_'||text[p]=='.'||text[p]=='$')) p++;
                if (p==0 || p>=text.size() || text[p]!=':') break;
                std::string name = text.substr(0,p);
                if (!std::isalpha((unsigned char)name[0]) && name[0]!='_' && name[0]!='.' && name[0]!='$') break;
                defineLabel(name, rl.lineNo);
                text = trim(text.substr(p+1));
            }
            if (text.empty()) continue;
            // etiqueta sin dos puntos: primer token sin sangria que no es un mnemonico ni directiva
            if (!std::isspace((unsigned char)rl.text[0]) && text[0] != '.') {
                size_t p = 0; while (p<text.size() && (std::isalnum((unsigned char)text[p])||text[p]=='_'||text[p]=='$')) p++;
                std::string tok = text.substr(0,p);
                if (!tok.empty() && (std::isalpha((unsigned char)tok[0])||tok[0]=='_') && !parseMnemonic(tok).ok && (p>=text.size() || std::isspace((unsigned char)text[p]))) {
                    defineLabel(tok, rl.lineNo);
                    text = trim(text.substr(p));
                }
            }
            if (text.empty()) continue;

            if (text[0]=='.') {
                size_t sp = text.find_first_of(" \t");
                std::string dir = upper(sp==std::string::npos? text : text.substr(0,sp));
                std::string rest = sp==std::string::npos? "" : trim(text.substr(sp+1));
                if (dir==".TEXT") { sec="text"; }
                else if (dir==".DATA" || dir==".BSS") { sec="data"; }
                else if (dir==".SECTION") {
                    // .section .text / .data / .rodata / .bss (con o sin flags): .text -> codigo; el resto -> datos
                    std::string nm = upper(trim(splitTop(rest).empty() ? std::string() : splitTop(rest)[0]));
                    sec = (nm.rfind(".TEXT",0)==0 || nm==".INIT" || nm==".FINI") ? "text" : "data";
                }
                else if (dir==".GLOBAL" || dir==".GLOBL" || dir==".EXTERN" || dir==".SYNTAX" || dir==".CODE" || dir==".ARM" || dir==".THUMB"
                         || dir==".TYPE" || dir==".SIZE" || dir==".FUNC" || dir==".ENDFUNC" || dir==".FNSTART" || dir==".FNEND" || dir==".ARCH" || dir==".CPU"
                         || dir==".FPU" || dir==".FILE" || dir==".IDENT" || dir==".EABI_ATTRIBUTE" || dir==".LTORG" || dir==".POOL" || dir==".WEAK"
                         || dir==".HIDDEN" || dir==".LOCAL" || dir==".PROTECTED" || dir==".GNU_ATTRIBUTE" || dir==".PREVIOUS" || dir.rfind(".CFI_",0)==0 || dir==".THUMB_FUNC" || dir==".CFI_STARTPROC" || dir==".CFI_ENDPROC") { }
                else if (dir==".ALIGN" || dir==".BALIGN" || dir==".P2ALIGN") {
                    // En ARM de GNU as, .align N alinea a 2^N bytes; .balign N, a N bytes. Solo importa en datos
                    // (las instrucciones ya van alineadas a 4).
                    {
                        auto parts = splitTop(rest);
                        long long n = parts.empty() ? (dir==".BALIGN" ? 4 : 2) : (long long)evalExpr(parts[0], symTmp, cur());
                        long long a = (dir==".BALIGN") ? n : (n >= 0 && n < 16 ? (1LL << n) : 0);
                        if (a > 1 && a <= 4096) {
                            uint32_t off = (sec=="text") ? tOff : dOff;
                            uint32_t pad = (uint32_t)((a - (off % a)) % a);
                            if (pad && !(sec=="text" && a <= 4)) {   // en .text las instrucciones ya van alineadas a 4
                                RawItem it; it.line=rl.lineNo; it.section=sec; it.kind="space"; it.spaceLen=(int)pad; it.addr=cur(); items.push_back(it); adv(pad);
                            }
                        }
                    }
                }
                else if (dir==".HWORD" || dir==".SHORT" || dir==".2BYTE") {
                    auto parts = splitTop(rest); if (parts.empty()) throw AsmException(dir+" espera al menos un valor");
                    RawItem it; it.line=rl.lineNo; it.section=sec; it.kind="half"; it.values=parts; it.addr = cur();
                    items.push_back(it); adv(2*(uint32_t)parts.size());
                }
                else if (dir==".COMM" || dir==".LCOMM") {
                    // .comm nombre, tamaño[, alineacion]: reserva memoria inicializada a 0 en la zona de datos
                    auto parts = splitTop(rest);
                    if (parts.size()<2) throw AsmException(dir+" espera: nombre, tamaño");
                    std::string savedSec = sec; sec = "data";
                    if (parts.size()>=3) { long long a = (long long)evalExpr(parts[2], symTmp, 0); if (a>1 && a<=4096) dOff += (uint32_t)((a - (dOff % a)) % a); }
                    defineLabel(parts[0], rl.lineNo);
                    int n = (int)evalExpr(parts[1], symTmp, opt.dataBase+dOff);
                    RawItem it; it.line=rl.lineNo; it.section="data"; it.kind="space"; it.spaceLen=n; it.addr=opt.dataBase+dOff;
                    items.push_back(it); dOff += (uint32_t)n; sec = savedSec;
                }
                else if (dir==".EQU" || dir==".SET") {
                    auto parts = splitTop(rest);
                    if (parts.size()<2) throw AsmException(dir+" espera un nombre y un valor: .equ NOMBRE, valor");
                    uint32_t v = evalExpr(parts[1], symTmp, sec=="text"?opt.textBase+tOff:opt.dataBase+dOff);
                    symTmp[parts[0]] = v;
                }
                else if (dir==".WORD" || dir==".LONG" || dir==".INT" || dir==".4BYTE") {
                    auto parts = splitTop(rest); if (parts.empty()) throw AsmException(".word espera al menos un valor");
                    RawItem it; it.line=rl.lineNo; it.section=sec; it.kind="word"; it.values=parts; it.addr = cur();
                    items.push_back(it); adv(4*(uint32_t)parts.size());
                }
                else if (dir==".BYTE") {
                    auto parts = splitTop(rest); if (parts.empty()) throw AsmException(".byte espera al menos un valor");
                    RawItem it; it.line=rl.lineNo; it.section=sec; it.kind="byte"; it.values=parts; it.addr = cur();
                    items.push_back(it); adv((uint32_t)parts.size());
                }
                else if (dir==".SPACE" || dir==".SKIP" || dir==".ZERO") {
                    int n = (int)evalExpr(rest, symTmp, cur());
                    RawItem it; it.line=rl.lineNo; it.section=sec; it.kind="space"; it.spaceLen=n; it.addr=cur();
                    items.push_back(it); adv((uint32_t)n);
                }
                else if (dir==".ASCII" || dir==".ASCIZ" || dir==".STRING") {
                    std::string str = parseStringLit(rest);
                    if (dir!=".ASCII") str += '\0';
                    RawItem it; it.line=rl.lineNo; it.section=sec; it.kind="ascii"; it.values={str}; it.addr=cur();
                    items.push_back(it); adv((uint32_t)str.size());
                }
                else if (dir==".END") { ended = true; }
                else if (dir==".ORG") {
                    uint32_t a = evalExpr(rest, symTmp, 0);
                    if (sec=="text") { if (a < opt.textBase) throw AsmException(".org por debajo del inicio de .text"); tOff = a - opt.textBase; }
                    else { if (a < opt.dataBase) throw AsmException(".org por debajo del inicio de .data"); dOff = a - opt.dataBase; }
                }
                else throw AsmException("Directiva no reconocida: " + dir);
                continue;
            }
            if (tOff % 4) {   // tras .byte/.ascii dentro de .text, las instrucciones vuelven a quedar alineadas
                RawItem pad; pad.line=rl.lineNo; pad.section="text"; pad.kind="space"; pad.spaceLen=(int)(4 - tOff%4); pad.addr=opt.textBase+tOff; items.push_back(pad); tOff += 4 - tOff%4;
            }
            RawItem it; it.line=rl.lineNo; it.section="text"; it.kind="instr"; it.text=text; it.addr = opt.textBase+tOff;
            items.push_back(it); tOff += 4;
        } catch (AsmException& e) { err(rl.lineNo, e); }
    }

    prog.textStart = opt.textBase; prog.textEnd = opt.textBase+tOff;
    prog.dataStart = opt.dataBase; prog.dataEnd = opt.dataBase+dOff;
    prog.poolEnd = prog.textEnd;   // el pool de literales va justo despues del codigo

    if (!prog.errors.empty()) { prog.ok = false; return prog; }

    // Funciones de biblioteca simuladas: si el programa no ha definido su propia
    // etiqueta con ese nombre, "printf"/"scanf"/"puts"/"gets"/"strlen" resuelven a
    // una direccion reservada (LIB_BASE...) que Machine::step() intercepta.
    for (size_t i = 0; i < LIB_FUNC_NAMES.size(); i++) {
        const std::string& name = LIB_FUNC_NAMES[i];
        if (!symTmp.count(name)) symTmp[name] = LIB_BASE + (uint32_t)(4 * i);
    }
    for (auto& al : LIB_ALIASES) if (!symTmp.count(al.first) && symTmp.count(al.second)) symTmp[al.first] = symTmp[al.second];

    std::vector<std::pair<std::string,uint32_t>> poolEntries;
    auto poolAddrFor = [&](const std::string& expr)->uint32_t {
        for (auto& e : poolEntries) if (e.first==expr) return e.second;
        uint32_t a = prog.poolEnd; poolEntries.push_back({expr,a}); prog.poolEnd += 4; return a;
    };

    struct InstrParse { RawItem* item; std::string mnemTok; std::vector<std::string> toks; bool isLdrEq=false; std::string eqExpr; };
    std::vector<InstrParse> iparsed;
    for (auto& it : items) {
        if (it.kind != "instr") continue;
        std::string text = it.text;
        size_t sp = text.find_first_of(" \t");
        std::string mnemTok = sp==std::string::npos? text : text.substr(0,sp);
        std::string rest = sp==std::string::npos? "" : trim(text.substr(sp+1));
        auto toks = splitTop(rest);
        InstrParse ip; ip.item=&it; ip.mnemTok=mnemTok; ip.toks=toks;
        if (toks.size()>=2 && !toks[1].empty() && toks[1][0]=='=') {
            ip.isLdrEq = true; ip.eqExpr = toks[1].substr(1);
            poolAddrFor(ip.eqExpr);
        }
        iparsed.push_back(ip);
    }

    for (auto& it : items) {
        if (it.kind=="word") { uint32_t a=it.addr; for (auto& expr : it.values) { uint32_t v; try { v = evalExpr(expr, symTmp, a);} catch(AsmException&e){ err(it.line,e); v=0;} for(int b=0;b<4;b++) prog.initialMem[a+b] = (uint8_t)((v>>(8*b))&0xFF); a+=4; } }
        else if (it.kind=="byte") { uint32_t a=it.addr; for (auto& expr : it.values) { uint32_t v; try { v = evalExpr(expr, symTmp, a);} catch(AsmException&e){ err(it.line,e); v=0;} prog.initialMem[a]=(uint8_t)(v&0xFF); a+=1; } }
        else if (it.kind=="half") { uint32_t a=it.addr; for (auto& expr : it.values) { uint32_t v; try { v = evalExpr(expr, symTmp, a);} catch(AsmException&e){ err(it.line,e); v=0;} prog.initialMem[a]=(uint8_t)(v&0xFF); prog.initialMem[a+1]=(uint8_t)((v>>8)&0xFF); a+=2; } }
        else if (it.kind=="ascii") { uint32_t a=it.addr; for (char c : it.values[0]) prog.initialMem[a++] = (uint8_t)c; }
        else if (it.kind=="space") { for (int i=0;i<it.spaceLen;i++) prog.initialMem[it.addr+i] = 0; }
    }
    for (auto& e : poolEntries) {
        uint32_t v; try { v = evalExpr(e.first, symTmp, e.second); } catch (AsmException& ex) { err(0, ex); v=0; }
        for (int b=0;b<4;b++) prog.initialMem[e.second+b] = (uint8_t)((v>>(8*b))&0xFF);
    }

    for (auto& ip : iparsed) {
        RawItem& it = *ip.item;
        uint32_t addr = it.addr;
        try {
            Instruction ins;
            ins.line = it.line; ins.addr = addr; ins.src = it.text;
            if (ip.isLdrEq) {
                ParsedMnemonic pm = parseMnemonic(ip.mnemTok);
                if (!pm.ok || pm.kind!="mem") throw AsmException("=valor solo es valido con LDR/STR");
                int rd = parseReg(ip.toks[0]); if (rd<0) throw AsmException("Se esperaba un registro destino");
                ins.mnemonic = pm.base; ins.cond=pm.cond; ins.rd=rd; ins.load=true; ins.size=4; ins.signedLoad=false;
                uint32_t poolAddr = poolAddrFor(ip.eqExpr);
                int32_t rel = (int32_t)poolAddr - (int32_t)(addr+8);
                ins.mem.baseReg = 15; ins.mem.preIndexed=true; ins.mem.writeBack=false; ins.mem.offsetIsReg=false;
                ins.mem.add = rel>=0; ins.mem.offsetImm = std::abs(rel);
                if (std::abs(rel) > 0xFFF) throw AsmException("El valor literal esta demasiado lejos para LDR " + regLabel(rd) + ", =" + ip.eqExpr);
                ins.t = IType::MEM;
                encodeMem(ins);
                ins.note = "Pseudo-instruccion: carga el valor " + ip.eqExpr + " desde el pool de literales en " + hx0x(poolAddr) + ".";
            } else {
                ParsedMnemonic pm = parseMnemonic(ip.mnemTok);
                if (!pm.ok) throw AsmException("Instruccion no reconocida: " + ip.mnemTok, "Comprueba el mnemonico y sus posibles sufijos de condicion.");
                ins.cond = pm.cond;
                auto& toks = ip.toks;
                if (pm.kind=="dp") {
                    ins.mnemonic = pm.base; ins.s = pm.s; ins.t = IType::DP;
                    if (isTestOp(pm.base)) { if (toks.size()<2) throw AsmException(pm.base+" espera dos operandos: Rn, operando2"); ins.rn=parseReg(toks[0]); if(ins.rn<0) throw AsmException("Se esperaba un registro"); ins.op2=parseOperand2(toks,1); }
                    else if (isMonoOp(pm.base)) { if (toks.empty()) throw AsmException(pm.base+" espera Rd, operando2"); ins.rd=parseReg(toks[0]); if(ins.rd<0) throw AsmException("Se esperaba un registro destino"); ins.op2=parseOperand2(toks,1); }
                    else { if (toks.size()<2) throw AsmException(pm.base+" espera Rd, Rn, operando2"); ins.rd=parseReg(toks[0]); ins.rn=parseReg(toks[1]); if(ins.rd<0||ins.rn<0) throw AsmException("Se esperaba un registro"); ins.op2=parseOperand2(toks,2); }
                    encodeDP(ins);
                } else if (pm.kind=="shift") {
                    if (toks.size()<3) throw AsmException(pm.base+" espera Rd, Rm, cantidad");
                    ins.mnemonic="MOV"; ins.s=pm.s; ins.t = IType::DP; ins.rd=parseReg(toks[0]); if(ins.rd<0) throw AsmException("Se esperaba un registro destino");
                    int rm = parseReg(toks[1]); if(rm<0) throw AsmException("Se esperaba un registro");
                    Operand2 op; op.isImm=false; op.reg=rm;
                    parseShiftSuffix(pm.base + " " + toks[2], op);
                    ins.op2 = op;
                    encodeDP(ins);
                    ins.note = pm.base + " Rd, Rm, cantidad es la forma abreviada de MOV Rd, Rm, " + pm.base + " cantidad.";
                } else if (pm.kind=="mul") {
                    bool mla = pm.base=="MLA";
                    if ((mla && toks.size()<4) || (!mla && toks.size()<3)) throw AsmException(pm.base+" espera "+std::string(mla?"Rd, Rm, Rs, Rn":"Rd, Rm, Rs"));
                    ins.mnemonic=pm.base; ins.s=pm.s; ins.t = IType::MUL;
                    ins.rd=parseReg(toks[0]); ins.rm=parseReg(toks[1]); ins.rs=parseReg(toks[2]);
                    if (mla) ins.rn = parseReg(toks[3]);
                    if (ins.rd<0||ins.rm<0||ins.rs<0||(mla&&ins.rn<0)) throw AsmException("Se esperaba un registro");
                    // Rd == Rm (o Rd == Rs, Rd == Rn) es valido: el emulador lee los operandos antes de escribir
                    // el resultado y las versiones modernas de ARM (v6+) lo permiten. Solo PC esta prohibido.
                    if (ins.rd==15||ins.rm==15||ins.rs==15||(mla&&ins.rn==15)) throw AsmException("En "+pm.base+" no se puede usar PC (R15) como operando");
                    encodeMul(ins);
                } else if (pm.kind=="mull") {
                    if (toks.size()<4) throw AsmException(pm.base+" espera RdLo, RdHi, Rm, Rs");
                    ins.mnemonic=pm.base; ins.s=pm.s; ins.t = IType::MULL;
                    ins.rdLo=parseReg(toks[0]); ins.rdHi=parseReg(toks[1]); ins.rm=parseReg(toks[2]); ins.rs=parseReg(toks[3]);
                    if (ins.rdLo<0||ins.rdHi<0||ins.rm<0||ins.rs<0) throw AsmException("Se esperaba un registro");
                    // RdLo y RdHi deben ser registros distintos (con el mismo registro el resultado seria impredecible),
                    // pero RdLo/RdHi SI pueden coincidir con Rm o Rs: los operandos se leen antes de escribir.
                    if (ins.rdLo==ins.rdHi) throw AsmException("En "+pm.base+", RdLo y RdHi deben ser registros distintos");
                    if (ins.rdLo==15||ins.rdHi==15||ins.rm==15||ins.rs==15) throw AsmException("En "+pm.base+" no se puede usar PC (R15) como operando");
                    encodeMull(ins);
                } else if (pm.kind=="mem") {
                    if (toks.size()<2) throw AsmException(pm.base+" espera Rd, [operando de memoria]");
                    ins.mnemonic=pm.base; ins.load=pm.load; ins.size=pm.size; ins.signedLoad=pm.signedLoad; ins.t = IType::MEM;
                    ins.rd=parseReg(toks[0]); if(ins.rd<0) throw AsmException("Se esperaba un registro");
                    if (toks[1].empty() || toks[1][0] != '[') {
                        // "LDR Rd, etiqueta" / "LDR Rd, etiqueta+4": direccionamiento relativo a PC (lo genera gcc para su pool de literales)
                        if (!pm.load && pm.base != "STR") throw AsmException("Se esperaba [ para el operando de memoria");
                        uint32_t target = evalExpr(toks[1], symTmp, addr);
                        int32_t rel = (int32_t)target - (int32_t)(addr + 8);
                        if (std::abs(rel) > 0xFFF) throw AsmException("La etiqueta " + toks[1] + " esta demasiado lejos para LDR/STR relativo a PC (maximo 4095 bytes)");
                        ins.mem.baseReg = 15; ins.mem.preIndexed = true; ins.mem.writeBack = false; ins.mem.offsetIsReg = false;
                        ins.mem.add = rel >= 0; ins.mem.offsetImm = std::abs(rel);
                        encodeMem(ins);
                        ins.note = "Direccionamiento relativo a PC: accede a " + toks[1] + " (" + hx0x(target) + ").";
                    } else {
                    ins.mem = parseMemOperand(toks,1);
                    encodeMem(ins);
                    }
                } else if (pm.kind=="blk") {
                    ins.mnemonic=pm.base; ins.load=pm.load; ins.blkMode=pm.mode; ins.isStack=pm.stack; ins.t = IType::BLK;
                    if (toks.empty()) throw AsmException(pm.base+" espera una lista de registros entre llaves");
                    parseBlockOperands(ins, toks, pm.stack);
                    encodeBlock(ins);
                } else if (pm.kind=="br") {
                    ins.mnemonic=pm.base; ins.link=pm.link; ins.t = IType::BR;
                    if (toks.empty()) throw AsmException(pm.base+" espera una etiqueta");
                    ins.targetLabel = toks[0];
                    auto sit = symTmp.find(toks[0]);
                    if (sit == symTmp.end()) throw AsmException("Etiqueta desconocida: " + toks[0], "Comprueba que la etiqueta existe (con sus dos puntos) en algun lugar del programa.");
                    uint32_t target = sit->second;
                    int32_t rel = (int32_t)target - (int32_t)(addr+8);
                    if (rel % 4 != 0) throw AsmException("El destino del salto no esta alineado a 4 bytes");
                    ins.branchOffsetWords = rel/4;
                    encodeBranch(ins);
                    if (const char* lf = libraryFunctionName(target)) ins.note = std::string("Llamada a la funcion de biblioteca ") + lf + "() (simulada): ver su efecto en la Consola y/o la memoria.";
                } else if (pm.kind=="bx") {
                    ins.mnemonic=pm.base; ins.t = IType::BX; if (toks.empty()) throw AsmException("BX espera un registro"); ins.rm=parseReg(toks[0]); if(ins.rm<0) throw AsmException("Se esperaba un registro");
                    encodeBX(ins);
                } else if (pm.kind=="swi") {
                    ins.mnemonic=pm.base; ins.t = IType::SWI; ins.swiNum = toks.empty()?0:evalExpr(toks[0][0]=='#'?toks[0].substr(1):toks[0], symTmp, addr);
                    encodeSwi(ins);
                } else if (pm.kind=="nop") {
                    ins.mnemonic="MOV"; ins.rd=0; ins.t = IType::DP; Operand2 op; op.isImm=false; op.reg=0; ins.op2=op; encodeDP(ins);
                    ins.note = "NOP se codifica como MOV R0, R0 (no tiene efecto).";
                } else throw AsmException("Instruccion no soportada: " + ip.mnemTok);
            }
            ListingLine ll; ll.addr=addr; ll.word=ins.word; ll.src=it.text; ll.line=it.line; ll.isInstr=true; ll.ins=ins;
            prog.listing.push_back(ll);
            prog.byAddr[addr] = ins;
            for (int b=0;b<4;b++) prog.initialMem[addr+b] = (uint8_t)((ins.word>>(8*b))&0xFF);
            prog.nInstr++;
        } catch (AsmException& e) { err(it.line, e); }
    }
    for (auto& e : poolEntries) {
        ListingLine ll; ll.addr=e.second; uint32_t v; try{ v=evalExpr(e.first,symTmp,e.second);}catch(...){v=0;}
        ll.word=v; ll.src="=" + e.first; ll.line=0; ll.isInstr=false;
        prog.listing.push_back(ll);
    }
    std::sort(prog.listing.begin(), prog.listing.end(), [](const ListingLine&a,const ListingLine&b){return a.addr<b.addr;});

    for (auto& kv : symTmp) prog.symbols[kv.first] = kv.second;
    prog.ok = prog.errors.empty();
    return prog;
}

// ============================================================================
// Codificador de una sola linea (para la UI: "codificador en vivo")
// ============================================================================
EncodeResult encodeLine(const std::string& textIn, uint32_t addr, const std::map<std::string,uint32_t>& syms) {
    EncodeResult res;
    std::string text = trim(textIn);
    if (text.empty()) { res.ok=false; res.error="Escribe una instruccion"; return res; }
    try {
        size_t sp = text.find_first_of(" \t");
        std::string mnemTok = sp==std::string::npos? text : text.substr(0,sp);
        std::string rest = sp==std::string::npos? "" : trim(text.substr(sp+1));
        auto toks = splitTop(rest);
        ParsedMnemonic pm = parseMnemonic(mnemTok);
        if (!pm.ok) throw AsmException("Instruccion no reconocida: " + mnemTok, "Comprueba el mnemonico y los posibles sufijos de condicion.");
        Instruction ins; ins.addr=addr; ins.src=text; ins.cond=pm.cond;
        if (pm.kind=="dp") {
            ins.mnemonic=pm.base; ins.s=pm.s;
            if (isTestOp(pm.base)) { if(toks.size()<2) throw AsmException(pm.base+" espera Rn, operando2"); ins.rn=parseReg(toks[0]); ins.op2=parseOperand2(toks,1); }
            else if (isMonoOp(pm.base)) { if(toks.empty()) throw AsmException(pm.base+" espera Rd, operando2"); ins.rd=parseReg(toks[0]); ins.op2=parseOperand2(toks,1); }
            else { if(toks.size()<2) throw AsmException(pm.base+" espera Rd, Rn, operando2"); ins.rd=parseReg(toks[0]); ins.rn=parseReg(toks[1]); ins.op2=parseOperand2(toks,2); }
            encodeDP(ins);
        } else if (pm.kind=="mem") {
            if (toks.size()<2) throw AsmException(pm.base+" espera Rd, [memoria]");
            ins.mnemonic=pm.base; ins.load=pm.load; ins.size=pm.size; ins.signedLoad=pm.signedLoad;
            ins.rd=parseReg(toks[0]); ins.mem=parseMemOperand(toks,1);
            encodeMem(ins);
        } else if (pm.kind=="br") {
            ins.mnemonic=pm.base; ins.link=pm.link;
            if (toks.empty()) throw AsmException(pm.base+" espera una etiqueta");
            uint32_t target;
            auto it = syms.find(toks[0]);
            if (it!=syms.end()) target = it->second; else target = addr;
            ins.branchOffsetWords = ((int32_t)target - (int32_t)(addr+8))/4;
            encodeBranch(ins);
            if (it==syms.end()) res.note = "Etiqueta " + toks[0] + " no encontrada: se muestra como si apuntara aqui mismo.";
        } else if (pm.kind=="bx") { ins.mnemonic=pm.base; ins.rm=parseReg(toks.empty()?"":toks[0]); encodeBX(ins); }
        else if (pm.kind=="mul") {
            bool mla=pm.base=="MLA"; ins.mnemonic=pm.base; ins.s=pm.s;
            ins.rd=parseReg(toks.size()>0?toks[0]:""); ins.rm=parseReg(toks.size()>1?toks[1]:""); ins.rs=parseReg(toks.size()>2?toks[2]:"");
            if (mla) ins.rn=parseReg(toks.size()>3?toks[3]:"");
            encodeMul(ins);
        } else if (pm.kind=="mull") {
            ins.mnemonic=pm.base; ins.s=pm.s;
            ins.rdLo=parseReg(toks.size()>0?toks[0]:""); ins.rdHi=parseReg(toks.size()>1?toks[1]:"");
            ins.rm=parseReg(toks.size()>2?toks[2]:""); ins.rs=parseReg(toks.size()>3?toks[3]:"");
            encodeMull(ins);
        } else if (pm.kind=="blk") {
            ins.mnemonic=pm.base; ins.load=pm.load; ins.blkMode=pm.mode;
            parseBlockOperands(ins, toks, pm.stack);
            encodeBlock(ins);
        } else if (pm.kind=="swi") { ins.mnemonic=pm.base; ins.swiNum = toks.empty()?0:evalExpr(toks[0][0]=='#'?toks[0].substr(1):toks[0], syms, addr); encodeSwi(ins); }
        else if (pm.kind=="shift") {
            if (toks.size()<3) throw AsmException(pm.base+" espera Rd, Rm, cantidad");
            ins.mnemonic="MOV"; ins.s=pm.s; ins.rd=parseReg(toks[0]); Operand2 op; op.isImm=false; op.reg=parseReg(toks[1]);
            parseShiftSuffix(pm.base+" "+toks[2], op); ins.op2=op; encodeDP(ins);
        } else if (pm.kind=="nop") { ins.mnemonic="MOV"; ins.rd=0; Operand2 op; op.isImm=false; op.reg=0; ins.op2=op; encodeDP(ins); }
        else throw AsmException("Tipo de instruccion no soportado en el codificador");

        res.ok=true; res.word=ins.word; res.format=ins.format; res.fields=ins.fields; if(res.note.empty()) res.note=ins.note;
    } catch (AsmException& e) { res.ok=false; res.error=e.what(); res.hint=e.hint; }
    return res;
}

// ============================================================================
// Maquina: ejecucion
// ============================================================================
Machine::Machine(const Program& prog) : prog_(prog) { reset(); }

void Machine::reset() {
    r.fill(0);
    r[13] = prog_.opt.stackTop;
    pc = prog_.textStart;
    // Como en un ejecutable real (ld/gcc), la ejecucion empieza en "main" si el programa la define:
    // asi se pueden escribir subrutinas (p. ej. "reproduce") ANTES de main en el fichero.
    { auto m = prog_.symbols.find("main"); if (m != prog_.symbols.end() && prog_.byAddr.count(m->second)) pc = m->second; }
    f = Flags{};
    halted = false; haltReason.clear(); haltKind.clear();
    steps = 0; output.clear(); lastWasLibraryCall = false;
    mem.clear(); touched.clear();
    history.clear();
    for (auto& kv : prog_.initialMem) mem[kv.first] = kv.second;
    if (!prog_.ok || prog_.nInstr==0) { halt(prog_.ok? "El programa no tiene instrucciones." : "Hay errores de ensamblado.", "error"); }
}

void Machine::halt(const std::string& reason, const std::string& kind) { halted=true; haltReason=reason; haltKind=kind; }

uint8_t Machine::readByte(uint32_t a) const { auto it = mem.find(a); return it==mem.end()? 0 : it->second; }
uint32_t Machine::readWord(uint32_t a) const {
    uint32_t v=0; for (int i=0;i<4;i++) v |= (uint32_t)readByte(a+i) << (8*i); return v;
}
void Machine::writeByte(uint32_t a, uint8_t v, StepRecord* rec) {
    uint8_t old = readByte(a);
    if (rec && old!=v) rec->mem.push_back({a, old, v});
    mem[a]=v; touched[a]=true;
}
void Machine::writeWord(uint32_t a, uint32_t v, StepRecord* rec) {
    for (int i=0;i<4;i++) writeByte(a+i, (uint8_t)((v>>(8*i))&0xFF), rec);
}

static uint32_t applyShift(int type, uint32_t v, int n, bool& carryOut, bool carryIn, bool isReg) {
    n &= isReg? 0xFF : 31;
    switch (type) {
        case 0: // LSL
            if (n==0) { carryOut=carryIn; return v; }
            if (n>32) { carryOut=false; return 0; }
            carryOut = (n>=1 && n<=32) ? ((v >> (32-n)) & 1) : false;
            return (n>=32)?0:(v << n);
        case 1: // LSR
            if (n==0) { if (isReg) { carryOut=carryIn; return v; } n=32; }
            if (n>32) { carryOut=false; return 0; }
            carryOut = (n>=1 && n<=32) ? ((v >> (n-1)) & 1) : false;
            return (n>=32)?0:(v >> n);
        case 2: { // ASR
            int32_t sv = (int32_t)v;
            if (n==0) { if (isReg) { carryOut=carryIn; return v; } n=32; }
            if (n>=32) { carryOut = (v>>31)&1; return (uint32_t)(sv>>31); }
            carryOut = (v >> (n-1)) & 1;
            return (uint32_t)(sv >> n);
        }
        case 3: default: { // ROR (n==0 && !isReg -> RRX)
            if (n==0 && !isReg) { carryOut = v&1; return (uint32_t)(((uint32_t)carryIn<<31) | (v>>1)); }
            if (n==0) { carryOut=carryIn; return v; }
            n &= 31; if (n==0) { carryOut = (v>>31)&1; return v; }
            carryOut = (v >> (n-1)) & 1;
            return rotr(v, n);
        }
    }
}


// ---------------------------------------------------------------------------------------------
// scanf "de verdad": consume de la entrada solo lo que casa con el formato (el resto queda para la
// siguiente lectura, igual que en C). Admite %d %i %u %x %X %o %s %c %%, anchura implicita y %*d.
// ---------------------------------------------------------------------------------------------
struct ScanItem { char type = 'd'; long long num = 0; std::string str; int size = 4; };
static int scanfParse(const std::string& buf, size_t pos, const std::string& fmt, std::vector<ScanItem>& items, size_t& newPos, bool& needMore) {
    needMore = false; int n = 0; size_t p = pos;
    auto skipWs = [&](size_t q) { while (q < buf.size() && std::isspace((unsigned char)buf[q])) q++; return q; };
    for (size_t i = 0; i < fmt.size(); i++) {
        char fc = fmt[i];
        if (std::isspace((unsigned char)fc)) { p = skipWs(p); continue; }
        if (fc != '%') { if (p >= buf.size()) { needMore = true; break; } if (buf[p] != fc) break; p++; continue; }
        if (++i >= fmt.size()) break;
        bool suppress = false; if (fmt[i] == '*') { suppress = true; if (++i >= fmt.size()) break; }
        while (i < fmt.size() && std::isdigit((unsigned char)fmt[i])) i++;
        int size = 4; while (i < fmt.size() && std::strchr("hlLzjt", fmt[i])) { if (fmt[i] == 'h') size = (size == 2 ? 1 : 2); i++; }
        if (i >= fmt.size()) break;
        char c = fmt[i];
        if (c != 'c') p = skipWs(p);
        if (p >= buf.size()) { needMore = true; break; }
        if (c == '%') { if (buf[p] != '%') break; p++; continue; }
        ScanItem it; it.type = c; it.size = size;
        if (std::strchr("diuxXo", c)) {
            size_t q = p; bool neg = false; if (buf[q] == '+' || buf[q] == '-') { neg = buf[q] == '-'; q++; }
            int base = (c == 'x' || c == 'X') ? 16 : c == 'o' ? 8 : c == 'i' ? 0 : 10;
            const char* st = buf.c_str() + q; char* end = nullptr;
            if (q >= buf.size() || std::isspace((unsigned char)*st)) { if (q >= buf.size()) needMore = true; break; }
            unsigned long long v = std::strtoull(st, &end, base); if (end == st) break;
            it.num = neg ? -(long long)v : (long long)v; p = q + (size_t)(end - st);
        } else if (c == 's') {
            size_t q = p; while (q < buf.size() && !std::isspace((unsigned char)buf[q])) q++;
            it.str = buf.substr(p, q - p); p = q;
        } else if (c == 'c') { it.str = std::string(1, buf[p]); p++; }
        else break;                       // %f, %e... no soportados (el simulador no tiene coma flotante)
        if (!suppress) { items.push_back(it); n++; }
    }
    newPos = p; return n;
}
bool Machine::scanfNeedsMore(const std::string& fmt) const {
    std::vector<ScanItem> items; size_t np; bool more = false;
    scanfParse(input_, inputPos_, fmt, items, np, more);
    return more;
}

void Machine::feedInput(const std::string& text) {
    input_ += text;
    if (halted && haltKind == "input") { halted = false; haltReason.clear(); haltKind.clear(); }
}
char Machine::takeChar() { return input_[inputPos_++]; }
std::string Machine::takeLine() {
    size_t nl = input_.find('\n', inputPos_);
    std::string s = input_.substr(inputPos_, nl - inputPos_);
    inputPos_ = nl + 1;
    return s;
}

std::string Machine::readCString(uint32_t addr, size_t maxLen) const {
    std::string s;
    for (size_t i = 0; i < maxLen; i++) { uint8_t b = readByte(addr + (uint32_t)i); if (!b) break; s += (char)b; }
    return s;
}

// Interpreta un formato estilo printf (%d %i %u %x %X %o %c %s %p %% con banderas, anchura y precision)
static std::string cFormat(const Machine& m, const std::string& fmt, const std::function<uint32_t()>& nextArg) {
    std::string outp;
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) { outp += fmt[i]; continue; }
        size_t j = i + 1; std::string flags, width, prec; bool hasPrec = false;
        while (j < fmt.size() && std::strchr("-+ 0#", fmt[j])) flags += fmt[j++];
        while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) width += fmt[j++];
        if (j < fmt.size() && fmt[j] == '.') { hasPrec = true; j++; while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) prec += fmt[j++]; }
        while (j < fmt.size() && std::strchr("hlzjt", fmt[j])) j++;      // modificadores de longitud: todo es de 32 bits
        if (j >= fmt.size()) { outp += fmt.substr(i); break; }
        char c = fmt[j]; std::string spec = "%" + flags + width + (hasPrec ? "." + (prec.empty() ? std::string("0") : prec) : std::string());
        char buf[512]; buf[0] = 0;
        switch (c) {
            case 'd': case 'i': std::snprintf(buf, sizeof buf, (spec + "d").c_str(), (int)(int32_t)nextArg()); outp += buf; break;
            case 'u': std::snprintf(buf, sizeof buf, (spec + "u").c_str(), (unsigned)nextArg()); outp += buf; break;
            case 'x': std::snprintf(buf, sizeof buf, (spec + "x").c_str(), (unsigned)nextArg()); outp += buf; break;
            case 'X': std::snprintf(buf, sizeof buf, (spec + "X").c_str(), (unsigned)nextArg()); outp += buf; break;
            case 'o': std::snprintf(buf, sizeof buf, (spec + "o").c_str(), (unsigned)nextArg()); outp += buf; break;
            case 'p': std::snprintf(buf, sizeof buf, "0x%x", (unsigned)nextArg()); outp += buf; break;
            case 'c': std::snprintf(buf, sizeof buf, (spec + "c").c_str(), (int)(nextArg() & 0xFF)); outp += buf; break;
            case 's': { std::string sv = m.readCStringPublic(nextArg()); std::snprintf(buf, sizeof buf, (spec + "s").c_str(), sv.substr(0, 400).c_str()); outp += buf; break; }
            case '%': outp += '%'; break;
            default: outp += fmt.substr(i, j - i + 1); break;
        }
        i = j;
    }
    return outp;
}


// Funciones de biblioteca adicionales (las que suele necesitar un programa en C compilado con gcc).
// Convenio AAPCS: argumentos en R0..R3 (y pila), resultado en R0.
bool Machine::runExtraLibraryCall(const std::string& fn, StepRecord& rec) {
    auto ret = [&](uint32_t v) { uint32_t old = r[0]; r[0] = v; if (old != v) rec.regs.push_back({0, old, v}); };
    auto setR = [&](int n, uint32_t v) { uint32_t old = r[n]; r[n] = v; if (old != v) rec.regs.push_back({n, old, v}); };
    auto say = [&](const std::string& sym, const std::string& txt) { rec.sym = sym; rec.text = txt; };
    uint32_t a0 = r[0], a1 = r[1], a2 = r[2];

    if (fn == "__aeabi_idiv" || fn == "__aeabi_idivmod") {
        int32_t n = (int32_t)a0, d = (int32_t)a1;
        if (d == 0) { halt("Division entre cero (" + std::to_string(n) + " / 0).", "error"); return true; }
        int64_t q = (int64_t)n / d, rem = (int64_t)n % d;
        ret((uint32_t)(int32_t)q); if (fn == "__aeabi_idivmod") setR(1, (uint32_t)(int32_t)rem);
        say(std::to_string(n) + " / " + std::to_string(d), "Division entera con signo (rutina de gcc: ARMv4 no tiene instruccion de division). Cociente en R0" + std::string(fn == "__aeabi_idivmod" ? ", resto en R1." : "."));
        return true;
    }
    if (fn == "__aeabi_uidiv" || fn == "__aeabi_uidivmod") {
        if (a1 == 0) { halt("Division entre cero (" + std::to_string(a0) + " / 0).", "error"); return true; }
        ret(a0 / a1); if (fn == "__aeabi_uidivmod") setR(1, a0 % a1);
        say(std::to_string(a0) + " / " + std::to_string(a1), "Division entera sin signo (rutina de gcc). Cociente en R0" + std::string(fn == "__aeabi_uidivmod" ? ", resto en R1." : "."));
        return true;
    }
    if (fn == "__aeabi_ldivmod" || fn == "__aeabi_uldivmod") {
        uint64_t nn = ((uint64_t)r[1] << 32) | r[0], dd = ((uint64_t)r[3] << 32) | r[2]; uint64_t q, rem;
        if (dd == 0) { halt("Division entre cero (64 bits).", "error"); return true; }
        if (fn == "__aeabi_ldivmod") { int64_t n = (int64_t)nn, d = (int64_t)dd; q = (uint64_t)(n / d); rem = (uint64_t)(n % d); }
        else { q = nn / dd; rem = nn % dd; }
        setR(0, (uint32_t)q); setR(1, (uint32_t)(q >> 32)); setR(2, (uint32_t)rem); setR(3, (uint32_t)(rem >> 32));
        say("division de 64 bits", "Division entera de 64 bits (rutina de gcc): cociente en R1:R0, resto en R3:R2."); return true;
    }
    if (fn == "sprintf") {
        std::string fmt = readCString(a1); int argi = 2;
        auto nextArg = [&]() -> uint32_t { uint32_t v = (argi <= 3) ? r[argi] : readWord(r[13] + 4u * (uint32_t)(argi - 4)); argi++; return v; };
        std::string outp = cFormat(*this, fmt, nextArg);
        for (size_t k = 0; k < outp.size(); k++) writeByte(a0 + (uint32_t)k, (uint8_t)outp[k], &rec);
        writeByte(a0 + (uint32_t)outp.size(), 0, &rec);
        ret((uint32_t)outp.size()); say("sprintf(buf, \"" + fmt + "\", ...)", "Como printf, pero escribe el texto (con un 0 final) en el buffer de R0 en vez de en la consola.");
        return true;
    }
    if (fn == "strcpy" || fn == "strncpy") {
        std::string s = readCString(a1); uint32_t n = (fn == "strncpy") ? a2 : (uint32_t)s.size() + 1;
        for (uint32_t k = 0; k < n; k++) writeByte(a0 + k, k < s.size() ? (uint8_t)s[k] : 0, &rec);
        say(fn + "(dest, \"" + s + "\")", "Copia la cadena apuntada por R1 en el buffer de R0; devuelve el destino en R0.");
        return true;
    }
    if (fn == "strcat") {
        std::string d = readCString(a0), s = readCString(a1);
        for (size_t k = 0; k <= s.size(); k++) writeByte(a0 + (uint32_t)d.size() + (uint32_t)k, k < s.size() ? (uint8_t)s[k] : 0, &rec);
        say("strcat(dest, \"" + s + "\")", "Anade la cadena de R1 al final de la de R0; devuelve el destino en R0.");
        return true;
    }
    if (fn == "strcmp" || fn == "strncmp") {
        std::string x = readCString(a0), y = readCString(a1); int c = (fn == "strncmp") ? std::strncmp(x.c_str(), y.c_str(), a2) : std::strcmp(x.c_str(), y.c_str());
        ret((uint32_t)(c < 0 ? -1 : c > 0 ? 1 : 0)); say(fn + "(\"" + x + "\", \"" + y + "\")", "Compara las dos cadenas: <0, 0 o >0 en R0.");
        return true;
    }
    if (fn == "strchr") {
        std::string x = readCString(a0); size_t p = (a1 & 0xFF) == 0 ? x.size() : x.find((char)(a1 & 0xFF));
        ret(p == std::string::npos ? 0u : a0 + (uint32_t)p); say("strchr(s, c)", "Busca el caracter R1 en la cadena R0: puntero a el, o 0 si no esta.");
        return true;
    }
    if (fn == "memcpy" || fn == "memmove") {
        std::vector<uint8_t> tmp(a2); for (uint32_t k = 0; k < a2; k++) tmp[k] = readByte(a1 + k);
        for (uint32_t k = 0; k < a2; k++) writeByte(a0 + k, tmp[k], &rec);
        say(fn + "(dest, src, n)", "Copia R2 bytes de R1 a R0; devuelve el destino.");
        return true;
    }
    if (fn == "memset") { for (uint32_t k = 0; k < a2; k++) writeByte(a0 + k, (uint8_t)a1, &rec); say("memset(p, c, n)", "Rellena R2 bytes desde R0 con el valor de R1; devuelve R0."); return true; }
    if (fn == "memcmp") {
        int c = 0; for (uint32_t k = 0; k < a2 && !c; k++) c = (int)readByte(a0 + k) - (int)readByte(a1 + k);
        ret((uint32_t)(c < 0 ? -1 : c > 0 ? 1 : 0)); say("memcmp(a, b, n)", "Compara R2 bytes de R0 y R1."); return true;
    }
    if (fn == "abs") { int32_t v = (int32_t)a0; ret((uint32_t)(v < 0 ? -v : v)); say("abs(R0)", "Valor absoluto de R0."); return true; }
    if (fn == "toupper") { ret(a0 >= 'a' && a0 <= 'z' ? a0 - 32 : a0); say("toupper(R0)", "Mayuscula del caracter de R0."); return true; }
    if (fn == "tolower") { ret(a0 >= 'A' && a0 <= 'Z' ? a0 + 32 : a0); say("tolower(R0)", "Minuscula del caracter de R0."); return true; }
    if (fn == "isdigit") { ret(a0 >= '0' && a0 <= '9'); say("isdigit(R0)", "1 si R0 es una cifra; 0 si no."); return true; }
    if (fn == "isalpha") { ret((a0 >= 'a' && a0 <= 'z') || (a0 >= 'A' && a0 <= 'Z')); say("isalpha(R0)", "1 si R0 es una letra; 0 si no."); return true; }
    if (fn == "isalnum") { ret((a0 >= 'a' && a0 <= 'z') || (a0 >= 'A' && a0 <= 'Z') || (a0 >= '0' && a0 <= '9')); say("isalnum(R0)", "1 si R0 es letra o cifra."); return true; }
    if (fn == "isspace") { ret(a0 == ' ' || (a0 >= 9 && a0 <= 13)); say("isspace(R0)", "1 si R0 es un espacio en blanco."); return true; }
    if (fn == "isupper") { ret(a0 >= 'A' && a0 <= 'Z'); say("isupper(R0)", "1 si R0 es mayuscula."); return true; }
    if (fn == "islower") { ret(a0 >= 'a' && a0 <= 'z'); say("islower(R0)", "1 si R0 es minuscula."); return true; }
    if (fn == "malloc" || fn == "calloc") {
        uint32_t n = (fn == "calloc") ? a0 * a1 : a0; n = (n + 7) & ~7u; uint32_t p = heapNext_; heapNext_ += n ? n : 8;
        if (fn == "calloc") for (uint32_t k = 0; k < n; k++) writeByte(p + k, 0, &rec);
        ret(p); say(fn + "(...)", "Reserva memoria en el monton simulado (desde 0x08000000); devuelve el puntero en R0."); return true;
    }
    if (fn == "fputs") { std::string x = readCString(a0); output += x; ret(1); say("fputs(\"" + x + "\", f)", "Escribe la cadena de R0 por consola (sin salto de linea)."); return true; }
    if (fn == "fprintf" || fn == "snprintf") {
        bool sn = fn == "snprintf";
        std::string fmt = readCString(sn ? a2 : a1); int argi = sn ? 3 : 2;
        auto nextArg = [&]() -> uint32_t { uint32_t v = (argi <= 3) ? r[argi] : readWord(r[13] + 4u * (uint32_t)(argi - 4)); argi++; return v; };
        std::string outp = cFormat(*this, fmt, nextArg);
        if (!sn) { output += outp; ret((uint32_t)outp.size()); say("fprintf(f, \"" + fmt + "\", ...)", "Como printf (stdout y stderr salen por la misma consola)."); return true; }
        if (a1) { std::string t = outp.substr(0, a1 - 1); for (size_t k = 0; k < t.size(); k++) writeByte(a0 + (uint32_t)k, (uint8_t)t[k], &rec); writeByte(a0 + (uint32_t)t.size(), 0, &rec); }
        ret((uint32_t)outp.size()); say("snprintf(buf, n, \"" + fmt + "\", ...)", "Como sprintf pero escribe como mucho n-1 caracteres."); return true;
    }
    if (fn == "time") { uint32_t t = 1700000000u + (uint32_t)steps; if (a0) writeWord(a0, t, &rec); ret(t); say("time(NULL)", "Hora simulada (segundos); crece con las instrucciones ejecutadas."); return true; }
    if (fn == "fflush") { ret(0); say("fflush(f)", "Vacia el bufer de salida: el simulador ya escribe todo al momento."); return true; }
    if (fn == "fgets") {
        if (!hasPendingLine() || a1 == 0) { ret(0); say("fgets(buf, n, stdin)", "Fin de la entrada: devuelve NULL."); return true; }
        std::string line = takeLine();
        if (line.size() > a1 - 1) line.resize(a1 - 1); else line += '\n';
        for (size_t k = 0; k < line.size(); k++) writeByte(a0 + (uint32_t)k, (uint8_t)line[k], &rec);
        writeByte(a0 + (uint32_t)line.size(), 0, &rec);
        ret(a0); say("fgets(buf, n, stdin)", "Lee una linea de la consola (con su salto de linea, maximo n-1 caracteres) y la guarda en R0.");
        return true;
    }
    if (fn == "free") { say("free(p)", "Libera memoria (el simulador no la reutiliza)."); return true; }
    if (fn == "srand") { randState_ = a0; say("srand(R0)", "Fija la semilla de rand()."); return true; }
    if (fn == "rand") {
        randState_ = randState_ * 1103515245u + 12345u; ret((randState_ >> 16) & 0x7FFF);
        say("rand()", "Numero pseudoaleatorio (generador propio del simulador; no coincide con el de glibc)."); return true;
    }
    return false;
}

// Simula el efecto de una llamada "BL printf/scanf/puts/gets/strlen" (convenio AAPCS:
// argumentos en R0..R3; printf/scanf usan R0 como formato y R1..R3 como argumentos).
void Machine::runLibraryCall(const std::string& fn, StepRecord& rec) {
    lastWasLibraryCall = true;
    if (runExtraLibraryCall(fn, rec)) return;
    if (fn == "printf" || fn == "putchar" || fn == "exit" || fn == "atoi" || fn == "getchar") {
        if (fn == "putchar") {
            output += (char)(r[0] & 0xFF);
            rec.sym = "putchar(" + std::to_string(r[0] & 0xFF) + ")";
            rec.text = "Escribe el caracter de R0 por consola; devuelve ese mismo caracter en R0.";
            r[0] &= 0xFF;
            return;
        }
        if (fn == "getchar") {
            uint32_t old = r[0]; if (hasPendingChar()) r[0] = (uint8_t)takeChar(); else r[0] = 0xFFFFFFFFu; rec.regs.push_back({0, old, r[0]});
            rec.sym = "getchar()"; rec.text = "Lee un caracter de la consola y lo devuelve en R0.";
            return;
        }
        if (fn == "atoi") {
            std::string s = readCString(r[0]); uint32_t old = r[0]; r[0] = (uint32_t)(int32_t)std::atoi(s.c_str()); rec.regs.push_back({0, old, r[0]});
            rec.sym = "atoi(\"" + s + "\")"; rec.text = "Convierte la cadena apuntada por R0 a entero (devuelto en R0).";
            return;
        }
        if (fn == "exit") {
            exitCode = r[0]; halt("El programa ha terminado con exit(" + std::to_string((int32_t)r[0]) + ").", "exit");
            rec.sym = "exit(R0)"; rec.text = "Termina el programa."; return;
        }
        // printf: argumentos en R1-R3 y, a partir del cuarto, en la pila (convenio AAPCS)
        std::string fmt = readCString(r[0]);
        int argi = 1;
        auto nextArg = [&]() -> uint32_t { uint32_t v = (argi <= 3) ? r[argi] : readWord(r[13] + 4u * (uint32_t)(argi - 4)); argi++; return v; };
        std::string outp = cFormat(*this, fmt, nextArg);
        output += outp;
        uint32_t old = r[0]; r[0] = (uint32_t)outp.size(); rec.regs.push_back({0, old, r[0]});
        rec.sym = "printf(\"" + fmt + "\", ...)";
        rec.num = "escribe " + std::to_string(outp.size()) + " caracteres";
        rec.text = "Llama a printf: interpreta el formato de R0 y los argumentos de R1-R3 (y de la pila a partir del cuarto): %d %i %u %x %X %o %c %s %p %% con anchura/relleno (%5d, %02x, %-8s...), y escribe el resultado por consola.";
    } else if (fn == "scanf") {
        std::string fmt = readCString(r[0]);
        std::vector<ScanItem> items; size_t np = inputPos_; bool more = false;
        int filled = scanfParse(input_, inputPos_, fmt, items, np, more);
        int argi = 1;
        auto nextPtr = [&]() -> uint32_t { uint32_t v = (argi <= 3) ? r[argi] : readWord(r[13] + 4u * (uint32_t)(argi - 4)); argi++; return v; };
        std::string got;
        for (auto& it : items) {
            uint32_t dest = nextPtr();
            if (it.type == 's') { for (size_t k = 0; k < it.str.size(); k++) writeByte(dest + (uint32_t)k, (uint8_t)it.str[k], &rec); writeByte(dest + (uint32_t)it.str.size(), 0, &rec); got += " \"" + it.str + "\""; }
            else if (it.type == 'c') { writeByte(dest, (uint8_t)it.str[0], &rec); got += " '" + it.str + "'"; }
            else if (it.size == 1) { writeByte(dest, (uint8_t)it.num, &rec); got += " " + std::to_string(it.num); }
            else if (it.size == 2) { writeByte(dest, (uint8_t)it.num, &rec); writeByte(dest + 1, (uint8_t)(it.num >> 8), &rec); got += " " + std::to_string(it.num); }
            else { writeWord(dest, (uint32_t)it.num, &rec); got += " " + std::to_string(it.num); }
        }
        std::string consumed = input_.substr(inputPos_, np - inputPos_);
        inputPos_ = np;
        int32_t result = filled; if (filled == 0 && np >= input_.size() && eof_) result = -1;   // EOF
        uint32_t old = r[0]; r[0] = (uint32_t)result; rec.regs.push_back({0, old, (uint32_t)result});
        rec.sym = "scanf(\"" + fmt + "\", ...)";
        std::string vis; for (char ch : consumed) { if (ch == '\n') vis += "\\n"; else vis += ch; }
        rec.num = "consume \"" + vis + "\"";
        rec.text = "Lee de la entrada solo lo que casa con el formato y guarda el resultado en los punteros R1-R3" + (got.empty() ? std::string(" (no se pudo leer ningun valor)") : ":" + got) + ". R0 = numero de valores leidos (" + std::to_string(result) + "). Lo que sobra queda para la siguiente lectura.";
    } else if (fn == "puts") {
        std::string s = readCString(r[0]);
        output += s + "\n";
        uint32_t old = r[0]; r[0] = (uint32_t)(s.size() + 1); rec.regs.push_back({0, old, r[0]});
        rec.sym = "puts(\"" + s + "\")";
        rec.text = "Escribe la cadena apuntada por R0 y un salto de linea por consola.";
    } else if (fn == "gets") {
        if (!hasPendingLine()) { uint32_t old = r[0]; r[0] = 0; rec.regs.push_back({0, old, 0}); rec.sym = "gets(buf)"; rec.text = "Fin de la entrada: devuelve NULL."; return; }
        std::string line = takeLine(); uint32_t a = r[0];
        for (size_t k = 0; k < line.size(); k++) writeByte(a + (uint32_t)k, (uint8_t)line[k], &rec);
        writeByte(a + (uint32_t)line.size(), 0, &rec);
        rec.sym = "gets(buf)";
        rec.num = "\"" + line + "\"";
        rec.text = "Lee una linea completa de la consola y la guarda (con un 0 final) en el buffer apuntado por R0, sin comprobar su tamano (como el gets() real: cuidado con el desbordamiento).";
    } else if (fn == "strlen") {
        uint32_t a = r[0]; uint32_t n = (uint32_t)readCString(a).size();
        uint32_t old = r[0]; r[0] = n; rec.regs.push_back({0, old, n});
        rec.sym = "strlen(...)";
        rec.text = "Calcula la longitud de la cadena apuntada por R0 (sin contar el 0 final): " + std::to_string(n) + ".";
    }
}

std::optional<StepRecord> Machine::step() {
    if (halted) return std::nullopt;
    auto it = prog_.byAddr.find(pc);
    if (it == prog_.byAddr.end()) { halt("PC (" + hx0x(pc) + ") no apunta a ninguna instruccion del programa.", "error"); return std::nullopt; }
    const Instruction& ins = it->second;

    // Las llamadas SWI de lectura pueden bloquear la ejecucion si no hay entrada disponible todavia.
    if (ins.t == IType::SWI && condPass(ins.cond, f)) {
        bool isCharRead = (ins.swiNum == 0x31);
        bool isLineRead = (ins.swiNum == 0x30 || ins.swiNum == 0x32);
        if (isCharRead && !hasPendingChar()) { halt("Esperando un caracter de entrada (SWI 0x31). Escribelo en la consola.", "input"); return std::nullopt; }
        if (isLineRead && !hasPendingLine()) { halt("Esperando una linea de entrada (SWI " + hx0x(ins.swiNum,2) + "). Escribela en la consola y pulsa Enter.", "input"); return std::nullopt; }
    }
    // Lo mismo para "BL scanf" / "BL gets": si el destino del salto es una funcion
    // de biblioteca de lectura y no hay una linea completa todavia, bloquea ANTES
    // de tocar nada (ni siquiera LR), para que al reintentar se re-ejecute limpio.
    if (ins.t == IType::BR && condPass(ins.cond, f)) {
        uint32_t tgt = (uint32_t)((int32_t)(pc + 8) + ins.branchOffsetWords * 4);
        const char* fn = libraryFunctionName(tgt);
        if (fn && std::string(fn) == "scanf" && !eof_ && scanfNeedsMore(readCString(r[0]))) {
            halt("Esperando datos de entrada para scanf(). Escribelos en la consola y pulsa Enter.", "input");
            return std::nullopt;
        }
        if (fn && std::string(fn) == "getchar" && !hasPendingChar() && !eof_) {
            halt("Esperando un caracter de entrada para getchar(). Escribelo en la consola.", "input");
            return std::nullopt;
        }
        if (fn && (std::string(fn) == "gets" || std::string(fn) == "fgets") && !hasPendingLine() && !eof_) {
            halt("Esperando una linea de entrada para " + std::string(fn) + "(). Escribela en la consola y pulsa Enter.", "input");
            return std::nullopt;
        }
    }

    lastWasLibraryCall = false;
    Snapshot snap; snap.r = r; snap.pc = pc; snap.f = f; snap.outLenBefore = (long)output.size();
    StepRecord rec; rec.addr = pc; rec.ins = ins; rec.pcOld = pc; rec.flagsOld = f;

    uint32_t pcPlus8 = pc + 8;
    bool executed = condPass(ins.cond, f);
    rec.executed = executed;
    uint32_t nextPc = pc + 4;

    if (!executed) {
        rec.text = "La condicion " + std::string(condName(ins.cond)) + " no se cumple: la instruccion no tiene efecto.";
    } else {
        switch (ins.t) {
        case IType::DP: {
            uint32_t rnv = (ins.rn==15)? pcPlus8 : (ins.rn>=0? r[ins.rn] : 0);
            bool carryIn = f.C, carryOut = f.C;
            uint32_t op2 = 0;
            if (ins.op2.isImm) { op2 = ins.op2.imm; ImmEnc e = encodeImm(ins.op2.imm); if (e.ok && e.rot!=0) carryOut = (op2>>31)&1; else carryOut = carryIn; }
            else {
                uint32_t rv = (ins.op2.reg==15)? pcPlus8 : r[ins.op2.reg];
                int n = ins.op2.rrx? 0 : (ins.op2.shiftIsReg? (int)((ins.op2.shiftReg==15?pcPlus8:r[ins.op2.shiftReg]) & 0xFF) : ins.op2.shiftAmount);
                int type = ins.op2.rrx? 3 : ins.op2.shiftType;
                op2 = applyShift(type, rv, n, carryOut, carryIn, ins.op2.shiftIsReg);
            }
            uint64_t result=0; bool doWrite=true; uint32_t vBefore=rnv;
            bool newC=carryOut, newV=f.V;
            std::string opWord;
            switch (dpOpcode(ins.mnemonic)) {
                case 0: result = rnv & op2; opWord="AND"; break;
                case 1: result = rnv ^ op2; opWord="EOR"; break;
                case 2: { uint64_t s = (uint64_t)rnv + (uint64_t)(uint32_t)(~op2) + 1; result=(uint32_t)s; newC = s>0xFFFFFFFFull; newV = (((rnv^op2)&(rnv^(uint32_t)result))>>31)&1; opWord="SUB"; break; }
                case 3: { uint64_t s = (uint64_t)op2 + (uint64_t)(uint32_t)(~rnv) + 1; result=(uint32_t)s; newC = s>0xFFFFFFFFull; newV = (((op2^rnv)&(op2^(uint32_t)result))>>31)&1; opWord="RSB"; break; }
                case 4: { uint64_t s=(uint64_t)rnv+(uint64_t)op2; result=(uint32_t)s; newC=s>0xFFFFFFFFull; newV=((~(rnv^op2)) & (rnv^(uint32_t)result))>>31 & 1; opWord="ADD"; break; }
                case 5: { uint64_t s=(uint64_t)rnv+(uint64_t)op2+(carryIn?1:0); result=(uint32_t)s; newC=s>0xFFFFFFFFull; newV=((~(rnv^op2)) & (rnv^(uint32_t)result))>>31 & 1; opWord="ADC"; break; }
                case 6: { uint64_t s=(uint64_t)rnv+(uint64_t)(uint32_t)(~op2)+(carryIn?1:0); result=(uint32_t)s; newC=s>0xFFFFFFFFull; newV=(((rnv^op2)&(rnv^(uint32_t)result))>>31)&1; opWord="SBC"; break; }
                case 7: { uint64_t s=(uint64_t)op2+(uint64_t)(uint32_t)(~rnv)+(carryIn?1:0); result=(uint32_t)s; newC=s>0xFFFFFFFFull; newV=(((op2^rnv)&(op2^(uint32_t)result))>>31)&1; opWord="RSC"; break; }
                case 8: result = rnv & op2; doWrite=false; opWord="TST"; break;
                case 9: result = rnv ^ op2; doWrite=false; opWord="TEQ"; break;
                case 10: { uint64_t s=(uint64_t)rnv+(uint64_t)(uint32_t)(~op2)+1; result=(uint32_t)s; newC=s>0xFFFFFFFFull; newV=(((rnv^op2)&(rnv^(uint32_t)result))>>31)&1; doWrite=false; opWord="CMP"; break; }
                case 11: { uint64_t s=(uint64_t)rnv+(uint64_t)op2; result=(uint32_t)s; newC=s>0xFFFFFFFFull; newV=((~(rnv^op2))&(rnv^(uint32_t)result))>>31&1; doWrite=false; opWord="CMN"; break; }
                case 12: result = rnv | op2; opWord="ORR"; break;
                case 13: result = op2; opWord="MOV"; break;
                case 14: result = rnv & ~op2; opWord="BIC"; break;
                case 15: result = ~op2; opWord="MVN"; break;
                default: result=0;
            }
            uint32_t res32 = (uint32_t)result;
            if (doWrite && ins.rd>=0) {
                if (ins.rd==15) { nextPc = res32 & ~3u; rec.extra.push_back("Escribir en PC provoca un salto a " + hx0x(nextPc)); }
                else { uint32_t old = r[ins.rd]; r[ins.rd] = res32; rec.regs.push_back({ins.rd, old, res32}); }
            }
            if (ins.s) {
                if (ins.rd==15) { }
                else { f.N = (res32>>31)&1; f.Z = res32==0; f.C = newC; f.V = newV; }
            }
            if (isMonoOp(ins.mnemonic)) rec.sym = regLabel(ins.rd) + " <- " + opWord + " " + (ins.op2.isImm? hx0x(op2) : regLabel(ins.op2.reg));
            else if (isTestOp(ins.mnemonic)) rec.sym = opWord + " " + regLabel(ins.rn) + ", " + (ins.op2.isImm? hx0x(op2):regLabel(ins.op2.reg));
            else rec.sym = regLabel(ins.rd) + " <- " + regLabel(ins.rn) + " " + opWord + " " + (ins.op2.isImm? hx0x(op2):regLabel(ins.op2.reg));
            rec.num = doWrite? (hx0x(vBefore)+" -> "+hx0x(res32)) : (hx0x(vBefore)+" (solo flags)");
            rec.text = doWrite? ("Calcula " + opWord + " y guarda el resultado en " + regLabel(ins.rd) + ".") : ("Calcula " + opWord + " y solo actualiza los flags.");
            break;
        }
        case IType::MEM: {
            uint32_t base = (ins.mem.baseReg==15)? pcPlus8 : r[ins.mem.baseReg];
            uint32_t offv;
            if (ins.mem.offsetIsReg) { uint32_t rv=(ins.mem.offsetReg==15)?pcPlus8:r[ins.mem.offsetReg]; bool co; offv = applyShift(ins.mem.offShiftType, rv, ins.mem.offShiftAmount, co, f.C, false); }
            else offv = (uint32_t)ins.mem.offsetImm;
            uint32_t effAddr = ins.mem.preIndexed ? (ins.mem.add? base+offv : base-offv) : base;
            if (ins.load) {
                uint32_t val;
                if (ins.size==1) { uint8_t b = readByte(effAddr); val = ins.signedLoad? (uint32_t)(int32_t)(int8_t)b : (uint32_t)b; }
                else if (ins.size==2) { uint32_t h = readByte(effAddr) | (readByte(effAddr+1)<<8); val = ins.signedLoad? (uint32_t)(int32_t)(int16_t)h : h; }
                else val = readWord(effAddr);
                uint32_t old = r[ins.rd]; r[ins.rd]=val; rec.regs.push_back({ins.rd, old, val});
                rec.sym = regLabel(ins.rd) + " <- memoria[" + hx0x(effAddr) + "]";
                rec.num = hx0x(old) + " -> " + hx0x(val);
                rec.text = "Carga " + std::string(ins.size==1?"un byte":ins.size==2?"una media palabra":"una palabra") + " desde " + hx0x(effAddr) + " en " + regLabel(ins.rd) + ".";
            } else {
                uint32_t val = (ins.rd==15)? pc+8 : r[ins.rd];
                if (ins.size==1) writeByte(effAddr, (uint8_t)(val&0xFF), &rec);
                else if (ins.size==2) { writeByte(effAddr,(uint8_t)(val&0xFF),&rec); writeByte(effAddr+1,(uint8_t)((val>>8)&0xFF),&rec); }
                else writeWord(effAddr, val, &rec);
                rec.sym = "memoria[" + hx0x(effAddr) + "] <- " + regLabel(ins.rd);
                rec.num = hx0x(val);
                rec.text = "Guarda " + regLabel(ins.rd) + " (" + hx0x(val) + ") en " + hx0x(effAddr) + ".";
            }
            if (ins.mem.writeBack) {
                uint32_t newBase = ins.mem.postIndexed ? (ins.mem.add? base+offv : base-offv) : effAddr;
                uint32_t old = r[ins.mem.baseReg]; r[ins.mem.baseReg]=newBase;
                if (old!=newBase) rec.regs.push_back({ins.mem.baseReg, old, newBase});
                rec.extra.push_back(regLabel(ins.mem.baseReg) + " actualizado a " + hx0x(newBase));
            }
            break;
        }
        case IType::BR: {
            if (ins.link) { uint32_t old=r[14]; r[14]=pc+4; rec.regs.push_back({14, old, r[14]}); }
            nextPc = (uint32_t)((int32_t)pcPlus8 + ins.branchOffsetWords*4);
            if (const char* fn = libraryFunctionName(nextPc)) {
                // El destino es una funcion de biblioteca simulada (printf, scanf...):
                // se ejecuta su efecto aqui mismo y se vuelve de inmediato a LR, como si
                // la funcion hubiera hecho ya su propio "BX LR". Si no se enlazo (era un
                // "B" en vez de "BL"), se vuelve a donde apuntara LR previamente.
                runLibraryCall(fn, rec);
                nextPc = r[14];
            } else {
                rec.sym = (ins.link? std::string("LR <- ")+hx0x(pc+4)+"; ":"") + "PC <- " + hx0x(nextPc);
                rec.text = ins.link? "Guarda la direccion de retorno en LR y salta." : "Salta a la direccion indicada.";
            }
            break;
        }
        case IType::BX: {
            nextPc = r[ins.rm] & ~1u;
            rec.sym = "PC <- " + regLabel(ins.rm);
            rec.text = "Salta a la direccion contenida en " + regLabel(ins.rm) + ".";
            break;
        }
        case IType::MUL: {
            bool mla = ins.mnemonic=="MLA";
            uint32_t res = r[ins.rm]*r[ins.rs] + (mla? r[ins.rn] : 0);
            uint32_t old=r[ins.rd]; r[ins.rd]=res; rec.regs.push_back({ins.rd, old, res});
            if (ins.s) { f.N=(res>>31)&1; f.Z=res==0; }
            rec.sym = regLabel(ins.rd) + " <- " + regLabel(ins.rm) + " * " + regLabel(ins.rs) + (mla? (" + "+regLabel(ins.rn)) : "");
            rec.num = hx0x(old) + " -> " + hx0x(res);
            rec.text = "Multiplica" + std::string(mla?" y acumula":"") + " (solo los 32 bits bajos).";
            break;
        }
        case IType::MULL: {
            bool uns = ins.mnemonic[0]=='U';
            int64_t r64;
            if (uns) r64 = (int64_t)((uint64_t)r[ins.rm] * (uint64_t)r[ins.rs]);
            else r64 = (int64_t)((int64_t)(int32_t)r[ins.rm] * (int64_t)(int32_t)r[ins.rs]);
            uint32_t lo = (uint32_t)((uint64_t)r64 & 0xFFFFFFFFu), hi = (uint32_t)((uint64_t)r64 >> 32);
            uint32_t oldLo=r[ins.rdLo], oldHi=r[ins.rdHi]; r[ins.rdLo]=lo; r[ins.rdHi]=hi;
            rec.regs.push_back({ins.rdLo, oldLo, lo}); rec.regs.push_back({ins.rdHi, oldHi, hi});
            if (ins.s) { f.N=(hi>>31)&1; f.Z=(lo==0 && hi==0); }
            rec.sym = regLabel(ins.rdHi)+":"+regLabel(ins.rdLo) + " <- " + regLabel(ins.rm) + " * " + regLabel(ins.rs);
            rec.num = hx0x(hi)+":"+hx0x(lo);
            rec.text = std::string("Multiplicacion de 64 bits ") + (uns?"sin signo.":"con signo.");
            break;
        }
        case IType::BLK: {
            bool P,U; translateBlockMode(ins.blkMode, ins.load, P, U);
            int count=0; for (int i=0;i<16;i++) if (ins.regList & (1u<<i)) count++;
            uint32_t base = r[ins.blkBase];
            uint32_t addr0 = U ? base : base - (uint32_t)count*4;
            uint32_t cur = addr0 + ((P == U) ? 4u : 0u);
            std::vector<std::string> names;
            for (int i=0;i<16;i++) {
                if (!(ins.regList & (1u<<i))) continue;
                if (ins.load) { uint32_t v=readWord(cur); uint32_t old=r[i]; r[i]=v; if(old!=v) rec.regs.push_back({i,old,v}); if (i==15) nextPc = v & ~3u; }
                else { uint32_t v=(i==15)?pc+8:r[i]; writeWord(cur, v, &rec); }
                names.push_back(regLabel(i));
                cur += 4;
            }
            uint32_t newBase = U ? base + (uint32_t)count*4 : base - (uint32_t)count*4;
            if (ins.blkWb && !(ins.load && (ins.regList & (1u<<ins.blkBase)))) { uint32_t oldBase = r[ins.blkBase]; r[ins.blkBase]=newBase; if (oldBase!=newBase) rec.regs.push_back({ins.blkBase, oldBase, newBase}); }
            std::string list; for (size_t i=0;i<names.size();i++){ if(i) list+=", "; list+=names[i]; }
            rec.sym = (ins.load? std::string("carga "):std::string("guarda ")) + "{" + list + "}";
            rec.text = ins.isStack ? (ins.load? "POP: recupera los registros y avanza SP." : "PUSH: guarda los registros y retrocede SP.") : (ins.load? "Carga multiple desde memoria (base " + regLabel(ins.blkBase) + (ins.blkWb?" con actualizacion).":")."):"Almacenamiento multiple en memoria (base " + regLabel(ins.blkBase) + (ins.blkWb?" con actualizacion).":")."));
            break;
        }
        case IType::SWI: {
            uint32_t n = ins.swiNum;
            if (n == 0x11 || n == 0x18) { halt("El programa ha terminado con SWI " + hx0x(n) + ".", "exit"); rec.text = "Fin del programa."; }
            else if (n == 0x00 || n == 0x20) { char ch = (char)(r[0] & 0xFF); output += ch; rec.text = "Imprime el caracter " + hx0x((uint8_t)ch,2) + " (R0) por consola."; }
            else if (n == 0x21) { std::string s = std::to_string((int32_t)r[0]); output += s; rec.text = "Imprime R0 como entero con signo: " + s + "."; }
            else if (n == 0x22) { std::string s = std::to_string(r[0]); output += s; rec.text = "Imprime R0 como entero sin signo: " + s + "."; }
            else if (n == 0x23) { std::string s = hx0x(r[0]); output += s; rec.text = "Imprime R0 en hexadecimal: " + s + "."; }
            else if (n == 0x24) {
                uint32_t a = r[0]; std::string s; for (int i = 0; i < 8192; i++) { uint8_t b = readByte(a + i); if (!b) break; s += (char)b; }
                output += s; rec.text = "Imprime la cadena terminada en cero que empieza en " + hx0x(a) + " (" + std::to_string(s.size()) + " caracteres).";
            }
            else if (n == 0x25) { output += '\n'; rec.text = "Imprime un salto de linea."; }
            else if (n == 0x6B) { std::string s = std::to_string((int32_t)r[1]); output += s; rec.text = "Imprime R1 (" + s + ")."; }
            else if (n == 0x30) {
                std::string line = takeLine(); long v = std::strtol(line.c_str(), nullptr, 0);
                uint32_t old = r[0]; r[0] = (uint32_t)(int32_t)v; rec.regs.push_back({0, old, r[0]});
                rec.text = "Lee un entero de la consola (\"" + line + "\") y lo guarda en R0.";
            }
            else if (n == 0x31) {
                char ch = takeChar(); uint32_t old = r[0]; r[0] = (uint8_t)ch; rec.regs.push_back({0, old, r[0]});
                rec.text = std::string("Lee un caracter de la consola ('") + ch + "') y lo guarda en R0.";
            }
            else if (n == 0x32) {
                std::string line = takeLine(); uint32_t buf = r[1], maxLen = r[2];
                uint32_t n2 = maxLen > 0 ? std::min((uint32_t)line.size(), maxLen - 1) : 0;
                for (uint32_t i = 0; i < n2; i++) writeByte(buf + i, (uint8_t)line[i], &rec);
                writeByte(buf + n2, 0, &rec);
                uint32_t old = r[0]; r[0] = n2; rec.regs.push_back({0, old, n2});
                rec.text = "Lee una linea de la consola (\"" + line + "\"), la guarda en [" + hx0x(buf) + "] y pone su longitud (" + std::to_string(n2) + ") en R0.";
            }
            else rec.text = "SWI " + hx0x(n) + " (sin efecto simulado).";
            rec.sym = "SWI " + hx0x(n);
            break;
        }
        case IType::NOP: rec.text = "No hace nada."; rec.sym="NOP"; break;
        default: rec.text = "Instruccion no soportada por el emulador."; rec.fault = true; break;
        }
    }

    rec.pcNew = nextPc; rec.flagsNew = f;
    pc = nextPc;
    steps++;

    if (keepHistory) {
        snap.after = rec;
        history.push_back(std::move(snap));
        if ((long)history.size() > 500) history.pop_front();
    }

    if (!halted) {
        if (steps >= stepLimit) halt("Limite de " + std::to_string(stepLimit) + " instrucciones alcanzado (bucle infinito?).", "limit");
        else if (prog_.byAddr.find(pc) == prog_.byAddr.end()) {
            if (pc >= prog_.textEnd) halt("El programa ha llegado al final del codigo.", "end");
            // Patron habitual en los apuntes: "main" termina con POP+BX LR sin que
            // nadie haya hecho BL a main (no hay un invocador real en el simulador).
            // Como LR vale 0 al arrancar, PC=0 aqui significa "main ha vuelto", no un
            // fallo: se trata como fin de programa en vez de un error de ejecucion.
            else if (pc == 0) halt("El programa ha vuelto a LR = 0 (main termino con BX LR sin que nadie le hiciera BL): fin de la ejecucion.", "exit");
            else halt("PC (" + hx0x(pc) + ") no apunta a ninguna instruccion.", "error");
        }
    }
    return rec;
}

bool Machine::back() {
    if (history.empty()) return false;
    Snapshot snap = std::move(history.back()); history.pop_back();
    r = snap.r; pc = snap.pc; f = snap.f;
    for (auto rit = snap.after.mem.rbegin(); rit != snap.after.mem.rend(); ++rit) mem[rit->addr] = rit->oldVal;
    output.resize((size_t)snap.outLenBefore);
    steps--;
    halted = false; haltReason.clear(); haltKind.clear();
    return true;
}

void Machine::run(int maxSteps, const std::vector<uint32_t>* breakpoints) {
    for (int i=0;i<maxSteps && !halted;i++) {
        if (breakpoints && i>0) {
            bool atBp = std::find(breakpoints->begin(), breakpoints->end(), pc) != breakpoints->end();
            if (atBp) break;
        }
        step();
    }
}

} // namespace arm
