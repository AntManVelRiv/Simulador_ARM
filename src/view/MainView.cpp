// view/MainView.cpp
#include "MainView.h"
#include "Theme.h"
#include "imgui_internal.h"
#include "model/Examples.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <chrono>

using namespace arm;

MainView::MainView(SimModel& model, SimController& ctrl) : model_(model), ctrl_(ctrl) {
    panels_.emplace_back(new EditorPanel);   panels_.emplace_back(new ListingPanel);  panels_.emplace_back(new RegistersPanel);
    panels_.emplace_back(new InstructionPanel); panels_.emplace_back(new DatapathPanel); panels_.emplace_back(new MemoryPanel);
    panels_.emplace_back(new StackPanel);    panels_.emplace_back(new TracePanel);    panels_.emplace_back(new OutputPanel);
    panels_.emplace_back(new SymbolsPanel);  panels_.emplace_back(new EncoderPanel);  panels_.emplace_back(new MemoryMapPanel);
    panels_.emplace_back(new ReferencePanel); panels_.emplace_back(new SettingsPanel); panels_.emplace_back(new AboutPanel);
    vs_.bootTime = ImGui::GetTime();
}

// ------------------------------- Disposición por defecto -------------------------------
void MainView::buildLayout() {
    ImGuiID dock = ImGui::GetID("MainDock");
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
    ImGuiID left, right, rTop, rBot, lTop, lBot, mid, bot;
    ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.34f, &left, &right);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.30f, &lBot, &lTop);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Right, 0.30f, &rTop, &mid);
    ImGui::DockBuilderSplitNode(mid, ImGuiDir_Down, 0.36f, &bot, &mid);
    ImGui::DockBuilderSplitNode(rTop, ImGuiDir_Down, 0.45f, &rBot, &rTop);
    auto dockW = [](PanelId id, ImGuiID node) { ImGui::DockBuilderDockWindow(panelTitle(id), node); };
    dockW(P_EDITOR, lTop); dockW(P_OUTPUT, lBot); dockW(P_TRACE, lBot);
    dockW(P_LISTING, mid); dockW(P_CPU, mid);
    dockW(P_INSTR, bot); dockW(P_MEM, bot); dockW(P_STACK, bot);
    dockW(P_REGS, rTop);
    dockW(P_SYMS, rBot); dockW(P_ENCODER, rBot); dockW(P_MAP, rBot); dockW(P_REF, rBot);
    ImGui::DockBuilderFinish(dock);
}

// ------------------------------------ Fondo animado ------------------------------------
void MainView::drawBackground() {
    const UiPrefs& ui = model_.settings().ui; Pal c(ui.col);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 p0 = ImGui::GetMainViewport()->Pos, sz = ImGui::GetMainViewport()->Size;
    dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), U32(c.bg), U32(c.bg), U32(mixc(c.bg, c.accent, 0.10f)), U32(mixc(c.bg, c.accent2, 0.06f)));
    double t = ui.anim() ? ImGui::GetTime() * ui.animSpeed : 0.0;
    if (ui.bgGrid) {
        float step = 48.0f * ui.fontScale; ImU32 gc = U32(withA(c.accent, 0.07f));
        float off = std::fmod((float)t * 6.0f, step);
        for (float x = p0.x - off; x < p0.x + sz.x; x += step) dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + sz.y), gc);
        for (float y = p0.y + off; y < p0.y + sz.y; y += step) dl->AddLine(ImVec2(p0.x, y), ImVec2(p0.x + sz.x, y), gc);
    }
    if (ui.bgPulses && !ui.lowPower) {   // "trazas de circuito" con pulsos de luz que las recorren
        for (int i = 0; i < 14; i++) {
            unsigned h = (unsigned)(i * 2654435761u);
            float y0 = p0.y + sz.y * (0.06f + 0.9f * ((h >> 3) % 1000) / 1000.0f);
            float len = sz.x * (0.5f + 0.5f * ((h >> 9) % 100) / 100.0f);
            float x0 = p0.x + sz.x * (((h >> 15) % 100) / 100.0f) * 0.6f;
            float kx = len * 0.3f, ky = ((h & 1) ? 1 : -1) * 22.0f;
            ImVec2 pts[4] = {ImVec2(x0, y0), ImVec2(x0 + kx, y0), ImVec2(x0 + kx + ky, y0 + ky), ImVec2(x0 + len, y0 + ky)};
            dl->AddPolyline(pts, 4, U32(withA(c.accent, 0.10f)), 0, 1.5f);
            float ph = std::fmod((float)t * (0.12f + 0.05f * (i % 4)) + i * 0.173f, 1.0f);
            float diag = std::fabs(ky) * 1.414f, total = kx + diag + (len - kx - std::fabs(ky));
            float d = ph * total; ImVec2 pp;
            if (d < kx) pp = ImVec2(x0 + d, y0);
            else if (d < kx + diag) { float q = (d - kx) / diag; pp = ImVec2(x0 + kx + ky * q, y0 + ky * q); }
            else pp = ImVec2(x0 + kx + std::fabs(ky) + (d - kx - diag), y0 + ky);
            ImVec4 pc = (i % 3) ? c.accent : c.accent2;
            dl->AddCircleFilled(pp, 3.2f, U32(withA(pc, 0.85f))); dl->AddCircleFilled(pp, 9.0f, U32(withA(pc, 0.10f)));
        }
    }
    if (ui.scanlines && !ui.lowPower) { ImU32 sc = U32(ImVec4(0, 0, 0, 0.10f)); for (float y = p0.y; y < p0.y + sz.y; y += 3.0f) dl->AddLine(ImVec2(p0.x, y), ImVec2(p0.x + sz.x, y), sc); }
}

