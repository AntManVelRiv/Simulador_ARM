// view/ViewState.h — estado que pertenece SOLO a la vista (selecciones, animaciones, cuadros de texto).
// No es estado de la simulación: eso vive en el modelo.
#pragma once
#include "model/SimModel.h"
#include "controller/SimController.h"
#include "FileBrowser.h"
#include "platform/FileDialog.h"
#include <future>
#include <map>
#include <cstring>
#include <string>

struct ViewState {
    // animaciones
    double bootTime = 0;
    float regFlash[16] = {0};
    std::map<uint32_t, float> memFlash;
    float stageT = 1e9f;                 // tiempo transcurrido desde el último paso (camino de datos)
    uint64_t seenStepSerial = 0, seenExtRev = ~0ull;
    float fps = 60;
    bool stepped = false;                // true durante el fotograma en que se ejecutó una instrucción
    // navegación y selección
    uint32_t memBase = arm::DATA_BASE, memSelAddr = arm::DATA_BASE;
    char memGoto[16] = "00002000";
    int selInstrAddr = -1;
    int editorJumpLine = -1;             // línea (0-based) a la que debe saltar el editor
    int editorCursorLine = 0;            // línea actual del cursor (1-based), publicada por el editor
    // diálogos y cuadros de texto
    bool openFileDlg = false, saveFileDlg = false, resetLayout = false;
    char filePath[512] = "";
    // selector de archivos: diálogo nativo del sistema (en un hilo aparte) o explorador interno
    FileBrowser browser;
    std::future<std::pair<platform::DialogResult, std::string>> nativeJob;
    bool nativeBusy = false, nativeSave = false;
    std::string fileError;               // último error al abrir/guardar, mostrado dentro del explorador
    char encBuf[128] = "ADD R1, R2, R3";

    // Actualiza animaciones a partir de lo que ha cambiado en el modelo
    void sync(const SimModel& m, float dt) {
        stepped = false;
        if (m.stepSerial() != seenStepSerial) {
            seenStepSerial = m.stepSerial(); stepped = true;
            if (m.settings().ui.lowPower) { stageT = 1e9f; selInstrAddr = -1; }   // ahorro de energía: sin destellos ni animaciones
            else if (const arm::StepRecord* r = m.lastStep()) {
                for (auto& rc : r->regs) regFlash[rc.r] = 1.0f;
                for (auto& mw : r->mem) memFlash[mw.addr & ~3u] = 1.0f;
                stageT = 0; selInstrAddr = -1;
            } else stageT = 1e9f;
        }
        for (int i = 0; i < 16; i++) regFlash[i] = regFlash[i] > 0 ? std::max(0.0f, regFlash[i] - dt * 1.4f) : 0;
        for (auto it = memFlash.begin(); it != memFlash.end();) { it->second -= dt * 1.2f; if (it->second <= 0) it = memFlash.erase(it); else ++it; }
        if (stageT < 1e8f) stageT += dt * m.settings().ui.animSpeed;
    }
};

// Lo que recibe cada panel: el modelo (solo lectura), el controlador (para pedir acciones) y el estado de la vista.
struct ViewContext {
    const SimModel& model;
    SimController& ctrl;
    ViewState& vs;
    const UiPrefs& ui() const { return model.settings().ui; }
    void goMem(uint32_t a) { vs.memBase = a & ~3u; std::snprintf(vs.memGoto, sizeof vs.memGoto, "%08X", (unsigned)a); ctrl.setPanelVisible(P_MEM, true); }
};
