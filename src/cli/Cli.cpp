// cli/Cli.cpp — ARM SIM sin interfaz gráfica.
// Equivale a "as + gcc + ./programa" de una Raspberry Pi, pero dentro del simulador:
//   armsim-cli programa.s                       ejecuta desde main (BL printf/scanf simulados)
//   armsim-cli programa.s --call reproduce 3    llama a UNA rutina como lo haría un main escrito en C
#include "Cli.h"
#include "model/Runner.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace arm;

static const char* HELP =
"ARM SIM sin interfaz — ensambla y ejecuta programas ARMv4 desde la terminal\n"
"\n"
"Uso:  armsim-cli [opciones] fichero.s [otro.s ...]\n"
"\n"
"Ejecución\n"
"  --entry ETQ         etiqueta donde empieza la ejecución (por defecto: main)\n"
"  --call RUTINA       llama a RUTINA como lo haría un programa en C: los argumentos que se\n"
"                      den con --arg van en R0..R3 y el resultado se lee de R0\n"
"  --arg VALOR         argumento de --call (se puede repetir). Formatos:\n"
"                        42  -7  0x1F  'A'   entero\n"
"                        s:hola              cadena (se pasa su puntero)\n"
"                        w:1,2,3             array de enteros de 32 bits (puntero)\n"
"                        b:1,2,3             array de bytes (puntero)\n"
"  --return            muestra el valor devuelto (R0) al terminar\n"
"  --input \"texto\"     texto que verá scanf/gets/SWI de lectura (\\n = nueva línea); sin esta opción\n"
"                      se lee del teclado (entrada estándar)\n"
"  --max-steps N       límite de instrucciones (por defecto 50000000)\n"
"\n"
"Depuración\n"
"  --trace             muestra cada instrucción ejecutada (en la salida de error)\n"
"  --regs              muestra los registros y flags al terminar\n"
"  --dump DIR[,N]      vuelca N bytes (16 por defecto) de memoria a partir de DIR al terminar\n"
"  --listing           muestra el listado ensamblado (dirección, código máquina, fuente) y sale\n"
"  --check             solo ensambla (informa de errores) y sale\n"
"  --symbols           muestra la tabla de símbolos y sale\n"
"\n"
"Mapa de memoria\n"
"  --text-base DIR  --data-base DIR  --stack-top DIR\n"
"\n"
"Códigos de salida: 0 correcto · 1 error de ensamblado/uso · 2 error de ejecución\n"
"\n"
"Ejemplos\n"
"  armsim-cli Pr2_Ejemplo1.s                       # equivale a: as/gcc + ./Pr2_Ejemplo1\n"
"  armsim-cli Pr2_Ejemplo2.s --input \"12\\n30\\n\"     # entrada simulada para scanf\n"
"  armsim-cli Ejercicio2.s --call reproduce --arg 3 --return\n";

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
}
static std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { char c = s[++i]; o += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == '\\' ? '\\' : (o += '\\', c); }
        else o += s[i];
    }
    return o;
}

bool cliWanted(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help" || a == "--run" || a == "--cli" || a == "--headless" || a == "--call" || a == "--check" || a == "--listing" || a == "--entry" || a == "--symbols") return true;
    }
    return false;
}

