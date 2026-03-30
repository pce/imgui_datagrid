#include "app.hpp"

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "util/sokol_imgui.h"

#include "adapters/adapter_kind.hpp"
#include "adapters/adapter_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <print>
#include <ranges>

#include "icons.hpp"

// Cross-platform primary modifier: Cmd on macOS, Ctrl elsewhere
#if defined(__APPLE__)
static constexpr uint32_t kPlatformMod = SAPP_MODIFIER_SUPER;
#else
static constexpr uint32_t kPlatformMod = SAPP_MODIFIER_CTRL;
#endif

void App::Init()
{
    const sg_desc gfx_desc = {
        .environment = sglue_environment(),
        .logger      = { .func = slog_func },
    };
    sg_setup(&gfx_desc);

    IMGUI_CHECKVERSION();
    const simgui_desc_t imgui_desc = {
        .logger = { .func = slog_func },
    };
    simgui_setup(&imgui_desc);

    ImGui::GetIO().IniFilename = "databrowser.ini";

    const float dpi = sapp_dpi_scale();

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const bool fontLoaded =
        io.Fonts->AddFontFromFileTTF("resources/fonts/Roboto-Medium.ttf", 14.0f * dpi)
        != nullptr;
    if (!fontLoaded)
        io.Fonts->AddFontDefault();

    // Merge FontAwesome 6 Solid as icon glyphs — gracefully optional.
    // Guard with exists() so ImGui never hits its assert on a missing file.
    // Drop fa-solid-900.ttf into resources/fonts/ to enable icons.
    {
        const char* faPath = "resources/fonts/fa-solid-900.otf";
        if (std::filesystem::exists(faPath)) {
            ImFontConfig cfg;
            cfg.MergeMode        = true;
            cfg.PixelSnapH       = true;
            cfg.GlyphMinAdvanceX = 14.0f * dpi;
            io.Fonts->AddFontFromFileTTF(faPath, 14.0f * dpi,
                                         &cfg, Icons::kFAGlyphRanges);
        }
    }

    io.FontGlobalScale = fontLoaded ? (1.0f / dpi) : 1.0f;

    // macOS: use Cmd (Super) for all text-editing shortcuts (copy/paste/undo/…)
    // Without this flag ImGui's InputText widgets expect Ctrl, which on macOS
    // conflicts with the system and leaves Cmd+C / Cmd+V non-functional.
#if defined(__APPLE__)
    io.ConfigMacOSXBehaviors = true;
#endif

    theme_.ApplyColorTheme(ThemeType::SolarizedDark);
    theme_.ApplyImGuiStyle(dpi);

    UpdateLayout();
    layout_.ApplyToImGui();

    config_ = LoadAppConfig();

    const auto entries = Adapters::AdapterRegistry::Entries();

    const std::string_view preferredAdapter =
        !dbPath.empty()               ? std::string_view{adapterName}         :
        !config_.adapterName.empty()  ? std::string_view{config_.adapterName} :
        !entries.empty()              ? entries.front().name                   :
                                        std::string_view{};

    if (const auto it = std::ranges::find(entries, preferredAdapter,
                                          &Adapters::AdapterEntry::name);
        it != entries.end())
    {
        adapterIdx_ = static_cast<int>(std::distance(entries.begin(), it));
    }

    const std::string preferredPath =
        !dbPath.empty()                   ? dbPath                   :
        !config_.connectionString.empty() ? config_.connectionString :
                                            std::string{};

    if (!preferredPath.empty())
        std::snprintf(pathBuf_, sizeof(pathBuf_), "%s", preferredPath.c_str());

    if (!preferredPath.empty() && !preferredAdapter.empty())
        TryConnect(std::string{preferredAdapter}, preferredPath);

    // Determine the starting directory for the secondary filesystem browser:
    //   • if we connected to a file-based source → show its parent directory
    //   • otherwise → show the user's home directory
    {
        namespace fs = std::filesystem;
        std::string startDir;
        if (!preferredPath.empty()) {
            const fs::path p{preferredPath};
            startDir = fs::is_directory(p)
                ? p.string()
                : (p.has_parent_path() ? p.parent_path().string() : p.string());
        } else {
            const char* home = std::getenv("HOME");
#ifdef _WIN32
            if (!home) home = std::getenv("USERPROFILE");
#endif
            startDir = home ? std::string{home} : std::string{"/"};
        }
        InitFsBrowser(startDir);
    }
}

