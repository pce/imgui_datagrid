#include "app.hpp"

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "util/sokol_imgui.h"

#include "adapters/adapter_kind.hpp"
#include "adapters/adapter_registry.hpp"
#include "adapters/data_source.hpp"
#ifdef DATAGRID_HAVE_DUCKDB
#include "adapters/duckdb/duckdb_adapter.hpp"
#endif

#include <algorithm>
#include <filesystem>
#include <format>
#include <print>
#include <ranges>

#include "io/drag_drop.hpp"
#include "ui/drag_drop_dialog.hpp"
#include "ui/icons.hpp"
#include "ui/filetype_sniffer.hpp"
#include "io/platform.hpp"
#include "settings.hpp"

#if defined(__APPLE__)
static constexpr uint32_t kPlatformMod = SAPP_MODIFIER_SUPER;
#else
static constexpr uint32_t kPlatformMod = SAPP_MODIFIER_CTRL;
#endif

namespace datagrid {

void App::Init()
{
    verbose_ = (std::getenv("DATAGRID_VERBOSE") != nullptr);

    // ── Mount points ──────────────────────────────────────────────────────────
    //  data/    (resources/) → RO  bundled assets: fonts, shaders, default themes
    //  config/  (exe-dir/)   → RW  user config: settings.json, theme overrides, .ini
    //
    //  More-specific data/ is mounted last so it wins in longest-prefix matching.
    exeDir_ = Settings::exe_dir();

    //  exeDir_/             RW — user config (settings.json, .ini, theme overrides)
    //  exeDir_/resources/   RO — bundled read-only assets (fonts, shaders, built-in themes)
    //  exeDir_/resources/themes/  RW — intentional escalation: user may write theme overrides
    //                             into the same directory as the built-in themes.
    //                             force=true because child is RW while parent is RO.
    auto logMount = [&](io::MountStatus s, const char* label) {
        if (!verbose_) return;
        const char* tag = [&] {
            switch (s) {
                case io::MountStatus::Mounted:   return "mounted";
                case io::MountStatus::Updated:   return "updated";
                case io::MountStatus::Narrowed:  return "narrowed";
                case io::MountStatus::Escalated: return "escalated";
                case io::MountStatus::Rejected:  return "REJECTED";
            }
            return "?";
        }();
        std::println("[RFS] {} {}", tag, label);
    };
    logMount(rfs_.mount(exeDir_,                          io::Perms::RW),             "config/");
    logMount(rfs_.mount(exeDir_ / "resources",            io::Perms::RO),             "data/");
    logMount(rfs_.mount(exeDir_ / "resources" / "themes", io::Perms::RW, /*force*/true), "data/themes/ (escalated RO→RW)");

    // Load persisted config before anything that depends on activeTheme_
    // (font loading, theme apply).  Adapter / path preferences are also here.
    config_      = LoadAppConfig();
    activeTheme_ = config_.activeTheme;

    // Load theme palettes + fonts: built-in JSON definitions first, then user overrides on top.
    const auto themesDir = exeDir_ / "resources" / "themes";
    {
        const int defs = customizer_.load_builtin_definitions(themesDir, rfs_);
        const int ovrd = customizer_.load_all(themesDir, rfs_);
        if (verbose_)
            std::println("[Themes] {} built-in definitions, {} user overrides loaded", defs, ovrd);
    }

    const sg_desc gfx_desc = {
        .logger      = {.func = slog_func},
        .environment = sglue_environment(),
    };
    sg_setup(&gfx_desc);

    IMGUI_CHECKVERSION();
    const simgui_desc_t imgui_desc = {
        .logger = {.func = slog_func},
    };
    simgui_setup(&imgui_desc);

    ImGui::GetIO().IniFilename = "databrowser.ini";

    dpi_ = sapp_dpi_scale();
    if (verbose_)
        std::println("[Init] DPI scale: {:.2f}", dpi_);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Wire up the theme-changed callback so fonts are lazy-loaded before apply().
    customizer_.onThemeChanged = [this](ui::ThemeStyle t) {
        activeTheme_ = t;
        LoadThemeFonts(t);
    };

    // Load the startup theme fonts — must be the FIRST font added (ImGui atlas default).
    LoadThemeFonts(activeTheme_);

    io.FontGlobalScale = (1.0f / dpi_);
#if defined(__APPLE__)
    io.ConfigMacOSXBehaviors = true;
#endif

    // Apply startup theme via the customizer so palette colors are also applied.
    theme_.ApplyImGuiStyle(dpi_);
    customizer_.apply(theme_, activeTheme_);

    // Push initial code font into the text viewer.
    textViewer_.SetCodeFont(theme_.codeFont);

    UpdateLayout();
    layout_.ApplyToImGui();

    // Sync code font, palette colors and persistence whenever the navbar switches themes.
    navbar_.onThemeApplied = [this](ui::ThemeStyle t) {
        activeTheme_ = t;
        LoadThemeFonts(t);          // lazy-load fonts; atlas rebuilt next frame
        customizer_.apply(theme_, t);
        textViewer_.SetCodeFont(theme_.codeFont);
        if (browser_)    browser_->SetCodeFont(theme_.codeFont);
        if (browser_fs_) browser_fs_->SetCodeFont(theme_.codeFont);
        config_.activeTheme = t;
        SaveAppConfig(config_);
    };

    const auto entries = adapters::AdapterRegistry::Entries();

    const std::string_view preferredAdapter = !dbPath.empty()                ? std::string_view{adapterName}
                                              : !config_.adapterName.empty() ? std::string_view{config_.adapterName}
                                              : !entries.empty()             ? entries.front().name
                                                                             : std::string_view{};

    if (const auto it = std::ranges::find(entries, preferredAdapter, &adapters::AdapterEntry::name);
        it != entries.end()) {
        adapterIdx_ = static_cast<int>(std::distance(entries.begin(), it));
    }

    const std::string preferredPath = !dbPath.empty()                     ? dbPath
                                      : !config_.connectionString.empty() ? config_.connectionString
                                                                          : std::string{};

    if (!preferredPath.empty())
        std::snprintf(pathBuf_, sizeof(pathBuf_), "%s", preferredPath.c_str());

    if (!preferredPath.empty() && !preferredAdapter.empty())
        TryConnect(std::string{preferredAdapter}, preferredPath);

    {
        namespace fs = std::filesystem;
        std::string startDir;
        if (!preferredPath.empty()) {
            const fs::path p{preferredPath};
            startDir = fs::is_directory(p) ? p.string() : (p.has_parent_path() ? p.parent_path().string() : p.string());
        } else {
            const char* home = std::getenv("HOME");
#ifdef _WIN32
            if (!home)
                home = std::getenv("USERPROFILE");
#endif
            startDir = home ? std::string{home} : std::string{"/"};
        }
        InitFsBrowser(startDir);
    }
    SetupDragDrop();
}

void App::Cleanup()
{
    browser_.reset();
    browser_fs_.reset();
    imageCache_.Clear();
    simgui_shutdown();
    sg_shutdown();
}

// ── LoadThemeFonts ────────────────────────────────────────────────────────────
//
//  Lazily loads the UI (sans-serif) and mono (code) fonts for theme t into the
//  ImGui font atlas.  Safe to call every frame; does nothing after the first call.
//
//  sokol_imgui sets ImGuiBackendFlags_RendererHasTextures so the atlas is
//  rebuilt and the GPU texture re-uploaded automatically on the next frame.
//
void App::LoadThemeFonts(ui::ThemeStyle t)
{
    const int i = static_cast<int>(t);
    if (themeFontsLoaded_[i]) return;

    const ui::ThemeFonts& tf = customizer_.fonts(t);
    ImGuiIO& io = ImGui::GetIO();

    // Extended Unicode ranges for UI fonts (Basic Latin, Latin-1, dashes, arrows).
    static const ImWchar kUiRanges[] = {
        0x0020, 0x00FF,
        0x2013, 0x2015,
        0x2190, 0x21FF,
        0,
    };

    // Helper: resolve an Rfs-relative path to a native absolute path.
    // Font paths in JSON/builtin are written relative to resources/ (e.g.
    // "fonts/Roboto-Medium.ttf"), so we prepend resources/ before resolving.
    // Falls back to a direct exeDir_-relative lookup for paths that already
    // start with "resources/".
    auto resolve = [&](const std::string& rel) -> std::string {
        if (rel.empty()) return {};
        namespace fs = std::filesystem;
        // Try resources/ prefix first (canonical location for bundled assets).
        for (const fs::path candidate : { exeDir_ / "resources" / rel, exeDir_ / rel }) {
            const auto r = rfs_.resolve(candidate);
            if (!r || !r->readable()) continue;
            std::error_code ec;
            if (!fs::exists(candidate, ec) || ec) continue;
            return candidate.string();
        }
        if (verbose_) std::println("[Font] not found or denied: {}", rel);
        return {};
    };

    // ── UI (sans-serif) font — Font Awesome icons merged in ───────────────────
    ImFont* uiFont = nullptr;
    {
        const std::string path = resolve(tf.ui.path);
        if (!path.empty()) {
            const float sz = (tf.ui.size_px > 0.f ? tf.ui.size_px : 15.f) * dpi_;
            uiFont = io.Fonts->AddFontFromFileTTF(path.c_str(), sz, nullptr, kUiRanges);
            if (uiFont) {
                if (verbose_) std::println("[Font] ui  {} @ {:.0f}px: OK", tf.ui.path, sz);
                // Merge FA icons into the UI font.
                const std::string iconPath = resolve(tf.icon.path);
                if (!iconPath.empty()) {
                    const float isz = (tf.icon.size_px > 0.f ? tf.icon.size_px : 14.f) * dpi_;
                    ImFontConfig cfg;
                    cfg.MergeMode        = true;
                    cfg.PixelSnapH       = true;
                    cfg.GlyphMinAdvanceX = isz;
                    io.Fonts->AddFontFromFileTTF(iconPath.c_str(), isz,
                                                 &cfg, ui::icons::kFAGlyphRanges);
                }
            } else {
                if (verbose_) std::println("[Font] ui  {} MISSING", tf.ui.path);
            }
        }
    }

    // ── Mono (code) font ──────────────────────────────────────────────────────
    ImFont* monoFont = nullptr;
    {
        const std::string path = resolve(tf.mono.path);
        if (!path.empty()) {
            const float sz = (tf.mono.size_px > 0.f ? tf.mono.size_px : 14.f) * dpi_;
            monoFont = io.Fonts->AddFontFromFileTTF(path.c_str(), sz);
            if (verbose_) std::println("[Font] mono {} @ {:.0f}px: {}",
                                        tf.mono.path, sz, monoFont ? "OK" : "MISSING");
        }
    }

    theme_.RegisterFonts(t, uiFont, monoFont);
    themeFontsLoaded_[i] = true;
}

void App::Frame()
{
    UpdateLayout();
    if (layout_.HasChanged())
        layout_.ApplyToImGui();

    if (navbar_.wantsFullscreen) {
        sapp_toggle_fullscreen();
        navbar_.wantsFullscreen = false;
    }
    if (navbar_.wantsQuit) {
        sapp_quit();
        return;
    }
    if (navbar_.wantsOpen) {
        showWelcome_ = true;
        connectError_.clear();
        navbar_.wantsOpen = false;
    }
    if (navbar_.wantsThemeCustomizer) {
        customizer_.Toggle();
        navbar_.wantsThemeCustomizer = false;
    }

    const simgui_frame_desc_t fd = {
        .width      = sapp_width(),
        .height     = sapp_height(),
        .delta_time = sapp_frame_duration(),
        .dpi_scale  = sapp_dpi_scale(),
    };
    simgui_new_frame(&fd);

    RenderFrame();

    sg_pass_action pass_action        = {};
    pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    const ImVec4 winBg                = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    pass_action.colors[0].clear_value = {winBg.x, winBg.y, winBg.z, winBg.w};

    const sg_pass pass = {
        .action    = pass_action,
        .swapchain = sglue_swapchain(),
    };
    sg_begin_pass(pass);
    simgui_render();
    sg_end_pass();
    sg_commit();
}

void App::Event(const sapp_event* ev)
{
    simgui_handle_event(ev);

    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
        const bool mod = (ev->modifiers & kPlatformMod) != 0;
        switch (ev->key_code) {
            case SAPP_KEYCODE_F11:
                sapp_toggle_fullscreen();
                break;

            case SAPP_KEYCODE_O:
                if (mod) {
                    showWelcome_ = true;
                    connectError_.clear();
                }
                break;

            case SAPP_KEYCODE_Q:
                if (mod)
                    sapp_quit();
                break;

            case SAPP_KEYCODE_W:
                if (mod) {
                    if (showWelcome_) {
                        showWelcome_ = false;
                    } else if (browser_ && browser_->IsInspectorOpen()) {
                        browser_->CloseInspector();
                    } else if (browser_fs_ && browser_fs_->IsInspectorOpen()) {
                        browser_fs_->CloseInspector();
                    }
                }
                break;

            case SAPP_KEYCODE_ESCAPE:
                if (showWelcome_)
                    showWelcome_ = false;
                break;

            default:
                break;
        }
    }
}

