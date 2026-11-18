# ARM SIM para Android — compila el simulador como libmain.so (la carga SDLActivity) junto con SDL2.
# Lo invoca Gradle (app/build.gradle); no hace falta ejecutarlo a mano. SDL2 se descarga con fetch-sdl.sh.
cmake_minimum_required(VERSION 3.22)
project(ARMSimAndroid CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SDL2_DIR_SRC "${CMAKE_CURRENT_LIST_DIR}/SDL2")
if(NOT EXISTS "${SDL2_DIR_SRC}/CMakeLists.txt")
  message(FATAL_ERROR "Falta android/SDL2. Ejecuta primero:  bash android/fetch-sdl.sh")
endif()

# --- SDL2 como biblioteca compartida (libSDL2.so) ---
set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_DIR_SRC}" SDL2-build)

# --- Dear ImGui + editor de texto ---
set(IMGUI "${ROOT}/third_party/imgui")
set(TEDIT "${ROOT}/third_party/ImGuiColorTextEdit")

file(GLOB MODEL_SRC      "${ROOT}/src/model/*.cpp")
file(GLOB CONTROLLER_SRC "${ROOT}/src/controller/*.cpp")
file(GLOB VIEW_SRC       "${ROOT}/src/view/*.cpp")
file(GLOB PANELS_SRC     "${ROOT}/src/view/panels/*.cpp")
file(GLOB PLATFORM_SRC   "${ROOT}/src/platform/*.cpp")

add_library(main SHARED
  "${ROOT}/src/main.cpp"
  "${ROOT}/src/cli/Cli.cpp" "${ROOT}/src/cli/CCompiler.cpp"      # (main_cli.cpp es otro ejecutable y no se incluye)
  ${MODEL_SRC} ${CONTROLLER_SRC} ${VIEW_SRC} ${PANELS_SRC} ${PLATFORM_SRC}
  "${IMGUI}/imgui.cpp" "${IMGUI}/imgui_draw.cpp" "${IMGUI}/imgui_widgets.cpp" "${IMGUI}/imgui_tables.cpp" "${IMGUI}/imgui_demo.cpp"
  "${IMGUI}/backends/imgui_impl_sdl2.cpp" "${IMGUI}/backends/imgui_impl_opengl3.cpp"
  "${TEDIT}/TextEditor.cpp")
target_include_directories(main PRIVATE "${ROOT}/src" "${ROOT}/src/view" "${IMGUI}" "${IMGUI}/backends" "${TEDIT}")
target_compile_options(main PRIVATE -O2 -fno-strict-aliasing)
# imgui_impl_opengl3.cpp usa OpenGL ES 3 automáticamente cuando __ANDROID__ está definido
target_link_libraries(main PRIVATE SDL2 GLESv3 EGL android log dl)
