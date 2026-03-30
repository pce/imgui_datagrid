#pragma once

#include "imgui.h"

enum class LayoutMode { Phone, Tablet, Desktop };

struct ResponsiveLayout {

    LayoutMode mode          = LayoutMode::Desktop;
    int        logicalW      = 1280;
    int        logicalH      = 720;
    float      dpiScale      = 1.0f;
    float      sidebarWidth  = 200.0f;
    float      touchTargetPx = 0.0f;

    [[nodiscard]] bool isPhone()   const { return mode == LayoutMode::Phone;   }
    [[nodiscard]] bool isTablet()  const { return mode == LayoutMode::Tablet;  }
    [[nodiscard]] bool isDesktop() const { return mode == LayoutMode::Desktop; }
    [[nodiscard]] bool isMobile()  const { return !isDesktop(); }

    void Update(int logW, int logH, int fbW, [[maybe_unused]] int fbH)
    {
        const float dpi = (logW > 0 && fbW > 0)
            ? static_cast<float>(fbW) / static_cast<float>(logW)
            : 1.0f;

        const auto next = FromSize(logW, logH, dpi);

        changed_ = (next.mode        != mode       )
                || (next.logicalW    != logicalW    )
                || (next.logicalH    != logicalH    )
                || (next.dpiScale    != dpiScale    )
                || (next.sidebarWidth != sidebarWidth);

        *this = next;
    }

    [[nodiscard]] bool HasChanged() const { return changed_; }

    void ApplyToImGui() const
    {
    }

    [[nodiscard]] static ResponsiveLayout FromSize(int w, int h, float dpi = 1.0f)
    {
        ResponsiveLayout l;
        l.logicalW  = w;
        l.logicalH  = h;
        l.dpiScale  = (dpi > 0.0f) ? dpi : 1.0f;

        if      (w < 600)  l.mode = LayoutMode::Phone;
        else if (w < 1024) l.mode = LayoutMode::Tablet;
        else               l.mode = LayoutMode::Desktop;

        switch (l.mode) {
            case LayoutMode::Phone:
                l.touchTargetPx = 44.0f;
                l.sidebarWidth  = static_cast<float>(w) * 0.85f;
                break;
            case LayoutMode::Tablet:
                l.touchTargetPx = 44.0f;
                l.sidebarWidth  = 160.0f;
                break;
            case LayoutMode::Desktop:
                l.touchTargetPx = 0.0f;
                l.sidebarWidth  = 200.0f;
                break;
        }

        return l;
    }

private:
    bool changed_ = true;
};
