// platform/AndroidBridge.h — puente con la parte Java de la app Android (ArmSimActivity).
// En el resto de sistemas operativos todas las funciones existen pero no hacen nada, así la vista no necesita #ifdef.
#pragma once

namespace platform {

constexpr bool isAndroid() {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

// Abre el administrador de archivos de Android (selector del sistema) para elegir un programa.
// Devuelve false si no se pudo lanzar (o si no estamos en Android). El archivo elegido llega más tarde como un evento
// SDL_DROPFILE (ya lo trata main.cpp): la actividad lo copia antes a la carpeta de la aplicación.
bool androidOpenFilePicker();

// Altura en píxeles que ocupa ahora mismo el teclado en pantalla (0 si no se ve o no estamos en Android).
int androidKeyboardInsetPx();

// Muestra el teclado en pantalla (útil si el usuario lo cerró con «Atrás» y vuelve a tocar el editor).
void showSoftKeyboard();

} // namespace platform
