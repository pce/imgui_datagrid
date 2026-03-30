#include "app.hpp"
#include "sokol_app.h"
#include "sokol_log.h"

#include "adapters/adapter_kind.hpp"

#include <filesystem>
#include <string>

#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <climits>
#elif defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#  include <climits>
#endif

/// Return the directory that contains the running executable.
static std::filesystem::path ExeDir()
{
    namespace fs = std::filesystem;
#if defined(__APPLE__)
    char buf[PATH_MAX] = {};
    uint32_t sz = static_cast<uint32_t>(sizeof(buf));
    if (_NSGetExecutablePath(buf, &sz) == 0)
        return fs::path(buf).parent_path();
#elif defined(_WIN32)
    char buf[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH))
        return fs::path(buf).parent_path();
#else
    char buf[PATH_MAX] = {};
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return fs::path(buf).parent_path(); }
#endif
    return fs::current_path();
}

/// Locate the "resources/" directory and make it the working directory.
///
/// Search order:
///   1. Beside the executable   (cmake POST_BUILD copies resources there)
///   2. Current working directory  (developer running from the source tree)
///
/// If neither location has a "resources/" folder we leave the CWD unchanged
/// so relative paths still have a chance of resolving.
static void SetResourceDirAsCwd()
{
    namespace fs = std::filesystem;

    const fs::path exeResources = ExeDir() / "resources";
    if (fs::is_directory(exeResources)) {
        fs::current_path(ExeDir());
        return;
    }

    const fs::path cwdResources = fs::current_path() / "resources";
    if (fs::is_directory(cwdResources))
        return;   // already correct — leave CWD as-is

    // Last resort: change to exe dir anyway; paths may still resolve
    fs::current_path(ExeDir());
}

static App         s_app;
static std::string s_window_title;

static std::string DetectAdapter(const std::filesystem::path& p)
{
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec))
        return std::string{Adapters::name_of(Adapters::AdapterKind::Filesystem)};
    const auto ext = p.extension().string();
    if (ext == ".csv")
        return std::string{Adapters::name_of(Adapters::AdapterKind::CSV)};
    return std::string{Adapters::name_of(Adapters::AdapterKind::SQLite)};
}

sapp_desc sokol_main(int argc, char* argv[])
{
    // Locate resources/ — search exe-dir first, then original CWD
    SetResourceDirAsCwd();

    if (argc > 1) {
        s_app.dbPath      = argv[1];
        s_app.adapterName = (argc > 2)
            ? argv[2]
            : DetectAdapter(std::filesystem::path{s_app.dbPath});
    }

    s_window_title = s_app.dbPath.empty()
        ? "DataBrowser"
        : "DataBrowser \xe2\x80\x94 " +
          std::filesystem::path{s_app.dbPath}.filename().string();

    return sapp_desc{
        .init_cb    = []                       { s_app.Init();    },
        .frame_cb   = []                       { s_app.Frame();   },
        .cleanup_cb = []                       { s_app.Cleanup(); },
        .event_cb   = [](const sapp_event* ev) { s_app.Event(ev); },

        .width        = 1280,
        .height       = 720,
        .window_title = s_window_title.c_str(),

        .high_dpi = true,

        .enable_clipboard = true,
        .clipboard_size   = 8192,

        .icon   = { .sokol_default = true },
        .logger = { .func = slog_func },
    };
}
