// cli/CCompiler.h — usa un gcc "cruzado" (o el gcc de una Raspberry Pi de 32 bits) para traducir C a ensamblador
// ARM, de modo que el simulador pueda ejecutar programas en C (y mezclarlos con rutinas .s) sin hardware ARM.
//
// El simulador NO es un compilador de C: delega en gcc la fase "gcc -S" y ejecuta el ensamblador resultante.
#pragma once
#include <string>
#include <vector>

namespace armcc {

struct CcConfig {
    std::string cc;                       // compilador pedido con --cc / ARMSIM_CC (vacio = autodetectar)
    int optLevel = 0;                     // -O0 (por defecto: codigo facil de seguir) ... -O3
    bool optSize = false;                 // -Os
    std::vector<std::string> extraFlags;  // -DNAME, -Idir, -std=..., --cflags "..."
    bool verbose = false;                 // mostrar la orden de gcc ejecutada
};

// Busca un compilador de C que genere ARM de 32 bits. Devuelve "" y rellena 'why' si no hay ninguno.
std::string findCompiler(const std::string& requested, std::string& why);

// Texto de ayuda para instalar un compilador cruzado.
std::string installHelp();

// Compila 'cFile' a ensamblador (equivale a "gcc -S"). 'unit' numera la unidad de compilacion para que las
// etiquetas locales (.L2...) de dos ficheros .c distintos no choquen. Los avisos del compilador se escriben en 'diag'.
bool compileToAsm(const std::string& cFile, int unit, const CcConfig& cfg, const std::string& cc, std::string& asmText, std::string& diag);

} // namespace armcc