void App::RenderFrame()
{
    {
        std::vector<ui::WindowEntry> wins;

        if (browser_) {
            const std::string winId = browser_->ImGuiWindowId();
            wins.push_back(
                {browser_->WindowTitle(), browser_->IsFocused(), [winId]() { ImGui::SetWindowFocus(winId.c_str()); }});
            const std::string inspLabel = browser_->InspectorWindowLabel();
            if (!inspLabel.empty()) {
                const std::string inspId = browser_->InspectorImGuiWindowId();
                wins.push_back({inspLabel,
                                false, // inspector focus tracking not needed
                                [inspId]() { ImGui::SetWindowFocus(inspId.c_str()); }});
            }
        }

        if (browser_fs_) {
            const std::string winId = browser_fs_->ImGuiWindowId();
            wins.push_back({browser_fs_->WindowTitle(), browser_fs_->IsFocused(), [winId]() {
                                ImGui::SetWindowFocus(winId.c_str());
                            }});
            const std::string inspLabel = browser_fs_->InspectorWindowLabel();
            if (!inspLabel.empty()) {
                const std::string inspId = browser_fs_->InspectorImGuiWindowId();
                wins.push_back({inspLabel, false, [inspId]() { ImGui::SetWindowFocus(inspId.c_str()); }});
            }
        }

        navbar_.SetWindows(std::move(wins));
    }

    navbar_.Render(theme_);

    const ImGuiIO& io    = ImGui::GetIO();
    const float    topY  = ImGui::GetFrameHeightWithSpacing();
    const float    dispW = io.DisplaySize.x;
    const float    dispH = io.DisplaySize.y - topY;

    const bool hasMain = browser_.has_value() && !showWelcome_;
    const bool hasFs   = browser_fs_.has_value();

    if (showWelcome_) {
        RenderWelcome();
        if (!hasFs)
            return;
    }

    if (hasFs && hasMain) {
        constexpr float kFsRatio = 0.35f;
        const float     fsW      = dispW * kFsRatio;
        const float     mainW    = dispW - fsW;

        browser_fs_->SetLayout(layout_);
        ImGui::SetNextWindowPos(ImVec2(0.0f, topY), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(fsW, dispH), ImGuiCond_Appearing);
        browser_fs_->Render();

        browser_->SetLayout(layout_);
        ImGui::SetNextWindowPos(ImVec2(fsW, topY), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(mainW, dispH), ImGuiCond_Appearing);
        browser_->Render();

    } else if (hasFs) {
        browser_fs_->SetLayout(layout_);
        ImGui::SetNextWindowPos(ImVec2(0.0f, topY), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(dispW, dispH), ImGuiCond_Appearing);
        browser_fs_->Render();

    } else if (hasMain) {
        browser_->SetLayout(layout_);
        ImGui::SetNextWindowPos(ImVec2(0.0f, topY), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(dispW, dispH), ImGuiCond_Appearing);
        browser_->Render();
    }

    RenderImageViewer();
    RenderTextViewer();
    dropDialog_.Render();

    // Theme Customizer floating window (toggled via Settings → "Theme Customizer…")
    if (customizer_.Render(theme_, activeTheme_)) {
        textViewer_.SetCodeFont(theme_.codeFont);
        if (browser_)    browser_->SetCodeFont(theme_.codeFont);
        if (browser_fs_) browser_fs_->SetCodeFont(theme_.codeFont);
        config_.activeTheme = activeTheme_;
        SaveAppConfig(config_);
    }
}