void MainView::drawBootOverlay() {
    const UiPrefs& ui = model_.settings().ui; Pal c(ui.col);
    double t = ImGui::GetTime() - vs_.bootTime;
    if (!ui.anim() || t > 1.6) return;
    float a = t < 1.0 ? 1.0f : (float)(1.6 - t) / 0.6f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 p0 = ImGui::GetMainViewport()->Pos, sz = ImGui::GetMainViewport()->Size;
    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), U32(withA(c.bg, a)));
    const char* txt = "ARM SIM  //  INICIALIZANDO NÚCLEO ARMv4";
    int n = (int)std::min<double>((double)std::strlen(txt), t * 40.0);
    std::string s(txt); while ((int)s.size() > n) { s.pop_back(); }   // (la escritura progresiva respeta bytes UTF-8 completos)
    while (!s.empty() && (((unsigned char)s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty() && ((unsigned char)s.back()) >= 0xC0) s.pop_back();
    ImVec2 ts = ImGui::CalcTextSize(txt), pos(p0.x + (sz.x - ts.x) * 0.5f, p0.y + sz.y * 0.5f - 10);
    dl->AddText(pos, U32(withA(c.accent, a)), s.c_str());
    float prog = (float)std::min(1.0, t / 1.0);
    dl->AddRect(ImVec2(pos.x, pos.y + 28), ImVec2(pos.x + ts.x, pos.y + 36), U32(withA(c.accent, a)));
    dl->AddRectFilled(ImVec2(pos.x + 2, pos.y + 30), ImVec2(pos.x + 2 + (ts.x - 4) * prog, pos.y + 34), U32(withA(c.accent2, a)));
}

// ------------------------------------ Menú y barra -------------------------------------
void MainView::mainMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    const UiPrefs& ui = model_.settings().ui; const Settings& st = model_.settings();
    if (ImGui::BeginMenu("Archivo")) {
        if (ImGui::MenuItem("Nuevo")) ctrl_.newProgram();
        if (ImGui::MenuItem("Abrir...", "Ctrl+O")) vs_.openFileDlg = true;
        if (ImGui::MenuItem("Abrir con el explorador interno...")) { vs_.fileError.clear(); vs_.browser.open(false, st.lastFile); }
        if (ImGui::MenuItem("Guardar", "Ctrl+S")) { if (st.lastFile.empty()) vs_.saveFileDlg = true; else ctrl_.saveFile(st.lastFile); }
        if (ImGui::MenuItem("Guardar como...")) vs_.saveFileDlg = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("Ejemplos")) { const auto& ex = examples(); for (size_t i = 0; i < ex.size(); i++) if (ImGui::MenuItem(ex[i].name)) ctrl_.loadExample(i); ImGui::EndMenu(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Salir")) ctrl_.requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ejecutar")) {
        if (ImGui::MenuItem("Ensamblar", "F7")) ctrl_.assemble();
        if (ImGui::MenuItem(model_.running() ? "Pausar" : "Ejecutar", "F5")) ctrl_.toggleRun();
        if (ImGui::MenuItem("Paso", "F10")) ctrl_.step();
        if (ImGui::MenuItem("Paso atrás", "Alt+Izq", false, model_.canStepBack())) ctrl_.stepBack();
        if (ImGui::MenuItem("Reiniciar", "Mayús+F5")) ctrl_.reset();
        ImGui::Separator();
        if (ImGui::MenuItem("Punto de parada en la línea actual", "F9")) ctrl_.toggleBreakpoint(vs_.editorCursorLine);
        if (ImGui::MenuItem("Quitar todos los puntos de parada")) ctrl_.clearBreakpoints();
        ImGui::Separator();
        bool f = ui.followExec; if (ImGui::MenuItem("Seguir la ejecución en el editor", nullptr, &f)) { Settings s = st; s.ui.followExec = f; ctrl_.updateSettings(s); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ver")) {
        for (int i = 0; i < P_COUNT; i++) { bool v = ui.show[i]; if (ImGui::MenuItem(panelTitle((PanelId)i), nullptr, &v)) ctrl_.setPanelVisible((PanelId)i, v); }
        ImGui::Separator();
        if (ImGui::MenuItem("Restaurar disposición")) vs_.resetLayout = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tema")) {
        const auto& P = themePresets();
        for (int i = 0; i < (int)P.size(); i++) if (ImGui::MenuItem(P[i].name, nullptr, ui.theme == i)) ctrl_.applyThemePreset(i);
        ImGui::Separator();
        if (ImGui::MenuItem("Aumentar texto", "Ctrl +")) { Settings s = st; s.ui.fontScale = std::min(3.0f, s.ui.fontScale + 0.1f); ctrl_.updateSettings(s); }
        if (ImGui::MenuItem("Reducir texto", "Ctrl -")) { Settings s = st; s.ui.fontScale = std::max(0.6f, s.ui.fontScale - 0.1f); ctrl_.updateSettings(s); }
        if (ImGui::MenuItem("Personalizar...")) ctrl_.setPanelVisible(P_SETTINGS, true);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ayuda")) {
        if (ImGui::MenuItem("Referencia de instrucciones")) ctrl_.setPanelVisible(P_REF, true);
        if (ImGui::MenuItem("Acerca de")) ctrl_.setPanelVisible(P_ABOUT, true);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void MainView::toolbar() {
    const UiPrefs& ui = model_.settings().ui; Pal c(ui.col);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = ImGui::GetFrameHeight() * 1.5f;
    ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
    ImGui::Begin("##toolbar", nullptr, fl);
    ImGui::PushStyleColor(ImGuiCol_Button, mixc(c.panel, c.good, 0.35f));
    if (ImGui::Button(model_.isDirty() ? "Ensamblar*" : "Ensamblar")) ctrl_.assemble();
    ImGui::PopStyleColor(); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, model_.running() ? mixc(c.panel, c.warn, 0.5f) : mixc(c.panel, c.accent, 0.5f));
    if (ImGui::Button(model_.running() ? "Pausa  F5" : "Ejecutar F5")) ctrl_.toggleRun();
    ImGui::PopStyleColor(); ImGui::SameLine();
    if (ImGui::Button("Paso F10")) ctrl_.step(); ImGui::SameLine();
    ImGui::BeginDisabled(!model_.canStepBack()); if (ImGui::Button("Atrás")) ctrl_.stepBack(); ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("Reiniciar")) ctrl_.reset(); ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    float sps = model_.settings().sim.stepsPerSec, lv = std::log10(sps);
    ImGui::SetNextItemWidth(160 * ui.fontScale);
    if (ImGui::SliderFloat("##speed", &lv, 0.0f, 5.0f, "")) ctrl_.setStepsPerSecond(std::pow(10.0f, lv));
    ImGui::SameLine();
    if (sps >= 100000) ImGui::Text("máxima"); else ImGui::Text("%.0f inst/s", sps);
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    const Status& s = model_.status();
    ImVec4 sc = s.kind == StatusKind::Error ? c.bad : s.kind == StatusKind::Ok ? c.good : s.kind == StatusKind::Warning ? c.warn : c.text;
    if (model_.running() && ui.anim()) sc.w = 0.6f + 0.4f * std::sin((float)ImGui::GetTime() * 6.0f);
    ImGui::TextColored(sc, "%s", s.text.c_str());
    if (ui.showFps) { ImGui::SameLine(ImGui::GetWindowWidth() - 90); ImGui::TextDisabled("%.0f FPS", vs_.fps); }
    ImGui::End(); ImGui::PopStyleVar();
}

void MainView::handleInput() {
    ImGuiIO& io = ImGui::GetIO(); const Settings& st = model_.settings();
    if (ImGui::IsKeyPressed(ImGuiKey_F5) && !io.KeyShift) ctrl_.toggleRun();
    if (ImGui::IsKeyPressed(ImGuiKey_F5) && io.KeyShift) ctrl_.reset();
    if (ImGui::IsKeyPressed(ImGuiKey_F10) || ImGui::IsKeyPressed(ImGuiKey_F11)) ctrl_.step();
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && io.KeyAlt) ctrl_.stepBack();
    if (ImGui::IsKeyPressed(ImGuiKey_F7)) ctrl_.assemble();
    if (ImGui::IsKeyPressed(ImGuiKey_F9)) ctrl_.toggleBreakpoint(vs_.editorCursorLine);
    if (ImGui::IsKeyPressed(ImGuiKey_S) && io.KeyCtrl) { if (st.lastFile.empty()) vs_.saveFileDlg = true; else ctrl_.saveFile(st.lastFile); }
    if (ImGui::IsKeyPressed(ImGuiKey_O) && io.KeyCtrl) vs_.openFileDlg = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) && io.KeyCtrl) { Settings s = st; s.ui.fontScale = std::min(3.0f, s.ui.fontScale + 0.1f); ctrl_.updateSettings(s); }
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) && io.KeyCtrl) { Settings s = st; s.ui.fontScale = std::max(0.6f, s.ui.fontScale - 0.1f); ctrl_.updateSettings(s); }
}

