// model/SimModel.cpp
#include "SimModel.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <filesystem>

using namespace arm;

SimModel::SimModel() {}

bool SimModel::hasBreakpointAt(uint32_t pc) const {
    auto it = prog_.byAddr.find(pc);
    return it != prog_.byAddr.end() && std::find(bps_.begin(), bps_.end(), it->second.line) != bps_.end();
}

void SimModel::setSource(const std::string& text, bool external) {
    source_ = text; dirty_ = true; running_ = false;
    if (external) { extRev_++; bps_.clear(); trace_.clear(); last_.reset(); }
    bump();
}

bool SimModel::assemble() {
    running_ = false;
    prog_ = arm::assemble(source_, settings_.sim.opt);
    dirty_ = false; trace_.clear(); last_.reset();
    bump();
    if (!prog_.ok) {
        mach_.reset();
        setStatus(std::to_string(prog_.errors.size()) + " error(es) de ensamblado", StatusKind::Error);
        return false;
    }
    mach_ = std::make_unique<Machine>(prog_);
    applyInitialState();
    setStatus("Ensamblado: " + std::to_string(prog_.nInstr) + " instrucciones", StatusKind::Ok);
    return true;
}

// Aplica registros iniciales y precarga de memoria de la configuración
void SimModel::applyInitialState() {
    const SimConfig& c = settings_.sim;
    for (int i = 0; i < 16; i++) if (c.initRegSet[i]) { if (i == 15) mach_->pc = c.initRegs[i]; else mach_->r[i] = c.initRegs[i]; }
    for (const Preload& p : c.preloads) {
        if (!p.enabled) continue;
        std::stringstream ss(p.words); std::string tok; uint32_t a = p.addr;
        while (std::getline(ss, tok, ',')) {
            try {
                uint32_t v = arm::evalExpr(tok, prog_.symbols, a);
                if (p.bytes) mach_->mem[a++] = (uint8_t)v;
                else { for (int b = 0; b < 4; b++) mach_->mem[a + b] = (uint8_t)(v >> (8 * b)); a += 4; }
            } catch (...) {}
        }
    }
}

void SimModel::reset() {
    running_ = false;
    if (!mach_) return;
    mach_->reset();
    applyInitialState();
    trace_.clear(); last_.reset(); bump();
    if (mach_->halted && mach_->haltKind == "error") setStatus(mach_->haltReason, StatusKind::Error);
    else setStatus("Reiniciado", StatusKind::Normal);
}

bool SimModel::step() {
    if (dirty_ || !mach_) { if (!assemble()) return false; }
    if (mach_->halted) { setStatus(mach_->haltReason, mach_->haltKind == "error" ? StatusKind::Error : StatusKind::Warning); running_ = false; return false; }
    auto rec = mach_->step();
    if (!rec) { setStatus(mach_->haltReason, StatusKind::Error); running_ = false; return false; }
    last_ = rec; trace_.push_back(*rec);
    if (trace_.size() > 4000) trace_.erase(trace_.begin(), trace_.begin() + 500);
    stepSerial_++; bump();
    if (mach_->halted) { setStatus(mach_->haltReason, mach_->haltKind == "error" ? StatusKind::Error : StatusKind::Warning); running_ = false; }
    else setStatus(std::to_string(mach_->steps) + " instrucciones ejecutadas", StatusKind::Normal);
    return true;
}

bool SimModel::stepBack() {
    running_ = false;
    if (!mach_ || !mach_->back()) { setStatus("No hay más pasos que deshacer", StatusKind::Warning); return false; }
    if (!trace_.empty()) trace_.pop_back();
    if (trace_.empty()) last_.reset(); else last_ = trace_.back();
    stepSerial_++; bump();
    setStatus(std::to_string(mach_->steps) + " instrucciones ejecutadas", StatusKind::Normal);
    return true;
}

void SimModel::toggleBreakpointLine(int line) {
    auto it = std::find(bps_.begin(), bps_.end(), line);
    if (it == bps_.end()) bps_.push_back(line); else bps_.erase(it);
    bump();
}
void SimModel::clearBreakpoints() { bps_.clear(); bump(); }

