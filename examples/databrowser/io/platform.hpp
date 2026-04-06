#pragma once
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

/// Compile-time OS detection — usable across adapter and UI layers.
/// Prefer `if constexpr` over raw `#ifdef` for branching so all branches are
/// syntax-checked on every platform.  Use `#if` only for platform-specific
/// *includes* (e.g. <windows.h>), isolated at the top of each TU.
// I/O utilities

namespace fs = std::filesystem;

namespace datagrid::io {

    namespace Platform {

        inline constexpr bool isWindows =
        #if defined(_WIN32)
            true;
#else
                false;
#endif

        inline constexpr bool isMacOS =
        #if defined(__APPLE__)
            true;
#else
                false;
#endif

        inline constexpr bool isLinux =
        #if defined(__linux__)
            true;
#else
                false;
#endif

        inline constexpr bool isPosix = !isWindows;

        /// Display name for the file-permission/attribute column.
        inline constexpr std::string_view kPermColName = isPosix ? "permissions" : "attributes";

        /// True when the entry should be treated as hidden.
        /// POSIX: dot-prefix.  Windows: FILE_ATTRIBUTE_HIDDEN flag OR dot-prefix.
        /// Never throws — errors are treated as "not hidden".
        [[nodiscard]] inline bool IsHidden(const std::filesystem::path& p) noexcept
        {
            const std::string name = p.filename().string();
            if (!name.empty() && name[0] == '.') return true;
            if constexpr (isWindows) {
#if defined(_WIN32)
                const DWORD attrs = ::GetFileAttributesW(p.wstring().c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN))
                    return true;
#endif
            }
            return false;
        }

    } // namespace Platform


/// Primary modifier key name for display in menu shortcuts.
inline constexpr const char* kModName        = Platform::isMacOS ? "Cmd" : "Ctrl";

/// Navigation convention: single-click on macOS (Finder), double-click elsewhere.
inline constexpr bool kClickNavigates    = Platform::isMacOS;
inline constexpr bool kDblClickNavigates = !Platform::isMacOS;

/// RAII file handle — move-only, zero-overhead deleter.
struct FCloseDeleter
{
    void operator()(std::FILE* f) const noexcept { std::fclose(f); }
};

using FileHandle = std::unique_ptr<std::FILE, FCloseDeleter>;

/// Open a file for binary reading.  Returns an empty handle on failure.
[[nodiscard]] inline FileHandle OpenBinaryFile(const fs::path& p) noexcept
{
#ifdef _WIN32
    return FileHandle{::_wfopen(p.c_str(), L"rb")};
#else
    return FileHandle{std::fopen(p.c_str(), "rb")};
#endif
}

/// Hand a path off to the OS default application.
inline void OpenWithSystem(const fs::path& filePath)
{
#ifdef _WIN32
    const auto cmd = "start \"\" \"" + filePath.string() + "\"";
#elif defined(__APPLE__)
    const auto cmd = "open \"" + filePath.string() + "\"";
#else
    const auto cmd = "xdg-open \"" + filePath.string() + "\"";
#endif
    std::system(cmd.c_str()); // NOLINT(concurrency-mt-unsafe)
}

} // namespace datagrid::io