// Abrir/Guardar: se usa el administrador de archivos del sistema (diálogo nativo, en un hilo para no congelar la
// ventana). Si el sistema no tiene ninguno, se muestra el explorador interno. Siempre se puede teclear la ruta.
void MainView::startFileDialog(bool save) {
    const Settings& st = model_.settings();
    std::string start = st.lastFile.empty() ? (save ? std::string("programa.s") : std::string()) : st.lastFile;
    vs_.fileError.clear();
    if (platform::nativeFileDialogAvailable()) {
        vs_.nativeBusy = true; vs_.nativeSave = save;
        vs_.nativeJob = std::async(std::launch::async, [save, start]() {
            std::string chosen; platform::DialogResult r = platform::nativeFileDialog(save, start, chosen);
            return std::make_pair(r, chosen);
        });
    } else vs_.browser.open(save, start);
}

void MainView::finishFileChoice(bool save, const std::string& pathIn) {
    std::string path = pathIn;
    bool ok = save ? ctrl_.saveFile(path) : ctrl_.openFile(path);
    if (!ok) { vs_.fileError = model_.status().text; vs_.browser.open(save, path); vs_.browser.reopen(); }
}

void MainView::fileDialogs() {
    const Settings& st = model_.settings();
    if (vs_.openFileDlg) { vs_.openFileDlg = false; if (!vs_.nativeBusy) startFileDialog(false); }
    if (vs_.saveFileDlg) { vs_.saveFileDlg = false; if (!vs_.nativeBusy) startFileDialog(true); }

    // resultado del diálogo nativo
    if (vs_.nativeBusy && vs_.nativeJob.valid() && vs_.nativeJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto res = vs_.nativeJob.get(); vs_.nativeBusy = false;
        if (res.first == platform::DialogResult::Chosen) finishFileChoice(vs_.nativeSave, res.second);
        else if (res.first == platform::DialogResult::Unavailable) vs_.browser.open(vs_.nativeSave, st.lastFile);
    }
    if (vs_.nativeBusy) {   // aviso discreto mientras el diálogo del sistema está abierto
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 60), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::Begin("##waitdlg", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::Text("Esperando al selector de archivos del sistema...");
        if (ImGui::Button("Usar el explorador interno")) { vs_.nativeJob = {}; vs_.nativeBusy = false; vs_.browser.open(vs_.nativeSave, st.lastFile); }
        ImGui::End();
    }

    // explorador interno
    std::string chosen;
    if (vs_.browser.draw(st.ui.fontScale, chosen, vs_.fileError)) finishFileChoice(vs_.browser.isSave(), chosen);
}

