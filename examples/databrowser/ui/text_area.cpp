#include "text_area.hpp"
#include "filetype_sniffer.hpp"
#include "../io/platform.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <memory>

#if defined(__APPLE__)
#   define TEXTAREA_MOD_KEY (ImGui::GetIO().KeySuper)
#else
#   define TEXTAREA_MOD_KEY (ImGui::GetIO().KeyCtrl)
#endif

namespace datagrid::ui {

void TextArea::OpenFile(const std::filesystem::path& path, std::string language_hint)
{
    const std::string ps = path.string();

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i)
    {
        if (tabs_[static_cast<std::size_t>(i)].path == ps)
        {
            active_tab_ = i;
            return;
        }
    }

    Tab tab;
    tab.path          = ps;
    tab.name          = path.filename().string();
    tab.language_hint = std::move(language_hint);

    LoadContent(tab);

    active_tab_ = static_cast<int>(tabs_.size());
    tabs_.push_back(std::move(tab));
}

void TextArea::LoadContent(Tab& tab)
{
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(tab.path, ec);
    if (ec) { tab.content = "[error: " + ec.message() + "]"; return; }

    auto f = io::OpenBinaryFile(std::filesystem::path(tab.path));
    if (!f) { tab.content = "[error: cannot open file]"; return; }

    const std::size_t readSz = std::min(fileSize, kMaxFileBytes);
    tab.truncated            = fileSize > kMaxFileBytes;

    tab.content.resize(readSz);
    const std::size_t got = std::fread(tab.content.data(), 1, readSz, f.get());
    if (std::ferror(f.get())) { tab.content = "[error: read failed]"; return; }
    tab.content.resize(got);

    if (tab.language_hint.empty())
    {
        const auto sampleLen = std::min(tab.content.size(), std::size_t{512});
        const auto sample    = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(tab.content.data()), sampleLen};
        tab.language_hint = sniff_bytes(sample).language;
    }
}

void TextArea::HandleShortcuts(bool& out_wants_close)
{
    if (TEXTAREA_MOD_KEY && ImGui::IsKeyPressed(ImGuiKey_W, /*repeat=*/false))
        out_wants_close = true;
}

void TextArea::Render()
{
    if (tabs_.empty()) return;

    if (!ImGui::BeginTabBar("##ta_tabs",
                            ImGuiTabBarFlags_AutoSelectNewTabs     |
                            ImGuiTabBarFlags_FittingPolicyScroll   |
                            ImGuiTabBarFlags_TabListPopupButton))
        return;

    for (int i = 0; i < static_cast<int>(tabs_.size()); )
    {
        auto& tab = tabs_[static_cast<std::size_t>(i)];

        char label[160];
        std::snprintf(label, sizeof(label), "%s ##ta%d", tab.name.c_str(), i);

        bool tab_open    = true;
        bool wants_close = false;

        if (ImGui::BeginTabItem(label, &tab_open))
        {
            active_tab_ = i;
            HandleShortcuts(wants_close);

            const bool hasLang = !tab.language_hint.empty();
            if (hasLang) {
                ImGui::TextDisabled("[%s]", tab.language_hint.c_str());
                // Push theme's monospace font for code / markdown content.
                if (codeFont_)
                    ImGui::PushFont(codeFont_);
            }
            else ImGui::TextDisabled("[text]");
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", tab.path.c_str());
            if (tab.truncated)
            {
                ImGui::SameLine();
                ImGui::TextColored({1.f, 0.65f, 0.f, 1.f}, "  [truncated at 4 MiB]");
            }

            char ed_id[32];
            std::snprintf(ed_id, sizeof(ed_id), "##ta_ed%d", i);

            // Read-only: passing content.size() + 1 as buf_size is safe because
            // std::string guarantees a null terminator at data()[size()], and
            // ImGui never writes back through the pointer in read-only mode.
            ImGui::InputTextMultiline(
                ed_id,
                tab.content.data(),
                tab.content.size() + 1,
                ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y),
                ImGuiInputTextFlags_ReadOnly);


            // switch back to last font (in this case default)
            if (hasLang && codeFont_)
                ImGui::PopFont();


            ImGui::EndTabItem();
        }

        // Erase AFTER EndTabItem so ImGui's internal tab list stays consistent.
        if (!tab_open || wants_close)
        {
            tabs_.erase(tabs_.begin() + i);
            active_tab_ = std::clamp(active_tab_, 0,
                                     std::max(0, static_cast<int>(tabs_.size()) - 1));
            // Don't increment — next tab slid into slot i
        }
        else
        {
            ++i;
        }
    }

    ImGui::EndTabBar();
}

} // namespace datagrid::ui

