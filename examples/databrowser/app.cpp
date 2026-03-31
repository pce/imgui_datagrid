#include "app.hpp"

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "util/sokol_imgui.h"

#include "adapters/adapter_kind.hpp"
#include "adapters/adapter_registry.hpp"
#ifdef DATAGRID_HAVE_DUCKDB
#include "adapters/duckdb/duckdb_adapter.hpp"
#endif

#include <algorithm>
#include <filesystem>
#include <format>
#include <print>
#include <ranges>

#include "drag_drop.hpp"
#include "ui/drag_drop_dialog.hpp"
#include "icons.hpp"
#include "ui/filetype_sniffer.hpp"
#include "ui/platform.hpp"

#if defined(__APPLE__)
static constexpr uint32_t kPlatformMod = SAPP_MODIFIER_SUPER;
#else
static constexpr uint32_t kPlatformMod = SAPP_MODIFIER_CTRL;
#endif


void App::Init()
{
    verbose_ = (std::getenv("DATAGRID_VERBOSE") != nullptr);

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

    const float dpi = sapp_dpi_scale();
    if (verbose_)
        std::println("[Init] DPI scale: {:.2f}", dpi);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Unicode ranges shared by all UI fonts:
    // Basic Latin + Latin-1 + em/en dash + Unicode arrows.
    static const ImWchar kUiRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
        0x2013, 0x2015, // en dash, em dash, horizontal bar
        0x2190, 0x21FF, // Arrows block  ← ↑ → ↓ …
        0,
    };

    const char* faPath   = "resources/fonts/fa-6-solid-900.otf";
    const bool  faExists = std::filesystem::exists(faPath);
    if (verbose_)
        std::println("[Font] FA6 solid: {}", faExists ? "found" : "MISSING — icons will be blank");

    // Helper: load a UI font then merge FA icons into it.
    // The first call also serves as the atlas default (fonts[0]).
    auto loadUiFont = [&](const char* path) -> ImFont* {
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, 14.0f * dpi, nullptr, kUiRanges);
        if (!f) {
            if (verbose_) std::println("[Font] {} MISSING", path);
            return nullptr;
        }
        if (verbose_) std::println("[Font] {}: OK", path);
        if (faExists) {
            ImFontConfig cfg;
            cfg.MergeMode        = true;
            cfg.PixelSnapH       = true;
            cfg.GlyphMinAdvanceX = 14.0f * dpi;
            io.Fonts->AddFontFromFileTTF(faPath, 14.0f * dpi, &cfg, Icons::kFAGlyphRanges);
        }
        return f;
    };

    // Helper: load a code/monospace font (no FA merge needed).
    auto loadCodeFont = [&](const char* path) -> ImFont* {
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, 14.0f * dpi);
        if (verbose_)
            std::println("[Font] code {}: {}", path, f ? "OK" : "MISSING");
        return f;
    };

    // ── UI fonts (one per theme group, FA merged into each) ───────────────────
    // Roboto must be first — it becomes the atlas default font.
    ImFont* fRoboto     = loadUiFont("resources/fonts/Roboto-Medium.ttf");
    ImFont* fSUSE       = loadUiFont("resources/fonts/SUSE-Light.ttf");
    ImFont* fHackUI     = loadUiFont("resources/fonts/Hack-Regular.ttf");
    ImFont* fKarla      = loadUiFont("resources/fonts/Karla-Regular.ttf");
    ImFont* fJetBrains  = loadUiFont("resources/fonts/JetBrainsMono-Thin.ttf");

    if (!fRoboto) {
        io.Fonts->AddFontDefault();
        fRoboto = io.Fonts->Fonts[0];
        if (verbose_) std::println("[Font] Falling back to ImGui built-in default");
    }

    // ── Code / monospace fonts (no FA) ────────────────────────────────────────
    ImFont* fHackCode    = loadCodeFont("resources/fonts/Hack-Regular.ttf");
    ImFont* fSUSEMono    = loadCodeFont("resources/fonts/SUSEMono-Light.ttf");
    ImFont* fMonaspace   = loadCodeFont("resources/fonts/MonaspaceArgonNF-Regular.otf");
    ImFont* fMonaKrypton = loadCodeFont("resources/fonts/MonaspaceKryptonNF-WideLight.otf");

    // Graceful fallbacks when a theme-specific font is missing.
    if (!fSUSEMono)    fSUSEMono    = fHackCode;
    if (!fMonaspace)   fMonaspace   = fHackCode;
    if (!fMonaKrypton) fMonaKrypton = fMonaspace;
    if (!fKarla)       fKarla       = fRoboto;
    if (!fJetBrains)   fJetBrains   = fRoboto;

    io.FontGlobalScale = (1.0f / dpi);

    // ── Register font pairs with all 16 themes ────────────────────────────────
    ImFont* const fSUSEui = fSUSE   ? fSUSE   : fRoboto;
    ImFont* const fHackUi = fHackUI ? fHackUI : fRoboto;
    theme_.RegisterFonts(ThemeType::SolarizedDark,      fRoboto,     fHackCode);
    theme_.RegisterFonts(ThemeType::SolarizedLight,     fRoboto,     fHackCode);
    theme_.RegisterFonts(ThemeType::Monokai,            fRoboto,     fHackCode);
    theme_.RegisterFonts(ThemeType::MonokaiDark,        fRoboto,     fHackCode);
    theme_.RegisterFonts(ThemeType::MonaSpaces,         fKarla,      fMonaspace);
    theme_.RegisterFonts(ThemeType::EarthSUSE,          fSUSEui,     fSUSEMono);
    theme_.RegisterFonts(ThemeType::EarthSUSEDark,      fSUSEui,     fSUSEMono);
    theme_.RegisterFonts(ThemeType::NeonSpaces,         fRoboto,     fMonaKrypton);
    theme_.RegisterFonts(ThemeType::DawnBringer16Dark,  fHackUi,     fHackCode);
    theme_.RegisterFonts(ThemeType::DawnBringer16Light, fHackUi,     fHackCode);
    theme_.RegisterFonts(ThemeType::Material,           fRoboto,     fHackCode);
    theme_.RegisterFonts(ThemeType::MaterialDark,       fRoboto,     fHackCode);
    theme_.RegisterFonts(ThemeType::MonoLight,          fJetBrains,  fJetBrains);
    theme_.RegisterFonts(ThemeType::MonoDark,           fJetBrains,  fJetBrains);
    theme_.RegisterFonts(ThemeType::DawnBringerLight,   fHackUi,     fMonaspace);
    theme_.RegisterFonts(ThemeType::DawnBringerDark,    fHackUi,     fMonaspace);

    // ── Navbar callback: sync TextArea code font on theme switch ──────────────
    navbar_.onThemeApplied = [this] { textViewer_.SetCodeFont(theme_.codeFont); };

