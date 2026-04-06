#pragma once

#include "imgui.h"
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace datagrid::ui {

/// How bytes are visualised in the dialog.
enum class HexViewMode {
    Standard,  ///< Offset | hex dump | ASCII sidebar (classic monospace layout)
    Inspector, ///< OpenCV-style: colored border boxes drawn around detected string
               ///<  runs; the run content is used as the box label.
    TextHex,   ///< Wide ASCII view; hovering any character shows a hex tooltip.
};

/// Summary of what was found in a byte buffer — shown in the info panel.
struct BlobInfo
{
    std::size_t size           = 0;
    bool        isText         = false;
    bool        truncated      = false;
    std::string typeHint;
    std::string encoding;
    std::string magic;
    int         printableRatio = 0;
    int         nullCount      = 0;
};

[[nodiscard]] BlobInfo AnalyseBlob(std::span<const std::byte> data,
                                   std::size_t                fullSize = 0) noexcept;

/// Stateful hex-view modal dialog.  Embed one per owner; call Open*() to arm,
/// then Render() every frame — it manages its own ImGui popup lifecycle.
class HexViewDialog
{
  public:
    static constexpr std::size_t kMaxPreviewBytes = 4096;

    void Open(std::string label, std::string_view data, std::size_t fullSize = 0);
    void Open(std::string label, std::vector<std::byte> data, std::size_t fullSize = 0);
    void OpenFile(const std::filesystem::path& path);

    void Render();

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }

    /// Set the monospace font used for the hex / text dump.
    /// Pass nullptr to use the ImGui default font (usually also mono).
    void SetFont(ImFont* f) noexcept { font_ = f; }

  private:
    void Reanalyse();
    void RenderStandard();   ///< Classic text-line hex + ASCII
    void RenderInspector();  ///< OpenCV-style string-run bounding boxes + labels
    void RenderTextHex();    ///< ASCII text with per-char hex tooltip on hover

    struct StringRun { int offset = 0; int length = 0; };
    static constexpr int kMinRunLen = 4; ///< minimum printable-char run to box

    bool                    open_         = false;
    bool                    pending_open_ = false;
    ImFont*                 font_         = nullptr; ///< monospace font for hex dump; nullptr = ImGui default
    std::string             label_;
    std::vector<std::byte>  data_;
    std::size_t             fullSize_ = 0;
    BlobInfo                info_;
    HexViewMode             mode_     = HexViewMode::Standard;
    int                     selected_ = -1;
    std::vector<StringRun>  runs_;     ///< pre-computed for Inspector mode
};

} // namespace datagrid::ui

