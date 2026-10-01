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

# ------------------------------- Dependencias -------------------------------
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(TEDIT_DIR ${CMAKE_SOURCE_DIR}/third_party/ImGuiColorTextEdit)

add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
if(TARGET SDL2::SDL2)
  target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)
  target_include_directories(imgui PUBLIC $<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>)
else()
  target_include_directories(imgui PUBLIC ${SDL2_INCLUDE_DIRS})
  target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES} OpenGL::GL)
endif()

add_library(texteditor STATIC ${TEDIT_DIR}/TextEditor.cpp)
target_include_directories(texteditor PUBLIC ${TEDIT_DIR})
target_link_libraries(texteditor PUBLIC imgui)

# ---------------------------------- Modelo -----------------------------------
add_library(armsim_model STATIC
  src/model/Arm.cpp
  src/model/Settings.cpp
  src/model/Examples.cpp
  src/model/SimModel.cpp
)
target_include_directories(armsim_model PUBLIC src)

# -------------------------------- Controlador ---------------------------------
add_library(armsim_controller STATIC src/controller/SimController.cpp)
target_link_libraries(armsim_controller PUBLIC armsim_model)

# ----------------------------------- Vista ------------------------------------
add_library(armsim_view STATIC
  src/view/Theme.cpp
  src/view/Panel.cpp
  src/view/MainView.cpp
  src/view/FontData.cpp
  src/view/panels/CodePanels.cpp
  src/view/panels/StatePanels.cpp
  src/view/panels/InfoPanels.cpp
  src/view/panels/ConfigPanels.cpp
)
target_link_libraries(armsim_view PUBLIC armsim_controller imgui texteditor)

# --------------------------------- Ejecutable ----------------------------------
if(WIN32)
  add_executable(armsim WIN32 src/main.cpp)
else()
  add_executable(armsim src/main.cpp)
endif()
target_link_libraries(armsim PRIVATE armsim_view)

# add_executable(... WIN32 ...) construye una app de subsistema Windows (sin consola),
# lo que exige un punto de entrada WinMain en vez de main(). SDL2 resuelve esto por
# nosotros: SDL.h redirige "main" a "SDL_main", y SDL2main.lib aporta el WinMain real
# que lo invoca. Sin enlazar SDL2main, el enlazador falla con "unresolved external
# symbol WinMain" (LNK2019). SDL2::SDL2main lo provee tanto vcpkg como la distribución
# oficial de SDL2 para Windows.
if(WIN32 AND TARGET SDL2::SDL2main)
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

# ------------------------------------ Pruebas -----------------------------------
if(ARMSIM_BUILD_TESTS)
  enable_testing()
  add_executable(armsim_tests tests/tests.cpp)
  target_link_libraries(armsim_tests PRIVATE armsim_controller)
  add_test(NAME model_controller_tests COMMAND armsim_tests)
endif()