void App::Cleanup()
{
    browser_.reset();
    browser_fs_.reset();
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
        showWelcome_  = true;
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

    sg_pass_action pass_action = {};
    pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass_action.colors[0].clear_value = { 0.08f, 0.08f, 0.08f, 1.0f };

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
                    showWelcome_  = true;
                    connectError_.clear();
                }
                break;

            case SAPP_KEYCODE_Q:
                if (mod) sapp_quit();
                break;

            case SAPP_KEYCODE_W:
                // Cmd/Ctrl+W — close frontmost modal/dialog
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

            default: break;
        }
    }
}

void App::RenderFrame()
{
    navbar_.Render(theme_);

    const ImGuiIO& io   = ImGui::GetIO();
    const float    topY = ImGui::GetFrameHeightWithSpacing();
    const float    dispW = io.DisplaySize.x;
    const float    dispH = io.DisplaySize.y - topY;

    const bool hasMain = browser_.has_value() && !showWelcome_;
    const bool hasFs   = browser_fs_.has_value();

    if (showWelcome_) {
        RenderWelcome();
        if (!hasFs) return;
    }

    if (hasFs && hasMain) {
        constexpr float kFsRatio = 0.35f;
        const float fsW   = dispW * kFsRatio;
        const float mainW = dispW - fsW;

        browser_fs_->SetLayout(layout_);
        ImGui::SetNextWindowPos(ImVec2(0.0f, topY),   ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(fsW, dispH),  ImGuiCond_FirstUseEver);
        browser_fs_->Render();

        browser_->SetLayout(layout_);
        ImGui::SetNextWindowPos(ImVec2(fsW, topY),      ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(mainW, dispH),  ImGuiCond_FirstUseEver);
        browser_->Render();

    } else if (hasFs) {
        browser_fs_->SetLayout(layout_);
        ImGui::SetNextWindowPos(ImVec2(0.0f, topY),    ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(dispW, dispH), ImGuiCond_FirstUseEver);
        browser_fs_->Render();

    } else if (hasMain) {
        browser_->SetLayout(layout_);
        ImGui::SetNextWindowPos(ImVec2(0.0f, topY),    ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(dispW, dispH), ImGuiCond_FirstUseEver);
        browser_->Render();
    }
}

void App::RenderWelcome()
{
    const ImGuiIO& io     = ImGui::GetIO();
    const ImVec2   center = { io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f };

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
        }
    }

    ImGui::Text("Path / URI");
    ImGui::SameLine(kLabelW);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const bool enterPressed =
        ImGui::InputTextWithHint("##path", hint,
                                 pathBuf_, sizeof(pathBuf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue);

    // ── Passphrase (SQLite/SQLCipher only) ────────────────────────────────
    const bool isSqlite = (entries[adapterIdx_].name == "sqlite");
    if (isSqlite) {
        ImGui::Spacing();
        ImGui::Text("Passphrase");
        ImGui::SameLine(kLabelW);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##passphrase", passBuf_, sizeof(passBuf_),
                         ImGuiInputTextFlags_Password);
        ImGui::Spacing();
        ImGui::Text("Read-only");
        ImGui::SameLine(kLabelW);
        ImGui::Checkbox("##readonly", &readOnly_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Open read-only (safer; required for encrypted DBs unless write is needed)");
    }

    ImGui::Spacing();

    const bool hasPath = pathBuf_[0] != '\0';
    if (!hasPath)
        ImGui::BeginDisabled();

    const bool doConnect =
        ImGui::Button("Connect", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))
        || (hasPath && enterPressed);

    if (!hasPath)
        ImGui::EndDisabled();

    if (!config_.connectionString.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Last used: %s  (%s)",
            std::filesystem::path(config_.connectionString).filename().string().c_str(),
            config_.adapterName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Restore")) {
            std::snprintf(pathBuf_, sizeof(pathBuf_),
                          "%s", config_.connectionString.c_str());
            if (const auto it = std::ranges::find(entries, config_.adapterName,
                                                   &Adapters::AdapterEntry::name);
                it != entries.end())
            {
                adapterIdx_ = static_cast<int>(std::distance(entries.begin(), it));
            }
        }
    }

    if (!connectError_.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "  %s", connectError_.c_str());
    }

    ImGui::End();

    if (doConnect)
        TryConnect(entries[adapterIdx_].name, pathBuf_,
                   passBuf_[0] != '\0' ? std::string{passBuf_} : std::string{},
                   readOnly_);
}

