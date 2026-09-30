// main.cpp — raíz de composición: crea modelo, controlador y vista y ejecuta el bucle de la aplicación.
#include "model/SimModel.h"
#include "controller/SimController.h"
#include "view/MainView.h"
#include "view/Theme.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdio>
#include <string>

extern const unsigned char kFontRegular[];
extern const unsigned int kFontRegularSize;

int main(int, char**) {
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
    controller.loadExample(0);
    applyTheme(model.settings().ui);
    MainView view(model, controller);

    Uint64 last = SDL_GetPerformanceCounter(); bool done = false;
    while (!done && !controller.quitRequested()) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(window))) done = true;
        }
        Uint64 now = SDL_GetPerformanceCounter(); float dt = (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency()); last = now;
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        view.frame(std::min(dt, 0.1f));
        ImGui::Render();
        int w, h; SDL_GL_GetDrawableSize(window, &w, &h); glViewport(0, 0, w, h);
        Color bg = model.settings().ui.col.bg; glClearColor(bg.r, bg.g, bg.b, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    SettingsStore::save(model.settings(), prefDir + "settings.ini");
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
