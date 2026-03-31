#include "hex_view.hpp"
#include "filetype_sniffer.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace UI {

static std::string FormatBytes(std::size_t n)
{
    char buf[64];
    if (n < 1024)
        std::snprintf(buf, sizeof(buf), "%zu B", n);
    else if (n < 1024UZ * 1024)
        std::snprintf(buf, sizeof(buf), "%zu B  (%.1f KiB)", n, static_cast<double>(n) / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%zu B  (%.2f MiB)", n,
                      static_cast<double>(n) / (1024.0 * 1024.0));
    return buf;
}

BlobInfo AnalyseBlob(std::span<const std::byte> data, std::size_t fullSize) noexcept
{
    BlobInfo info;
    info.size      = fullSize ? fullSize : data.size();
    info.truncated = fullSize > data.size();

    {
        const std::size_t n = std::min(data.size(), std::size_t{8});
        char              buf[32] = {};
        char*             p       = buf;
        for (std::size_t i = 0; i < n; ++i) {
            if (i) *p++ = ' ';
            p += std::snprintf(p, 4, "%02X",
                               static_cast<unsigned>(std::to_integer<uint8_t>(data[i])));
        }
        info.magic = buf;
    }

    int printable = 0;
    for (const auto b : data) {
        const auto v = std::to_integer<unsigned char>(b);
        if (v == 0)                ++info.nullCount;
        if (v >= 0x20 && v < 0x7F) ++printable;
    }
    info.printableRatio = data.empty() ? 0
                                       : static_cast<int>(100UZ * static_cast<std::size_t>(printable)
                                                                 / data.size());

    const auto sniff = sniff_bytes(data);
    info.isText   = sniff.is_text;
    info.encoding = sniff.is_text ? (sniff.encoding.empty() ? "utf-8" : sniff.encoding) : "binary";

    // Use uint8_t literals to avoid signed-char overflow for bytes > 0x7F.
    auto magicMatch = [&](std::initializer_list<uint8_t> bytes) noexcept -> bool {
        if (data.size() < bytes.size()) return false;
        std::size_t i = 0;
        for (const uint8_t b : bytes)
            if (std::to_integer<uint8_t>(data[i++]) != b) return false;
        return true;
    };

    if      (magicMatch({0xFF,0xD8,0xFF}))                       info.typeHint = "JPEG image";
    else if (magicMatch({0x89,'P','N','G','\r','\n',0x1A,'\n'})) info.typeHint = "PNG image";
    else if (magicMatch({'G','I','F','8'}))                       info.typeHint = "GIF image";
    else if (magicMatch({'B','M'}))                               info.typeHint = "BMP image";
    else if (magicMatch({'I','I','*',0x00})||
             magicMatch({'M','M',0x00,'*'}))                      info.typeHint = "TIFF image";
    else if (magicMatch({'%','P','D','F'}))                       info.typeHint = "PDF document";
    else if (magicMatch({'P','K',0x03,0x04}))                     info.typeHint = "ZIP / Office archive";
    else if (magicMatch({0x1F,0x8B}))                             info.typeHint = "Gzip archive";
    else if (magicMatch({'B','Z','h'}))                           info.typeHint = "Bzip2 archive";
    else if (magicMatch({0xFD,'7','z','X','Z',0x00}))             info.typeHint = "XZ archive";
    else if (magicMatch({0x7F,'E','L','F'}))                      info.typeHint = "ELF binary";
    else if (magicMatch({'M','Z'}))                               info.typeHint = "PE / DOS executable";
    else if (magicMatch({0xCF,0xFA,0xED,0xFE})||
             magicMatch({0xCE,0xFA,0xED,0xFE})||
             magicMatch({0xFE,0xED,0xFA,0xCF})||
             magicMatch({0xFE,0xED,0xFA,0xCE}))                   info.typeHint = "Mach-O binary";
    else if (magicMatch({0xCA,0xFE,0xBA,0xBE}))                   info.typeHint = "Java class / fat Mach-O";
    else if (magicMatch({'S','Q','L','i','t','e',' ','f'}))       info.typeHint = "SQLite database";
    else if (magicMatch({0x44,0x55,0x43,0x4B}))                   info.typeHint = "DuckDB database";
    else if (magicMatch({'R','I','F','F'}))                       info.typeHint = "WAV / AVI / RIFF";
    else if (magicMatch({'f','L','a','C'}))                       info.typeHint = "FLAC audio";
    else if (magicMatch({'O','g','g','S'}))                       info.typeHint = "Ogg container";
    else if (magicMatch({'I','D','3'})||
             (!data.empty() && std::to_integer<uint8_t>(data[0])==0xFF &&
              data.size()>1  && (std::to_integer<uint8_t>(data[1])&0xE0)==0xE0))
                                                                  info.typeHint = "MP3 audio";
    else if (sniff.is_text)
        info.typeHint = sniff.language.empty() ? "plain text" : sniff.language + " text";
    else
        info.typeHint = "binary data";

    return info;
}

