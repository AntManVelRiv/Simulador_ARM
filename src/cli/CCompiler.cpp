// cli/CCompiler.cpp — ver CCompiler.h
#include "CCompiler.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#ifdef _WIN32
#  define NULL_DEV "NUL"
#else
#  define NULL_DEV "/dev/null"
#endif

namespace fs = std::filesystem;

namespace armcc {

// ----------------------------------------------------------------------------------------------
// Cabeceras minimas (solo prototipos). Se compila con -ffreestanding y estas cabeceras antes que las
// del sistema, asi no hace falta ninguna libc para ARM: las funciones las simula el propio simulador.
// ----------------------------------------------------------------------------------------------
struct EmbeddedHeader { const char* name; const char* text; };
static const EmbeddedHeader HEADERS[] = {
{"stdio.h", R"(#ifndef ARMSIM_STDIO_H
#define ARMSIM_STDIO_H
#include <stddef.h>
#define EOF (-1)
typedef struct armsim_file FILE;
#define stdin  ((FILE *)0)
#define stdout ((FILE *)1)
#define stderr ((FILE *)2)
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int scanf(const char *fmt, ...) __attribute__((format(scanf, 1, 2)));
int puts(const char *s);
int putchar(int c);
int getchar(void);
char *gets(char *buf);
char *fgets(char *buf, int n, FILE *f);
int fputs(const char *s, FILE *f);
int fflush(FILE *f);
#define fputc(c, f) putchar(c)
#define putc(c, f)  putchar(c)
#define getc(f)     getchar()
#endif
)"},
{"stdlib.h", R"(#ifndef ARMSIM_STDLIB_H
#define ARMSIM_STDLIB_H
#include <stddef.h>
#define RAND_MAX 32767
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void free(void *p);
int rand(void);
void srand(unsigned seed);
int abs(int v);
int atoi(const char *s);
void exit(int code) __attribute__((noreturn));
#endif
)"},
{"string.h", R"(#ifndef ARMSIM_STRING_H
#define ARMSIM_STRING_H
#include <stddef.h>
size_t strlen(const char *s);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *p, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#endif
)"},
{"ctype.h", R"(#ifndef ARMSIM_CTYPE_H
#define ARMSIM_CTYPE_H
int toupper(int c);
int tolower(int c);
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
#endif
)"},
{"time.h", R"(#ifndef ARMSIM_TIME_H
#define ARMSIM_TIME_H
typedef long time_t;
time_t time(time_t *t);
#endif
)"},
};

// ----------------------------------------------------------------------------------------------
// Utilidades para lanzar procesos de forma portable (Windows / Linux / macOS)
// ----------------------------------------------------------------------------------------------
static std::string quote(const std::string& s) {
#ifdef _WIN32
    std::string o = "\""; for (char c : s) { if (c == '"') o += "\\\""; else o += c; } return o + "\"";
#else
    std::string o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'";
#endif
}
static int runShell(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());     // cmd.exe pierde las comillas externas si no se duplican
#else
    return std::system(cmd.c_str());
#endif
}
static std::string readAll(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static std::string trimEnd(std::string s) { while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back(); return s; }

// Directorio temporal propio, que se borra al salir
struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd; std::error_code ec;
        path = fs::temp_directory_path(ec) / ("armsim-" + std::to_string(rd()) + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count() % 1000000));
        fs::create_directories(path / "include", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};

static bool works(const std::string& exe) { return runShell(quote(exe) + " --version > " NULL_DEV " 2>&1") == 0; }

// ¿El compilador genera codigo ARM de 32 bits? (descarta gcc de x86 y el de ARM de 64 bits)
static bool targetsArm32(const std::string& exe) {
    TempDir t; fs::path out = t.path / "machine.txt";
    if (runShell(quote(exe) + " -dumpmachine > " + quote(out.string()) + " 2> " NULL_DEV) != 0) return false;
    std::string m = trimEnd(readAll(out));
    return m.rfind("arm", 0) == 0 && m.find("aarch64") == std::string::npos;
}