#if defined(__APPLE__)
    io.ConfigMacOSXBehaviors = true;
#endif

    // Apply startup theme. ApplyImGuiStyle must come first to store dpiScale_
    // so that ApplyColorTheme can call ApplyThemeStyle_ with the correct scale.
    theme_.ApplyImGuiStyle(dpi);
    theme_.ApplyColorTheme(ThemeType::SolarizedDark);

    // Push initial code font into the text viewer.
    textViewer_.SetCodeFont(theme_.codeFont);

    UpdateLayout();
    layout_.ApplyToImGui();

    config_ = LoadAppConfig();

    const auto entries = Adapters::AdapterRegistry::Entries();

    const std::string_view preferredAdapter = !dbPath.empty()                ? std::string_view{adapterName}
                                              : !config_.adapterName.empty() ? std::string_view{config_.adapterName}
                                              : !entries.empty()             ? entries.front().name
                                                                             : std::string_view{};

    if (const auto it = std::ranges::find(entries, preferredAdapter, &Adapters::AdapterEntry::name);
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
        std::vector<UI::WindowEntry> wins;

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

    const auto entries = Adapters::AdapterRegistry::Entries();

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
    if (const auto k = Adapters::kind_of(entries[adapterIdx_].name)) {
        switch (*k) {
            case Adapters::AdapterKind::SQLite:
                hint = "Path to .db or .sqlite file \xe2\x80\xa6";
                break;
            case Adapters::AdapterKind::CSV:
                hint = "Path to .csv file \xe2\x80\xa6";
                break;
            case Adapters::AdapterKind::Filesystem:
                hint = "Directory path \xe2\x80\xa6";
                break;
                case Adapters::AdapterKind::DuckDB:
                    hint = "Path to .duckdb file, or :memory: for in-memory \xe2\x80\xa6";
                    break;
                case Adapters::AdapterKind::PostgreSQL:
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
            if (const auto it = std::ranges::find(entries, config_.adapterName, &Adapters::AdapterEntry::name);
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

    Adapters::ConnectionParams p;
    p.adapterName      = adapter;
    p.connectionString = path;
    p.readOnly         = readOnly;
    if (!passphrase.empty())
        p.password = passphrase;

    (void)Adapters::AdapterRegistry::CreateConnected(adapter, p)
        .or_else([&](std::string err) -> std::expected<Adapters::DataSourcePtr, std::string> {
            connectError_ = err;
            std::println(stderr, "[App] Connect failed ({}): {}", adapter, err);
            return std::unexpected(std::move(err));
        })
        .and_then([&](Adapters::DataSourcePtr ds) -> std::expected<void, std::string> {
            config_.adapterName      = adapter;
            config_.connectionString = path;
            SaveAppConfig(config_);

            // For DuckDB :memory: (or empty path) show a readable label
            // instead of the empty string that filename() would return.
            const std::string pathLabel = (path.empty() || path == ":memory:")
                                              ? std::string{"(in-memory)"}
                                              : std::filesystem::path(path).filename().string();

            browser_.emplace(std::move(ds), std::format("{} \xe2\x80\x94 {}", adapter, pathLabel));
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
                                auto* duck   = dynamic_cast<Adapters::DuckDBAdapter*>(browser_->GetSource());
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
    Adapters::ConnectionParams fsp;
    fsp.adapterName      = std::string{Adapters::name_of(Adapters::AdapterKind::Filesystem)};
    fsp.connectionString = dirPath;

    (void)Adapters::AdapterRegistry::CreateConnected(fsp.adapterName, fsp)
        .and_then([&](Adapters::DataSourcePtr ds) -> std::expected<void, std::string> {
            // Keep a typed pointer for navigation callbacks (raw — owned by the DataBrowser)
            fsAdapter_ = dynamic_cast<Adapters::FilesystemAdapter*>(ds.get());

            const std::string title = std::format("Files \xe2\x80\x94 {}",
                                                  std::filesystem::path(dirPath).filename().empty()
                                                      ? dirPath
                                                      : std::filesystem::path(dirPath).filename().string());

            browser_fs_.emplace(std::move(ds), title);

            std::snprintf(fsPathBuf_, sizeof(fsPathBuf_), "%s", dirPath.c_str());
            fsPathSync_ = false;

            if (fsAdapter_) {
                browser_fs_->SetPreContentHook([this]() { RenderFsNavBar(); });

                browser_fs_->SetOnRowClick([this](int, const std::vector<std::string>& row) {
                    if (row.size() < 6)
                        return;
                    const std::string kind = row[1];
                    const std::string path = row[5];
                    if (kind == "dir") {
                        if constexpr (UI::Platform::kClickNavigates)
                            NavigateFs(path);
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
                    if (row.size() < 6)
                        return;
                    const std::string kind = row[1]; // copy before NavigateFs may clear rows
                    const std::string path = row[5];
                    if (kind == "dir")
                        NavigateFs(path);
                });

                browser_fs_->SetDragSourceCallback([this](int, const std::vector<std::string>& row) {
                    if (row.size() < 6)
                        return;

                    UI::FilePayload payload{};
                    payload.sourceBrowserId = browser_fs_->InstanceId();

                    namespace fs               = std::filesystem;
                    const std::string& absPath = row[5];
                    const std::string& kind    = row[1];
                    const std::string  fname   = fs::path(absPath).filename().string();
                    const std::string  ext     = UI::FileExtension(absPath);

                    std::snprintf(payload.path, sizeof(payload.path), "%s", absPath.c_str());
                    std::snprintf(payload.kind, sizeof(payload.kind), "%s", kind.c_str());
                    std::snprintf(payload.name, sizeof(payload.name), "%s", fname.c_str());
                    std::snprintf(payload.extension, sizeof(payload.extension), "%s", ext.c_str());

                    ImGui::SetDragDropPayload(UI::kFilePayloadType, &payload, sizeof(payload));

                    const char* icon = (kind == "dir") ? Icons::Folder : Icons::File;
                    ImGui::TextDisabled("%s ", icon);
                    ImGui::SameLine();
                    ImGui::Text("%s", fname.c_str());
                });

                browser_fs_->SetDropHandler([this](const char* type, const void* data, std::size_t sz) {
                    using namespace UI;
                    if (std::string_view{type} == kFilePayloadType && sz == sizeof(FilePayload)) {
                        const auto&       fp     = *static_cast<const FilePayload*>(data);
                        const std::string dstDir = fsAdapter_ ? fsAdapter_->GetCurrentPath() : "";
                        dropDialog_.TriggerFsCopyMove(fp, dstDir, &*browser_fs_);
                    }
                });

                browser_fs_->SetColumnCustomizer([](std::vector<ImGuiExt::ColumnDef>& cols) {
                    for (auto& col : cols) {
                        if (col.key == "name") {
                            col.renderer = [](const std::string& name, int) {
                                const char* icon = UI::IsImageFile(name) ? Icons::FileImage : Icons::File;
                                ImGui::TextDisabled("%s", icon);
                                ImGui::SameLine(0.0f, 4.0f);
                                ImGui::TextUnformatted(name.c_str());
                            };
                        } else if (col.key == "kind") {
                            col.renderer = [](const std::string& kind, int) {
                                const char* icon = (kind == "dir") ? Icons::Folder : Icons::File;
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
    if (how == "hex")   { return; } // hex view opened directly via context menu

    if (how == "system") {
        UI::Platform::OpenWithSystem(path);
        return;
    }

    // "auto" — detect by content
    if (!fs::is_regular_file(path, ec) || ec) {
        // Might be a directory or bundle — hand to the OS
        UI::Platform::OpenWithSystem(path);
        return;
    }

    if (UI::IsImageFile(path))  { OpenImageViewer(path);  return; }
    if (UI::IsSqliteFile(path)) { OpenSqliteViewer(path); return; }
    if (UI::IsPdfFile(path))    { UI::Platform::OpenWithSystem(path); return; }

    // Check executable — launch via OS to respect interpreter associations
    if (UI::IsExecutableFile(path)) { UI::Platform::OpenWithSystem(path); return; }

    // Fall back to text sniffing
    const auto sniff = UI::sniff_file(path);
    if (sniff.is_text)
        OpenTextViewer(path);
    else
        UI::Platform::OpenWithSystem(path); // unknown binary — let the OS decide
}

void App::RenderFsNavBar()
{
    if (!fsAdapter_)
        return;

    if (fsPathSync_) {
        std::snprintf(fsPathBuf_, sizeof(fsPathBuf_), "%s", fsAdapter_->GetCurrentPath().c_str());
        fsPathSync_ = false;
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

    if (ImGui::Button(Icons::Home)) {
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
        UI::Platform::OpenWithSystem(absolutePath);
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
            if (entry.is_regular_file(ec2) && !ec2 && UI::IsImageFile(entry.path()))
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
        UI::RowPayload payload{};
        payload.sourceBrowserId = browser_->InstanceId();
        payload.pageRowIndex    = rowIdx;

        const std::string adapterName = browser_->GetSource() ? browser_->GetSource()->AdapterName() : "";
        std::snprintf(payload.adapterName, sizeof(payload.adapterName), "%s", adapterName.c_str());
        std::snprintf(payload.tableName, sizeof(payload.tableName), "%s", browser_->CurrentTable().c_str());

        const auto        keys    = browser_->GetCurrentColumnKeys();
        const std::string encoded = UI::EncodeRowData(keys, row);
        std::snprintf(payload.rowData, sizeof(payload.rowData), "%s", encoded.c_str());

        ImGui::SetDragDropPayload(UI::kRowPayloadType, &payload, sizeof(payload));
        ImGui::TextDisabled("%s  ", Icons::TableIcon);
        ImGui::SameLine();
        ImGui::Text("Row from %s", browser_->CurrentTable().c_str());
    });

    browser_->SetDropHandler([this](const char* type, const void* data, std::size_t sz) {
        const std::string_view t{type};

        if (t == UI::kFilePayloadType && sz == sizeof(UI::FilePayload)) {
            const auto& fp     = *static_cast<const UI::FilePayload*>(data);
            const auto  dbType = UI::SniffDbType(fp.path);
            if (dbType != UI::FileDbType::Unknown) {
                dropDialog_.TriggerDbFileOpen(fp, &*browser_);
            }
#ifdef DATAGRID_HAVE_DUCKDB
            else if (Adapters::DuckDBAdapter::IsQueryableExtension(fp.extension) && browser_ && browser_->GetSource() &&
                     browser_->GetSource()->AdapterName() == "duckdb") {
                dropDialog_.TriggerFileToView(fp, &*browser_);
            }
#endif
        } else if (t == UI::kRowPayloadType && sz == sizeof(UI::RowPayload)) {
            const auto& rp = *static_cast<const UI::RowPayload*>(data);
            if (!browser_ || !browser_->GetSource())
                return;
            if (!browser_->GetSource()->SupportsWrite())
                return;

            auto [srcKeys, srcVals] = UI::ParseRowData(rp.rowData);
            const auto targetCols   = browser_->GetSource()->GetColumns(browser_->CurrentTable());

            dropDialog_.TriggerRowInsert(rp, &*browser_, targetCols, srcKeys, srcVals);
        }
    });
}