void App::RenderWelcome()
{
    const ImGuiIO& io     = ImGui::GetIO();
    const ImVec2   center = {io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f};

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540.0f, 0.0f), ImGuiCond_Always);

    ImGui::Begin("##welcome", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::SetWindowFontScale(1.45f);
    ImGui::TextUnformatted("DataBrowser");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Open a data source to get started");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const auto entries = adapters::AdapterRegistry::Entries();

    if (entries.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "No adapters registered.");
        ImGui::End();
        return;
    }

    if (adapterIdx_ >= static_cast<int>(entries.size()))
        adapterIdx_ = 0;

    constexpr float kLabelW = 110.0f;

    ImGui::Text("Adapter");
    ImGui::SameLine(kLabelW);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##adapter", entries[adapterIdx_].name.c_str())) {
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const bool sel = (i == adapterIdx_);
            if (ImGui::Selectable(entries[i].name.c_str(), sel))
                adapterIdx_ = i;
            if (sel)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    const char* hint = "Connection string \xe2\x80\xa6";
    if (const auto k = adapters::kind_of(entries[adapterIdx_].name)) {
        switch (*k) {
            case adapters::AdapterKind::SQLite:
                hint = "Path to .db or .sqlite file \xe2\x80\xa6";
                break;
            case adapters::AdapterKind::CSV:
                hint = "Path to .csv file \xe2\x80\xa6";
                break;
            case adapters::AdapterKind::Filesystem:
                hint = "Directory path \xe2\x80\xa6";
                break;
            case adapters::AdapterKind::DuckDB:
                hint = "Path to .duckdb file, or :memory: for in-memory \xe2\x80\xa6";
                break;
            case adapters::AdapterKind::PostgreSQL:
                hint = "host=localhost port=5432 dbname=mydb user=postgres \xe2\x80\xa6";
                break;
        }
    }

    ImGui::Text("Path / URI");
    ImGui::SameLine(kLabelW);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const bool enterPressed =
        ImGui::InputTextWithHint("##path", hint, pathBuf_, sizeof(pathBuf_), ImGuiInputTextFlags_EnterReturnsTrue);

    const bool isSqlite = (entries[adapterIdx_].name == "sqlite");
    const bool isDuckDb = (entries[adapterIdx_].name == "duckdb");
    const bool isCsv    = (entries[adapterIdx_].name == "csv");
    const bool isFs     = (entries[adapterIdx_].name == "filesystem");

    if (isSqlite) {
        ImGui::Spacing();
        ImGui::Text("Passphrase");
        ImGui::SameLine(kLabelW);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##passphrase", passBuf_, sizeof(passBuf_), ImGuiInputTextFlags_Password);
    }

    {
        ImGui::Spacing();
        ImGui::Text("Read-only");
        ImGui::SameLine(kLabelW);
        ImGui::Checkbox("##readonly", &readOnly_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            const char* tip = isSqlite   ? "Open read-only (safer; required for encrypted DBs unless write is needed)"
                              : isDuckDb ? "Open DuckDB file in read-only mode"
                              : isCsv    ? "Open CSV in read-only mode — prevents in-place edits"
                              : isFs     ? "Browse directory in read-only mode — prevents file operations"
                                         : "Open in read-only mode";
            ImGui::SetTooltip("%s", tip);
        }
    }

    ImGui::Spacing();

    const bool hasPath = pathBuf_[0] != '\0';
    if (!hasPath)
        ImGui::BeginDisabled();

    const bool doConnect =
        ImGui::Button("Connect", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)) || (hasPath && enterPressed);

    if (!hasPath)
        ImGui::EndDisabled();

    if (!config_.connectionString.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Last used: %s  (%s)",
                            std::filesystem::path(config_.connectionString).filename().string().c_str(),
                            config_.adapterName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Restore")) {
            std::snprintf(pathBuf_, sizeof(pathBuf_), "%s", config_.connectionString.c_str());
            if (const auto it = std::ranges::find(entries, config_.adapterName, &adapters::AdapterEntry::name);
                it != entries.end()) {
                adapterIdx_ = static_cast<int>(std::distance(entries.begin(), it));
            }
        }
    }

    if (!connectError_.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "  %s", connectError_.c_str());
    }

    ImGui::End();

    if (doConnect)
        TryConnect(entries[adapterIdx_].name,
                   pathBuf_,
                   passBuf_[0] != '\0' ? std::string{passBuf_} : std::string{},
                   readOnly_);
}