void HexViewDialog::Open(std::string label, std::string_view raw, std::size_t fullSize)
{
    label_    = std::move(label);
    fullSize_ = fullSize ? fullSize : raw.size();
    const std::size_t cap = std::min(raw.size(), kMaxPreviewBytes);
    data_.resize(cap);
    const auto* src = reinterpret_cast<const std::byte*>(raw.data());
    std::copy(src, src + cap, data_.begin());
    Reanalyse();
    open_         = true;
    pending_open_ = true; // ImGui::OpenPopup is called from Render() so the ID
                          // is always resolved in the same window that hosts
                          // BeginPopupModal — never from inside a child popup.
}

void HexViewDialog::Open(std::string label, std::vector<std::byte> data, std::size_t fullSize)
{
    label_    = std::move(label);
    fullSize_ = fullSize ? fullSize : data.size();
    const std::size_t cap = std::min(data.size(), kMaxPreviewBytes);
    data_.assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(cap));
    Reanalyse();
    open_         = true;
    pending_open_ = true;
}

void HexViewDialog::OpenFile(const std::filesystem::path& path)
{
    label_ = path.string();
    data_.clear();
    std::error_code   ec;
    const std::size_t total = std::filesystem::file_size(path, ec);
    fullSize_ = ec ? 0 : total;

#ifdef _WIN32
    std::FILE* raw = ::_wfopen(path.c_str(), L"rb");
#else
    std::FILE* raw = std::fopen(path.c_str(), "rb");
#endif
    if (raw) {
        std::unique_ptr<std::FILE, decltype(&std::fclose)> f(raw, &std::fclose);
        const std::size_t readSz = std::min(ec ? kMaxPreviewBytes : total, kMaxPreviewBytes);
        data_.resize(readSz);
        const std::size_t got = std::fread(data_.data(), 1, readSz, f.get());
        if (std::ferror(f.get())) data_.clear();
        else                      data_.resize(got);
        if (fullSize_ == 0) fullSize_ = data_.size();
    }
    Reanalyse();
    open_         = true;
    pending_open_ = true;
}

void HexViewDialog::Reanalyse()
{
    info_ = AnalyseBlob({data_.data(), data_.size()}, fullSize_);
}

// ─── Render ──────────────────────────────────────────────────────────────────
// Shared popup frame: title, mode toggle, info panel, then dispatch.
void HexViewDialog::Render()
{
    if (pending_open_) {
        ImGui::OpenPopup("##hexview");
        pending_open_ = false;
    }
    if (!open_) return;

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(660.0f, 360.0f), ImVec2(1400.0f, 920.0f));
    ImGui::SetNextWindowSize(ImVec2(860.0f, 600.0f), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("##hexview", nullptr,
                                ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoScrollWithMouse))
    {
        open_ = false;
        return;
    }

    // ── title ─────────────────────────────────────────────────────────────────
    ImGui::TextDisabled("Byte Inspector");
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextUnformatted("\xe2\x80\x94");
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextUnformatted(label_.c_str());

    // ── mode toggle (right-aligned) ────────────────────────────────────────────
    const HexViewMode kModeList[]  = { HexViewMode::Standard, HexViewMode::Inspector, HexViewMode::TextHex };
    const char* const kModeLabels[]= { "Standard", "Inspector", "Text" };
    const float btnW = 76.0f;
    const float allW = btnW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - allW - ImGui::GetStyle().WindowPadding.x);
    for (int mi = 0; mi < 3; ++mi) {
        const bool active = (mode_ == kModeList[mi]);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(kModeLabels[mi], ImVec2(btnW, 0.0f))) {
            mode_     = kModeList[mi];
            selected_ = -1;
        }
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();
    ImGui::Spacing();

    // ── info panel ────────────────────────────────────────────────────────────
    constexpr ImGuiTableFlags kInfoFlags = ImGuiTableFlags_SizingFixedFit |
                                           ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##hexinfo", 2, kInfoFlags)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed,  96.0f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

        auto InfoRow = [&](const char* key, const char* val) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", key);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(val);
        };

        { std::string sz = FormatBytes(info_.size);
          if (info_.truncated) sz += "   [preview: first " + FormatBytes(data_.size()) + "]";
          InfoRow("Size", sz.c_str()); }

        InfoRow("Type",     info_.typeHint.empty() ? "(empty)" : info_.typeHint.c_str());
        InfoRow("Encoding", info_.encoding.c_str());
        if (!info_.magic.empty()) InfoRow("Magic", info_.magic.c_str());

        { char buf[64];
          std::snprintf(buf, sizeof(buf), "%d %%   %s", info_.printableRatio,
                        info_.isText ? "(text)" : "(binary)");
          InfoRow("Printable", buf); }

        if (info_.nullCount > 0) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%d", info_.nullCount);
            InfoRow("Null bytes", buf);
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── dispatch ──────────────────────────────────────────────────────────────
    switch (mode_) {
        case HexViewMode::Inspector: RenderInspector(); break;
        case HexViewMode::TextHex:   RenderTextHex();   break;
        default:                     RenderStandard();  break;
    }

    ImGui::EndPopup();
}

