// view/Panel.h — clase base de cada ventana de la interfaz
#pragma once
#include "ViewState.h"
#include "Draw.h"
#include "imgui.h"

class Panel {
public:
    explicit Panel(PanelId id) : id_(id) {}
    virtual ~Panel() = default;
    void draw(ViewContext& ctx);                       // gestiona visibilidad, título y animación de aparición
    PanelId id() const { return id_; }
protected:
    virtual void content(ViewContext& ctx) = 0;        // dibuja el contenido de la ventana
    virtual ImGuiWindowFlags flags() const { return 0; }
    PanelId id_;
};
