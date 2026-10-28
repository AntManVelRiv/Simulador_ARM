// cli/Cli.h — interfaz de línea de órdenes (sin ventana). La usan "armsim-cli" y "armsim --run ...".
#pragma once
// Devuelve true si los argumentos piden el modo sin interfaz (cualquier opción distinta de las de la GUI).
bool cliWanted(int argc, char** argv);
// Ejecuta el modo sin interfaz. Devuelve el código de salida del proceso.
int cliMain(int argc, char** argv);
