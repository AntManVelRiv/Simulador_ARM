// model/SimModel.h — MODELO de la aplicación.
// Guarda todo el estado de la simulación (código fuente, programa ensamblado, máquina,
// traza, puntos de parada, estado, ajustes). No conoce ni ImGui ni SDL: la vista lo LEE
// y el controlador lo MODIFICA mediante estos métodos.
#pragma once
#include "Arm.h"
#include "Settings.h"
#include <memory>
#include <string>
#include <vector>

// Limpia una ruta escrita/pegada por el usuario (comillas, espacios, ~, file://)
std::string normalizeUserPath(std::string p);

enum class StatusKind { Normal, Ok, Error, Warning };
struct Status { std::string text = "Listo"; StatusKind kind = StatusKind::Normal; };

class SimModel {
public:
    SimModel();

    // ------------------------------- Consultas (para la vista) -------------------------------
    const std::string& source() const { return source_; }
    uint64_t externalSourceRevision() const { return extRev_; }  // cambia cuando el texto se carga desde fuera del editor
    bool isDirty() const { return dirty_; }
    const arm::Program& program() const { return prog_; }
    const arm::Machine* machine() const { return mach_.get(); }
    const std::vector<arm::StepRecord>& trace() const { return trace_; }
    const arm::StepRecord* lastStep() const { return last_ ? &*last_ : nullptr; }
    uint64_t stepSerial() const { return stepSerial_; }          // +1 con cada instrucción ejecutada
    uint64_t stateSerial() const { return stateSerial_; }        // +1 con cualquier cambio de estado
    const std::vector<int>& breakpoints() const { return bps_; } // líneas (1-based)
    bool hasBreakpointAt(uint32_t pc) const;
    bool running() const { return running_; }
    const Status& status() const { return status_; }
    const Settings& settings() const { return settings_; }
    uint64_t settingsRevision() const { return settingsRev_; }
    bool canStepBack() const { return mach_ && mach_->canBack(); }
    bool waitingForInput() const { return mach_ && mach_->halted && mach_->haltKind == "input"; }

    // -------------------------------- Órdenes (las usa el controlador) -------------------------
    void setSource(const std::string& text, bool external);     // external=true => el editor debe recargar
    bool assemble();
    void reset();
    bool step();                 // ejecuta una instrucción; false si no se pudo
    bool stepBack();
    void setRunning(bool r) { running_ = r; }
    void toggleBreakpointLine(int line);
    void clearBreakpoints();
    void clearTrace() { trace_.clear(); bump(); }
    void setRegister(int r, uint32_t v);
    void setFlag(char which, bool v);
    void writeMemory(uint32_t addr, uint32_t value, int size);  // size 1, 2 o 4
    bool loadFile(const std::string& path);
    bool saveFile(const std::string& path);
    void provideInput(const std::string& text);   // el usuario escribe algo en la consola
    void setStatus(const std::string& t, StatusKind k) { status_ = {t, k}; }
    void setCurrentFile(const std::string& f) { settings_.lastFile = f; }
    void applySettings(const Settings& s) { settings_ = s; settings_.ui.lowPower = resolveLowPower(settings_.ui.powerMode); settingsRev_++; }
    Settings& settingsRef() { return settings_; }
    void touchSettings() { settingsRev_++; }

private:
    void bump() { stateSerial_++; }
    void applyInitialState();

    std::string source_;
    uint64_t extRev_ = 0, stepSerial_ = 0, stateSerial_ = 0, settingsRev_ = 0;
    bool dirty_ = true, running_ = false;
    arm::Program prog_;
    std::unique_ptr<arm::Machine> mach_;
    std::vector<arm::StepRecord> trace_;
    std::optional<arm::StepRecord> last_;
    std::vector<int> bps_;
    Status status_;
    Settings settings_;
};
