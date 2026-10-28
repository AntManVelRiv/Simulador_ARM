// view/MainView.h — vista raíz: disposición acoplable, menús, barra de herramientas, fondo animado y atajos.
#pragma once
#include "Panels.h"
#include <memory>
#include <vector>

class MainView {
public:
    MainView(SimModel& model, SimController& ctrl);
    void frame(float dt);            // dibuja un fotograma completo
private:
    void buildLayout();
    void drawBackground();
    void drawBootOverlay();
    void mainMenu();
    void toolbar();
    void handleInput();              // convierte teclas en órdenes al controlador
    void fileDialogs();
    void startFileDialog(bool save);
    void finishFileChoice(bool save, const std::string& path);
public:
    bool wantsFastPoll() const { return vs_.nativeBusy; }   // mientras esperamos al diálogo nativo hay que sondear a menudo
private:

    SimModel& model_;
    SimController& ctrl_;
    ViewState vs_;
    std::vector<std::unique_ptr<Panel>> panels_;
    uint64_t seenSettings_ = ~0ull;
    bool layoutDone_ = false;
};
