// model/SimModel.cpp
#include "SimModel.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>

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

bool SimModel::loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { setStatus("No se pudo abrir " + path, StatusKind::Error); return false; }
    std::stringstream ss; ss << f.rdbuf();
    setSource(ss.str(), true); settings_.lastFile = path; assemble();
    setStatus("Abierto: " + path, StatusKind::Ok);
    return true;
}
bool SimModel::saveFile(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { setStatus("No se pudo guardar en " + path, StatusKind::Error); return false; }
    f << source_; settings_.lastFile = path;
    setStatus("Guardado: " + path, StatusKind::Ok);
    return true;
}
