// controller/SimController.h — CONTROLADOR.
// Traduce las intenciones del usuario (que le envía la vista) en cambios sobre el modelo,
// y gestiona el bucle de ejecución continua. No dibuja nada.
#pragma once
#include "model/SimModel.h"
#include <string>

class SimController {
public:
    explicit SimController(SimModel& model) : m_(model) {}

    // Ejecución
    void assemble() { m_.assemble(); }
    void step()     { m_.setRunning(false); m_.step(); }
    void stepBack() { m_.stepBack(); }
    void reset()    { m_.reset(); }
    void toggleRun();
    void tick(double dt);                      // llamado una vez por fotograma

    // Edición
    void editorChanged(const std::string& text) { m_.setSource(text, false); }
    void newProgram();
    void loadExample(size_t index);
    bool openFile(const std::string& path) { return m_.loadFile(path); }
    bool saveFile(const std::string& path) { return m_.saveFile(path); }

    // Depuración
    void toggleBreakpoint(int line) { m_.toggleBreakpointLine(line); }
    void clearBreakpoints() { m_.clearBreakpoints(); }
    void clearTrace() { m_.clearTrace(); }

    // Estado de la máquina editable por el usuario
    void setRegister(int r, uint32_t v) { m_.setRegister(r, v); }
    void toggleFlag(char which);
    void writeMemory(uint32_t a, uint32_t v, int size) { m_.writeMemory(a, v, size); m_.setStatus("Escrito " + arm::hx0x(v) + " en " + arm::hx0x(a), StatusKind::Ok); }
    bool evalNumber(const std::string& expr, uint32_t dot, uint32_t& out) const;
    void submitInput(const std::string& text) { m_.provideInput(text); }

    // Configuración
    void updateSettings(const Settings& s) { m_.applySettings(s); }
    void setStepsPerSecond(float v);
    void setPanelVisible(PanelId id, bool v);
    void applyThemePreset(int index);
    void resetSettings();
    void notifyStatus(const std::string& t, StatusKind k) { m_.setStatus(t, k); }

    // Aplicación
    void requestQuit() { quit_ = true; }
    bool quitRequested() const { return quit_; }

private:
    SimModel& m_;
    double accum_ = 0;
    bool quit_ = false;
    bool justResumed_ = false;
};
