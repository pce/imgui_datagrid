#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "../io/platform.hpp"

namespace datagrid::ui {

struct SniffResult
{
    bool        is_text  = false;
    std::string language; ///< "python" | "shell" | "json" | "xml" | "cpp" | … or ""
    std::string encoding; ///< "ascii" | "utf-8" | "utf-8-bom"
};

namespace detail {

inline bool has_utf8_bom(std::span<const std::byte> s) noexcept
{
    return s.size() >= 3 &&
           std::to_integer<uint8_t>(s[0]) == 0xEF &&
           std::to_integer<uint8_t>(s[1]) == 0xBB &&
           std::to_integer<uint8_t>(s[2]) == 0xBF;
}

inline bool has_utf16_bom(std::span<const std::byte> s) noexcept
{
    if (s.size() < 2) return false;
    const auto b0 = std::to_integer<uint8_t>(s[0]);
    const auto b1 = std::to_integer<uint8_t>(s[1]);
    return (b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF);
}


inline std::string detect_language(std::span<const std::byte> sample) noexcept
{
    if (sample.empty()) return {};
    const std::string_view sv{reinterpret_cast<const char*>(sample.data()), sample.size()};

    if (sv.starts_with("#!")) {
        const auto nl      = sv.find('\n');
        const auto shebang = sv.substr(2, nl == std::string_view::npos ? std::string_view::npos : nl - 2);
        if (shebang.contains("python"))   return "python";
        if (shebang.contains("ruby"))     return "ruby";
        if (shebang.contains("node"))     return "javascript";
        if (shebang.contains("perl"))     return "perl";
        if (shebang.contains("lua"))      return "lua";
        if (shebang.contains("php"))      return "php";
        if (shebang.contains("swift"))    return "swift";
        return "shell";
    }

    if (sv.starts_with("<?xml") || sv.starts_with("<?XML")) return "xml";
    if (sv.starts_with("<!DOCTYPE") || sv.starts_with("<html") ||
        sv.starts_with("<HTML"))                            return "html";
    if (sv.starts_with("---\n") || sv.starts_with("---\r")) return "yaml";

    { const auto p = sv.find_first_not_of(" \t\r\n");
      if (p != std::string_view::npos && (sv[p] == '{' || sv[p] == '[')) return "json"; }

    auto starts_icase = [&](std::string_view kw) noexcept {
        const auto h = sv.substr(0, std::min(sv.size(), std::size_t{100}));
        if (h.size() < kw.size()) return false;
        for (std::size_t i = 0; i < kw.size(); ++i)
            if (std::toupper(static_cast<unsigned char>(h[i])) !=
                std::toupper(static_cast<unsigned char>(kw[i]))) return false;
        return true;
    };

    if (starts_icase("SELECT ") || starts_icase("CREATE ") || starts_icase("INSERT ") ||
        starts_icase("UPDATE ") || starts_icase("DELETE ") || starts_icase("DROP ")   ||
        starts_icase("ALTER ")  || starts_icase("WITH ")   || starts_icase("PRAGMA ")) return "sql";
    if (sv.contains("#include ") || sv.contains("#pragma once") ||
        sv.contains("namespace ") || sv.contains("template<"))  return "cpp";
    if (sv.starts_with("use ") || sv.contains("fn main(") || sv.starts_with("mod ")) return "rust";
    if (sv.starts_with("package "))                              return "go";
    if (sv.contains("cmake_minimum_required") || sv.contains("add_executable") ||
        sv.contains("target_link_libraries"))                    return "cmake";
    if (sv.starts_with("# ") || sv.starts_with("## "))          return "markdown";
    if (sv.starts_with("[") && sv.find(" = ") != std::string_view::npos) return "toml";

    return {};
}

} // namespace detail

/// Classify a byte buffer as text or binary — same heuristic as git and file(1):
///   - NUL byte (0x00)     → binary immediately
///   - UTF-16 BOM          → binary (not displayable as plain text)
///   - High bytes ≥ 0x80   → valid UTF-8, never penalised
///   - Control bytes < 0x20 (excluding \t \n \r) count as non-printable
///   - > 30 % non-printable → binary
[[nodiscard]] inline SniffResult sniff_bytes(std::span<const std::byte> sample) noexcept
{
    SniffResult r;
    if (sample.empty()) { r.is_text = true; r.encoding = "utf-8"; return r; }

    if (detail::has_utf16_bom(sample)) return r;

    const bool utf8_bom = detail::has_utf8_bom(sample);
    if (utf8_bom) r.encoding = "utf-8-bom";

    std::size_t non_printable = 0;
    bool        has_high      = false;

    for (const auto b : sample) {
        const auto c = std::to_integer<unsigned char>(b);

        if (c == 0x00)                           return r;           // NUL → binary
        if (c == '\n' || c == '\r' || c == '\t') continue;           // whitespace OK
        if (c >= 0x80)                           { has_high = true; continue; } // UTF-8 OK
        if (c < 0x20)                            ++non_printable;

        if (non_printable * 3 > sample.size())   return r;           // early exit
    }

    r.is_text  = true;
    if (!utf8_bom) r.encoding = has_high ? "utf-8" : "ascii";
    r.language = detail::detect_language(sample);
    return r;
}

[[nodiscard]] inline SniffResult sniff_file(const std::filesystem::path& path) noexcept
{
    constexpr std::size_t kMax = 512;
    std::array<std::byte, kMax> buf;

    auto f = io::OpenBinaryFile(path);
    if (!f) return {};

    const std::size_t n = std::fread(buf.data(), 1, buf.size(), f.get());
    if (n == 0 || std::ferror(f.get())) return {};
    return sniff_bytes({buf.data(), n});
}

// ── File-type classifiers ─────────────────────────────────────────────────
// All accept std::filesystem::path (implicitly constructible from std::string).
// Extension checks are case-insensitive; magic-byte checks open at most
// 16 bytes from the file and close immediately.

namespace detail {
inline std::string lower_ext(const std::filesystem::path& p) noexcept {
    std::string e = p.extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}
} // namespace detail

[[nodiscard]] inline bool IsImageFile(const std::filesystem::path& p) noexcept
{
    const std::string e = detail::lower_ext(p);
    for (auto sv : {".jpg",".jpeg",".png",".bmp",".gif",".tga",".hdr",".pic",".pnm",".ppm",".pgm"})
        if (e == sv) return true;
    return false;
}

[[nodiscard]] inline bool IsSqliteFile(const std::filesystem::path& p) noexcept
{
    const std::string e = detail::lower_ext(p);
    if (e == ".db" || e == ".sqlite" || e == ".sqlite3" || e == ".db3") return true;
    // Magic: "SQLite format 3\0"
    char magic[15] = {};
    auto f = io::OpenBinaryFile(p);
    if (!f) return false;
    const auto n = std::fread(magic, 1, 15, f.get());
    return n == 15 && std::string_view{magic, 15} == "SQLite format 3";
}

[[nodiscard]] inline bool IsPdfFile(const std::filesystem::path& p) noexcept
{
    if (detail::lower_ext(p) == ".pdf") return true;
    char magic[4] = {};
    auto f = io::OpenBinaryFile(p);
    if (!f) return false;
    const auto n = std::fread(magic, 1, 4, f.get());
    return n == 4 && magic[0]=='%' && magic[1]=='P' && magic[2]=='D' && magic[3]=='F';
}

[[nodiscard]] inline bool IsTextFile(const std::filesystem::path& p) noexcept {
    SniffResult r = sniff_file(p);
    return r.is_text;
}

/// True when the path represents something the OS should launch.
///
/// Platform semantics:
///   macOS   — regular file with owner-exec bit set, OR a ".app" bundle directory.
///   Windows — executable by extension (.exe / .com / .bat / .cmd / .ps1 / .msi).
///   Linux   — regular file with owner-exec bit set.
[[nodiscard]] inline bool IsExecutableFile(const std::filesystem::path& p) noexcept
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if constexpr (io::Platform::isWindows) {
        const std::string ext = detail::lower_ext(p);
        for (auto sv : {".exe", ".com", ".bat", ".cmd", ".ps1", ".msi"})
            if (ext == sv) return true;
        return false;
    } else {
        if constexpr (io::Platform::isMacOS) {
            // .app bundles are launchable directory packages on macOS.
            if (p.extension() == ".app") {
                const auto st = fs::status(p, ec);
                return !ec && fs::is_directory(st);
            }
        }
        // POSIX: exec bit on a regular file only — avoids false-positives on directories.
        const auto st = fs::status(p, ec);
        if (ec || !fs::is_regular_file(st)) return false;
        return (st.permissions() & fs::perms::owner_exec) != fs::perms::none;
    }
}

} // namespace datagrid::ui

