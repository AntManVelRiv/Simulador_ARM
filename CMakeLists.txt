# ARM SIM — simulador de ensamblador ARMv4 con interfaz gráfica (Dear ImGui + SDL2)
# Compila en Windows, Linux y macOS. Ver README.md para instrucciones por sistema operativo.
cmake_minimum_required(VERSION 3.16)
project(ARMSim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ARMSIM_BUILD_TESTS "Compilar las pruebas del modelo/controlador (sin interfaz gráfica)" ON)
option(ARMSIM_BUILD_GUI   "Compilar la interfaz gráfica (necesita SDL2 y OpenGL). Con OFF solo se construye armsim-cli (sin interfaz)" ON)

# ------------------------------- Dependencias -------------------------------
find_package(Threads REQUIRED)
set(ARMSIM_GUI_AVAILABLE FALSE)
if(ARMSIM_BUILD_GUI)
  find_package(SDL2)
  find_package(OpenGL)
  if(SDL2_FOUND AND OpenGL_FOUND)
    set(ARMSIM_GUI_AVAILABLE TRUE)
  else()
    message(WARNING "SDL2/OpenGL no encontrados: se construye SOLO la version sin interfaz (armsim-cli). Instala SDL2 (install-deps.sh) para la GUI.")
  endif()
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(TEDIT_DIR ${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit)

if(ARMSIM_GUI_AVAILABLE)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
# imgui_impl_opengl3.cpp carga las funciones de OpenGL con dlopen() en tiempo de
# ejecución (en Linux/macOS). CMAKE_DL_LIBS añade -ldl donde hace falta (en algunos
# sistemas, como ciertas Raspberry Pi OS de 32 bits, no se cuela solo como dependencia
# transitiva de otra librería) y no añade nada donde no es necesario (p. ej. Windows).
if(TARGET SDL2::SDL2)
  target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL ${CMAKE_DL_LIBS})
  target_include_directories(imgui PUBLIC $<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>)
else()
  target_include_directories(imgui PUBLIC ${SDL2_INCLUDE_DIRS})
  target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES} OpenGL::GL ${CMAKE_DL_LIBS})
endif()

add_library(texteditor STATIC ${TEDIT_DIR}/TextEditor.cpp)
target_include_directories(texteditor PUBLIC ${TEDIT_DIR})
target_link_libraries(texteditor PUBLIC imgui)
endif()

# ---------------------------------- Modelo -----------------------------------
add_library(armsim_model STATIC
  src/model/Arm.cpp
  src/model/Runner.cpp
  src/model/Settings.cpp
  src/model/Examples.cpp
  src/model/SimModel.cpp
  src/platform/FileDialog.cpp
)
target_include_directories(armsim_model PUBLIC src)
target_link_libraries(armsim_model PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(armsim_model PUBLIC comdlg32)
endif()

# ------------------------- Modo sin interfaz (CLI) ---------------------------
# Sin SDL, OpenGL ni ImGui: se puede compilar en cualquier máquina (también sin Raspberry Pi).
add_library(armsim_cli STATIC src/cli/Cli.cpp)
target_link_libraries(armsim_cli PUBLIC armsim_model)
add_executable(armsim-cli src/cli/main_cli.cpp)
target_link_libraries(armsim-cli PRIVATE armsim_cli)
if(MSVC)
  target_compile_options(armsim-cli PRIVATE /utf-8)
  target_compile_options(armsim_cli PRIVATE /utf-8)
  target_compile_options(armsim_model PRIVATE /utf-8)
endif()
install(TARGETS armsim-cli RUNTIME DESTINATION .)

# -------------------------------- Controlador ---------------------------------
add_library(armsim_controller STATIC src/controller/SimController.cpp)
target_link_libraries(armsim_controller PUBLIC armsim_model)

if(ARMSIM_GUI_AVAILABLE)
# ----------------------------------- Vista ------------------------------------
add_library(armsim_view STATIC
  src/view/Theme.cpp
  src/view/Panel.cpp
  src/view/FileBrowser.cpp
  src/view/MainView.cpp
  src/view/FontData.cpp
  src/view/panels/CodePanels.cpp
  src/view/panels/StatePanels.cpp
  src/view/panels/InfoPanels.cpp
  src/view/panels/ConfigPanels.cpp
)
target_link_libraries(armsim_view PUBLIC armsim_controller imgui texteditor)

# --------------------------------- Ejecutable ----------------------------------
# add_executable(... WIN32 ...) construye una app de subsistema Windows (sin consola),
# lo que exige un punto de entrada WinMain en vez de main(). SDL2 resuelve esto por
# nosotros: SDL.h redirige "main" a "SDL_main", y SDL2::SDL2main aporta el WinMain
# real que lo invoca. Sin enlazar SDL2main, el enlazador falla con "unresolved
# external symbol WinMain" (LNK2019).
#
# Por si en algún entorno (vcpkg antiguo, SDL2 del sistema, triplet distinto...)
# ese target no existiera con ese nombre exacto, lo comprobamos ANTES de decidir el
# subsistema: si no está disponible, construimos un ejecutable de consola normal
# (aparecerá una ventana de consola detrás de la GUI, pero el enlazado nunca falla
# por este motivo) en vez de arriesgarnos a repetir el error de enlazado.
set(ARMSIM_USE_WIN32_SUBSYSTEM FALSE)
if(WIN32)
  if(TARGET SDL2::SDL2main)
    set(ARMSIM_USE_WIN32_SUBSYSTEM TRUE)
  else()
    message(WARNING "SDL2::SDL2main no disponible: armsim se construira con subsistema de consola en Windows (se vera una ventana de consola junto a la GUI).")
  endif()
endif()

if(ARMSIM_USE_WIN32_SUBSYSTEM)
  add_executable(armsim WIN32 src/main.cpp)
else()
  add_executable(armsim src/main.cpp)
endif()
target_link_libraries(armsim PRIVATE armsim_view armsim_cli)
if(ARMSIM_USE_WIN32_SUBSYSTEM)
  target_link_libraries(armsim PRIVATE SDL2::SDL2main)
endif()

if(APPLE)
  target_link_libraries(armsim PRIVATE "-framework CoreFoundation")
endif()
if(MSVC)
  target_compile_options(armsim PRIVATE /utf-8)
  target_compile_options(imgui PRIVATE /utf-8)
  target_compile_options(armsim_view PRIVATE /utf-8)
endif()

install(TARGETS armsim RUNTIME DESTINATION .)
endif()

# ------------------------------------ Pruebas -----------------------------------
if(ARMSIM_BUILD_TESTS)
  enable_testing()
  add_executable(armsim_tests tests/tests.cpp)
  target_link_libraries(armsim_tests PRIVATE armsim_controller)
  add_test(NAME model_controller_tests COMMAND armsim_tests)
endif()

# Compatibilidad con std::filesystem en GCC 8
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(armsim_model PUBLIC stdc++fs)
    target_link_libraries(armsim_tests PRIVATE stdc++fs)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(armsim_cli PUBLIC stdc++fs)
endif()