void App::TryConnect(const std::string& adapter, const std::string& path,
                     const std::string& passphrase, bool readOnly)
{
    connectError_.clear();

    Adapters::ConnectionParams p;
    p.adapterName      = adapter;
    p.connectionString = path;
    p.readOnly         = readOnly;
    if (!passphrase.empty())
        p.password = passphrase;

    auto result = Adapters::AdapterRegistry::CreateConnected(adapter, p);
    if (!result) {
        connectError_ = result.error();
        std::println(stderr, "[App] Connect failed ({}): {}", adapter, connectError_);
        return;
    }

    config_.adapterName      = adapter;
    config_.connectionString = path;
    SaveAppConfig(config_);

    const std::string title = std::format("{} \xe2\x80\x94 {}",
        adapter,
        std::filesystem::path(path).filename().string());

    browser_.emplace(std::move(*result), title);
    showWelcome_ = false;

    // If we opened a file-based source, sync the FS browser to its parent dir
    if (browser_fs_) {
        namespace fs = std::filesystem;
        const fs::path fp{path};
        if (!fs::is_directory(fp)) {
            const std::string parentDir = fp.parent_path().string();
            if (fsAdapter_) {
                fsAdapter_->SetCurrentPath(parentDir);
                browser_fs_->NavigateTo(parentDir);
                browser_fs_->SetWindowTitle(
                    std::format("Files \xe2\x80\x94 {}",
                        fs::path(parentDir).filename().string()));
                fsPathSync_ = true;
            }
        }
    }

    std::println(stderr, "[App] Connected: {} ({})", path, adapter);
}

void App::InitFsBrowser(const std::string& dirPath)
{
    Adapters::ConnectionParams fsp;
    fsp.adapterName      = std::string{Adapters::name_of(Adapters::AdapterKind::Filesystem)};
    fsp.connectionString = dirPath;

    auto r = Adapters::AdapterRegistry::CreateConnected(fsp.adapterName, fsp);
    if (!r) return;

    // Keep a typed pointer for navigation callbacks (raw — owned by the DataBrowser)
    fsAdapter_ = dynamic_cast<Adapters::FilesystemAdapter*>(r->get());

    const std::string title = std::format("Files \xe2\x80\x94 {}",
        std::filesystem::path(dirPath).filename().empty()
            ? dirPath
            : std::filesystem::path(dirPath).filename().string());

    browser_fs_.emplace(std::move(*r), title);

    // Seed the path bar
    std::snprintf(fsPathBuf_, sizeof(fsPathBuf_), "%s", dirPath.c_str());
    fsPathSync_ = false;

    // ── Install filesystem-specific hooks on the DataBrowser ─────────────
    if (fsAdapter_) {
        // Navigation bar rendered above the grid
        browser_fs_->SetPreContentHook([this]() { RenderFsNavBar(); });

        // Double-click a directory row → navigate into it
        // Row layout for filesystem adapter: [name, kind, size, modified, perms, path]
        browser_fs_->SetOnRowDblClick([this](int /*rowIdx*/,
                                             const std::vector<std::string>& row)
        {
            if (row.size() < 6) return;
            const std::string& kind = row[1];
            const std::string& path = row[5];
            if (kind != "dir") return;

            fsAdapter_->SetCurrentPath(path);
            browser_fs_->NavigateTo(path);

            // Update title
            browser_fs_->SetWindowTitle(
                std::format("Files \xe2\x80\x94 {}",
                    std::filesystem::path(path).filename().empty()
                        ? path
                        : std::filesystem::path(path).filename().string()));

            fsPathSync_ = true;
        });

        // ── Column customizer: platform-specific click actions ─────────────
        // FA filesystem row layout: [name, kind, size, modified, permissions, path]
        browser_fs_->SetColumnCustomizer([this](std::vector<ImGuiExt::ColumnDef>& cols) {
            for (auto& col : cols) {
                if (col.key == "path") {
                    // Platform navigate policy from ColumnPolicy helper
                    const auto nav = ImGuiExt::PlatformNavigatePolicy();
                    if (nav.clickNavigates) {
                        col.onCellClick = [this](const std::string& v, int) {
                            NavigateFs(v);
                        };
                    } else {
                        col.onCellDblClick = [this](const std::string& v, int) {
                            NavigateFs(v);
                        };
                    }
                } else if (col.key == "name") {
                    // Prepend a kind icon to the name cell
                    col.renderer = [](const std::string& name, int /*rowIdx*/) {
                        ImGui::TextUnformatted(name.c_str());
                    };
                    // Name column: same navigate behaviour but scoped to row context
                    // (full path comes from row[5] — handled by SetOnRowDblClick on
                    // the DataBrowser level, so no per-column action needed here)
                }
            }
        });
    }
}

