# ARM SIM — Simulador de ensamblador ARMv4 (escritorio)

Aplicación de escritorio nativa en C++ para ensamblar y ejecutar programas ARMv4, con interfaz gráfica personalizable ("futurista"), animaciones, edición de memoria y registros, entrada/salida por consola interactiva, y todas las vistas necesarias para depurar un programa a fondo. Funciona en **Windows, Linux y macOS**.

## Características

- **Ensamblador de dos pasadas** y **emulador de CPU** ARMv4: instrucciones de procesado de datos (con todos los códigos de condición, sufijo `S` y desplazamientos), acceso a memoria (`LDR`/`STR`/`LDRB`/`STRB`/`LDRH`/`STRH`/`LDRSB`/`LDRSH`, con offset/pre/post-indexado y registro+shift), `LDM`/`STM`/`PUSH`/`POP`, saltos (`B`/`BL`/`BX`), multiplicación de 32 y 64 bits, la pseudo-instrucción `LDR Rd, =valor` y las directivas `.text .data .word .byte .space .ascii .asciz .equ .org .end`.
- **Entrada/salida real por consola**: el programa puede leer enteros, caracteres y cadenas (bloqueando la ejecución hasta que el usuario escribe algo) e imprimir texto, caracteres, enteros y hexadecimales — no es solo una demo educativa con programas fijos, se pueden escribir programas interactivos de propósito general.
- **Depuración completa**: paso a paso, paso atrás (deshacer), ejecución continua a velocidad ajustable, puntos de parada por línea, pila de llamadas, inspector de memoria con edición directa (por byte/media palabra/palabra, en cualquier dirección), registros editables, flags editables a mano.
- **Camino de datos animado**: cada instrucción ejecutada ilumina en tiempo real las etapas (búsqueda, decodificación, ejecución, memoria, escritura) sobre el diagrama de la CPU.
- **Codificación en vivo**: cualquier instrucción se descompone campo a campo (cond, opcode, registros, inmediato...) con explicación de cada uno.
- **Totalmente personalizable**: 6 temas de color predefinidos ("futuristas": neón cian, ámbar terminal, violeta synth, matrix, carmesí, y un tema claro), edición de cada color, tamaño de texto, redondeo de bordes, opacidad, velocidad y activación de animaciones, rejilla y pulsos de fondo tipo "placa de circuito", líneas de barrido CRT.
- **Mapa de memoria configurable**: puedes elegir en qué dirección empieza el código, los datos y la pila, precargar bloques de memoria arbitrarios y fijar valores iniciales de cualquier registro antes de ejecutar.
- **Ventanas acoplables** (arrástralas, apílalas en pestañas, sepáralas en su propia ventana): Editor, Código máquina, Registros, Instrucción actual, Camino de datos, Memoria, Pila, Traza, Consola, Símbolos, Codificador, Mapa de memoria, Referencia — todas activables/desactivables desde el menú **Ver**, con la disposición restaurable a la de fábrica en cualquier momento.

## Descargar

Ve a la pestaña **Actions** de este repositorio (o a **Releases** si se ha publicado una etiqueta `vX.Y.Z`) y descarga el paquete de tu sistema operativo, generados automáticamente por el flujo de compilación multiplataforma en `.github/workflows/build.yml`:

| Paquete | Sistema |
|---|---|
| `armsim-windows-x64` | Windows 10/11, 64 bits |
| `armsim-linux-x64` | Linux de escritorio (Ubuntu, Debian, Fedora, Arch...), 64 bits |
| `armsim-linux-arm64` | Raspberry Pi 4/5 con Raspberry Pi OS de **64 bits** (el que trae por defecto desde 2022) u otro Linux ARM64 |
| `armsim-macos-arm64` | macOS con Apple Silicon (M1/M2/M3...) |

> Para Raspberry Pi OS de **32 bits** (armhf: modelos antiguos, Pi Zero/1/2, o una instalación de 32 bits en Pi 3/4) no hay artefacto prebuilt — GitHub no ofrece runners ARM de 32 bits gratuitos. Compílalo directamente en la propia Pi: es rápido con `install-deps.sh` (ver abajo).

## Compilar tú mismo

