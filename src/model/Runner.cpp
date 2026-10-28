// model/Runner.cpp — ejecución sin interfaz (ver Runner.h)
#include "Runner.h"
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace arm {

bool parseCallArg(const std::string& t, CallArg& out, std::string& error) {
    auto splitNums = [&](const std::string& s, std::vector<int64_t>& v) -> bool {
        std::stringstream ss(s); std::string tok;
        while (std::getline(ss, tok, ',')) {
            size_t a = tok.find_first_not_of(" \t"); if (a == std::string::npos) continue;
            tok = tok.substr(a);
            char* e = nullptr; long long x = std::strtoll(tok.c_str(), &e, 0);
            if (e == tok.c_str() || *e != '\0') { error = "numero no valido en el argumento: '" + tok + "'"; return false; }
            v.push_back(x);
        }
        return true;
    };
    if (t.rfind("s:", 0) == 0) { out.kind = CallArg::Str; out.text = t.substr(2); return true; }
    if (t.rfind("w:", 0) == 0) { std::vector<int64_t> v; if (!splitNums(t.substr(2), v)) return false; out.kind = CallArg::Words; for (auto x : v) out.words.push_back((uint32_t)x); return true; }
    if (t.rfind("b:", 0) == 0) { std::vector<int64_t> v; if (!splitNums(t.substr(2), v)) return false; out.kind = CallArg::Bytes; for (auto x : v) out.bytes.push_back((uint8_t)x); return true; }
    if (t.size() == 3 && t[0] == '\'' && t[2] == '\'') { out.kind = CallArg::Int; out.value = (unsigned char)t[1]; return true; }
    char* e = nullptr; long long x = std::strtoll(t.c_str(), &e, 0);
    if (t.empty() || e == t.c_str() || *e != '\0') { error = "argumento no valido: '" + t + "' (usa un entero, s:texto, w:1,2,3 o b:1,2,3)"; return false; }
    out.kind = CallArg::Int; out.value = x; return true;
}

static void dumpRegs(const Machine& m, std::ostream& d) {
    for (int i = 0; i < 16; i++) {
        uint32_t v = i == 15 ? m.pc : m.r[i];
        d << "  " << regLabel(i) << (regLabel(i).size() < 3 ? " " : "") << " = " << hx0x(v) << "  (" << (int32_t)v << ")\n";
    }
    d << "  flags: N=" << m.f.N << " Z=" << m.f.Z << " C=" << m.f.C << " V=" << m.f.V << "\n";
}

RunResult runProgram(const Program& prog, const RunOptions& opt, std::istream& in, std::ostream& out, std::ostream& diag) {
    RunResult res;
    if (!prog.ok) { res.error = "el programa tiene errores de ensamblado"; return res; }
    Machine m(prog);
    m.stepLimit = opt.maxSteps; m.keepHistory = false;
    if (m.halted) { res.error = m.haltReason; res.haltKind = m.haltKind; return res; }

    // Punto de entrada
    auto ent = prog.symbols.find(opt.entry);
    if (ent == prog.symbols.end() || !prog.byAddr.count(ent->second)) {
        if (opt.entry != "main" || opt.callStyle) { res.error = "no existe la etiqueta de codigo '" + opt.entry + "'"; return res; }
        // sin "main": se empieza en la primera instruccion (comportamiento clasico del simulador)
    } else m.pc = ent->second;

    // Llamada estilo C: argumentos en R0..R3 (el resto en la pila), copiando cadenas/arrays a memoria
    if (opt.callStyle) {
        uint32_t scratch = opt.scratchBase;
        auto place = [&](const void* data, size_t n) -> uint32_t {
            uint32_t a = scratch;
            for (size_t i = 0; i < n; i++) m.mem[a + (uint32_t)i] = ((const uint8_t*)data)[i];
            scratch += (uint32_t)((n + 4) & ~3u); return a;
        };
        std::vector<uint32_t> vals;
        for (const CallArg& a : opt.args) {
            switch (a.kind) {
                case CallArg::Int:   vals.push_back((uint32_t)a.value); break;
                case CallArg::Str:   vals.push_back(place(a.text.c_str(), a.text.size() + 1)); break;
                case CallArg::Words: { std::vector<uint8_t> b; for (uint32_t w : a.words) for (int k = 0; k < 4; k++) b.push_back((uint8_t)(w >> (8 * k))); vals.push_back(place(b.data(), b.size())); break; }
                case CallArg::Bytes: vals.push_back(place(a.bytes.data(), a.bytes.size())); break;
            }
        }
        for (size_t i = 0; i < vals.size() && i < 4; i++) m.r[i] = vals[i];
        if (vals.size() > 4) {   // argumentos 5.. en la pila (el 5.º en [SP])
            uint32_t extra = (uint32_t)(vals.size() - 4);
            m.r[13] -= 4 * extra; if (m.r[13] & 4) m.r[13] -= 4;      // SP alineado a 8 (AAPCS)
            for (uint32_t i = 0; i < extra; i++) for (int k = 0; k < 4; k++) m.mem[m.r[13] + 4 * i + k] = (uint8_t)(vals[4 + i] >> (8 * k));
        }
    }
    m.r[14] = 0;   // LR = 0: "volver" a 0 se interpreta como "main/la rutina ha terminado" (ver Machine::step)

    size_t flushed = 0;
    auto flush = [&]() { if (m.output.size() > flushed) { out << m.output.substr(flushed); out.flush(); flushed = m.output.size(); } };
    std::istringstream sin(opt.stdinText);

    while (true) {
        if (m.halted) {
            flush();
            if (m.haltKind != "input") break;
            std::string line; std::istream& src = opt.useStdinText ? (std::istream&)sin : in;
            if (!std::getline(src, line)) { res.error = "el programa pide entrada pero no hay mas datos (fin de la entrada)"; break; }
            m.feedInput(line + "\n"); continue;
        }
        auto rec = m.step();
        if (opt.trace && rec) {
            diag << hx0x(rec->addr) << "  " << (rec->executed ? " " : "x") << " " << rec->ins.src;
            if (!rec->sym.empty() && rec->executed) diag << "    ; " << rec->sym << "  [" << rec->num << "]";
            diag << "\n";
        }
        if (m.output.size() - flushed > 4096) flush();
    }
    flush();

    res.output = m.output; res.steps = m.steps; res.r0 = m.r[0];
    res.haltKind = m.haltKind; res.haltReason = m.haltReason;
    if (m.haltKind == "exit" && m.exitCode) res.r0 = m.exitCode;
    if (m.haltKind == "error" || m.haltKind == "limit") { if (res.error.empty()) res.error = m.haltReason; }
    else if (m.haltKind == "input" && res.error.empty()) res.error = m.haltReason;
    res.ok = res.error.empty();

    if (opt.showRegs) { diag << "\n[registros finales tras " << m.steps << " instrucciones]\n"; dumpRegs(m, diag); }
    if (opt.showReturn) {   // por la salida normal (no la de diagnostico) para poder capturarlo con $(...) o redirigirlo
        if (!res.output.empty() && res.output.back() != '\n') out << "\n";
        out << "Valor devuelto (R0) = " << (int32_t)res.r0 << "  (" << hx0x(res.r0) << ", sin signo " << res.r0 << ")\n";
    }
    for (auto& d : opt.dumps) {
        diag << "\n[memoria " << hx0x(d.first) << " .. +" << d.second << " bytes]\n";
        for (uint32_t off = 0; off < d.second; off += 16) {
            diag << "  " << hex(d.first + off) << ":";
            for (uint32_t k = 0; k < 16 && off + k < d.second; k++) diag << " " << hex(m.readByte(d.first + off + k), 2);
            diag << "\n";
        }
    }
    return res;
}

} // namespace arm
