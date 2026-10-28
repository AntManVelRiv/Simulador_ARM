// platform/FileDialog.cpp
#include "FileDialog.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <commdlg.h>
#else
  #include <sys/wait.h>
  #include <unistd.h>
#endif

namespace platform {

#if defined(_WIN32)

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w;
}
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}

bool nativeFileDialogAvailable() { return true; }

DialogResult nativeFileDialog(bool save, const std::string& startPath, std::string& chosen) {
    wchar_t buf[MAX_PATH * 4] = {0};
    std::wstring start = widen(startPath);
    std::wstring initDir;
    size_t slash = start.find_last_of(L"\\/");
    if (slash != std::wstring::npos) { initDir = start.substr(0, slash); start = start.substr(slash + 1); }
    wcsncpy(buf, start.c_str(), MAX_PATH * 4 - 1);
    OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    static const wchar_t filter[] = L"Ensamblador (*.s;*.S;*.asm;*.txt)\0*.s;*.S;*.asm;*.txt\0Todos los archivos (*.*)\0*.*\0\0";
    ofn.lpstrFilter = filter; ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrInitialDir = initDir.empty() ? nullptr : initDir.c_str();
    ofn.lpstrTitle = save ? L"Guardar programa" : L"Abrir programa";
    ofn.lpstrDefExt = L"s";
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return DialogResult::Cancelled;
    chosen = narrow(buf); return DialogResult::Chosen;
}

bool isRaspberryPi() { return false; }

#else  // ---------------------------- Linux / macOS ----------------------------

static std::string shQuote(const std::string& s) {
    std::string o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } o += "'"; return o;
}
static bool commandExists(const char* name) {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}
// Ejecuta una orden y devuelve su salida estándar (sin salto de línea final) y su código de salida
static int runCapture(const std::string& cmd, std::string& out) {
    out.clear();
    FILE* p = popen(cmd.c_str(), "r"); if (!p) return -1;
    char b[1024]; size_t n; while ((n = fread(b, 1, sizeof b, p)) > 0) out.append(b, n);
    int st = pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

enum class Tool { None, Zenity, KDialog, Yad, Osascript };
static Tool detectTool() {
    static Tool t = [] {
#if defined(__APPLE__)
        return commandExists("osascript") ? Tool::Osascript : Tool::None;
#else
        // Sin sesión gráfica no hay diálogo posible
        if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) return Tool::None;
        if (commandExists("zenity")) return Tool::Zenity;
        if (commandExists("kdialog")) return Tool::KDialog;
        if (commandExists("yad")) return Tool::Yad;
        return Tool::None;
#endif
    }();
    return t;
}

bool nativeFileDialogAvailable() { return detectTool() != Tool::None; }

DialogResult nativeFileDialog(bool save, const std::string& startPath, std::string& chosen) {
    Tool t = detectTool(); if (t == Tool::None) return DialogResult::Unavailable;
    std::string start = startPath, cmd, out; int rc;
    switch (t) {
        case Tool::Zenity:
        case Tool::Yad: {
            const char* exe = t == Tool::Zenity ? "zenity" : "yad";
            cmd = std::string(exe) + " --file-selection --title=" + shQuote(save ? "Guardar programa" : "Abrir programa");
            if (save) cmd += t == Tool::Zenity ? " --save --confirm-overwrite" : " --save --confirm-overwrite";
            if (!start.empty()) cmd += " --filename=" + shQuote(start);
            cmd += " --file-filter=" + shQuote("Ensamblador | *.s *.S *.asm *.txt") + " --file-filter=" + shQuote("Todos los archivos | *");
            cmd += " 2>/dev/null";
            rc = runCapture(cmd, out);
            if (rc == 0 && !out.empty()) { chosen = out; return DialogResult::Chosen; }
            return rc == 1 || rc == 0 || rc == 252 ? DialogResult::Cancelled : DialogResult::Unavailable;
        }
        case Tool::KDialog: {
            std::string dir = start.empty() ? "." : start;
            cmd = std::string("kdialog ") + (save ? "--getsavefilename " : "--getopenfilename ") + shQuote(dir) + " " + shQuote("*.s *.S *.asm *.txt|Ensamblador\n*|Todos los archivos") + " 2>/dev/null";
            rc = runCapture(cmd, out);
            if (rc == 0 && !out.empty()) { chosen = out; return DialogResult::Chosen; }
            return rc == 1 || rc == 0 ? DialogResult::Cancelled : DialogResult::Unavailable;
        }
        case Tool::Osascript: {
            std::string script = save ? "POSIX path of (choose file name with prompt \"Guardar programa\")" : "POSIX path of (choose file with prompt \"Abrir programa\")";
            cmd = "osascript -e " + shQuote(script) + " 2>/dev/null";
            rc = runCapture(cmd, out);
            if (rc == 0 && !out.empty()) { chosen = out; return DialogResult::Chosen; }
            return DialogResult::Cancelled;
        }
        default: return DialogResult::Unavailable;
    }
}

// Raspberry Pi: el modelo figura en el árbol de dispositivos o en /proc/cpuinfo
bool isRaspberryPi() {
#if defined(__linux__)
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    cached = 0;
    { std::ifstream f("/proc/device-tree/model"); std::string s; if (f && std::getline(f, s) && s.find("Raspberry Pi") != std::string::npos) cached = 1; }
    if (!cached) { std::ifstream f("/proc/cpuinfo"); std::string l; while (std::getline(f, l)) if (l.find("Raspberry Pi") != std::string::npos) { cached = 1; break; } }
    return cached == 1;
#else
    return false;
#endif
}

#endif

} // namespace platform
