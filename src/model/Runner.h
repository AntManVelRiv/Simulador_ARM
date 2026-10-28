// model/Runner.h — ejecución SIN interfaz gráfica (parte del MODELO; no depende de ImGui ni de SDL).
// Permite lo mismo que hace una Raspberry Pi con "as" + "gcc" + "./programa", pero dentro del simulador:
//   * ejecutar un programa ensamblador completo desde "main" (con BL printf/scanf simulados);
//   * llamar a UNA rutina ensamblador como lo haría un programa en C (argumentos en R0-R3, resultado en R0),
//     que es justo lo que ocurre cuando un main escrito en C invoca a una función escrita en ensamblador.
#pragma once
#include "Arm.h"
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace arm {

// Argumento de una llamada estilo C a una rutina ensamblador
struct CallArg {
    enum Kind { Int, Str, Words, Bytes } kind = Int;
    int64_t value = 0;                 // Int: valor (se pasa en 32 bits)
    std::string text;                  // Str: cadena (se copia a memoria con un 0 final y se pasa su puntero)
    std::vector<uint32_t> words;       // Words: array de enteros de 32 bits (se pasa su puntero)
    std::vector<uint8_t> bytes;        // Bytes: array de bytes (se pasa su puntero)
};

// Interpreta un argumento escrito en la línea de órdenes:
//   42  -0x1F  'A'          -> entero
//   s:hola                  -> cadena de texto
//   w:1,2,3                 -> array de palabras (enteros de 32 bits)
//   b:1,2,3                 -> array de bytes
bool parseCallArg(const std::string& text, CallArg& out, std::string& error);

struct RunOptions {
    std::string entry = "main";        // etiqueta donde empieza la ejecución
    bool callStyle = false;            // true: preparar R0-R3 con 'args' y vuelta a un "return" simulado (como una llamada desde C)
    std::vector<CallArg> args;
    long maxSteps = 50'000'000;
    bool trace = false;                // imprime cada instrucción ejecutada
    bool showRegs = false;             // vuelca registros y flags al terminar
    bool showReturn = false;           // imprime R0 al terminar (valor de retorno)
    std::vector<std::pair<uint32_t,uint32_t>> dumps;   // (dirección, nº de bytes) a volcar al terminar
    std::string stdinText;             // texto con el que se alimenta la consola (si useStdinText)
    bool useStdinText = false;         // false: leer de std::cin cuando el programa pida entrada
    uint32_t scratchBase = 0x00100000; // zona donde se copian cadenas/arrays pasados como argumento
};

struct RunResult {
    bool ok = false;                   // false = error de ensamblado/ejecución (ver 'error')
    std::string error;
    std::string haltKind;              // exit | end | error | limit | input
    std::string haltReason;
    uint32_t r0 = 0;                   // valor de retorno / de salida
    long steps = 0;
    std::string output;
};

// Ejecuta un programa ya ensamblado. La salida del programa se escribe en 'out' a medida que se produce.
RunResult runProgram(const Program& prog, const RunOptions& opt, std::istream& in, std::ostream& out, std::ostream& diag);

} // namespace arm
