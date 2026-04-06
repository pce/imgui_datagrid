#pragma once
// io/file_io.hpp — C-level file helpers that avoid std::ifstream/std::ofstream
// with toolchain they should ne issue>?
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace datagrid::io {

/// Read an entire text file into a string using C stdio.
/// Returns std::nullopt when the file cannot be opened.
[[nodiscard]] inline std::optional<std::string>
read_text_file(const std::filesystem::path& p) noexcept {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return std::nullopt;

    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (sz < 0) { std::fclose(f); return std::string{}; }

    std::string s(static_cast<std::size_t>(sz), '\0');
    const std::size_t n = std::fread(s.data(), 1, static_cast<std::size_t>(sz), f);
    std::fclose(f);
    s.resize(n);
    return s;
}

/// Write content to a file using C stdio, creating or truncating it.
/// Returns false when the file cannot be opened for writing.
[[nodiscard]] inline bool
write_text_file(const std::filesystem::path& p, std::string_view content) noexcept {
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return false;
    const std::size_t n = std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return n == content.size();
}

} // namespace datagrid::io

