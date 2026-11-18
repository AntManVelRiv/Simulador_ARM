// view/Panels.h — declaración de todas las ventanas (vistas) de la aplicación
#pragma once
#include "Panel.h"
#include "TextEditor.h"
#include <string>

class EditorPanel : public Panel {
public:
    EditorPanel();
    void configureLanguage();
protected:
    void content(ViewContext& c) override;
private:
    void editToolbar(ViewContext& c);                 // botones Copiar / Cortar / Pegar... (pantalla táctil)
    void applyEditCommand(EditCmd cmd);
    void requestSoftKeyboard();                        // le pide a ImGui/SDL que muestre el teclado en pantalla
    TextEditor ed_;
    uint64_t seenSettings_ = ~0ull, seenState_ = ~0ull;
    bool kbWanted_ = false;                            // el usuario tocó el editor: hay que mostrar el teclado en pantalla
    float prevKbInset_ = 0;
};

class ListingPanel : public Panel { public: ListingPanel() : Panel(P_LISTING) {} protected: void content(ViewContext&) override; };
class InstructionPanel : public Panel { public: InstructionPanel() : Panel(P_INSTR) {} protected: void content(ViewContext&) override; };
class DatapathPanel : public Panel { public: DatapathPanel() : Panel(P_CPU) {} protected: void content(ViewContext&) override; };

class RegistersPanel : public Panel {
public: RegistersPanel() : Panel(P_REGS) {}
protected: void content(ViewContext&) override;
private: int editReg_ = -1; char buf_[40] = "";
};
class MemoryPanel : public Panel {
public: MemoryPanel() : Panel(P_MEM) {}
protected: void content(ViewContext&) override;
private: int editAddr_ = -1; char ebuf_[8] = ""; char wa_[16] = "00003000", wv_[24] = "0x12345678"; int wsz_ = 2;
};
class StackPanel : public Panel { public: StackPanel() : Panel(P_STACK) {} protected: void content(ViewContext&) override; };
class SymbolsPanel : public Panel { public: SymbolsPanel() : Panel(P_SYMS) {} protected: void content(ViewContext&) override; };

class TracePanel : public Panel { public: TracePanel() : Panel(P_TRACE) {} protected: void content(ViewContext&) override; };
class OutputPanel : public Panel { public: OutputPanel() : Panel(P_OUTPUT) {} protected: void content(ViewContext&) override; };
class EncoderPanel : public Panel { public: EncoderPanel() : Panel(P_ENCODER) {} protected: void content(ViewContext&) override; };
class ReferencePanel : public Panel { public: ReferencePanel() : Panel(P_REF) {} protected: void content(ViewContext&) override; };
class PipelinePanel : public Panel {
public: PipelinePanel() : Panel(P_PIPE) {}
protected: void content(ViewContext&) override;
private: long seenCycles_ = -1;
};
class AboutPanel : public Panel { public: AboutPanel() : Panel(P_ABOUT) {} protected: void content(ViewContext&) override; };

class MemoryMapPanel : public Panel { public: MemoryMapPanel() : Panel(P_MAP) {} protected: void content(ViewContext&) override; };
class SettingsPanel : public Panel { public: SettingsPanel() : Panel(P_SETTINGS) {} protected: void content(ViewContext&) override; };

// utilidad compartida: fila de campos de bits de una instrucción
void drawFieldRow(const ViewContext& c, const std::vector<arm::Field>& fields, float width);

// selector del tipo de procesador (monociclo / multiciclo / segmentado, nº de etapas y anticipación): se usa en el camino de datos
void drawCpuSelector(ViewContext& c);