int cliMain(int argc, char** argv) {
    RunOptions ro; AsmOptions ao; std::vector<std::string> files;
    bool listing = false, check = false, symbols = false; std::string callName;
    auto need = [&](int& i, const char* opt) -> const char* { if (i + 1 >= argc) { std::cerr << "Falta el valor de " << opt << "\n"; return nullptr; } return argv[++i]; };
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i]; const char* v = nullptr;
        if (a == "-h" || a == "--help") { std::cout << HELP; return 0; }
        else if (a == "--run" || a == "--cli" || a == "--headless") {}
        else if (a == "--trace") ro.trace = true;
        else if (a == "--regs") ro.showRegs = true;
        else if (a == "--return") ro.showReturn = true;
        else if (a == "--listing") listing = true;
        else if (a == "--check") check = true;
        else if (a == "--symbols") symbols = true;
        else if (a == "--entry") { if (!(v = need(i, "--entry"))) return 1; ro.entry = v; }
        else if (a == "--call") { if (!(v = need(i, "--call"))) return 1; callName = v; ro.callStyle = true; ro.entry = v; ro.showReturn = true; }
        else if (a == "--arg") { if (!(v = need(i, "--arg"))) return 1; CallArg ca; std::string e; if (!parseCallArg(v, ca, e)) { std::cerr << e << "\n"; return 1; } ro.args.push_back(ca); }
        else if (a == "--input") { if (!(v = need(i, "--input"))) return 1; ro.stdinText = unescape(v); ro.useStdinText = true; }
        else if (a == "--max-steps") { if (!(v = need(i, "--max-steps"))) return 1; ro.maxSteps = std::atol(v); }
        else if (a == "--dump") { if (!(v = need(i, "--dump"))) return 1; std::string s = v; size_t c = s.find(','); uint32_t addr = (uint32_t)std::strtoul(s.substr(0, c).c_str(), nullptr, 0); uint32_t n = c == std::string::npos ? 16 : (uint32_t)std::strtoul(s.substr(c + 1).c_str(), nullptr, 0); ro.dumps.push_back({addr, n}); }
        else if (a == "--text-base") { if (!(v = need(i, "--text-base"))) return 1; ao.textBase = (uint32_t)std::strtoul(v, nullptr, 0); }
        else if (a == "--data-base") { if (!(v = need(i, "--data-base"))) return 1; ao.dataBase = (uint32_t)std::strtoul(v, nullptr, 0); }
        else if (a == "--stack-top") { if (!(v = need(i, "--stack-top"))) return 1; ao.stackTop = (uint32_t)std::strtoul(v, nullptr, 0); }
        else if (a.size() > 1 && a[0] == '-' && a != "-") { std::cerr << "Opcion desconocida: " << a << "  (usa --help)\n"; return 1; }
        else files.push_back(a);
    }
    if (files.empty()) { std::cerr << "Indica al menos un fichero .s  (usa --help)\n"; return 1; }

    // Varios ficheros = "enlazado" por concatenación; se recuerdan los límites para dar errores como fichero:línea
    std::string src; std::vector<std::pair<int,std::string>> starts;   // (primera línea global, fichero)
    int lineCount = 0;
    for (auto& f : files) {
        std::string t; if (!readFile(f, t)) { std::cerr << "No se pudo abrir " << f << "\n"; return 1; }
        if (!t.empty() && t.back() != '\n') t += '\n';
        starts.push_back({lineCount + 1, f}); src += t;
        lineCount += (int)std::count(t.begin(), t.end(), '\n');
    }
    auto where = [&](int line) { std::string f = starts[0].second; int base = 1; for (auto& s : starts) if (line >= s.first) { f = s.second; base = s.first; } return f + ":" + std::to_string(line - base + 1); };

    Program prog = assemble(src, ao);
    if (!prog.ok) {
        for (auto& e : prog.errors) { std::cerr << where(e.line) << ": error: " << e.msg << "\n"; if (!e.hint.empty()) std::cerr << "    sugerencia: " << e.hint << "\n"; }
        std::cerr << prog.errors.size() << " error(es) de ensamblado\n";
        return 1;
    }
    if (listing) {
        for (auto& l : prog.listing) std::cout << hx0x(l.addr) << "  " << hex(l.word) << "  " << l.src << "\n";
        return 0;
    }
    if (symbols) { for (auto& kv : prog.symbols) if (kv.second < LIB_BASE) std::cout << hx0x(kv.second) << "  " << kv.first << "\n"; return 0; }
    if (check) { std::cout << "Ensamblado correcto: " << prog.nInstr << " instrucciones\n"; return 0; }

    RunResult r = runProgram(prog, ro, std::cin, std::cout, std::cerr);
    if (!r.ok) { std::cerr << "\nError de ejecucion: " << r.error << "\n"; return 2; }
    return 0;
}