// ─── RenderStandard ──────────────────────────────────────────────────────────
// Fast text-line renderer: XXXXXXXX  XX XX... |ASCII|
// Virtual-scrolled via ImGuiListClipper.
void HexViewDialog::RenderStandard()
{
    constexpr int kBPR  = 16;
    const int     total = static_cast<int>(data_.size());
    const int     nRows = (total + kBPR - 1) / kBPR;

    const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.06f, 1.0f));
    ImGui::BeginChild("##hexstd",
                      ImVec2(0.0f, ImGui::GetContentRegionAvail().y - footerH),
                      false, ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clipper;
    clipper.Begin(nRows);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int off   = row * kBPR;
            const int count = std::min(kBPR, total - off);

            char line[128]; int pos = 0;
            pos += std::snprintf(line + pos, sizeof(line) - static_cast<std::size_t>(pos),
                                 "%08X  ", off);
            for (int i = 0; i < kBPR; ++i) {
                if (i == 8) line[pos++] = ' ';
                if (i < count)
                    pos += std::snprintf(line + pos, sizeof(line) - static_cast<std::size_t>(pos),
                                         "%02X ",
                                         std::to_integer<unsigned>(data_[static_cast<std::size_t>(off + i)]));
                else { line[pos++] = ' '; line[pos++] = ' '; line[pos++] = ' '; }
            }
            line[pos++] = ' '; line[pos++] = '|';
            for (int i = 0; i < count; ++i) {
                const auto b = std::to_integer<unsigned char>(data_[static_cast<std::size_t>(off + i)]);
                line[pos++] = (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
            }
            for (int i = count; i < kBPR; ++i) line[pos++] = ' ';
            line[pos++] = '|'; line[pos] = '\0';
            ImGui::TextUnformatted(line);
        }
    }
    clipper.End();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    if (ImGui::Button("Copy hex")) {
        std::string out; out.reserve(static_cast<std::size_t>(nRows) * 80);
        for (int row = 0; row < nRows; ++row) {
            char line[128]; int pos = 0;
            int  off   = row * kBPR;
            int  count = std::min(kBPR, total - off);
            pos += std::snprintf(line + pos, sizeof(line) - static_cast<std::size_t>(pos), "%08X  ", off);
            for (int i = 0; i < kBPR; ++i) {
                if (i == 8) line[pos++] = ' ';
                if (i < count)
                    pos += std::snprintf(line + pos, sizeof(line) - static_cast<std::size_t>(pos), "%02X ",
                                         std::to_integer<unsigned>(data_[static_cast<std::size_t>(off + i)]));
                else { line[pos++] = ' '; line[pos++] = ' '; line[pos++] = ' '; }
            }
            line[pos++] = ' '; line[pos++] = '|';
            for (int i = 0; i < count; ++i) {
                const auto b = std::to_integer<unsigned char>(data_[static_cast<std::size_t>(off + i)]);
                line[pos++] = (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
            }
            for (int i = count; i < kBPR; ++i) line[pos++] = ' ';
            line[pos++] = '|'; line[pos++] = '\n'; line[pos] = '\0';
            out += line;
        }
        ImGui::SetClipboardText(out.c_str());
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 80.0f);
    if (ImGui::Button("Close##std", ImVec2(80.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup(); open_ = false;
    }
}

// ─── RenderInspector ─────────────────────────────────────────────────────────
// Interactive per-byte grid: 4-column table (Addr | Hex 0-7 | Hex 8-15 | ASCII).
// Each hex cell is a Selectable; background tinted by byte class.
// Hover tooltip shows offset/hex/dec/char. Click to select; status bar shows selection.
void HexViewDialog::RenderInspector()
{
    constexpr int   kBPR   = 16;
    constexpr float kByteW = 24.0f; // selectable width per byte

    const int total = static_cast<int>(data_.size());
    const int nRows = (total + kBPR - 1) / kBPR;

    // leave room for status bar + footer
    const float footerH = ImGui::GetFrameHeightWithSpacing() * 2.0f
                        + ImGui::GetStyle().ItemSpacing.y;

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
    ImGui::BeginChild("##hexlbl",
                      ImVec2(0.0f, ImGui::GetContentRegionAvail().y - footerH),
                      false, ImGuiWindowFlags_HorizontalScrollbar);

    constexpr ImGuiTableFlags kTblF = ImGuiTableFlags_SizingFixedFit |
                                      ImGuiTableFlags_RowBg          |
                                      ImGuiTableFlags_BordersInnerV;
    // 4 columns: Addr | Bytes 0-7 | Bytes 8-15 | ASCII
    if (ImGui::BeginTable("##hbt", 4, kTblF)) {
        ImGui::TableSetupColumn("Addr",  ImGuiTableColumnFlags_WidthFixed,  72.0f);
        ImGui::TableSetupColumn("0-7",   ImGuiTableColumnFlags_WidthFixed, kByteW * 8 + 14.0f);
        ImGui::TableSetupColumn("8-15",  ImGuiTableColumnFlags_WidthFixed, kByteW * 8 + 14.0f);
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(nRows);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const int off = row * kBPR;
                ImGui::TableNextRow();

                // ── Addr ────────────────────────────────────────────────────
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%08X", off);

                // ── Two groups of 8 hex bytes ────────────────────────────────
                for (int grp = 0; grp < 2; ++grp) {
                    ImGui::TableSetColumnIndex(grp + 1);
                    for (int i = grp * 8, iend = grp * 8 + 8; i < iend; ++i) {
                        const int idx = off + i;
                        if (idx >= total) break;

                        const auto  byt     = std::to_integer<unsigned char>(data_[static_cast<std::size_t>(idx)]);
                        const bool  isPrint = (byt >= 0x20 && byt < 0x7F);
                        const bool  isNull  = (byt == 0x00);
                        const bool  isHigh  = (byt >= 0x80);
                        const bool  isSel   = (selected_ == idx);

                        // per-byte tint on top of row bg
                        ImVec4 tint = {0,0,0,0};
                        if      (isSel)    tint = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
                        else if (isNull)   tint = { 0.65f, 0.12f, 0.12f, 0.55f };
                        else if (isHigh)   tint = { 0.40f, 0.12f, 0.70f, 0.40f };
                        else if (!isPrint) tint = { 0.70f, 0.42f, 0.10f, 0.45f };

                        int nPush = 0;
                        if (tint.w > 0.0f) {
                            ImGui::PushStyleColor(ImGuiCol_Header,        tint);
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tint);
                            nPush = 2;
                        }

                        char lbl[8], sid[16];
                        std::snprintf(lbl, sizeof(lbl), "%02X", byt);
                        std::snprintf(sid, sizeof(sid), "##b%d", idx);

                        // Selectable label rendered via text, invisible selectable for hit-test
                        if (ImGui::Selectable(lbl, isSel, ImGuiSelectableFlags_None,
                                              ImVec2(kByteW, 0.0f)))
                            selected_ = isSel ? -1 : idx;

                        if (nPush) ImGui::PopStyleColor(nPush);

                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::TextDisabled("Offset"); ImGui::SameLine(56.0f);
                            ImGui::Text("0x%08X  (%d)", idx, idx);
                            ImGui::TextDisabled("Hex");    ImGui::SameLine(56.0f);
                            ImGui::Text("0x%02X", byt);
                            ImGui::TextDisabled("Dec");    ImGui::SameLine(56.0f);
                            ImGui::Text("%d",    byt);
                            ImGui::TextDisabled("Oct");    ImGui::SameLine(56.0f);
                            ImGui::Text("0%03o", byt);
                            if (isPrint) {
                                ImGui::TextDisabled("Char"); ImGui::SameLine(56.0f);
                                ImGui::Text("'%c'", static_cast<char>(byt));
                            }
                            ImGui::EndTooltip();
                        }

                        if (i < iend - 1 && (off + i + 1) < total)
                            ImGui::SameLine(0.0f, 2.0f);
                    }
                }

                // ── ASCII ───────────────────────────────────────────────────
                ImGui::TableSetColumnIndex(3);
                for (int i = 0; i < kBPR; ++i) {
                    const int idx = off + i;
                    if (idx >= total) break;
                    const auto b      = std::to_integer<unsigned char>(data_[static_cast<std::size_t>(idx)]);
                    const bool print  = (b >= 0x20 && b < 0x7F);
                    const bool isSel  = (selected_ == idx);

                    if (!print) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (isSel)  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

                    char ch[2] = { print ? static_cast<char>(b) : '.', '\0' };
                    ImGui::TextUnformatted(ch);

                    if (isSel)  ImGui::PopStyleColor();
                    if (!print) ImGui::PopStyleColor();

                    if (i < kBPR - 1 && (idx + 1) < total)
                        ImGui::SameLine(0.0f, 0.0f);
                }
            }
        }
        clipper.End();
        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // ── status bar ────────────────────────────────────────────────────────────
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    if (selected_ >= 0 && selected_ < total) {
        const auto b     = std::to_integer<unsigned char>(data_[static_cast<std::size_t>(selected_)]);
        const bool print = (b >= 0x20 && b < 0x7F);
        if (print)
            ImGui::TextDisabled("  0x%08X  |  0x%02X  |  %3d  |  '%c'",
                                selected_, b, b, static_cast<char>(b));
        else
            ImGui::TextDisabled("  0x%08X  |  0x%02X  |  %3d  |  (non-printable)",
                                selected_, b, b);
        ImGui::SameLine();
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 80.0f);
    if (ImGui::Button("Close##lbl", ImVec2(80.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup(); open_ = false;
    }
}

// ─── RenderTextHex ───────────────────────────────────────────────────────────
// 64 chars per row, printable shown as-is, non-printable as UTF-8 middle dot ·
void HexViewDialog::RenderTextHex()
{
    constexpr int kCPR  = 64;
    const int     total = static_cast<int>(data_.size());
    const int     nRows = (total + kCPR - 1) / kCPR;

    const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.06f, 1.0f));
    ImGui::BeginChild("##hextxt",
                      ImVec2(0.0f, ImGui::GetContentRegionAvail().y - footerH),
                      false, ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clipper;
    clipper.Begin(nRows);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int off   = row * kCPR;
            const int count = std::min(kCPR, total - off);

            // Address prefix
            char addr[12];
            std::snprintf(addr, sizeof(addr), "%08X  ", off);
            ImGui::TextDisabled("%s", addr);
            ImGui::SameLine(0.0f, 0.0f);

            // Content: printable as char, non-printable as · (U+00B7, UTF-8: C2 B7)
            char line[kCPR * 3 + 1]; // worst case every byte → 2-byte UTF-8
            int  pos = 0;
            for (int i = 0; i < count; ++i) {
                const auto b = std::to_integer<unsigned char>(data_[static_cast<std::size_t>(off + i)]);
                if (b >= 0x20 && b < 0x7F) {
                    line[pos++] = static_cast<char>(b);
                } else {
                    line[pos++] = '\xC2'; // middle dot ·
                    line[pos++] = '\xB7';
                }
            }
            line[pos] = '\0';
            ImGui::TextUnformatted(line);
        }
    }
    clipper.End();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    if (ImGui::Button("Copy text")) {
        std::string out; out.reserve(static_cast<std::size_t>(total));
        for (const auto& by : data_) {
            const auto b = std::to_integer<unsigned char>(by);
            out += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
        }
        ImGui::SetClipboardText(out.c_str());
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 80.0f);
    if (ImGui::Button("Close##txt", ImVec2(80.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup(); open_ = false;
    }
}

} // namespace UI
