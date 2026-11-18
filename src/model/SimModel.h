// model/SimModel.h — MODELO de la aplicación.
// Guarda todo el estado de la simulación (código fuente, programa ensamblado, máquina,
// traza, puntos de parada, estado, ajustes). No conoce ni ImGui ni SDL: la vista lo LEE
// y el controlador lo MODIFICA mediante estos métodos.
#pragma once
#include "Arm.h"
#include "Settings.h"
#include "Cpu.h"
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
    const arm::CpuModel* cpu() const { return cpu_.get(); }      // modelo de ciclos (monociclo / multiciclo / segmentado)
    const arm::CpuConfig& cpuConfig() const { return settings_.sim.cpu; }
    bool finished() const;                                        // la máquina ha terminado y la tubería está vacía
    bool breakpointBeforeNext() const;                            // monociclo/multiciclo: ¿la instrucción que va a empezar tiene punto de parada?
    bool breakpointAfterClock() const;                            // segmentado: ¿la instrucción recién captada tiene punto de parada?
    uint64_t execSerial() const { return execSerial_; }           // +1 con cada instrucción ejecutada (no con cada ciclo)
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
    bool canStepBack() const { return mach_ && cpu_ && cpu_->canBack(); }
    bool waitingForInput() const { return mach_ && mach_->halted && mach_->haltKind == "input"; }

    // -------------------------------- Órdenes (las usa el controlador) -------------------------
    void setSource(const std::string& text, bool external);     // external=true => el editor debe recargar
    bool assemble();
    void reset();
    bool step();                 // avanza un ciclo de reloj (en el monociclo, una instrucción); false si no se pudo
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
    void applySettings(const Settings& s);   // si cambia el tipo de procesador, se reconstruye el modelo de ciclos y se reinicia la simulación
    Settings& settingsRef() { return settings_; }
    void touchSettings() { settingsRev_++; }

private:
    void bump() { stateSerial_++; }
    void applyInitialState();

    std::string source_;
    uint64_t extRev_ = 0, stepSerial_ = 0, stateSerial_ = 0, settingsRev_ = 0, execSerial_ = 0;
    bool dirty_ = true, running_ = false;
    arm::Program prog_;
    std::unique_ptr<arm::Machine> mach_;
    std::unique_ptr<arm::CpuModel> cpu_;
    std::vector<arm::StepRecord> trace_;
    std::optional<arm::StepRecord> last_;
    std::vector<int> bps_;
    Status status_;
    Settings settings_;
};