Requisitos: un compilador con C++17, [CMake](https://cmake.org/) ≥ 3.16 y la librería de desarrollo de **SDL2**. El proyecto no usa submódulos de git: un `git clone` normal ya trae todo lo necesario (Dear ImGui e ImGuiColorTextEdit están vendorizados como código fuente en `third_party/`).

### Linux — cualquier distribución (Ubuntu, Debian, Raspberry Pi OS, Fedora, openSUSE, Arch, Alpine...)

El script `install-deps.sh` detecta tu gestor de paquetes (`apt`, `dnf`, `zypper`, `pacman` o `apk`) e instala lo necesario automáticamente — funciona igual en un PC con Ubuntu que en una Raspberry Pi:

```bash
git clone <url-del-repositorio> armsim && cd armsim
chmod +x install-deps.sh && ./install-deps.sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/armsim
```

En una Raspberry Pi con poca RAM (1-2 GB), compila con menos trabajos en paralelo para no quedarte sin memoria: `cmake --build build --parallel 2`.

### Windows (Visual Studio 2022 + vcpkg)

El script `install-deps.ps1` instala Git/CMake si faltan (con `winget`) y compila SDL2 con vcpkg:

```powershell
git clone <url-del-repositorio> armsim
cd armsim
powershell -ExecutionPolicy Bypass -File install-deps.ps1

cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
build\Release\armsim.exe
```

Necesitas Visual Studio 2022 con el workload **"Desarrollo para el escritorio con C++"** instalado (CMake lo usa como compilador). También puedes abrir la carpeta directamente en Visual Studio 2022, que detecta `CMakeLists.txt` automáticamente.

> Si mueves `armsim.exe` a otra carpeta o a otro PC, llévate también `C:\vcpkg\installed\x64-windows\bin\SDL2.dll` — se enlaza dinámicamente.

### macOS

```bash
brew install cmake sdl2
git clone <url-del-repositorio> armsim && cd armsim
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/armsim
```

### Compilar sin interfaz gráfica (solo el núcleo y las pruebas)

El ensamblador, el emulador, el modelo y el controlador no dependen de ninguna librería gráfica y se pueden compilar y probar de forma aislada:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target armsim_tests
./build/armsim_tests     # 109 comprobaciones automáticas
```

## Arquitectura del código (Modelo-Vista-Controlador)

```
src/
├── model/            MODELO — no depende de ImGui/SDL, se puede probar sin interfaz
│   ├── Arm.h/.cpp        ensamblador + emulador de CPU ARMv4
│   ├── Settings.h/.cpp   preferencias, temas y su persistencia en disco
│   ├── Examples.h/.cpp   programas de ejemplo incluidos
│   └── SimModel.h/.cpp   estado completo de la simulación (fuente, programa, máquina, traza...)
├── controller/       CONTROLADOR — traduce acciones del usuario en cambios sobre el modelo
│   └── SimController.h/.cpp
├── view/             VISTA — Dear ImGui; solo lee el modelo y llama al controlador
│   ├── MainView.h/.cpp     disposición, menús, barra de herramientas, fondo animado
│   ├── Theme.h/.cpp        traduce las preferencias a estilo de ImGui
│   ├── Panel.h/.cpp        clase base de cada ventana
│   ├── Panels.h            declaración de todas las ventanas
│   ├── panels/             una implementación por grupo de ventanas
│   └── FontData.cpp        fuente JetBrains Mono incrustada en el binario
└── main.cpp          raíz de composición: crea SDL/OpenGL/ImGui y arranca el bucle principal

tests/tests.cpp       pruebas del modelo y del controlador (sin GUI): 109 comprobaciones
third_party/          Dear ImGui (rama docking) e ImGuiColorTextEdit, vendorizados como fuente
assets/fonts/         JetBrains Mono (licencia OFL, incluida)
install-deps.sh       instala las dependencias en cualquier Linux (detecta apt/dnf/zypper/pacman/apk)
install-deps.ps1      instala las dependencias en Windows (Git/CMake + SDL2 vía vcpkg)
.github/workflows/    CI: compila para Windows, Linux x64, Linux ARM64 y macOS en cada push
```

El **modelo** (`SimModel`) es la única fuente de verdad: guarda el código fuente, el programa ensamblado, la máquina, la traza de ejecución, los puntos de parada y las preferencias. No sabe nada de ImGui ni de SDL. El **controlador** (`SimController`) es la única vía para modificar el modelo: ensamblar, ejecutar, pausar, editar un registro, enviar una línea a la consola, cambiar de tema, etc. La **vista** (`MainView` y las clases `Panel`) solo lee el modelo para dibujar y llama al controlador cuando el usuario interactúa — nunca modifica el modelo directamente. Esta separación es la que permite compilar y probar `armsim_model` + `armsim_controller` sin ninguna librería gráfica (ver `tests/tests.cpp`).

## Entrada/salida por consola (para programas de propósito general)

Además de terminar el programa (`SWI 0x11`), el emulador ofrece un pequeño conjunto de llamadas al sistema para hacer programas interactivos de verdad, no solo ejemplos con datos fijos:

| SWI | Efecto |
|---|---|
| `0x11` / `0x18` | Termina el programa |
| `0x20` (o `0x00`) | Escribe el carácter de `R0` |
| `0x21` | Escribe `R0` como entero con signo |
| `0x22` | Escribe `R0` como entero sin signo |
| `0x23` | Escribe `R0` en hexadecimal |
| `0x24` | Escribe la cadena terminada en cero apuntada por `R0` |
| `0x25` | Escribe un salto de línea |
| `0x30` | Lee una línea de la consola y la interpreta como entero → `R0` |
| `0x31` | Lee un carácter de la consola → `R0` |
| `0x32` | Lee una línea en el buffer `[R1]` (máximo `R2` bytes); longitud → `R0` |

Las llamadas de lectura **detienen la ejecución** ("esperando entrada") hasta que el usuario escribe algo en el panel **Consola** y pulsa Intro o "Enviar"; el programa continúa automáticamente en cuanto llega el dato. El menú **Archivo → Ejemplos** incluye dos programas interactivos ("Consola: leer y sumar dos enteros" y "Consola: leer el nombre y saludar") que lo demuestran.

### `BL printf`, `BL scanf` y otras funciones de biblioteca

Además de las llamadas `SWI` de arriba, el simulador reconoce `printf`, `scanf`, `puts`, `gets` y `strlen` como si existieran de verdad: no hace falta definirlas, `BL printf` resuelve solo y, al ejecutarse, simula el efecto real de la función (y vuelve igual que si hubiera hecho su propio `BX LR`) — útil para seguir al pie de la letra los ejercicios que llaman directamente a funciones de la biblioteca C en vez de usar `SWI`.

| Llamada | Efecto |
|---|---|
| `BL printf` | `R0` = formato, `R1`-`R3` = argumentos. Admite `%s %d %u %x %c %%`. Escribe por consola; `R0` ← caracteres escritos. |
| `BL scanf` | `R0` = formato, `R1`-`R3` = punteros donde guardar lo leído (`%d %u %x %s %c`). Bloquea hasta tener una línea; `R0` ← nº de valores leídos. |
| `BL puts` | `R0` = cadena a escribir; añade un salto de línea. |
| `BL gets` | `R0` = buffer donde guardar la línea leída (sin límite de tamaño, como el `gets()` real). Bloquea hasta tener una línea. |
| `BL strlen` | `R0` = cadena; devuelve su longitud en `R0`. |

Si defines tu propia etiqueta con uno de estos nombres, la tuya tiene prioridad sobre la simulada.

### Macros (`.macro` / `.endm`)

Sintaxis estilo GNU `as`: dentro del cuerpo, `\parametro` se sustituye por el argumento de cada invocación, y `\@` por un número único por invocación (para etiquetas locales si la macro se usa varias veces):

```armasm
.macro reducir a,b,c
add \a,\a,\b
add \a,\a,\c
.endm
...
reducir r0,r1,r2     @ se expande aquí mismo a los dos ADD de arriba
```

El menú **Archivo → Ejemplos** incluye "Macro: reducir(a,b,c)" y "BL printf: pasar una cadena a mayúsculas" — ambos tomados literalmente de los ejercicios de la Relación 2 de Arquitectura de Computadores (UAL) — y "Consola: printf y scanf (estilo C)".

## Notas de compilación (problemas ya resueltos)

Si partes de una copia antigua del proyecto o tocas el `CMakeLists.txt`/el flujo de CI, ten en cuenta estos tres puntos — ya están corregidos en este repositorio, pero son fáciles de reintroducir por accidente:

- **Windows / `LNK2019: unresolved external symbol WinMain`**: el ejecutable se construye con `add_executable(armsim WIN32 ...)` para no mostrar una ventana de consola, lo que exige un punto de entrada `WinMain`. SDL2 lo resuelve enlazando `SDL2::SDL2main` (que a su vez redirige a nuestro `main()` normal vía `SDL.h`). Si ese target no existiera en tu entorno, el `CMakeLists.txt` cae automáticamente a un ejecutable de consola en vez de fallar el enlazado.
- **Linux / `undefined reference to dlopen@@GLIBC_2.4`**: el backend de OpenGL de Dear ImGui carga funciones con `dlopen()` en tiempo de ejecución. En algunos sistemas (observado en Raspberry Pi OS de 32 bits) `libdl` no se enlaza automáticamente como dependencia transitiva. Se añade `${CMAKE_DL_LIBS}` explícitamente al enlazar `imgui`.
- **`windows-latest` en GitHub Actions**: a fecha de este README, `windows-latest` está migrando a Visual Studio 2026 (toolset 18.x, todavía en beta) y puede dar fallos de compilación ajenos al propio código. El flujo de CI fija `windows-2022` (Visual Studio 2022 estable) a propósito.

## Licencias de terceros

- [Dear ImGui](https://github.com/ocornut/imgui) — MIT.
- [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) — MIT.
- [SDL2](https://www.libsdl.org/) — zlib.
- Fuente [JetBrains Mono](https://www.jetbrains.com/lp/mono/) — SIL Open Font License 1.1 (ver `assets/fonts/LICENSE-JetBrainsMono.txt`).

## Atajos de teclado

`F5` ejecutar/pausar · `Mayús+F5` reiniciar · `F10`/`F11` paso · `Alt+←` paso atrás · `F7` ensamblar · `F9` punto de parada en la línea del cursor · `Ctrl+O` abrir · `Ctrl+S` guardar · `Ctrl +` / `Ctrl -` tamaño del texto.
