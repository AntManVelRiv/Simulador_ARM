#!/usr/bin/env bash
# install-deps.sh — instala las dependencias de compilación de ARM SIM en cualquier
# Linux: Ubuntu/Debian/Raspberry Pi OS (apt), Fedora/RHEL (dnf), openSUSE (zypper),
# Arch/Manjaro (pacman) o Alpine (apk). Detecta el gestor de paquetes automáticamente.
#
# Uso:
#   chmod +x install-deps.sh
#   ./install-deps.sh
#
# Después, compilar es igual en cualquier distribución (incluida la Raspberry Pi,
# compilando directamente en ella, sin cross-compilar):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#   ./build/armsim

set -e

need_sudo=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then
    need_sudo="sudo"
  else
    echo "Este script necesita privilegios de administrador (root) o 'sudo' instalado." >&2
    exit 1
  fi
fi

echo "==> Detectando el gestor de paquetes..."

if command -v apt-get >/dev/null 2>&1; then
  echo "==> apt (Ubuntu / Debian / Raspberry Pi OS / Linux Mint...)"
  $need_sudo apt-get update
  $need_sudo apt-get install -y build-essential cmake git libsdl2-dev libgl1-mesa-dev
  # zenity: selector de archivos nativo para Abrir/Guardar (opcional; sin él se usa el explorador interno)
  $need_sudo apt-get install -y zenity || true

elif command -v dnf >/dev/null 2>&1; then
  echo "==> dnf (Fedora / RHEL / Rocky / AlmaLinux...)"
  $need_sudo dnf install -y gcc-c++ cmake git SDL2-devel mesa-libGL-devel

elif command -v zypper >/dev/null 2>&1; then
  echo "==> zypper (openSUSE)"
  $need_sudo zypper install -y gcc-c++ cmake git libSDL2-devel Mesa-libGL-devel

elif command -v pacman >/dev/null 2>&1; then
  echo "==> pacman (Arch Linux / Manjaro)"
  $need_sudo pacman -Sy --needed --noconfirm base-devel cmake git sdl2 mesa

elif command -v apk >/dev/null 2>&1; then
  echo "==> apk (Alpine Linux)"
  $need_sudo apk add --no-cache build-base cmake git sdl2-dev mesa-dev linux-headers

else
  echo "No se reconoce el gestor de paquetes de este sistema." >&2
  echo "Instala manualmente: un compilador de C++17 (gcc/clang), cmake, git," >&2
  echo "las cabeceras de desarrollo de SDL2 y las de OpenGL (Mesa/libGL)." >&2
  exit 1
fi

echo ""
echo "==> Dependencias instaladas. Para compilar:"
echo "    cmake -B build -DCMAKE_BUILD_TYPE=Release"
echo "    cmake --build build --parallel"
echo "    ./build/armsim"
echo ""
echo "    (En equipos con poca RAM, como una Raspberry Pi con 1-2 GB, usa"
echo "     'cmake --build build --parallel 2' para no quedarte sin memoria"
echo "     al compilar en paralelo.)"
