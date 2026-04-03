#include "settings.hpp"

#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#elif defined(_WIN32)
#  include <windows.h>
#endif

#include <nlohmann/json.hpp>
#include <print>
#include <filesystem>

#include "imgui.h"
#include "io/file_io.hpp"

using json = nlohmann::json;

namespace datagrid {

// ── JSON helpers ──────────────────────────────────────────────────────────────

namespace {

void to_json(json& j, const FontConfig& f) {
    j = json{ { "path", f.path }, { "size_px", f.size_px } };
}

void from_json(const json& j, FontConfig& f) {
    f.path    = j.value("path",    f.path);
    f.size_px = j.value("size_px", f.size_px);
}

void to_json(json& j, const StyleParams& p) {
    j = json{
        { "rounding",      p.rounding      },
        { "item_spacing",  p.item_spacing  },
        { "frame_padding", p.frame_padding },
        { "font_scale",    p.font_scale    },
    };
}

void from_json(const json& j, StyleParams& p) {
    p.rounding      = j.value("rounding",      p.rounding);
    p.item_spacing  = j.value("item_spacing",  p.item_spacing);
    p.frame_padding = j.value("frame_padding", p.frame_padding);
    p.font_scale    = j.value("font_scale",    p.font_scale);
}

void to_json(json& j, const ThemeSettings& t) {
    j = json{
        { "active",     std::string(theme_id(t.active)) },
        { "style",      {} },
        { "main_font",  {} },
        { "icon_font",  {} },
    };
    to_json(j["style"],     t.style);
    to_json(j["main_font"], t.main_font);
    to_json(j["icon_font"], t.icon_font);
}

void from_json(const json& j, ThemeSettings& t) {
    t.active = theme_from_id(j.value("active", std::string(theme_id(t.active))));
    if (j.contains("style"))     from_json(j.at("style"),     t.style);
    if (j.contains("main_font")) from_json(j.at("main_font"), t.main_font);
    if (j.contains("icon_font")) from_json(j.at("icon_font"), t.icon_font);
}

void to_json(json& j, const PanelSettings& p) {
    j = json{
        { "show_layers",        p.show_layers        },
        { "show_properties",    p.show_properties    },
        { "show_canvas",        p.show_canvas        },
        { "show_text_editor",   p.show_text_editor   },
        { "show_shader_list",   p.show_shader_list   },
        { "show_shader_render", p.show_shader_render },
    };
}

void from_json(const json& j, PanelSettings& p) {
    p.show_layers        = j.value("show_layers",        p.show_layers);
    p.show_properties    = j.value("show_properties",    p.show_properties);
    p.show_canvas        = j.value("show_canvas",        p.show_canvas);
    p.show_text_editor   = j.value("show_text_editor",   p.show_text_editor);
    p.show_shader_list   = j.value("show_shader_list",   p.show_shader_list);
    p.show_shader_render = j.value("show_shader_render", p.show_shader_render);
}

} // anon namespace

// ── Settings::load / save ─────────────────────────────────────────────────────

bool Settings::load(const std::filesystem::path& path) noexcept {
    const auto text = io::read_text_file(path);
    if (!text) return false;
    try {
        json j = json::parse(*text, nullptr, /*exceptions=*/true);
        version = j.value("version", version);
        if (j.contains("theme"))  from_json(j.at("theme"),  theme);
        if (j.contains("panels")) from_json(j.at("panels"), panels);
        return true;
    } catch (const std::exception& ex) {
        std::println("[Settings] parse error ({}): {}", path.string(), ex.what());
        return false;
    }
}

bool Settings::save(const std::filesystem::path& path) const noexcept {
    try {
        json j;
        j["version"] = version;
        to_json(j["theme"],  theme);
        to_json(j["panels"], panels);

        const auto content = j.dump(2) + "\n";
        if (!io::write_text_file(path, content)) {
            std::println("[Settings] cannot write: {}", path.string());
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        std::println("[Settings] save error: {}", ex.what());
        return false;
    }
}

std::filesystem::path Settings::default_path() noexcept {
#if defined(__APPLE__)
    char buf[4096] = {};
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        return std::filesystem::path(buf).parent_path() / "settings.json";
    }
#elif defined(_WIN32)
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path() / "settings.json";
#endif
    return std::filesystem::current_path() / "settings.json";
}

std::filesystem::path Settings::exe_dir() noexcept {
#if defined(__APPLE__)
    char buf[4096] = {};
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        return std::filesystem::path(buf).parent_path();
#elif defined(_WIN32)
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#endif
    return std::filesystem::current_path();
}

void Settings::apply_theme(float dpi_scale) const noexcept {
    ui::Theme t;
    t.ApplyImGuiStyle(dpi_scale, theme.style);
    t.ApplyColorTheme(theme.active);
}

// ── Font atlas loading ────────────────────────────────────────────────────────
//
//  Call AFTER simgui_setup() with no_default_font=true.
//  sokol-imgui (with ImGui 1.92+ RendererHasTextures) builds the atlas
//  automatically on the first frame — do NOT call Clear() or Build() manually.
//  Font paths are resolved via io::Rfs (e.g. resolve("resources/fonts/foo.ttf")).
//

void Settings::load_fonts(const io::Rfs& rfs, float dpi_scale) const noexcept {
    if (!ImGui::GetCurrentContext()) return;

    const float scale = (dpi_scale > 0.f) ? dpi_scale : 1.f;

    ImGuiIO& io = ImGui::GetIO();

    // ── Resolve main font ─────────────────────────────────────────────────────

    FontConfig main = theme.main_font;
    if (main.path.empty())
        main = default_main_font(theme.active);

    ImFont* main_ptr = nullptr;
    if (!main.path.empty()) {
        if (const auto full = rfs.resolve(main.path)) {
            main_ptr = io.Fonts->AddFontFromFileTTF(
                full->native().string().c_str(), main.size_px * scale);
            if (main_ptr)
                std::println("[Fonts] loaded main: {} @ {}px", main.path, main.size_px * scale);
        } else {
            std::println("[Fonts] main font not found: {}", main.path);
        }
    }
    if (!main_ptr) {
        io.Fonts->AddFontDefault();
        std::println("[Fonts] using ImGui built-in default font");
    }

    // ── Merge icon font ───────────────────────────────────────────────────────

    FontConfig icon = theme.icon_font;
    if (icon.path.empty())
        icon = default_icon_font(theme.active);

    if (!icon.path.empty()) {
        if (const auto full = rfs.resolve(icon.path)) {
            static const ImWchar fa_ranges[] = { 0xF000, 0xF8FF, 0 };
            ImFontConfig cfg;
            cfg.MergeMode        = true;
            cfg.GlyphMinAdvanceX = icon.size_px * scale;
            cfg.PixelSnapH       = true;
            io.Fonts->AddFontFromFileTTF(
                full->native().string().c_str(), icon.size_px * scale, &cfg, fa_ranges);
            std::println("[Fonts] merged icons: {} @ {}px", icon.path, icon.size_px * scale);
        } else {
            std::println("[Fonts] icon font not found: {}", icon.path);
        }
    }
    // ImGui 1.92 with RendererHasTextures builds the atlas on the first frame automatically.
}

} // namespace datagrid
