// platform/FileDialog.h — diálogo nativo "Abrir / Guardar" del sistema operativo (el administrador de archivos).
// Sin dependencias gráficas: Windows usa comdlg32, macOS usa osascript y Linux usa zenity/kdialog/yad si están instalados.
// Si no hay ninguno disponible, la interfaz recurre a su explorador de archivos interno (ver view/FileBrowser).
#pragma once
#include <string>

namespace platform {

enum class DialogResult { Chosen, Cancelled, Unavailable };

// ¿Hay un diálogo nativo utilizable en este sistema?
bool nativeFileDialogAvailable();

// Muestra el diálogo (BLOQUEA hasta que el usuario lo cierre; llamarlo desde un hilo aparte).
//   save: false = abrir, true = guardar     startPath: archivo o carpeta inicial (puede estar vacío)
DialogResult nativeFileDialog(bool save, const std::string& startPath, std::string& chosen);

// Detecta si el programa se ejecuta en una Raspberry Pi (u otra placa ARM de bajos recursos).
bool isRaspberryPi();

} // namespace platform
