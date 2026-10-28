// model/Settings.cpp
#include "Settings.h"
#include "platform/FileDialog.h"
#include <fstream>
#include <map>
#include <cstdlib>
#include <cstdio>

static Color c8(int r, int g, int b) { return Color{r / 255.f, g / 255.f, b / 255.f, 1.f}; }

const std::vector<ThemePreset>& themePresets() {
    static const std::vector<ThemePreset> P = {
        {"Neón cian",      {c8(6,10,24),   c8(14,20,42),  c8(0,229,255),  c8(255,140,40), c8(214,226,255), c8(64,255,176), c8(255,200,64), c8(255,72,96)}},
        {"Ámbar terminal", {c8(12,8,2),    c8(26,18,6),   c8(255,176,0),  c8(255,96,32),  c8(255,232,190), c8(160,255,80),  c8(255,220,80), c8(255,64,64)}},
        {"Violeta synth",  {c8(12,6,26),   c8(24,14,48),  c8(190,110,255),c8(255,64,180), c8(236,226,255), c8(80,255,200),  c8(255,210,90), c8(255,80,110)}},
        {"Matrix",         {c8(2,10,4),    c8(6,22,10),   c8(40,255,110), c8(160,255,0),  c8(200,255,215), c8(40,255,110),  c8(230,255,60), c8(255,70,70)}},
        {"Carmesí",        {c8(16,4,8),    c8(34,10,18),  c8(255,60,90),  c8(255,170,60), c8(255,225,230), c8(90,255,170),  c8(255,210,80), c8(255,60,60)}},
        {"Hielo (claro)",  {c8(232,240,252),c8(250,252,255),c8(0,110,190),c8(220,90,10),  c8(14,24,44),    c8(0,150,90),    c8(190,120,0),  c8(200,30,50)}},
    };
    return P;
}

static const char* TITLES[P_COUNT] = {
    "Editor", "Código máquina", "Registros", "Instrucción actual", "Camino de datos", "Memoria", "Pila",
    "Traza", "Consola", "Símbolos", "Codificador", "Mapa de memoria y estado inicial", "Referencia", "Ajustes y personalización", "Acerca de"
};
const char* panelTitle(PanelId id) { return TITLES[id]; }

UiPrefs::UiPrefs() {
    for (int i = 0; i < P_COUNT; i++) show[i] = true;
    show[P_SETTINGS] = show[P_ABOUT] = show[P_REF] = show[P_MAP] = show[P_ENCODER] = show[P_SYMS] = false;
    lowPower = resolveLowPower(powerMode);
}

static int g_powerOverride = -1;
void setPowerOverride(int forced) { g_powerOverride = forced; }
bool resolveLowPower(int mode) {
    if (g_powerOverride == 0) return false;
    if (g_powerOverride == 1) return true;
    if (mode == 1) return true;
    if (mode == 2) return false;
    return platform::isRaspberryPi();
}

static std::string colStr(const Color& c) { char b[96]; std::snprintf(b, sizeof b, "%.4f,%.4f,%.4f,%.4f", c.r, c.g, c.b, c.a); return b; }
static Color strCol(const std::string& s, Color d) { float a, b, c, e; if (std::sscanf(s.c_str(), "%f,%f,%f,%f", &a, &b, &c, &e) == 4) return Color{a, b, c, e}; return d; }

bool SettingsStore::save(const Settings& s, const std::string& path) {
    std::ofstream f(path); if (!f) return false;
    const UiPrefs& u = s.ui;
    f << "theme=" << u.theme << "\nfontScale=" << u.fontScale << "\nrounding=" << u.rounding << "\nglow=" << u.glow << "\nwindowAlpha=" << u.windowAlpha << "\n";
    f << "animations=" << u.animations << "\nanimSpeed=" << u.animSpeed << "\nbgGrid=" << u.bgGrid << "\nbgPulses=" << u.bgPulses << "\nscanlines=" << u.scanlines << "\nfollowExec=" << u.followExec << "\nshowFps=" << u.showFps << "\n";
    f << "powerMode=" << u.powerMode << "\n";
    f << "regFormat=" << u.regFormat << "\nmemCols=" << u.memCols << "\nmemAscii=" << u.memAscii << "\n";
    f << "stepsPerSec=" << s.sim.stepsPerSec << "\ntextBase=" << s.sim.opt.textBase << "\ndataBase=" << s.sim.opt.dataBase << "\nstackTop=" << s.sim.opt.stackTop << "\nlastFile=" << s.lastFile << "\n";
    const char* n[8] = {"bg","panel","accent","accent2","text","good","warn","bad"};
    const Color* v[8] = {&u.col.bg,&u.col.panel,&u.col.accent,&u.col.accent2,&u.col.text,&u.col.good,&u.col.warn,&u.col.bad};
    for (int i = 0; i < 8; i++) f << "col_" << n[i] << "=" << colStr(*v[i]) << "\n";
    for (int i = 0; i < P_COUNT; i++) f << "show" << i << "=" << u.show[i] << "\n";
    return true;
}

bool SettingsStore::load(Settings& s, const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    std::map<std::string, std::string> kv; std::string line;
    while (std::getline(f, line)) { auto p = line.find('='); if (p != std::string::npos) kv[line.substr(0, p)] = line.substr(p + 1); }
    auto gi = [&](const char* k, int d) { return kv.count(k) ? std::atoi(kv[k].c_str()) : d; };
    auto gf = [&](const char* k, float d) { return kv.count(k) ? (float)std::atof(kv[k].c_str()) : d; };
    auto gu = [&](const char* k, uint32_t d) { return kv.count(k) ? (uint32_t)std::strtoul(kv[k].c_str(), nullptr, 10) : d; };
    UiPrefs& u = s.ui;
    u.theme = gi("theme", 0); u.fontScale = gf("fontScale", 1); u.rounding = gf("rounding", 6); u.glow = gf("glow", 0.6f); u.windowAlpha = gf("windowAlpha", 0.9f);
    u.animations = gi("animations", 1); u.animSpeed = gf("animSpeed", 1); u.bgGrid = gi("bgGrid", 1); u.bgPulses = gi("bgPulses", 1);
    u.scanlines = gi("scanlines", 0); u.followExec = gi("followExec", 1); u.showFps = gi("showFps", 0);
    u.powerMode = gi("powerMode", 0); u.lowPower = resolveLowPower(u.powerMode);
    u.regFormat = gi("regFormat", 0); u.memCols = gi("memCols", 16); u.memAscii = gi("memAscii", 1);
    s.sim.stepsPerSec = gf("stepsPerSec", 30); s.sim.opt.textBase = gu("textBase", arm::TEXT_BASE); s.sim.opt.dataBase = gu("dataBase", arm::DATA_BASE); s.sim.opt.stackTop = gu("stackTop", arm::STACK_TOP);
    if (kv.count("lastFile")) s.lastFile = kv["lastFile"];
    const char* n[8] = {"bg","panel","accent","accent2","text","good","warn","bad"};
    Color* v[8] = {&u.col.bg,&u.col.panel,&u.col.accent,&u.col.accent2,&u.col.text,&u.col.good,&u.col.warn,&u.col.bad};
    for (int i = 0; i < 8; i++) { std::string k = std::string("col_") + n[i]; if (kv.count(k)) *v[i] = strCol(kv[k], *v[i]); }
    for (int i = 0; i < P_COUNT; i++) { std::string k = "show" + std::to_string(i); if (kv.count(k)) u.show[i] = std::atoi(kv[k].c_str()) != 0; }
    return true;
}
