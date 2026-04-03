#include "theme_customizer.hpp"

#include <nlohmann/json.hpp>
#include <print>

#include "imgui.h"
#include "../io/file_io.hpp"

struct ImVec4;
using json = nlohmann::json;

namespace datagrid::ui {

// ── internal helpers ──────────────────────────────────────────────────────────

namespace {

constexpr const char* kThemeNames[kThemeCount] = {
    "Solarized Dark",
    "Solarized Light",
    "Monokai",
    "Monokai Dark",
    "MonaSpaces",
    "Earth SUSE",
    "Earth SUSE Dark",
    "Neon Spaces",
    "DawnBringer 16 Dark",
    "DawnBringer 16 Light",
    "Material",
    "Material Dark",
    "Mono Light",
    "Mono Dark",
    "DawnBringer Light",
    "DawnBringer Dark",
};

constexpr const char* kThemeIDs[kThemeCount] = {
    "SolarizedDark",
    "SolarizedLight",
    "Monokai",
    "MonokaiDark",
    "MonaSpaces",
    "EarthSUSE",
    "EarthSUSEDark",
    "NeonSpaces",
    "DawnBringer16Dark",
    "DawnBringer16Light",
    "Material",
    "MaterialDark",
    "MonoLight",
    "MonoDark",
    "DawnBringerLight",
    "DawnBringerDark",
};

[[nodiscard]] ImVec4 lerp(ImVec4 a, ImVec4 b, float t) noexcept {
    return { a.x + (b.x - a.x) * t,
             a.y + (b.y - a.y) * t,
             a.z + (b.z - a.z) * t,
             a.w + (b.w - a.w) * t };
}

[[nodiscard]] ImVec4 with_alpha(ImVec4 v, float a) noexcept {
    return { v.x, v.y, v.z, a };
}

json vec4_to_json(ImVec4 v) {
    return json::array({ v.x, v.y, v.z, v.w });
}

ImVec4 json_to_vec4(const json& j, ImVec4 fallback) {
    if (!j.is_array() || j.size() < 4) return fallback;
    return { j[0].get<float>(), j[1].get<float>(),
             j[2].get<float>(), j[3].get<float>() };
}

} // anon namespace

// ── palette_to_imgui ──────────────────────────────────────────────────────────

void ThemeCustomizer::palette_to_imgui(const ThemePalette& p, ImVec4* c) {
    const ImVec4 black = ImVec4(0.f, 0.f, 0.f, 1.f);
    const ImVec4 white = ImVec4(1.f, 1.f, 1.f, 1.f);

    // Detect light theme: text is dark -> light background
    const bool light =
        (p.fg0.x * 0.2126f + p.fg0.y * 0.7152f + p.fg0.z * 0.0722f) < 0.5f;

    const ImVec4 bg_dark = light
        ? lerp(p.bg0, black, 0.05f)
        : lerp(black, p.bg0, 0.75f);

    const ImVec4 popup_bg = light
        ? lerp(p.bg0, white, 0.08f)
        : lerp(black, p.bg0, 0.88f);

    const ImVec4 stripe = light
        ? ImVec4(0.f, 0.f, 0.f, 0.04f)
        : ImVec4(1.f, 1.f, 1.f, 0.03f);

    c[ImGuiCol_Text]                  = p.fg0;
    c[ImGuiCol_TextDisabled]          = p.fg1;
    c[ImGuiCol_WindowBg]              = p.bg0;
    c[ImGuiCol_ChildBg]               = lerp(p.bg0, p.bg1, 0.50f);
    c[ImGuiCol_PopupBg]               = with_alpha(popup_bg, 0.98f);
    c[ImGuiCol_Border]                = p.brd;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_FrameBg]               = p.bg1;
    c[ImGuiCol_FrameBgHovered]        = p.bg2;
    c[ImGuiCol_FrameBgActive]         = lerp(p.bg2, p.brd, 0.50f);
    c[ImGuiCol_TitleBg]               = bg_dark;
    c[ImGuiCol_TitleBgActive]         = p.bg0;
    c[ImGuiCol_TitleBgCollapsed]      = light ? lerp(p.bg0, white, 0.12f)
                                              : lerp(black, p.bg0, 0.60f);
    c[ImGuiCol_MenuBarBg]             = bg_dark;
    c[ImGuiCol_ScrollbarBg]           = bg_dark;
    c[ImGuiCol_ScrollbarGrab]         = p.brd;
    c[ImGuiCol_ScrollbarGrabHovered]  = lerp(p.brd, p.acc, 0.30f);
    c[ImGuiCol_ScrollbarGrabActive]   = lerp(p.brd, p.acc, 0.60f);
    c[ImGuiCol_CheckMark]             = p.acc;
    c[ImGuiCol_SliderGrab]            = lerp(p.acc2, p.acc, 0.40f);
    c[ImGuiCol_SliderGrabActive]      = p.acc;
    c[ImGuiCol_Button]                = p.acc2;
    c[ImGuiCol_ButtonHovered]         = lerp(p.acc2, p.acc, 0.50f);
    c[ImGuiCol_ButtonActive]          = p.acc;
    c[ImGuiCol_Header]                = lerp(p.bg1, p.acc2, 0.40f);
    c[ImGuiCol_HeaderHovered]         = lerp(p.acc2, p.acc, 0.40f);
    c[ImGuiCol_HeaderActive]          = lerp(p.acc2, p.acc, 0.70f);
    c[ImGuiCol_Separator]             = p.brd;
    c[ImGuiCol_SeparatorHovered]      = with_alpha(lerp(p.brd, p.acc, 0.50f), 0.80f);
    c[ImGuiCol_SeparatorActive]       = p.acc;
    c[ImGuiCol_ResizeGrip]            = with_alpha(p.acc2, 0.40f);
    c[ImGuiCol_ResizeGripHovered]     = with_alpha(p.acc2, 0.75f);
    c[ImGuiCol_ResizeGripActive]      = p.acc;
    c[ImGuiCol_Tab]                   = p.acc2;
    c[ImGuiCol_TabHovered]            = lerp(p.acc2, p.acc, 0.50f);
    c[ImGuiCol_TabSelected]           = p.acc;
    c[ImGuiCol_TabDimmed]             = lerp(p.acc2, p.bg1, 0.60f);
    c[ImGuiCol_TabDimmedSelected]     = lerp(p.acc2, p.bg0, 0.40f);
    c[ImGuiCol_TableHeaderBg]         = lerp(p.bg0, p.bg1, 0.40f);
    c[ImGuiCol_TableBorderStrong]     = p.brd;
    c[ImGuiCol_TableBorderLight]      = lerp(p.bg0, p.brd, 0.40f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0.f, 0.f, 0.f, 0.f);
    c[ImGuiCol_TableRowBgAlt]         = stripe;
    c[ImGuiCol_TextSelectedBg]        = with_alpha(p.acc, 0.35f);
    c[ImGuiCol_DragDropTarget]        = with_alpha(p.acc, 0.90f);
    c[ImGuiCol_NavHighlight]          = p.acc;
    c[ImGuiCol_NavWindowingHighlight] = with_alpha(p.fg0, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = with_alpha(p.bg0, 0.20f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
    c[ImGuiCol_PlotLines]             = lerp(p.acc, p.fg0, 0.30f);
    c[ImGuiCol_PlotLinesHovered]      = p.acc;
    c[ImGuiCol_PlotHistogram]         = p.acc;
    c[ImGuiCol_PlotHistogramHovered]  = lerp(p.acc, p.fg0, 0.40f);
}

// ── apply_style ───────────────────────────────────────────────────────────────

void ThemeCustomizer::apply_style(const StyleParams& p) {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding    = p.rounding;
    st.FrameRounding     = p.rounding;
    st.ChildRounding     = p.rounding;
    st.PopupRounding     = p.rounding;
    st.TabRounding       = p.rounding;
    st.ScrollbarRounding = p.rounding;
    st.GrabRounding      = p.rounding;
    st.ItemSpacing       = { p.item_spacing,  p.item_spacing  * 0.50f };
    st.FramePadding      = { p.frame_padding, p.frame_padding * 0.667f };
}

// ── apply ─────────────────────────────────────────────────────────────────────

void ThemeCustomizer::apply(Theme& theme, ThemeStyle t) const {
    // Built-in path first: sets per-theme border sizes, spacing, and colors.
    theme.ApplyImGuiStyle(1.f);
    theme.ApplyColorTheme(t);
    // Override with our palette and style params.
    palette_to_imgui(palettes_[idx(t)], ImGui::GetStyle().Colors);
    apply_style(styles_[idx(t)]);
}

// ── Render ────────────────────────────────────────────────────────────────────

bool ThemeCustomizer::Render(Theme& theme, ThemeStyle& active) {
    if (!show_) return false;

    bool changed = false;

    ImGui::SetNextWindowSizeConstraints({ 300.f, 480.f }, { 420.f, 820.f });
    if (!ImGui::Begin("Theme Customizer", &show_,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return false;
    }

    // Theme selector
    int cur = idx(active);
    if (ImGui::Combo("##theme", &cur, kThemeNames, kThemeCount)) {
        active = static_cast<ThemeStyle>(cur);
        if (onThemeChanged) onThemeChanged(active); // lazy-load fonts before apply
        apply(theme, active);
        changed = true;
    }
    ImGui::SameLine(0.f, 6.f);
    ImGui::TextDisabled("theme");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Palette color pickers
    ThemePalette& pal    = palettes_[idx(active)];
    bool          pal_ch = false;

    constexpr ImGuiColorEditFlags kPickFlags =
        ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_NoLabel  |
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_DisplayHex;

    auto pick = [&](const char* id, const char* label, ImVec4& color) {
        ImGui::PushID(id);
        if (ImGui::ColorEdit4("##c", reinterpret_cast<float*>(&color), kPickFlags))
            pal_ch = true;
        ImGui::SameLine(0.f, 8.f);
        ImGui::TextUnformatted(label);
        ImGui::PopID();
    };

    ImGui::TextDisabled("background");
    pick("bg0", "bg0  window / title",   pal.bg0);
    pick("bg1", "bg1  frame / child",    pal.bg1);
    pick("bg2", "bg2  hover",            pal.bg2);

    ImGui::Spacing();
    ImGui::TextDisabled("foreground");
    pick("fg0", "fg0  text",             pal.fg0);
    pick("fg1", "fg1  text disabled",    pal.fg1);

    ImGui::Spacing();
    ImGui::TextDisabled("accent");
    pick("brd", "brd  border",           pal.brd);
    pick("acc", "acc  primary",          pal.acc);
    pick("ac2", "acc2 secondary",        pal.acc2);

    if (pal_ch) {
        palette_to_imgui(pal, ImGui::GetStyle().Colors);
        changed = true;
    }

    // Style sliders
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("style");

    StyleParams& st   = styles_[idx(active)];
    bool         st_ch = false;

    st_ch |= ImGui::SliderFloat("Rounding",      &st.rounding,      0.f, 12.f, "%.1f");
    st_ch |= ImGui::SliderFloat("Item spacing",  &st.item_spacing,  2.f, 16.f, "%.1f");
    st_ch |= ImGui::SliderFloat("Frame padding", &st.frame_padding, 1.f, 12.f, "%.1f");

    if (st_ch) {
        apply_style(st);
        changed = true;
    }

    // Buttons
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Apply")) {
        apply(theme, active);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to built-in")) {
        reset_to_builtin(active);
        apply(theme, active);
        changed = true;
    }

    ImGui::End();
    return changed;
}

// ── reset_to_builtin ──────────────────────────────────────────────────────────

void ThemeCustomizer::reset_to_builtin(ThemeStyle t) {
    palettes_[idx(t)] = builtin_palettes()[idx(t)];
    styles_  [idx(t)] = builtin_styles()  [idx(t)];
    fonts_   [idx(t)] = builtin_fonts()   [idx(t)];
}

// ── theme_file ────────────────────────────────────────────────────────────────

std::filesystem::path ThemeCustomizer::theme_file(ThemeStyle t,
                                                   const std::filesystem::path& dir) {
    std::string name = "#";
    name += kThemeIDs[idx(t)];
    name += ".json";
    return dir / name;
}

// ── save_theme / save_all ─────────────────────────────────────────────────────

bool ThemeCustomizer::save_theme(ThemeStyle t, const std::filesystem::path& dir,
                                 const io::Rfs& rfs) const {
    const auto r = rfs.resolve(dir);
    if (!r || !r->writable()) {
        std::println("[ThemeCustomizer] {} is read-only — cannot save theme", dir.string());
        return false;
    }
    try {
        std::filesystem::create_directories(dir);

        const int           i  = idx(t);
        const ThemePalette& p  = palettes_[i];
        const StyleParams&  st = styles_  [i];
        const ThemeFonts&   tf = fonts_   [i];

        json j;
        j["id"]      = kThemeIDs[i];
        j["title"]   = kThemeNames[i];
        j["palette"] = {
            { "bg0",  vec4_to_json(p.bg0)  },
            { "bg1",  vec4_to_json(p.bg1)  },
            { "bg2",  vec4_to_json(p.bg2)  },
            { "fg0",  vec4_to_json(p.fg0)  },
            { "fg1",  vec4_to_json(p.fg1)  },
            { "brd",  vec4_to_json(p.brd)  },
            { "acc",  vec4_to_json(p.acc)  },
            { "acc2", vec4_to_json(p.acc2) },
        };
        j["style"] = {
            { "rounding",      st.rounding      },
            { "item_spacing",  st.item_spacing  },
            { "frame_padding", st.frame_padding },
            { "font_scale",    st.font_scale    },
        };
        j["font"] = {
            { "main", { { "path", tf.ui.path   }, { "size_px", tf.ui.size_px   } } },
            { "mono", { { "path", tf.mono.path }, { "size_px", tf.mono.size_px } } },
            { "icon", { { "path", tf.icon.path }, { "size_px", tf.icon.size_px } } },
        };

        const auto   fname   = theme_file(t, dir);
        const auto   content = j.dump(2) + "\n";
        if (!io::write_text_file(fname, content)) {
            std::println("[ThemeCustomizer] cannot write: {}", fname.string());
            return false;
        }
        return true;

    } catch (const std::exception& ex) {
        std::println("[ThemeCustomizer] save error: {}", ex.what());
        return false;
    }
}

bool ThemeCustomizer::save_all(const std::filesystem::path& dir, const io::Rfs& rfs) const {
    bool ok = true;
    for (int i = 0; i < kThemeCount; ++i)
        ok &= save_theme(static_cast<ThemeStyle>(i), dir, rfs);
    return ok;
}

// ── parse_fonts_from_json ─────────────────────────────────────────────────────

namespace {
void parse_fonts_from_json(const json& j, ThemeFonts& tf) {
    if (!j.contains("font")) return;
    const auto& fnt = j["font"];
    auto read_fc = [](const json& fc, FontConfig& out) {
        if (!fc.is_object()) return;
        out.path    = fc.value("path",    out.path);
        out.size_px = fc.value("size_px", out.size_px);
    };
    if (fnt.contains("main")) read_fc(fnt["main"], tf.ui);
    if (fnt.contains("mono")) read_fc(fnt["mono"], tf.mono);
    if (fnt.contains("icon")) read_fc(fnt["icon"], tf.icon);
}
} // anon namespace

// ── load_builtin_definitions ─────────────────────────────────────────────────
//
//  Reads {id}.json (no # prefix) from themes_dir for indices 2–15.
//  SolarizedDark (0) and SolarizedLight (1) always come from builtin_palettes().
//
int ThemeCustomizer::load_builtin_definitions(const std::filesystem::path& dir,
                                              const io::Rfs& rfs) {
    const auto r = rfs.resolve(dir);
    if (!r || !r->readable()) return 0;
    int loaded = 0;
    for (int i = 2; i < kThemeCount; ++i) {
        const auto path = dir / (std::string(kThemeIDs[i]) + ".json");
        const auto text = io::read_text_file(path);
        if (!text) continue;
        try {
            const json j = json::parse(*text, nullptr, /*exceptions=*/true);
            ThemePalette& p  = palettes_[i];
            StyleParams&  st = styles_[i];
            ThemeFonts&   tf = fonts_[i];

            if (j.contains("palette")) {
                const auto& pal = j["palette"];
                if (pal.contains("bg0"))  p.bg0  = json_to_vec4(pal["bg0"],  p.bg0);
                if (pal.contains("bg1"))  p.bg1  = json_to_vec4(pal["bg1"],  p.bg1);
                if (pal.contains("bg2"))  p.bg2  = json_to_vec4(pal["bg2"],  p.bg2);
                if (pal.contains("fg0"))  p.fg0  = json_to_vec4(pal["fg0"],  p.fg0);
                if (pal.contains("fg1"))  p.fg1  = json_to_vec4(pal["fg1"],  p.fg1);
                if (pal.contains("brd"))  p.brd  = json_to_vec4(pal["brd"],  p.brd);
                if (pal.contains("acc"))  p.acc  = json_to_vec4(pal["acc"],  p.acc);
                if (pal.contains("acc2")) p.acc2 = json_to_vec4(pal["acc2"], p.acc2);
            }
            if (j.contains("style")) {
                const auto& s = j["style"];
                st.rounding      = s.value("rounding",      st.rounding);
                st.item_spacing  = s.value("item_spacing",  st.item_spacing);
                st.frame_padding = s.value("frame_padding", st.frame_padding);
                st.font_scale    = s.value("font_scale",    st.font_scale);
            }
            parse_fonts_from_json(j, tf);
            ++loaded;
        } catch (const std::exception& ex) {
            std::println("[ThemeCustomizer] builtin parse error ({}): {}",
                         path.string(), ex.what());
        }
    }
    return loaded;
}

// ── load_all ─────────────────────────────────────────────────────────────────

int ThemeCustomizer::load_all(const std::filesystem::path& dir, const io::Rfs& rfs) {
    const auto r = rfs.resolve(dir);
    if (!r || !r->readable()) return 0;
    int loaded = 0;
    for (int i = 0; i < kThemeCount; ++i) {
        const auto path = theme_file(static_cast<ThemeStyle>(i), dir);
        const auto text = io::read_text_file(path);
        if (!text) continue;
        try {
            const json j = json::parse(*text, nullptr, /*exceptions=*/true);
            ThemePalette& p  = palettes_[i];
            StyleParams&  st = styles_[i];
            ThemeFonts&   tf = fonts_[i];

            if (j.contains("palette")) {
                const auto& pal = j["palette"];
                if (pal.contains("bg0"))  p.bg0  = json_to_vec4(pal["bg0"],  p.bg0);
                if (pal.contains("bg1"))  p.bg1  = json_to_vec4(pal["bg1"],  p.bg1);
                if (pal.contains("bg2"))  p.bg2  = json_to_vec4(pal["bg2"],  p.bg2);
                if (pal.contains("fg0"))  p.fg0  = json_to_vec4(pal["fg0"],  p.fg0);
                if (pal.contains("fg1"))  p.fg1  = json_to_vec4(pal["fg1"],  p.fg1);
                if (pal.contains("brd"))  p.brd  = json_to_vec4(pal["brd"],  p.brd);
                if (pal.contains("acc"))  p.acc  = json_to_vec4(pal["acc"],  p.acc);
                if (pal.contains("acc2")) p.acc2 = json_to_vec4(pal["acc2"], p.acc2);
            }
            if (j.contains("style")) {
                const auto& s = j["style"];
                st.rounding      = s.value("rounding",      st.rounding);
                st.item_spacing  = s.value("item_spacing",  st.item_spacing);
                st.frame_padding = s.value("frame_padding", st.frame_padding);
                st.font_scale    = s.value("font_scale",    st.font_scale);
            }
            parse_fonts_from_json(j, tf);
            ++loaded;
        } catch (const std::exception& ex) {
            std::println("[ThemeCustomizer] parse error ({}): {}",
                         path.string(), ex.what());
        }
    }
    return loaded;
}

// ── builtin_palettes ─────────────────────────────────────────────────────────
//
//  Only the two default themes are hardcoded; all others load from JSON via
//  load_builtin_definitions().  The ThemePalette default-member-initializers
//  act as a last-resort fallback if a JSON file is missing.
//
std::array<ThemePalette, kThemeCount> ThemeCustomizer::builtin_palettes() noexcept {
    std::array<ThemePalette, kThemeCount> p{};

    auto make = [](float r0,float g0,float b0,float a0,
                   float r1,float g1,float b1,float a1,
                   float r2,float g2,float b2,float a2,
                   float r3,float g3,float b3,float a3,
                   float r4,float g4,float b4,float a4,
                   float r5,float g5,float b5,float a5,
                   float r6,float g6,float b6,float a6,
                   float r7,float g7,float b7,float a7) -> ThemePalette {
        ThemePalette t;
        t.bg0  = ImVec4(r0,g0,b0,a0);
        t.bg1  = ImVec4(r1,g1,b1,a1);
        t.bg2  = ImVec4(r2,g2,b2,a2);
        t.fg0  = ImVec4(r3,g3,b3,a3);
        t.fg1  = ImVec4(r4,g4,b4,a4);
        t.brd  = ImVec4(r5,g5,b5,a5);
        t.acc  = ImVec4(r6,g6,b6,a6);
        t.acc2 = ImVec4(r7,g7,b7,a7);
        return t;
    };

    // ── Default dark (SolarizedDark) ──────────────────────────────────────────
    p[0]  = make(0.000f,0.170f,0.210f,1.f,  0.020f,0.250f,0.300f,1.f,
                 0.180f,0.380f,0.380f,1.f,  0.930f,0.910f,0.840f,1.f,
                 0.400f,0.550f,0.560f,1.f,  0.080f,0.300f,0.350f,1.f,
                 0.420f,0.800f,0.820f,1.f,  0.110f,0.280f,0.320f,1.f);

    // ── Default light (SolarizedLight) ────────────────────────────────────────
    p[1]  = make(0.990f,0.960f,0.890f,1.f,  0.870f,0.840f,0.780f,1.f,
                 0.720f,0.760f,0.760f,1.f,  0.000f,0.170f,0.210f,1.f,
                 0.450f,0.550f,0.570f,1.f,  0.720f,0.720f,0.680f,1.f,
                 0.200f,0.520f,0.530f,1.f,  0.750f,0.650f,0.530f,1.f);

    // Indices 2–15 are populated by load_builtin_definitions() at runtime.
    // ThemePalette default-member-initializers provide a neutral dark fallback.

    return p;
}

// ── builtin_styles ────────────────────────────────────────────────────────────
//
//  Mirrors Theme::ApplyThemeStyle_() groupings.
//
std::array<StyleParams, kThemeCount> ThemeCustomizer::builtin_styles() noexcept {
    std::array<StyleParams, kThemeCount> s{};

    // Classic: Solarized x2, Monokai x2
    s[0] = s[1] = s[2] = s[3] = { 4.f, 8.f,  6.f, 1.f };
    // MonaSpaces (botanical)
    s[4]                       = { 10.f, 10.f, 10.f, 1.f };
    // EarthSUSE x2
    s[5] = s[6]                = { 5.f, 8.f,  6.f, 1.f };
    // NeonSpaces
    s[7]                       = { 2.f, 8.f,  6.f, 1.f };
    // DawnBringer16 x2 (retro pixel)
    s[8] = s[9]                = { 0.f, 10.f, 8.f, 1.f };
    // Material x2
    s[10] = s[11]              = { 8.f, 8.f,  8.f, 1.f };
    // MonoLight, MonoDark
    s[12] = s[13]              = { 2.f, 6.f,  4.f, 1.f };
    // DawnBringerLight, DawnBringerDark
    s[14] = s[15]              = { 0.f, 10.f, 8.f, 1.f };

    return s;
}

// ── builtin_fonts ─────────────────────────────────────────────────────────────
//
//  Default font pair for each theme.  These are the same fonts that
//  default_main_font() / default_icon_font() return, pre-packaged as a
//  ThemeFonts.  load_builtin_definitions() overlays JSON values on top.
//
std::array<ThemeFonts, kThemeCount> ThemeCustomizer::builtin_fonts() noexcept {
    using enum ThemeStyle;
    std::array<ThemeFonts, kThemeCount> f{};

    const FontConfig kIconDefault = { "fonts/fa-6-solid-900.otf", 14.f };

    // Proportional-UI themes: ui = Roboto, mono = JetBrainsMono
    for (int i : {int(SolarizedDark), int(SolarizedLight),
                  int(Monokai), int(MonokaiDark),
                  int(Material), int(MaterialDark)}) {
        f[i].ui   = { "fonts/Roboto-Medium.ttf",     15.f };
        f[i].mono = { "fonts/JetBrainsMono-Thin.ttf", 14.f };
        f[i].icon = kIconDefault;
    }
    // EarthSUSE: SUSE-Light UI, SUSEMono code
    for (int i : {int(EarthSUSE), int(EarthSUSEDark)}) {
        f[i].ui   = { "fonts/SUSE-Light.ttf",    15.f };
        f[i].mono = { "fonts/SUSEMono-Light.ttf", 14.f };
        f[i].icon = kIconDefault;
    }
    // Monaspace / NeonSpaces: same font for both UI and code
    for (int i : {int(MonaSpaces), int(NeonSpaces)}) {
        f[i].ui   = { "fonts/MonaspaceArgonNF-Regular.otf", 15.f };
        f[i].mono = f[i].ui;
        f[i].icon = kIconDefault;
    }
    // DawnBringer16: Krypton for both
    for (int i : {int(DawnBringer16Dark), int(DawnBringer16Light)}) {
        f[i].ui   = { "fonts/MonaspaceKryptonNF-WideLight.otf", 13.f };
        f[i].mono = f[i].ui;
        f[i].icon = kIconDefault;
    }
    // MonoLight/MonoDark: JetBrains for both
    for (int i : {int(MonoLight), int(MonoDark)}) {
        f[i].ui   = { "fonts/JetBrainsMono-Thin.ttf", 14.f };
        f[i].mono = f[i].ui;
        f[i].icon = kIconDefault;
    }
    // DawnBringer: Hack for both
    for (int i : {int(DawnBringerLight), int(DawnBringerDark)}) {
        f[i].ui   = { "fonts/Hack-Regular.ttf", 14.f };
        f[i].mono = f[i].ui;
        f[i].icon = kIconDefault;
    }
    return f;
}

} // namespace datagrid::ui