// --------------------------------------- Fotograma --------------------------------------
void MainView::frame(float dt) {
    if (seenSettings_ != model_.settingsRevision()) { seenSettings_ = model_.settingsRevision(); applyTheme(model_.settings().ui); }
    vs_.fps += (ImGui::GetIO().Framerate - vs_.fps) * 0.05f;
    ctrl_.tick(dt);                       // el controlador avanza la ejecución continua
    vs_.sync(model_, dt);                 // la vista detecta cambios del modelo para animar
    if (model_.settings().ui.followExec && model_.stepSerial() == vs_.seenStepSerial && (vs_.stageT < 0.05f || vs_.stepped) && model_.machine()) {
        auto it = model_.program().byAddr.find(model_.machine()->pc);
        if (it != model_.program().byAddr.end()) vs_.editorJumpLine = it->second.line - 1;
    }
    drawBackground();

    ImGuiViewport* vp = ImGui::GetMainViewport(); float th = ImGui::GetFrameHeight() * 1.5f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + th));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - th));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags hf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##host", nullptr, hf); ImGui::PopStyleVar(2);
    ImGuiID dock = ImGui::GetID("MainDock");
    if (!ImGui::DockBuilderGetNode(dock) || vs_.resetLayout) { vs_.resetLayout = false; buildLayout(); }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    mainMenu(); toolbar(); handleInput();
    ViewContext ctx{model_, ctrl_, vs_};
    for (auto& p : panels_) p->draw(ctx);
    fileDialogs(); drawBootOverlay();
}