void SimModel::setRegister(int r, uint32_t v) {
    if (!mach_) return;
    if (r == 15) mach_->pc = v; else mach_->r[r] = v;
    mach_->clearHistory(); bump();
}
void SimModel::setFlag(char which, bool v) {
    if (!mach_) return;
    switch (which) { case 'N': mach_->f.N = v; break; case 'Z': mach_->f.Z = v; break; case 'C': mach_->f.C = v; break; case 'V': mach_->f.V = v; break; }
    mach_->clearHistory(); bump();
}
void SimModel::writeMemory(uint32_t addr, uint32_t value, int size) {
    if (!mach_) return;
    for (int i = 0; i < size; i++) { mach_->mem[addr + i] = (uint8_t)(value >> (8 * i)); mach_->touched[addr + i] = true; }
    mach_->clearHistory(); bump();
}

void SimModel::provideInput(const std::string& text) {
    if (!mach_) return;
    bool wasWaiting = mach_->halted && mach_->haltKind == "input";
    mach_->feedInput(text + "\n");
    bump();
    if (wasWaiting) { setStatus("Entrada recibida", StatusKind::Ok); step(); }
}

// ---- Rutas: tolera lo que suele fallar al teclear/pegar una ruta (comillas, espacios, ~, file://, \\ de Windows...) ----
std::string normalizeUserPath(std::string p) {
    auto trim = [](std::string& t) { size_t a = t.find_first_not_of(" \t\r\n"); size_t b = t.find_last_not_of(" \t\r\n"); t = a == std::string::npos ? "" : t.substr(a, b - a + 1); };
    trim(p);
    if (p.size() >= 2 && ((p.front() == '"' && p.back() == '"') || (p.front() == '\'' && p.back() == '\''))) { p = p.substr(1, p.size() - 2); trim(p); }
    if (p.rfind("file://", 0) == 0) {              // file:///home/x/a%20b.s (arrastrar desde algunos gestores de archivos)
        p = p.substr(7); std::string d;
        for (size_t i = 0; i < p.size(); i++) {
            if (p[i] == '%' && i + 2 < p.size() + 0 && std::isxdigit((unsigned char)p[i+1]) && std::isxdigit((unsigned char)p[i+2])) { d += (char)std::stoi(p.substr(i + 1, 2), nullptr, 16); i += 2; }
            else d += p[i];
        }
        p = d;
#if defined(_WIN32)
        if (p.size() > 2 && p[0] == '/' && p[2] == ':') p = p.substr(1);   // /C:/dir/f.s -> C:/dir/f.s
#endif
    }
    if (!p.empty() && p[0] == '~' && (p.size() == 1 || p[1] == '/' || p[1] == '\\')) {
#if defined(_WIN32)
        const char* h = std::getenv("USERPROFILE");
#else
        const char* h = std::getenv("HOME");
#endif
        if (h) p = std::string(h) + p.substr(1);
    }
    return p;
}

static std::filesystem::path toFsPath(const std::string& s) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(s.begin(), s.end()));      // UTF-8 -> ruta nativa (importante en Windows)
#else
    return std::filesystem::u8path(s);
#endif
}

bool SimModel::loadFile(const std::string& rawPath) {
    std::string path = normalizeUserPath(rawPath);
    namespace fs = std::filesystem; std::error_code ec;
    fs::path fp = toFsPath(path);
    if (path.empty()) { setStatus("No se ha indicado ningún archivo", StatusKind::Error); return false; }
    if (!fs::exists(fp, ec)) { setStatus("No existe el archivo: " + path, StatusKind::Error); return false; }
    if (fs::is_directory(fp, ec)) { setStatus("Es una carpeta, no un archivo: " + path, StatusKind::Error); return false; }
    std::ifstream f(fp, std::ios::binary);
    if (!f) { setStatus("No se pudo abrir " + path + " (¿permisos?)", StatusKind::Error); return false; }
    std::stringstream ss; ss << f.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);   // BOM UTF-8
    setSource(text, true); settings_.lastFile = path; assemble();
    setStatus("Abierto: " + path, StatusKind::Ok);
    return true;
}
bool SimModel::saveFile(const std::string& rawPath) {
    std::string path = normalizeUserPath(rawPath);
    if (path.empty()) { setStatus("No se ha indicado dónde guardar", StatusKind::Error); return false; }
    std::ofstream f(toFsPath(path), std::ios::binary);
    if (!f) { setStatus("No se pudo guardar en " + path, StatusKind::Error); return false; }
    f << source_; settings_.lastFile = path;
    setStatus("Guardado: " + path, StatusKind::Ok);
    return true;
}
