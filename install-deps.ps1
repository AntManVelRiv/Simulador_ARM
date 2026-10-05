# install-deps.ps1 — instala las dependencias de compilación de ARM SIM en Windows.
# Requiere: Visual Studio 2022 (con el workload "Desarrollo para el escritorio con C++")
# y PowerShell. Si no tienes Git o CMake, este script los instala con winget.
#
# Uso (desde una terminal normal, no hace falta ser administrador salvo la 1ª vez
# que winget instale algo):
#   powershell -ExecutionPolicy Bypass -File install-deps.ps1
#
# Después, compilar:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
#   cmake --build build --config Release
#   build\Release\armsim.exe   (o build\armsim.exe si usaste el generador Ninja)

$ErrorActionPreference = "Stop"

function Test-Command($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

Write-Host "==> Comprobando herramientas básicas..."

if (-not (Test-Command git)) {
    Write-Host "==> Instalando Git con winget..."
    winget install --id Git.Git -e --source winget
}

if (-not (Test-Command cmake)) {
    Write-Host "==> Instalando CMake con winget..."
    winget install --id Kitware.CMake -e --source winget
}

Write-Host "==> Instalando SDL2 con vcpkg (esto compila SDL2 la primera vez; tarda unos minutos)..."

if (-not (Test-Path "C:\vcpkg")) {
    git clone --depth 1 https://github.com/microsoft/vcpkg C:\vcpkg
    & C:\vcpkg\bootstrap-vcpkg.bat
}

& C:\vcpkg\vcpkg.exe install sdl2:x64-windows

Write-Host ""
Write-Host "==> Listo. Para compilar ARM SIM:"
Write-Host '    cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows'
Write-Host '    cmake --build build --config Release'
Write-Host ""
Write-Host "    Si el ejecutable no arranca al moverlo a otra carpeta, copia junto a él:"
Write-Host '    C:\vcpkg\installed\x64-windows\bin\SDL2.dll'
Write-Host ""
Write-Host "    Nota: necesitas Visual Studio 2022 con el workload 'Desarrollo para"
Write-Host "    el escritorio con C++' instalado para que CMake encuentre un compilador."
