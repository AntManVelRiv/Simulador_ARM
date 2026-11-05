// cli/Cli.cpp — ARM SIM sin interfaz gráfica.
// Equivale a "as + gcc + ./programa" de una Raspberry Pi, pero dentro del simulador:
//   armsim-cli programa.s                       ejecuta desde main (BL printf/scanf simulados)
//   armsim-cli programa.s --call reproduce 3    llama a UNA rutina como lo haría un main escrito en C
#include "Cli.h"
#include "model/Runner.h"
#include "CCompiler.h"
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
"Uso:  armsim-cli [opciones] fichero.s|fichero.c [otro.s|otro.c ...]\n"
"\n"
"Ficheros .c (necesitan un gcc que genere ARM de 32 bits; ver --help-gcc)\n"
"  Cada .c se traduce a ensamblador con gcc (\"gcc -S\") y se ejecuta junto con los .s: un main en C que\n"
"  llama a rutinas en ensamblador, igual que  \"gcc main.c rutina.o -o prog && ./prog\"  en la Raspberry.\n"
"  --cc GCC            compilador a usar (por defecto se busca arm-linux-gnueabi-gcc, arm-none-eabi-gcc...;\n"
"                      tambien vale la variable de entorno ARMSIM_CC)\n"
"  -O0 -O1 -O2 -O3 -Os nivel de optimizacion de gcc (por defecto -O0)\n"
"  -DNOMBRE[=V] -Idir -std=c99 ...   se pasan tal cual a gcc;  --cflags \"...\" para cualquier otra opcion\n"
"  --show-asm  (-S)    muestra el ensamblador que genera gcc para los .c y sale (como \"gcc -S\")\n"
"  --emit-asm FICHERO  guarda ese ensamblador y ejecuta igualmente\n"
"  --verbose           muestra la orden de gcc que se ejecuta\n"
"  --help-gcc          como instalar un compilador cruzado ARM\n"
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
"  armsim-cli Ejercicio2.s --call reproduce --arg 3 --return\n"
"  armsim-cli Pr2_Ejercicio5.c reproduce.s          # main en C + rutina en ensamblador (como gcc main.c reproduce.o)\n"
"  armsim-cli programa.c --input \"3 4\\n\" -O2 -S   # ver el ensamblador que genera gcc\n";

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
        if (a == "-h" || a == "--help" || a == "--run" || a == "--cli" || a == "--headless" || a == "--call" || a == "--check" || a == "--listing" || a == "--entry" || a == "--symbols" || a == "--cc" || a == "--help-gcc" || a == "--show-asm" || a == "--emit-asm") return true;
        if (a.size() > 2 && (a.compare(a.size() - 2, 2, ".c") == 0 || a.compare(a.size() - 2, 2, ".C") == 0)) return true;
    }
    return false;
}

int cliMain(int argc, char** argv) {
    RunOptions ro; AsmOptions ao; std::vector<std::string> files;
    bool listing = false, check = false, symbols = false; std::string callName;
    armcc::CcConfig cc; bool showAsm = false; std::string emitAsm;
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
        else if (a == "--help-gcc") { std::cout << armcc::installHelp() << "\n"; return 0; }
        else if (a == "--cc") { if (!(v = need(i, "--cc"))) return 1; cc.cc = v; }
        else if (a == "--cflags") { if (!(v = need(i, "--cflags"))) return 1; std::istringstream ss(v); std::string w; while (ss >> w) cc.extraFlags.push_back(w); }
        else if (a == "--show-asm" || a == "-S") showAsm = true;
        else if (a == "--emit-asm") { if (!(v = need(i, "--emit-asm"))) return 1; emitAsm = v; }
        else if (a == "--verbose" || a == "-v") cc.verbose = true;
        else if (a == "-O0" || a == "-O1" || a == "-O2" || a == "-O3") cc.optLevel = a[2] - '0';
        else if (a == "-Os") cc.optSize = true;
        else if (a.rfind("-D", 0) == 0 || a.rfind("-I", 0) == 0 || a.rfind("-std=", 0) == 0 || a.rfind("-W", 0) == 0) cc.extraFlags.push_back(a);
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

    // Varios ficheros = "enlazado" por concatenación; se recuerdan los límites para dar errores como fichero:línea.
    // Los .c se compilan antes con gcc (-S) y se concatena el ensamblador que produce.
    std::string src; std::vector<std::pair<int,std::string>> starts;   // (primera línea global, fichero)
    int lineCount = 0, unit = 0; std::string ccExe, generatedAsm;
    auto isC = [](const std::string& f) { return f.size() > 2 && (f.compare(f.size() - 2, 2, ".c") == 0 || f.compare(f.size() - 2, 2, ".C") == 0); };
    for (auto& f : files) {
        std::string t; std::string label = f;
        if (isC(f)) {
            if (ccExe.empty()) {
                std::string why; ccExe = armcc::findCompiler(cc.cc, why);
                if (ccExe.empty()) { std::cerr << why << "\n\n" << armcc::installHelp() << "\n"; return 1; }
            }
            std::string diag;
            bool ok = armcc::compileToAsm(f, ++unit, cc, ccExe, t, diag);
            std::cerr << diag;
            if (!ok) { std::cerr << "No se pudo compilar " << f << "\n"; return 1; }
            label = f + " (ensamblador generado por gcc)";
            generatedAsm += t;
        } else if (!readFile(f, t)) { std::cerr << "No se pudo abrir " << f << "\n"; return 1; }
        if (!t.empty() && t.back() != '\n') t += '\n';
        starts.push_back({lineCount + 1, label}); src += t;
        lineCount += (int)std::count(t.begin(), t.end(), '\n');
    }
    if (!emitAsm.empty()) { std::ofstream o(emitAsm, std::ios::binary); if (!o) { std::cerr << "No se pudo escribir " << emitAsm << "\n"; return 1; } o << generatedAsm; }
    if (showAsm) { if (generatedAsm.empty()) { std::cerr << "--show-asm solo tiene sentido con ficheros .c\n"; return 1; } std::cout << generatedAsm; return 0; }
    auto where = [&](int line) { std::string f = starts[0].second; int base = 1; for (auto& s : starts) if (line >= s.first) { f = s.second; base = s.first; } return f + ":" + std::to_string(line - base + 1); };

    Program prog = assemble(src, ao);
    if (!prog.ok) {
        bool unknownLib = false;
        for (auto& e : prog.errors) {
            std::cerr << where(e.line) << ": error: " << e.msg << "\n"; if (!e.hint.empty()) std::cerr << "    sugerencia: " << e.hint << "\n";
            if (e.msg.find("Etiqueta desconocida") != std::string::npos && !generatedAsm.empty()) unknownLib = true;
        }
        if (unknownLib) std::cerr << "\nNota: el codigo C llama a una funcion que el simulador no tiene (coma flotante float/double, math.h, ficheros...)\n"
                                     "o a una funcion tuya que no esta en ninguno de los ficheros dados. Funciones simuladas: ver docs/C_CON_GCC.md.\n";
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