std::string findCompiler(const std::string& requested, std::string& why) {
    std::string req = requested;
    if (req.empty()) if (const char* e = std::getenv("ARMSIM_CC")) req = e;
    if (!req.empty()) {
        if (!works(req)) { why = "No se puede ejecutar el compilador indicado: " + req; return ""; }
        if (!targetsArm32(req)) { why = "El compilador " + req + " no genera codigo ARM de 32 bits (es para otra arquitectura). Necesitas uno ARM, p. ej. arm-linux-gnueabi-gcc."; return ""; }
        return req;
    }
    static const char* CANDIDATES[] = { "arm-linux-gnueabi-gcc", "arm-linux-gnueabihf-gcc", "arm-none-eabi-gcc", "arm-none-linux-gnueabihf-gcc", "armv7l-unknown-linux-gnueabihf-gcc" };
    for (const char* c : CANDIDATES) if (works(c)) return c;
    for (const char* c : { "gcc", "cc" }) if (works(c) && targetsArm32(c)) return c;   // Raspberry Pi OS de 32 bits
    why = "No hay ningun compilador de C para ARM instalado.";
    return "";
}

std::string installHelp() {
    return
"Para compilar ficheros .c hace falta un gcc que genere codigo ARM de 32 bits (el simulador ejecuta el\n"
"ensamblador que produce; no incluye compilador de C). Instala uno:\n"
"  Ubuntu / Debian / WSL : sudo apt install gcc-arm-linux-gnueabi        (o: gcc-arm-none-eabi)\n"
"  Fedora                : sudo dnf install gcc-arm-linux-gnu\n"
"  Arch                  : sudo pacman -S arm-none-eabi-gcc\n"
"  macOS (Homebrew)      : brew install arm-none-eabi-gcc\n"
"  Windows               : \"Arm GNU Toolchain\" (arm-none-eabi) de developer.arm.com, o lo mas facil: WSL + apt\n"
"  Raspberry Pi OS 32 bit: ya tienes gcc (se detecta solo)\n"
"No hace falta la libreria C del compilador: el simulador aporta sus propias cabeceras y funciones\n"
"(printf, scanf, strlen, malloc...). Si el compilador tiene otro nombre: --cc RUTA  o  variable ARMSIM_CC.\n"
"Sin compilador siempre puedes usar solo ficheros .s (ensamblador).";
}

// Renombra las etiquetas locales de gcc (.L2, .LC0...) anadiendo el numero de unidad, fuera de las cadenas
static std::string renameLocalLabels(const std::string& src, int unit) {
    std::string out; out.reserve(src.size() + 64); bool inStr = false;
    const std::string tag = ".Lu" + std::to_string(unit) + "_";
    for (size_t i = 0; i < src.size(); i++) {
        char c = src[i];
        if (c == '\n') inStr = false;
        if (c == '"' && (i == 0 || src[i-1] != '\\')) inStr = !inStr;
        bool prevIdent = i > 0 && (std::isalnum((unsigned char)src[i-1]) || src[i-1] == '_' || src[i-1] == '.' || src[i-1] == '$');
        if (!inStr && c == '.' && i + 1 < src.size() && src[i+1] == 'L' && !prevIdent) { out += tag; i += 1; continue; }
        out += c;
    }
    return out;
}

bool compileToAsm(const std::string& cFile, int unit, const CcConfig& cfg, const std::string& cc, std::string& asmText, std::string& diag) {
    TempDir t;
    for (auto& h : HEADERS) { std::ofstream f(t.path / "include" / h.name, std::ios::binary); f << h.text; }
    fs::path outS = t.path / "out.s", errTxt = t.path / "err.txt";
    std::error_code ec; fs::path cdir = fs::absolute(fs::path(cFile), ec).parent_path();

    std::string cmd = quote(cc) + " -S -marm -march=armv4t -mfloat-abi=soft -mno-unaligned-access"
                      " -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -Wall";
    cmd += cfg.optSize ? " -Os" : " -O" + std::to_string(cfg.optLevel);
    cmd += " -I" + quote((t.path / "include").string()) + " -I" + quote(cdir.string());
    for (auto& f : cfg.extraFlags) cmd += " " + (f.find(' ') == std::string::npos ? f : quote(f));
    cmd += " " + quote(cFile) + " -o " + quote(outS.string()) + " 2> " + quote(errTxt.string());
    if (cfg.verbose) diag += "$ " + cmd + "\n";

    int rc = runShell(cmd);
    std::string errs = readAll(errTxt);
    if (!errs.empty()) diag += errs;
    if (rc != 0 || !fs::exists(outS)) { if (errs.empty()) diag += "gcc ha fallado (codigo " + std::to_string(rc) + ") al compilar " + cFile + "\n"; return false; }
    asmText = renameLocalLabels(readAll(outS), unit);
    return true;
}

} // namespace armcc
