#include "app.hpp"
#include "sokol_app.h"
#include "sokol_log.h"

#include "adapters/adapter_kind.hpp"

#include <filesystem>
#include <string>

#if defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace {

std::filesystem::path ExeDir()
{
    namespace fs = std::filesystem;
#if defined(__APPLE__)
    char     buf[PATH_MAX] = {};
    uint32_t sz            = static_cast<uint32_t>(sizeof(buf));
    if (_NSGetExecutablePath(buf, &sz) == 0)
        return fs::path(buf).parent_path();
#elif defined(_WIN32)
    char buf[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH))
        return fs::path(buf).parent_path();
#else
    char          buf[PATH_MAX] = {};
    const ssize_t n             = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return fs::path(buf).parent_path();
    }
#endif
    return fs::current_path();
}

void SetResourceDirAsCwd()
{
    namespace fs                = std::filesystem;
    const fs::path exeResources = ExeDir() / "resources";
    if (fs::is_directory(exeResources)) {
        fs::current_path(ExeDir());
        return;
    }
    if (fs::is_directory(fs::current_path() / "resources"))
        return;
    fs::current_path(ExeDir());
}

std::string DetectAdapter(const std::filesystem::path& p)
{
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec))
        return std::string{Adapters::name_of(Adapters::AdapterKind::Filesystem)};
    const auto ext = p.extension().string();
    if (ext == ".csv")
        return std::string{Adapters::name_of(Adapters::AdapterKind::CSV)};
    if (ext == ".duckdb")
        return std::string{Adapters::name_of(Adapters::AdapterKind::DuckDB)};
    return std::string{Adapters::name_of(Adapters::AdapterKind::SQLite)};
}

// Sokol requires C-function callbacks; wrap app state so we can reach it
// via sapp_userdata() without file-scope globals.
struct AppState
{
    App         app;
    std::string windowTitle;
};

App& GetApp()
{
    return static_cast<AppState*>(sapp_userdata())->app;
}

} // namespace

sapp_desc sokol_main(int argc, char* argv[])
{
    SetResourceDirAsCwd();

    // Heap-allocate so the pointer stays valid for the entire sokol lifetime.
    auto* state = new AppState{};

    if (argc > 1) {
        state->app.dbPath      = argv[1];
        state->app.adapterName = (argc > 2) ? argv[2] : DetectAdapter(std::filesystem::path{state->app.dbPath});
    }

    state->windowTitle = state->app.dbPath.empty() ? "DataBrowser"
                                                   : "DataBrowser \xe2\x80\x94 " +
                                                         std::filesystem::path{state->app.dbPath}.filename().string();

    return sapp_desc{
        .init_cb    = [] { GetApp().Init(); },
        .frame_cb   = [] { GetApp().Frame(); },
        .cleanup_cb = [] { GetApp().Cleanup(); },
        .event_cb   = [](const sapp_event* ev) { GetApp().Event(ev); },

        .user_data = state,

        .width        = 1280,
        .height       = 720,

        .high_dpi = true,
        .window_title = state->windowTitle.c_str(),

        .enable_clipboard = true,
        .clipboard_size   = 8192,

        .icon   = {.sokol_default = true},
        .logger = {.func = slog_func},
    };
}
