// controller/SimController.cpp
#include "SimController.h"
#include "model/Examples.h"
#include <algorithm>

using namespace arm;

void SimController::newProgram() { m_.setSource("        MOV R0, #0\n", true); m_.assemble(); m_.setStatus("Programa nuevo", StatusKind::Ok); }

void SimController::loadExample(size_t i) {
    const auto& ex = examples();
    if (i >= ex.size()) return;
    m_.setSource(ex[i].code, true); m_.assemble();
    m_.setStatus(std::string("Cargado: ") + ex[i].name, StatusKind::Ok);
}

void SimController::toggleRun() {
    if (m_.running()) { m_.setRunning(false); m_.setStatus("En pausa", StatusKind::Warning); return; }
    if (m_.isDirty() || !m_.machine()) { if (!m_.assemble()) return; }
    if (m_.machine()->halted) m_.reset();
    m_.setRunning(true); accum_ = 0; justResumed_ = true;
    m_.setStatus("Ejecutando...", StatusKind::Ok);
}

void SimController::tick(double dt) {
    if (!m_.running() || !m_.machine()) return;
    float sps = m_.settings().sim.stepsPerSec;
    int n = (sps >= 100000) ? 200000 : (int)(accum_ += dt * sps);
    if (sps < 100000) accum_ -= n;
    n = std::min(n, 200000);
    for (int i = 0; i < n && m_.running(); i++) {
        // punto de parada: se comprueba antes de ejecutar (salvo en la primera instrucción tras reanudar)
        if (!justResumed_ && m_.hasBreakpointAt(m_.machine()->pc)) {
            m_.setRunning(false);
            auto it = m_.program().byAddr.find(m_.machine()->pc);
            m_.setStatus("Punto de parada en la línea " + std::to_string(it->second.line), StatusKind::Warning);
            return;
        }
        justResumed_ = false;
        if (!m_.step()) return;
    }
}

void SimController::toggleFlag(char which) {
    const Machine* mc = m_.machine(); if (!mc) return;
    bool cur = which == 'N' ? mc->f.N : which == 'Z' ? mc->f.Z : which == 'C' ? mc->f.C : mc->f.V;
    m_.setFlag(which, !cur);
}

bool SimController::evalNumber(const std::string& expr, uint32_t dot, uint32_t& out) const {
    try { out = arm::evalExpr(expr, m_.program().symbols, dot); return true; } catch (...) { return false; }
}

void SimController::setStepsPerSecond(float v) { Settings s = m_.settings(); s.sim.stepsPerSec = v; m_.applySettings(s); }
void SimController::setPanelVisible(PanelId id, bool v) { Settings s = m_.settings(); s.ui.show[id] = v; m_.applySettings(s); }
void SimController::applyThemePreset(int i) {
    const auto& P = themePresets(); if (i < 0 || i >= (int)P.size()) return;
    Settings s = m_.settings(); s.ui.theme = i; s.ui.col = P[i].palette; m_.applySettings(s);
}
void SimController::resetSettings() { Settings s; s.lastFile = m_.settings().lastFile; m_.applySettings(s); }
