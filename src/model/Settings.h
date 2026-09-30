// model/Settings.h — preferencias y configuración de la simulación (parte del MODELO).
// No depende de ninguna biblioteca gráfica: los colores son estructuras simples.
#pragma once
#include "Arm.h"
#include <string>
#include <vector>

struct Color { float r = 0, g = 0, b = 0, a = 1; };
struct Palette { Color bg, panel, accent, accent2, text, good, warn, bad; };

struct ThemePreset { const char* name; Palette palette; };
const std::vector<ThemePreset>& themePresets();

// Identificadores de los paneles (vistas) de la aplicación
enum PanelId { P_EDITOR, P_LISTING, P_REGS, P_INSTR, P_CPU, P_MEM, P_STACK, P_TRACE, P_OUTPUT, P_SYMS,
               P_ENCODER, P_MAP, P_REF, P_SETTINGS, P_ABOUT, P_COUNT };
const char* panelTitle(PanelId id);

// Bloque de memoria que se precarga al reiniciar la máquina
struct Preload { uint32_t addr = 0x3000; std::string words = "1, 2, 3"; bool bytes = false; bool enabled = true; };

// Configuración que afecta a la simulación
struct SimConfig {
    arm::AsmOptions opt;                 // direcciones base de .text/.data y de la pila
    uint32_t initRegs[16] = {0};
    bool initRegSet[16] = {false};
    std::vector<Preload> preloads;
    float stepsPerSec = 30.0f;           // velocidad de ejecución continua (>=100000 = máxima)
};

// Preferencias de aspecto e interfaz
struct UiPrefs {
    int theme = 0;
    Palette col = themePresets()[0].palette;
    float fontScale = 1.0f, rounding = 6.0f, glow = 0.6f, windowAlpha = 0.90f;
    bool animations = true, bgGrid = true, bgPulses = true, scanlines = false, followExec = true, showFps = false;
    float animSpeed = 1.0f;
    int regFormat = 0;                   // 0 hex, 1 dec con signo, 2 dec sin signo, 3 binario
    int memCols = 16; bool memAscii = true;
    bool show[P_COUNT];
    UiPrefs();
};

struct Settings { UiPrefs ui; SimConfig sim; std::string lastFile; };

// Persistencia en un fichero de texto clave=valor
struct SettingsStore {
    static bool save(const Settings& s, const std::string& path);
    static bool load(Settings& s, const std::string& path);
};
