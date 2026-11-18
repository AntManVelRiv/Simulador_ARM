// platform/AndroidBridge.cpp
#include "AndroidBridge.h"

#if defined(__ANDROID__)

#include <jni.h>
#include <SDL.h>

namespace platform {
namespace {

// Referencias JNI locales a la actividad (se liberan al salir del ámbito).
struct ActivityRef {
    JNIEnv* env = nullptr; jobject act = nullptr; jclass cls = nullptr;
    ActivityRef() {
        env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        if (env) act = (jobject)SDL_AndroidGetActivity();
        if (act) cls = env->GetObjectClass(act);
    }
    ~ActivityRef() { if (cls) env->DeleteLocalRef(cls); if (act) env->DeleteLocalRef(act); }
    bool ok() const { return env && act && cls; }
    jmethodID method(const char* name, const char* sig) {
        jmethodID m = env->GetMethodID(cls, name, sig);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }   // NoSuchMethodError
        return m;
    }
};

} // namespace

bool androidOpenFilePicker() {
    ActivityRef a; if (!a.ok()) return false;
    static jmethodID m = nullptr;                       // el identificador es válido mientras exista la clase
    if (!m) m = a.method("openFilePicker", "()V");
    if (!m) return false;
    a.env->CallVoidMethod(a.act, m);
    if (a.env->ExceptionCheck()) { a.env->ExceptionClear(); return false; }
    return true;
}

int androidKeyboardInsetPx() {
    if (!SDL_IsTextInputActive()) return 0;             // sin entrada de texto no hay teclado: nos ahorramos la llamada JNI
    ActivityRef a; if (!a.ok()) return 0;
    static jmethodID m = nullptr;
    if (!m) m = a.method("getKeyboardInset", "()I");
    if (!m) return 0;
    jint v = a.env->CallIntMethod(a.act, m);
    if (a.env->ExceptionCheck()) { a.env->ExceptionClear(); return 0; }
    return v > 0 ? (int)v : 0;
}

void showSoftKeyboard() {
    if (!SDL_IsScreenKeyboardShown(SDL_GetKeyboardFocus())) SDL_StartTextInput();
}

} // namespace platform

#else  // ---------------------------- resto de sistemas ----------------------------

namespace platform {
bool androidOpenFilePicker() { return false; }
int androidKeyboardInsetPx() { return 0; }
void showSoftKeyboard() {}
} // namespace platform

#endif
