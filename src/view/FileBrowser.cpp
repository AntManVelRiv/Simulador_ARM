// view/FileBrowser.cpp
#include "FileBrowser.h"
#include "imgui.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace fs = std::filesystem;

static std::string u8(const fs::path& p) {
#if defined(__cpp_char8_t)
    auto s = p.u8string(); return std::string(s.begin(), s.end());
#else
    return p.u8string();
#endif
}
static fs::path fromU8(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}
static bool isAsmName(const std::string& n) {
    auto dot = n.find_last_of('.'); if (dot == std::string::npos) return false;
    std::string e = n.substr(dot + 1); for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    return e == "s" || e == "asm" || e == "txt" || e == "inc";
}
static std::string homeDir() {
#if defined(_WIN32)
    const char* h = std::getenv("USERPROFILE");
#else
    const char* h = std::getenv("HOME");
#endif
    return h ? h : ".";
}

void FileBrowser::open(bool save, const std::string& startPath) {
    save_ = save; open_ = true; requestOpen_ = true; confirmOverwrite_ = false; listError_.clear();
    std::error_code ec; fs::path p = startPath.empty() ? fs::path() : fromU8(startPath);
    fs::path dir; std::string name;
    if (!p.empty() && fs::is_directory(p, ec)) dir = p;
    else if (!p.empty()) { dir = p.has_parent_path() ? p.parent_path() : fs::current_path(ec); name = u8(p.filename()); }
    if (dir.empty() || !fs::is_directory(dir, ec)) dir = fs::current_path(ec);
    std::snprintf(nameBuf_, sizeof nameBuf_, "%s", save ? (name.empty() ? "programa.s" : name.c_str()) : name.c_str());
    navigate(dir);
}

void FileBrowser::navigate(const fs::path& dir) {
    std::error_code ec; fs::path c = fs::weakly_canonical(dir, ec); if (ec) c = dir;
    dir_ = c; std::snprintf(pathBuf_, sizeof pathBuf_, "%s", u8(dir_).c_str()); refresh();
}

void FileBrowser::refresh() {
    entries_.clear(); listError_.clear();
    std::error_code ec; fs::directory_iterator it(dir_, fs::directory_options::skip_permission_denied, ec);
    if (ec) { listError_ = "No se puede leer la carpeta: " + ec.message(); return; }
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code e2; Entry en; en.name = u8(it->path().filename());
        if (en.name.empty() || en.name[0] == '.') continue;                     // ocultos fuera
        en.dir = it->is_directory(e2); en.size = en.dir ? 0 : (uintmax_t)it->file_size(e2);
        entries_.push_back(en);
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.dir != b.dir) return a.dir;
        std::string x = a.name, y = b.name; for (auto& c : x) c = (char)std::tolower((unsigned char)c); for (auto& c : y) c = (char)std::tolower((unsigned char)c);
        return x < y; });
}

bool FileBrowser::draw(float scale, std::string& chosen, const std::string& error) {
    if (!open_) return false;
    const char* title = save_ ? "Guardar programa (explorador interno)" : "Abrir programa (explorador interno)";
    if (requestOpen_) { ImGui::OpenPopup(title); requestOpen_ = false; }
    bool picked = false;
    ImGui::SetNextWindowSize(ImVec2(640 * scale, 470 * scale), ImGuiCond_Appearing);
    bool keepOpen = true;
    if (ImGui::BeginPopupModal(title, &keepOpen, ImGuiWindowFlags_NoSavedSettings)) {
        // barra de navegación
        if (ImGui::Button("Subir")) { if (dir_.has_parent_path() && dir_.parent_path() != dir_) navigate(dir_.parent_path()); }
        ImGui::SameLine(); if (ImGui::Button("Inicio")) navigate(fromU8(homeDir()));
#if defined(_WIN32)
        ImGui::SameLine(); if (ImGui::BeginCombo("##drives", "Unidades", ImGuiComboFlags_WidthFitPreview)) {
            for (char d = 'A'; d <= 'Z'; d++) { std::string root = std::string(1, d) + ":\\"; std::error_code ec; if (fs::exists(root, ec) && ImGui::Selectable(root.c_str())) navigate(fromU8(root)); }
            ImGui::EndCombo(); }
#endif
        ImGui::SameLine(); if (ImGui::Button("Actualizar")) refresh();
        ImGui::SameLine(); if (ImGui::Checkbox("Todos los archivos", &showAll_)) {}
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##dir", pathBuf_, sizeof pathBuf_, ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::error_code ec; fs::path p = fromU8(pathBuf_);
            if (fs::is_directory(p, ec)) navigate(p);
            else if (fs::is_regular_file(p, ec)) { chosen = pathBuf_; picked = true; }
            else listError_ = "La ruta no existe: " + std::string(pathBuf_);
        }
        // lista
        float footer = ImGui::GetFrameHeightWithSpacing() * 3.2f + ImGui::GetStyle().ItemSpacing.y * 3;
        if (ImGui::BeginChild("##list", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            if (ImGui::Selectable("[..]  (carpeta superior)", false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
                if (dir_.has_parent_path() && dir_.parent_path() != dir_) navigate(dir_.parent_path());
            }
            for (size_t i = 0; i < entries_.size(); i++) {
                const Entry& e = entries_[i];
                if (!e.dir && !showAll_ && !isAsmName(e.name)) continue;
                ImGui::PushID((int)i);
                std::string label = e.dir ? "[+] " + e.name : "     " + e.name;
                bool sel = !e.dir && e.name == nameBuf_;
                if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (e.dir) { if (ImGui::IsMouseDoubleClicked(0)) { ImGui::PopID(); navigate(dir_ / fromU8(e.name)); break; } }
                    else {
                        std::snprintf(nameBuf_, sizeof nameBuf_, "%s", e.name.c_str()); confirmOverwrite_ = false;
                        if (ImGui::IsMouseDoubleClicked(0)) { chosen = u8(dir_ / fromU8(e.name)); picked = true; }
                    }
                }
                if (!e.dir) { ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70 * scale + ImGui::GetCursorPosX()); ImGui::TextDisabled("%ju B", e.size); }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        // pie
        ImGui::AlignTextToFramePadding(); ImGui::Text("Nombre:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputText("##name", nameBuf_, sizeof nameBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
        bool go = ImGui::Button(save_ ? "Guardar" : "Abrir") || enter;
        ImGui::SameLine(); if (ImGui::Button("Cancelar")) keepOpen = false;
        if (go && nameBuf_[0]) {
            fs::path target = fromU8(nameBuf_); if (target.is_relative()) target = dir_ / target;
            std::error_code ec;
            if (save_ && fs::exists(target, ec) && !confirmOverwrite_) confirmOverwrite_ = true;
            else if (!save_ && fs::is_directory(target, ec)) navigate(target);
            else { chosen = u8(target); picked = true; }
        }
        if (confirmOverwrite_) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Ya existe: pulsa Guardar otra vez para sobrescribir"); }
        else if (!error.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", error.c_str()); }
        else if (!listError_.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", listError_.c_str()); }
        if (picked) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (!keepOpen) open_ = false;
    if (picked) open_ = false;
    return picked;
}
