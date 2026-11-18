#!/usr/bin/env bash
# Construye el APK UNIVERSAL (arm64-v8a, armeabi-v7a, x86, x86_64) de ARM SIM.
#
# Requisitos (una sola vez):
#   * JDK 17                      (sudo apt install openjdk-17-jdk)
#   * Android SDK con "cmdline-tools" y la variable ANDROID_HOME (o ANDROID_SDK_ROOT) apuntando a él.
#       Sin Android Studio:  https://developer.android.com/studio#command-line-tools-only
#       sdkmanager --licenses            (acepta las licencias)
#     Gradle descargará solo el NDK y CMake que necesite.
# Uso:   bash android/build-apk.sh            ->  ARMSim-universal.apk en la carpeta del proyecto
set -euo pipefail
cd "$(dirname "$0")"
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[ -n "$SDK" ] && [ -d "$SDK" ] || { echo "ERROR: define ANDROID_HOME con la ruta del SDK de Android."; exit 1; }
command -v java >/dev/null || { echo "ERROR: falta Java (JDK 17)."; exit 1; }
echo "sdk.dir=$SDK" > local.properties

bash fetch-sdl.sh

GRADLE_VERSION=8.7
if command -v gradle >/dev/null 2>&1; then GRADLE=gradle
else
  if [ ! -x ".gradle-dist/gradle-$GRADLE_VERSION/bin/gradle" ]; then
    echo "Descargando Gradle $GRADLE_VERSION..."
    mkdir -p .gradle-dist && curl -fsSL -o .gradle-dist/gradle.zip "https://services.gradle.org/distributions/gradle-$GRADLE_VERSION-bin.zip"
    (cd .gradle-dist && unzip -q -o gradle.zip && rm gradle.zip)
  fi
  GRADLE=".gradle-dist/gradle-$GRADLE_VERSION/bin/gradle"
fi

"$GRADLE" --no-daemon assembleRelease
OUT="app/build/outputs/apk/release/app-release.apk"
[ -f "$OUT" ] || { echo "ERROR: no se generó $OUT"; exit 1; }
cp "$OUT" ../ARMSim-universal.apk
echo; echo "Listo: $(cd .. && pwd)/ARMSim-universal.apk"
echo "Instálalo con:  adb install -r ARMSim-universal.apk   (o copia el .apk al móvil y ábrelo)"
