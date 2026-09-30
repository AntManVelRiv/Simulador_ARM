// view/Theme.h — traduce las preferencias del modelo a estilo de Dear ImGui
#pragma once
#include "model/Settings.h"
#include "TextEditor.h"

void applyTheme(const UiPrefs& ui);                     // colores, redondeo, tamaño de texto
TextEditor::Palette makeEditorPalette(const UiPrefs& ui);
