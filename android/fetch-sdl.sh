#!/usr/bin/env bash
# Descarga el código fuente de SDL2 en android/SDL2 (se compila junto al simulador y aporta la parte Java).
set -euo pipefail
SDL_VERSION="${SDL_VERSION:-2.30.9}"
cd "$(dirname "$0")"

# Ajuste del teclado en pantalla para el editor de código (idempotente; se aplica también si SDL2 ya estaba descargado).
# SDL declara el campo de texto como texto normal: Gboard y compañía lo tratan con autocorrección y "texto en composición",
# y SDL solo entrega las letras al terminar la palabra (al escribir "ldr r0" no se vería nada hasta pulsar espacio).
# Con NO_SUGGESTIONS + VISIBLE_PASSWORD el teclado manda cada letra al instante y no corrige el ensamblador.
patch_sdl() {
    local f=SDL2/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java
    [ -f "$f" ] || { echo "ERROR: no encuentro $f"; exit 1; }
    if grep -q "ARMSIM_PATCH_IME" "$f"; then return 0; fi
    sed -i 's|InputType.TYPE_TEXT_FLAG_MULTI_LINE;|InputType.TYPE_TEXT_FLAG_MULTI_LINE \| InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS \| InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD; /* ARMSIM_PATCH_IME */|' "$f"
    grep -q "ARMSIM_PATCH_IME" "$f" || { echo "ERROR: no se pudo ajustar el teclado de SDLActivity.java (¿otra versión de SDL?)"; exit 1; }
    echo "SDL2: teclado ajustado para escribir código"
}

if [ -f SDL2/CMakeLists.txt ] && [ -d SDL2/android-project ]; then echo "SDL2 ya está en android/SDL2"; patch_sdl; exit 0; fi
rm -rf SDL2 SDL2.tar.gz
echo "Descargando SDL2 $SDL_VERSION..."
curl -fsSL -o SDL2.tar.gz "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-${SDL_VERSION}.tar.gz"
mkdir SDL2 && tar -xzf SDL2.tar.gz -C SDL2 --strip-components=1
rm -f SDL2.tar.gz
test -d SDL2/android-project || { echo "ERROR: SDL2 descargado sin android-project"; exit 1; }
patch_sdl
echo "SDL2 $SDL_VERSION listo en android/SDL2"