void App::TryConnect(const std::string& adapter, const std::string& path, const std::string& passphrase, bool readOnly)
{
    connectError_.clear();

    adapters::ConnectionParams p;
    p.adapterName      = adapter;
    p.connectionString = path;
    p.readOnly         = readOnly;
    if (!passphrase.empty())
        p.password = passphrase;

    (void)adapters::AdapterRegistry::CreateConnected(adapter, p)
        .or_else([&](std::string err) -> std::expected<adapters::DataSourcePtr, std::string> {
            connectError_ = err;
            std::println(stderr, "[App] Connect failed ({}): {}", adapter, err);
            return std::unexpected(std::move(err));
        })
        .and_then([&](adapters::DataSourcePtr ds) -> std::expected<void, std::string> {
            config_.adapterName      = adapter;
            config_.connectionString = path;
            SaveAppConfig(config_);

            // For DuckDB :memory: (or empty path) show a readable label
            // instead of the empty string that filename() would return.
            const std::string pathLabel = (path.empty() || path == ":memory:")
                                              ? std::string{"(in-memory)"}
                                              : std::filesystem::path(path).filename().string();

            browser_.emplace(std::move(ds), std::format("{} \xe2\x80\x94 {}", adapter, pathLabel));
            browser_->SetCodeFont(theme_.codeFont);
            showWelcome_ = false;

#ifdef DATAGRID_HAVE_DUCKDB
            if (adapter == "duckdb") {
                browser_->SetPreContentHook([this]() {
                    if (ImGui::SmallButton("+ File source")) {
                        ImGui::OpenPopup("##duckdb_addsource");
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Scan a CSV / Parquet / JSON file into DuckDB as a view");

                    if (ImGui::BeginPopup("##duckdb_addsource")) {
                        ImGui::TextDisabled("Enter a file or directory path:");
                        ImGui::SetNextItemWidth(320.0f);
                        static char scanPathBuf[1024] = {};
                        const bool  go                = ImGui::InputText(
                            "##scanpath", scanPathBuf, sizeof(scanPathBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::SameLine();
                        if (ImGui::Button("Scan") || go) {
                            if (scanPathBuf[0] != '\0' && browser_ && browser_->GetSource()) {
                                namespace fs = std::filesystem;
                                auto* duck   = dynamic_cast<adapters::DuckDBAdapter*>(browser_->GetSource());
                                if (duck) {
                                    const fs::path  p{scanPathBuf};
                                    std::error_code ec;
                                    const auto      result = fs::is_directory(p, ec) ? duck->ScanDirectory(scanPathBuf)
                                                                                     : duck->ScanFile(scanPathBuf);
                                    if (!result)
                                        ImGui::OpenPopup("##scan_err");
                                    else {
                                        browser_->InvalidateData();
                                        scanPathBuf[0] = '\0';
                                        ImGui::CloseCurrentPopup();
                                    }
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::SameLine();
                });
            }
#endif // DATAGRID_HAVE_DUCKDB

            if (browser_fs_) {
                namespace fs = std::filesystem;
                const fs::path fp{path};
                if (!fs::is_directory(fp)) {
                    const std::string parentDir = fp.parent_path().string();
                    // Skip FS sync for virtual paths like ":memory:" (no parent dir)
                    if (fsAdapter_ && !parentDir.empty()) {
                        fsAdapter_->SetCurrentPath(parentDir);
                        browser_fs_->NavigateTo(parentDir);
                        browser_fs_->SetWindowTitle(
                            std::format("Files \xe2\x80\x94 {}", fs::path(parentDir).filename().string()));
                        fsPathSync_ = true;
                    }
                }
            }

            std::println(stderr, "[App] Connected: {} ({})", path, adapter);
            SetupDbBrowserDragDrop();
            return {};
        });
}

void App::InitFsBrowser(const std::string& dirPath)
{
    adapters::ConnectionParams fsp;
    fsp.adapterName      = std::string{adapters::name_of(adapters::AdapterKind::Filesystem)};
    fsp.connectionString = dirPath;

    (void)adapters::AdapterRegistry::CreateConnected(fsp.adapterName, fsp)
        .and_then([&](adapters::DataSourcePtr ds) -> std::expected<void, std::string> {
            // Keep a typed pointer for navigation callbacks (raw — owned by the DataBrowser)
            fsAdapter_ = dynamic_cast<adapters::FilesystemAdapter*>(ds.get());

            const std::string title = std::format("Files \xe2\x80\x94 {}",
                                                  std::filesystem::path(dirPath).filename().empty()
                                                      ? dirPath
                                                      : std::filesystem::path(dirPath).filename().string());

            browser_fs_.emplace(std::move(ds), title);
            browser_fs_->SetCodeFont(theme_.codeFont);

            std::snprintf(fsPathBuf_, sizeof(fsPathBuf_), "%s", dirPath.c_str());
            fsPathSync_ = false;

            if (fsAdapter_) {
                browser_fs_->SetPreContentHook([this]() { RenderFsNavBar(); });

                browser_fs_->SetOnRowClick([this](int, const std::vector<std::string>& row) {
                    const int kindCol = browser_fs_->ResolveRawColumnIndex("kind", adapters::column_role::kEntryKind);
                    const int pathCol = browser_fs_->ResolveRawColumnIndex("path", adapters::column_role::kFilePath);
                    if (kindCol < 0 || pathCol < 0 ||
                        row.size() <= static_cast<size_t>(std::max(kindCol, pathCol)))
                        return;
                    const std::string kind = row[static_cast<size_t>(kindCol)];
                    const std::string path = row[static_cast<size_t>(pathCol)];
                    if (kind == "dir") {
                        if constexpr (io::kClickNavigates) {
                            // .app bundles are launchable packages — open with OS, don't step in.
                            if (ui::IsExecutableFile(std::filesystem::path(path)))
                                OpenFsFile(path, "system");
                            else
                                NavigateFs(path);
                        }
                    } else if (kind == "file") {
                        if (path.empty())
                            return;
                        std::error_code ec;
                        if (!std::filesystem::is_regular_file(path, ec) || ec)
                            return;
                        OpenFsFile(path, "auto");
                    }
                });

                browser_fs_->SetOnRowDblClick([this](int, const std::vector<std::string>& row) {
                    const int kindCol = browser_fs_->ResolveRawColumnIndex("kind", adapters::column_role::kEntryKind);
                    const int pathCol = browser_fs_->ResolveRawColumnIndex("path", adapters::column_role::kFilePath);
                    if (kindCol < 0 || pathCol < 0 ||
                        row.size() <= static_cast<size_t>(std::max(kindCol, pathCol)))
                        return;
                    const std::string kind = row[static_cast<size_t>(kindCol)]; // copy before NavigateFs may clear rows
                    const std::string path = row[static_cast<size_t>(pathCol)];
                    if (kind == "dir") {
                        // .app bundles are launchable packages — open with OS, don't step in.
                        if (ui::IsExecutableFile(std::filesystem::path(path)))
                            OpenFsFile(path, "system");
                        else
                            NavigateFs(path);
                    }
                });

                browser_fs_->SetDragSourceCallback([this](int, const std::vector<std::string>& row) {
                    const int pathCol = browser_fs_->ResolveRawColumnIndex("path", adapters::column_role::kFilePath);
                    const int kindCol = browser_fs_->ResolveRawColumnIndex("kind", adapters::column_role::kEntryKind);
                    if (pathCol < 0 || kindCol < 0 ||
                        row.size() <= static_cast<size_t>(std::max(pathCol, kindCol)))
                        return;

                    io::FilePayload payload{};
                    payload.sourceBrowserId = browser_fs_->InstanceId();

                    namespace fs               = std::filesystem;
                    const std::string& absPath = row[static_cast<size_t>(pathCol)];
                    const std::string& kind    = row[static_cast<size_t>(kindCol)];
                    const std::string  fname   = fs::path(absPath).filename().string();
                    const std::string  ext     = io::FileExtension(absPath);

                    std::snprintf(payload.path, sizeof(payload.path), "%s", absPath.c_str());
                    std::snprintf(payload.kind, sizeof(payload.kind), "%s", kind.c_str());
                    std::snprintf(payload.name, sizeof(payload.name), "%s", fname.c_str());
                    std::snprintf(payload.extension, sizeof(payload.extension), "%s", ext.c_str());

                    ImGui::SetDragDropPayload(io::kFilePayloadType, &payload, sizeof(payload));

                    const char* icon = (kind == "dir") ? ui::icons::Folder : ui::icons::File;
                    ImGui::TextDisabled("%s ", icon);
                    ImGui::SameLine();
                    ImGui::Text("%s", fname.c_str());
                });

                browser_fs_->SetDropHandler([this](const char* type, const void* data, std::size_t sz) {
                    // using namespace io;
                    if (std::string_view{type} == io::kFilePayloadType && sz == sizeof(io::FilePayload)) {
                        const auto&       fp     = *static_cast<const io::FilePayload*>(data);
                        const std::string dstDir = fsAdapter_ ? fsAdapter_->GetCurrentPath() : "";
                        dropDialog_.TriggerFsCopyMove(fp, dstDir, &*browser_fs_);
                    }
                });

                browser_fs_->SetColumnCustomizer([](std::vector<ImGuiExt::ColumnDef>& cols) {
                    for (auto& col : cols) {
                        if (col.key == "name") {
                            col.renderer = [](const std::string& name, int) {
                                const char* icon = ui::IsImageFile(name) ? ui::icons::FileImage : ui::icons::File;
                                ImGui::TextDisabled("%s", icon);
                                ImGui::SameLine(0.0f, 4.0f);
                                ImGui::TextUnformatted(name.c_str());
                            };
                        } else if (col.key == "kind") {
                            col.renderer = [](const std::string& kind, int) {
                                const char* icon = (kind == "dir") ? ui::icons::Folder : ui::icons::File;
                                ImGui::TextDisabled("%s", icon);
                                ImGui::SameLine(0.0f, 4.0f);
                                ImGui::TextUnformatted(kind.c_str());
                            };
                        }
                    }
                });

                browser_fs_->SetOpenCallback([this](const std::string& path, const std::string& how) {
                    OpenFsFile(path, how);
                });
            }

            return {};
        });
}

void App::OpenFsFile(const std::string& path, const std::string& how)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (how == "image") { OpenImageViewer(path); return; }
    if (how == "text")  { OpenTextViewer(path); return; }
    if (how == "sqlite"){ OpenSqliteViewer(path); return; }
    if (how == "hex")   { return; } // hex view opened directly via context menu / action bar

    // "browse" — navigate into a directory (used for .app bundles and other dirs)
    if (how == "browse") {
        if (browser_fs_ && fsAdapter_) {
            NavigateFs(path);
        }
        return;
    }

    if (how == "system") {
        io::OpenWithSystem(path);
        return;
    }

    // "auto" — detect by content
    if (!fs::is_regular_file(path, ec) || ec) {
        // Might be a directory or bundle — hand to the OS
        io::OpenWithSystem(path);
        return;
    }

    if (ui::IsImageFile(path))  { OpenImageViewer(path);  return; }
    if (ui::IsSqliteFile(path)) { OpenSqliteViewer(path); return; }
    if (ui::IsPdfFile(path))    { io::OpenWithSystem(path); return; }

    // Check executable — launch via OS to respect interpreter associations
    if (ui::IsExecutableFile(path)) {io::OpenWithSystem(path); return; }

    // Fall back to text sniffing
    const auto sniff = ui::sniff_file(path);
    if (sniff.is_text)
        OpenTextViewer(path);
    else
        io::OpenWithSystem(path); // unknown binary — let the OS decide
}

void App::RenderFsNavBar()
{
    if (!fsAdapter_)
        return;

    if (fsPathSync_) {
        std::snprintf(fsPathBuf_, sizeof(fsPathBuf_), "%s", fsAdapter_->GetCurrentPath().c_str());
        fsPathSync_ = false;
    }

    // ── Keyboard shortcut: toggle hidden files ───────────────────────────────
    // macOS: Cmd+Shift+.   (mirrors Finder)
    // Win/Linux: Ctrl+H    (mirrors Nautilus / Windows Explorer)
    {
        const ImGuiIO& kio  = ImGui::GetIO();
#if defined(__APPLE__)
        const bool shortcutHit = ImGui::IsKeyPressed(ImGuiKey_Period, false)
                              && (kio.KeyMods == (ImGuiMod_Super | ImGuiMod_Shift));
#else
        const bool shortcutHit = ImGui::IsKeyPressed(ImGuiKey_H, false)
                              && (kio.KeyMods == ImGuiMod_Ctrl);
#endif
        if (shortcutHit) {
            fsAdapter_->SetShowHidden(!fsAdapter_->GetShowHidden());
            browser_fs_->InvalidateData();
        }
    }

    const bool atRoot = (fsAdapter_->GetCurrentPath() == fsAdapter_->GetParentPath());
    if (atRoot)
        ImGui::BeginDisabled();
    if (ImGui::Button("\xe2\x86\x91 Up")) { // ↑
        fsAdapter_->NavigateUp();
        const std::string p = fsAdapter_->GetCurrentPath();
        browser_fs_->NavigateTo(p);
        browser_fs_->SetWindowTitle(std::format(
            "Files \xe2\x80\x94 {}",
            std::filesystem::path(p).filename().empty() ? p : std::filesystem::path(p).filename().string()));
        fsPathSync_ = true;
    }
    if (atRoot)
        ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button(ui::icons::Home)) {
        fsAdapter_->NavigateHome();
        const std::string p = fsAdapter_->GetCurrentPath();
        browser_fs_->NavigateTo(p);
        browser_fs_->SetWindowTitle(std::format(
            "Files \xe2\x80\x94 {}",
            std::filesystem::path(p).filename().empty() ? p : std::filesystem::path(p).filename().string()));
        fsPathSync_ = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Home directory");

    ImGui::SameLine();

    // ── Show/hide hidden files toggle button ─────────────────────────────────
    const bool showHidden = fsAdapter_->GetShowHidden();
#if defined(__APPLE__)
    constexpr const char* kHiddenShortcut = "Cmd+Shift+.";
#else
    constexpr const char* kHiddenShortcut = "Ctrl+H";
#endif
    // Use Eye / EyeSlash icon so the state is visually obvious.
    const char* eyeIcon = showHidden ? ui::icons::Eye : ui::icons::EyeSlash;
    if (showHidden)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
    if (ImGui::Button(eyeIcon)) {
        fsAdapter_->SetShowHidden(!showHidden);
        browser_fs_->InvalidateData();
    }
    if (showHidden)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s hidden files  (%s)",
                          showHidden ? "Hide" : "Show",
                          kHiddenShortcut);

    ImGui::SameLine();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##fspathbar", fsPathBuf_, sizeof(fsPathBuf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::error_code ec;
        const bool isDir = std::filesystem::is_directory(std::filesystem::path(fsPathBuf_), ec);
        if (!ec && isDir) {
            fsAdapter_->SetCurrentPath(fsPathBuf_);
            const std::string p = fsAdapter_->GetCurrentPath();
            browser_fs_->NavigateTo(p);
            browser_fs_->SetWindowTitle(std::format(
                "Files \xe2\x80\x94 {}",
                std::filesystem::path(p).filename().empty() ? p : std::filesystem::path(p).filename().string()));
            std::snprintf(fsPathBuf_, sizeof(fsPathBuf_), "%s", p.c_str());
        }
    }
}

void App::NavigateFs(std::string absolutePath)
{
    if (!fsAdapter_ || !browser_fs_)
        return;

    namespace fs = std::filesystem;
    std::error_code ec;

    if (fsAdapter_->EntryIsDir(absolutePath)) {
        fsAdapter_->SetCurrentPath(absolutePath);
        browser_fs_->NavigateTo(absolutePath);
        browser_fs_->SetWindowTitle(std::format(
            "Files \xe2\x80\x94 {}",
            fs::path(absolutePath).filename().empty() ? absolutePath : fs::path(absolutePath).filename().string()));
        fsPathSync_ = true;
    } else if (fsAdapter_->EntryIsFile(absolutePath)) {
        io::OpenWithSystem(absolutePath);
    }
}

void App::UpdateLayout()
{
    const int   logW = sapp_width();
    const int   logH = sapp_height();
    const float dpi  = sapp_dpi_scale();
    const int   fbW  = static_cast<int>(logW * dpi);
    const int   fbH  = static_cast<int>(logH * dpi);
    layout_.Update(logW, logH, fbW, fbH);
}

void App::OpenImageViewer(const std::string& path)
{
    imageViewerKey_  = path;
    imageViewerHint_ = "image_path";
    showImageViewer_ = true;

    // Build a sorted list of sibling image files for Left/Right navigation.
    namespace fs = std::filesystem;
    imageViewerSiblings_.clear();
    imageViewerSiblingIdx_ = -1;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(parent, ec)) {
            std::error_code ec2;
            if (entry.is_regular_file(ec2) && !ec2 && ui::IsImageFile(entry.path()))
                imageViewerSiblings_.push_back(entry.path().string());
        }
        std::ranges::sort(imageViewerSiblings_);
        const auto it = std::ranges::find(imageViewerSiblings_, path);
        if (it != imageViewerSiblings_.end())
            imageViewerSiblingIdx_ = static_cast<int>(std::distance(imageViewerSiblings_.begin(), it));
    }
}

void App::OpenSqliteViewer(const std::string& path)
{
    TryConnect("sqlite", path, {}, /*readOnly=*/true);
}

void App::OpenTextViewer(const std::filesystem::path& path)
{
    textViewer_.OpenFile(path);
}

void App::RenderTextViewer()
{
    if (!textViewer_.IsOpen())
        return;

    const ImGuiIO& io   = ImGui::GetIO();
    const float    topY = ImGui::GetFrameHeightWithSpacing();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.15f, topY), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(
        ImVec2(io.DisplaySize.x * 0.80f, io.DisplaySize.y * 0.85f - topY),
        ImGuiCond_Appearing);

    bool open = true;
    ImGui::Begin("Text Viewer##tav", &open, ImGuiWindowFlags_NoScrollbar);
    textViewer_.Render();
    ImGui::End();

    if (!open)
        textViewer_.Clear();
}

void App::InstallImageRenderers(DataBrowser& browser)
{
    browser.SetColumnCustomizer([this](std::vector<ImGuiExt::ColumnDef>& cols) {
        for (auto& col : cols) {
            if (col.semanticHint == "image_path") {
                col.renderer = [this](const std::string& val, int) {
                    if (val.empty()) {
                        ImGui::TextDisabled("--");
                        return;
                    }
                    const auto* e = imageCache_.GetOrLoad(val);
                    if (e) {
                        const ImTextureID tid = simgui_imtextureid_with_sampler(e->view, imageCache_.DefaultSampler());
                        ImGui::Image(tid, {64.0f, 64.0f});
                    } else {
                        ImGui::TextDisabled("[?]");
                    }
                };
                col.onDblClick = ImGuiExt::CellCustom{[this](const std::string& val, int) { OpenImageViewer(val); }};
            } else if (col.semanticHint == "image_blob") {
                col.renderer = [this, k = col.key](const std::string& val, int row) {
                    if (val.empty()) {
                        ImGui::TextDisabled("--");
                        return;
                    }
                    const std::string key = k + "." + std::to_string(row);
                    const auto*       e   = imageCache_.GetOrLoadMemory(
                        key, reinterpret_cast<const unsigned char*>(val.data()), static_cast<int>(val.size()));
                    if (e) {
                        const ImTextureID tid = simgui_imtextureid_with_sampler(e->view, imageCache_.DefaultSampler());
                        ImGui::Image(tid, {64.0f, 64.0f});
                    } else {
                        ImGui::TextDisabled("[?]");
                    }
                };
                col.onDblClick = ImGuiExt::CellCustom{[this, k = col.key](const std::string& val, int row) {
                    const std::string key = k + "." + std::to_string(row);
                    (void)imageCache_.GetOrLoadMemory(
                        key, reinterpret_cast<const unsigned char*>(val.data()), static_cast<int>(val.size()));
                    imageViewerKey_  = key;
                    imageViewerHint_ = "image_blob";
                    showImageViewer_ = true;
                }};
            }
        }
    });
}

void App::RenderImageViewer()
{
    if (showImageViewer_) {
        ImGui::OpenPopup("Image Viewer##imgview");
        showImageViewer_ = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({300.0f, 200.0f}, {1200.0f, 900.0f});
    ImGui::SetNextWindowSize({640.0f, 520.0f}, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Image Viewer##imgview", nullptr, ImGuiWindowFlags_NoScrollbar))
        return;

    const auto* e = imageCache_.Get(imageViewerKey_);
    if (!e && imageViewerHint_ == "image_path")
        e = imageCache_.GetOrLoad(imageViewerKey_);

    if (e && e->handle.id != SG_INVALID_ID) {
        const ImVec2 avail   = ImGui::GetContentRegionAvail();
        const float  reserve = ImGui::GetFrameHeightWithSpacing() + 8.0f;
        const ImVec2 canvas  = {avail.x, avail.y - reserve};
        const float  scale   = std::min(canvas.x / static_cast<float>(e->w), canvas.y / static_cast<float>(e->h));
        const ImVec2 sz      = {static_cast<float>(e->w) * scale, static_cast<float>(e->h) * scale};

        ImGui::SetCursorPosX((avail.x - sz.x) * 0.5f + ImGui::GetCursorPosX());
        const ImTextureID tid = simgui_imtextureid_with_sampler(e->view, imageCache_.DefaultSampler());
        ImGui::Image(tid, sz);
        ImGui::Spacing();
        ImGui::TextDisabled("%d x %d px  --  %s", e->w, e->h, imageViewerKey_.c_str());
    } else {
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Failed to load image");
        ImGui::TextUnformatted(imageViewerKey_.c_str());
    }

    //  Navigation
    // For image_blob keys the format is "colKey.rowIndex"
    // (rfind handles the case where colKey itself contains dots).
    auto navigateBlobImage = [&](int delta) {
        const auto dot = imageViewerKey_.rfind('.');
        if (dot == std::string::npos) return;
        const std::string blobColKey = imageViewerKey_.substr(0, dot);
        const int         blobRowIdx = std::stoi(imageViewerKey_.substr(dot + 1));
        if (!browser_) return;
        const auto& pageRows    = browser_->GetCurrentRows();
        const int   blobColIdx  = browser_->GetColumnIndex(blobColKey);
        if (blobColIdx < 0) return;
        for (int ri = blobRowIdx + delta;
             ri >= 0 && ri < static_cast<int>(pageRows.size());
             ri += delta) {
            if (blobColIdx < static_cast<int>(pageRows[ri].size()) && !pageRows[ri][blobColIdx].empty()) {
                const std::string newKey = blobColKey + "." + std::to_string(ri);
                (void)imageCache_.GetOrLoadMemory(newKey,
                    reinterpret_cast<const unsigned char*>(pageRows[ri][blobColIdx].data()),
                    static_cast<int>(pageRows[ri][blobColIdx].size()));
                imageViewerKey_ = newKey;
                break;
            }
        }
    };

    // Boundary checks for image_path
    const bool isPath  = (imageViewerHint_ == "image_path");
    const bool isBlob  = (imageViewerHint_ == "image_blob");
    const bool atFirst = isPath && (imageViewerSiblingIdx_ <= 0);
    const bool atLast  = isPath && (imageViewerSiblingIdx_ < 0 ||
                                    imageViewerSiblingIdx_ >= static_cast<int>(imageViewerSiblings_.size()) - 1);


    if (atFirst) ImGui::BeginDisabled();
    if (ImGui::Button("\xe2\x86\x90 Prev") || (isBlob && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                                           || (isPath && !atFirst && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))) {
        if (isPath && imageViewerSiblingIdx_ > 0) {
            --imageViewerSiblingIdx_;
            imageViewerKey_ = imageViewerSiblings_[imageViewerSiblingIdx_];
        } else if (isBlob) {
            navigateBlobImage(-1);
        }
    }
    if (atFirst) ImGui::EndDisabled();

    ImGui::SameLine();

    if (atLast) ImGui::BeginDisabled();
    if (ImGui::Button("Next \xe2\x86\x92") || (isBlob && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                                           || (isPath && !atLast && ImGui::IsKeyPressed(ImGuiKey_RightArrow))) {
        if (isPath &&
            imageViewerSiblingIdx_ >= 0 &&
            imageViewerSiblingIdx_ < static_cast<int>(imageViewerSiblings_.size()) - 1) {
            ++imageViewerSiblingIdx_;
            imageViewerKey_ = imageViewerSiblings_[imageViewerSiblingIdx_];
        } else if (isBlob) {
            navigateBlobImage(+1);
        }
    }
    if (atLast) ImGui::EndDisabled();

    // Position counter (only for filesystem navigation where siblings are known)
    if (isPath && !imageViewerSiblings_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d / %d",
            imageViewerSiblingIdx_ + 1,
            static_cast<int>(imageViewerSiblings_.size()));
    }

    ImGui::SameLine();
    // Push Close to the right edge
    const float closeW = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, ImGui::GetContentRegionAvail().x - closeW));
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}


void App::SetupDragDrop()
{
    dropDialog_.onOpenNewWindow = [this](const std::string& adapter, const std::string& path) {
        TryConnect(adapter, path, {}, true);
    };

    dropDialog_.onReplaceWindow = [this](const std::string& adapter, const std::string& path, DataBrowser* /*target*/) {
        TryConnect(adapter, path, {}, true);
    };
}

void App::SetupDbBrowserDragDrop()
{
    if (!browser_)
        return;

    browser_->SetDragSourceCallback([this](int rowIdx, const std::vector<std::string>& row) {
        io::RowPayload payload{};
        payload.sourceBrowserId = browser_->InstanceId();
        payload.pageRowIndex    = rowIdx;

        const std::string adapterName = browser_->GetSource() ? browser_->GetSource()->AdapterName() : "";
        std::snprintf(payload.adapterName, sizeof(payload.adapterName), "%s", adapterName.c_str());
        std::snprintf(payload.tableName, sizeof(payload.tableName), "%s", browser_->CurrentTable().c_str());

        const auto        keys    = browser_->GetCurrentColumnKeys();
        const std::string encoded = io::EncodeRowData(keys, row);
        std::snprintf(payload.rowData, sizeof(payload.rowData), "%s", encoded.c_str());

        ImGui::SetDragDropPayload(io::kRowPayloadType, &payload, sizeof(payload));
        ImGui::TextDisabled("%s  ", ui::icons::TableIcon);
        ImGui::SameLine();
        ImGui::Text("Row from %s", browser_->CurrentTable().c_str());
    });

    browser_->SetDropHandler([this](const char* type, const void* data, std::size_t sz) {
        const std::string_view t{type};

        if (t == io::kFilePayloadType && sz == sizeof(io::FilePayload)) {
            const auto& fp     = *static_cast<const io::FilePayload*>(data);
            const auto  dbType = io::SniffDbType(fp.path);
            if (dbType != io::FileDbType::Unknown) {
                dropDialog_.TriggerDbFileOpen(fp, &*browser_);
            }
#ifdef DATAGRID_HAVE_DUCKDB
            else if (adapters::DuckDBAdapter::IsQueryableExtension(fp.extension) && browser_ && browser_->GetSource() &&
                     browser_->GetSource()->AdapterName() == "duckdb") {
                dropDialog_.TriggerFileToView(fp, &*browser_);
            }
#endif
        } else if (t == io::kRowPayloadType && sz == sizeof(io::RowPayload)) {
            const auto& rp = *static_cast<const io::RowPayload*>(data);
            if (!browser_ || !browser_->GetSource())
                return;
            if (!browser_->GetSource()->SupportsWrite())
                return;

            auto [srcKeys, srcVals] = io::ParseRowData(rp.rowData);
            const auto targetCols   = browser_->GetSource()->GetColumns(browser_->CurrentTable());

            dropDialog_.TriggerRowInsert(rp, &*browser_, targetCols, srcKeys, srcVals);
        }
    });
}
}