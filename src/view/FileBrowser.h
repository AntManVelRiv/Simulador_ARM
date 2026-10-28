// view/FileBrowser.h — explorador de archivos interno (ImGui). Se usa cuando el sistema no tiene un diálogo
// nativo (p. ej. Raspberry Pi OS sin zenity) o si el usuario prefiere no usarlo. Sin dependencias externas.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

class FileBrowser {
public:
    void open(bool save, const std::string& startPath);
    bool isOpen() const { return open_; }
    // Dibuja el diálogo. Devuelve true cuando el usuario ha elegido un archivo (en 'chosen').
    // 'error' es un mensaje (p. ej. "no se pudo abrir") que se muestra dentro del propio diálogo.
    bool draw(float scale, std::string& chosen, const std::string& error);
    bool wantsClose() const { return !open_; }
    void reopen() { open_ = true; requestOpen_ = true; }   // volver a mostrarlo (p. ej. tras un error al abrir el archivo)
    bool isSave() const { return save_; }
private:
    void navigate(const std::filesystem::path& dir);
    void refresh();
    struct Entry { std::string name; bool dir; uintmax_t size; };
    bool open_ = false, save_ = false, requestOpen_ = false, showAll_ = false, confirmOverwrite_ = false;
    std::filesystem::path dir_;
    std::vector<Entry> entries_;
    char pathBuf_[1024] = "";
    char nameBuf_[512] = "";
    std::string listError_;
};
