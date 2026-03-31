
#pragma once

#include "imgui.h"

#include <filesystem>
#include <string>
#include <vector>

namespace UI {

/// Multi-tab read-only text viewer backed by ImGui::InputTextMultiline.
///
/// Designed as a drop-in component: embed one instance in your App,
/// call OpenFile() on demand, and call Render() every frame inside an
/// already-opened ImGui window.
///
/// Language is auto-detected from file content (not extension) via
/// TextSniffer; pass an explicit hint to override.
class TextArea
{
  public:
    /// Files larger than this are loaded truncated.
    static constexpr std::size_t kMaxFileBytes = 4UZ * 1024 * 1024; // 4 MiB

    /// Open a file in a new tab, or focus it if already open.
    /// Pass language_hint = "" to let the sniffer decide.
    void OpenFile(const std::filesystem::path& path, std::string language_hint = "");

    /// Render the tab-bar and editor content inside the current ImGui window.
    /// Must be called every frame while IsOpen() is true.
    void Render();

    /// True while at least one tab is open.
    [[nodiscard]] bool IsOpen() const noexcept { return !tabs_.empty(); }

    /// Discard all tabs.
    void Clear() noexcept { tabs_.clear(); active_tab_ = 0; }

    /// Set the monospace font used when rendering code / markdown content.
    /// Pass nullptr to fall back to the current ImGui default font.
    /// Call this once after App::Init() and again whenever the theme changes.
    void SetCodeFont(ImFont* font) noexcept { codeFont_ = font; }

  private:
    struct Tab
    {
        std::string path;
        std::string name;          ///< Filename shown on the tab
        std::string language_hint; ///< e.g. "python", "json", "" → shown as [lang]
        std::string content;       ///< Raw file bytes (null-terminated for ImGui)
        bool        truncated = false;
    };

    void LoadContent(Tab& tab);
    void HandleShortcuts(bool& out_wants_close);

    std::vector<Tab> tabs_;
    int              active_tab_ = 0;
    ImFont*          codeFont_   = nullptr; ///< Set via SetCodeFont(); null = default font
};

} // namespace UI
