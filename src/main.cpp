// main.cpp — raíz de composición: crea modelo, controlador y vista y ejecuta el bucle de la aplicación.
#include "model/SimModel.h"
#include "controller/SimController.h"
#include "view/MainView.h"
#include "view/Theme.h"
#include "cli/Cli.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

extern const unsigned char kFontRegular[];
extern const unsigned int kFontRegularSize;

int main(int argc, char** argv) {
    // Modo sin interfaz: "armsim --run programa.s", "armsim --call rutina ...", "armsim --help"... (ver src/cli/Cli.cpp)
    if (cliWanted(argc, argv)) return cliMain(argc, argv);
    // Argumentos de la interfaz: [fichero.s] [--low-power | --full-power]
    std::string openPath;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--low-power")) setPowerOverride(1);
        else if (!std::strcmp(argv[i], "--full-power")) setPowerOverride(0);
        else if (argv[i][0] != '-') openPath = argv[i];
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
#if defined(__APPLE__)
    const char* glsl = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    const char* glsl = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_WindowFlags wf = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("ARM SIM — Simulador ARMv4", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1500, 900, wf);
    if (!window) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    char* pref = SDL_GetPrefPath("ARMSim", "ARMSim"); std::string prefDir = pref ? pref : "./"; if (pref) SDL_free(pref);
    static std::string iniPath = prefDir + "layout.ini"; io.IniFilename = iniPath.c_str();

    // fuente incrustada en el ejecutable (JetBrains Mono, licencia OFL)
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)kFontRegular, (int)kFontRegularSize, 16.0f, &fc);
    ImGui_ImplSDL2_InitForOpenGL(window, gl); ImGui_ImplOpenGL3_Init(glsl);

    // ---------------- MVC: modelo, controlador y vista ----------------
    SimModel model; SimController controller(model);
    { Settings s; SettingsStore::load(s, prefDir + "settings.ini"); controller.updateSettings(s); }
    { Settings s = model.settings(); controller.updateSettings(s); }   // recalcula el modo de ahorro de energía (--low-power / Raspberry Pi)
    if (openPath.empty() || !controller.openFile(openPath)) controller.loadExample(0);
    applyTheme(model.settings().ui);
    MainView view(model, controller);

    Uint64 last = SDL_GetPerformanceCounter(); bool done = false;
    int redraw = 3;                  // fotogramas pendientes tras una entrada del usuario (ImGui necesita un par para asentarse)
    while (!done && !controller.quitRequested()) {
        const bool lowPower = model.settings().ui.lowPower;
        const bool active = model.running();
        Uint64 frameStart = SDL_GetPerformanceCounter();
        bool gotEvent = false;
        auto handle = [&](SDL_Event& e) {
            gotEvent = true;
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(window))) done = true;
            if (e.type == SDL_DROPFILE) { if (e.drop.file) { controller.openFile(e.drop.file); SDL_free(e.drop.file); } }   // arrastrar un .s sobre la ventana
        };
        SDL_Event e;
        // AHORRO DE ENERGÍA (Raspberry Pi): si no hay nada que animar ni ejecutar, la aplicación DUERME hasta que llegue
        // un evento (o venza un temporizador corto para el cursor parpadeante) en vez de repintar 60 veces por segundo.
        if (lowPower && !active && redraw <= 0) {
            if (SDL_WaitEventTimeout(&e, view.wantsFastPoll() ? 40 : 250)) handle(e);
        }
        while (SDL_PollEvent(&e)) handle(e);
        if (gotEvent) redraw = 3;
        if (done) break;

        Uint64 now = SDL_GetPerformanceCounter(); float dt = (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency()); last = now;
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        view.frame(std::min(dt, 0.1f));
        ImGui::Render();
        int w, h; SDL_GL_GetDrawableSize(window, &w, &h); glViewport(0, 0, w, h);
        Color bg = model.settings().ui.col.bg; glClearColor(bg.r, bg.g, bg.b, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        if (redraw > 0) redraw--;

        // En ahorro de energía se limita la tasa de fotogramas: ~20 FPS mientras el programa corre, ~30 al interactuar
        if (lowPower) {
            double ms = 1000.0 * (double)(SDL_GetPerformanceCounter() - frameStart) / (double)SDL_GetPerformanceFrequency();
            double target = active ? 50.0 : 33.0;
            if (ms < target) SDL_Delay((Uint32)(target - ms));
        }
    }
    SettingsStore::save(model.settings(), prefDir + "settings.ini");
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
