#pragma once

#include "imgui.h"

enum class ThemeType { SolarizedDark, SolarizedLight, Monokai };

class Theme {
public:
    void ApplyImGuiStyle(float dpiScale = 1.0f);
    void ApplyColorTheme(ThemeType theme);
};