void App::RenderFsNavBar()
{
    if (!fsAdapter_) return;

    // Sync path bar when the adapter was navigated programmatically
    if (fsPathSync_) {
        std::snprintf(fsPathBuf_, sizeof(fsPathBuf_),
                      "%s", fsAdapter_->GetCurrentPath().c_str());
        fsPathSync_ = false;
    }

    // ── Up button ──────────────────────────────────────────────────────────
    const bool atRoot = (fsAdapter_->GetCurrentPath() == fsAdapter_->GetParentPath());
    if (atRoot) ImGui::BeginDisabled();
    if (ImGui::Button("\xe2\x86\x91 Up")) {    // ↑
        fsAdapter_->NavigateUp();
        const std::string p = fsAdapter_->GetCurrentPath();
        browser_fs_->NavigateTo(p);
        browser_fs_->SetWindowTitle(
            std::format("Files \xe2\x80\x94 {}",
                std::filesystem::path(p).filename().empty()
                    ? p : std::filesystem::path(p).filename().string()));
        fsPathSync_ = true;
    }
    if (atRoot) ImGui::EndDisabled();

    ImGui::SameLine();

    // ── Home button ───────────────────────────────────────────────────────
    if (ImGui::Button("\xe2\x8c\x82")) {   // ⌂
        fsAdapter_->NavigateHome();
        const std::string p = fsAdapter_->GetCurrentPath();
        browser_fs_->NavigateTo(p);
        browser_fs_->SetWindowTitle(
            std::format("Files \xe2\x80\x94 {}",
                std::filesystem::path(p).filename().empty()
                    ? p : std::filesystem::path(p).filename().string()));
        fsPathSync_ = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Home directory");

    ImGui::SameLine();

    // ── Editable path bar ─────────────────────────────────────────────────
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##fspathbar", fsPathBuf_, sizeof(fsPathBuf_),
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        // User pressed Enter — validate and navigate
        std::error_code ec;
        const bool isDir = std::filesystem::is_directory(
            std::filesystem::path(fsPathBuf_), ec);
        if (!ec && isDir) {
            fsAdapter_->SetCurrentPath(fsPathBuf_);
            const std::string p = fsAdapter_->GetCurrentPath();
            browser_fs_->NavigateTo(p);
            browser_fs_->SetWindowTitle(
                std::format("Files \xe2\x80\x94 {}",
                    std::filesystem::path(p).filename().empty()
                        ? p : std::filesystem::path(p).filename().string()));
            std::snprintf(fsPathBuf_, sizeof(fsPathBuf_), "%s", p.c_str());
        }
        // If not a valid directory, leave the buffer as-is for correction
    }
}

void App::NavigateFs(const std::string& absolutePath)
{
    if (!fsAdapter_ || !browser_fs_) return;

    namespace fs = std::filesystem;
    std::error_code ec;

    if (fsAdapter_->EntryIsDir(absolutePath)) {
        // Navigate into the directory
        fsAdapter_->SetCurrentPath(absolutePath);
        browser_fs_->NavigateTo(absolutePath);
        browser_fs_->SetWindowTitle(
            std::format("Files \xe2\x80\x94 {}",
                fs::path(absolutePath).filename().empty()
                    ? absolutePath
                    : fs::path(absolutePath).filename().string()));
        fsPathSync_ = true;
    } else if (fsAdapter_->EntryIsFile(absolutePath)) {
        // Open file with system-default viewer
#if defined(__APPLE__)
        const std::string cmd = "open \"" + absolutePath + "\"";
#elif defined(_WIN32)
        const std::string cmd = "start \"\" \"" + absolutePath + "\"";
#else
        const std::string cmd = "xdg-open \"" + absolutePath + "\"";
#endif
        (void)std::system(cmd.c_str());
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
