#include "sql_editor.hpp"

#include "imgui.h"

#include <cstdio>

namespace UI {

bool DrawSqlEditor(SqlEditorState& state, Adapters::IDataSource* source)
{
    if (!state.visible)
        return false;

    constexpr float kEditorHeight = 70.0f;
    bool executed = false;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
    ImGui::BeginChild("##sqleditor", ImVec2(0.0f, kEditorHeight), true);

    ImGui::InputTextMultiline(
        "##sql",
        state.buf,
        sizeof(state.buf),
        ImVec2(ImGui::GetContentRegionAvail().x
                   - ImGui::CalcTextSize("Run ").x
                   - ImGui::GetStyle().ItemSpacing.x * 3,
               kEditorHeight - ImGui::GetStyle().WindowPadding.y * 2),
        ImGuiInputTextFlags_None);

    ImGui::SameLine();
    ImGui::BeginGroup();

    if (ImGui::Button("Run") && state.buf[0] != '\0' && source) {
        state.error.clear();
        state.status.clear();

        const Adapters::QueryResult res = source->Execute(state.buf);

        if (!res.ok()) {
            state.error = res.error;
        } else {
            state.resultColumns = res.columns;
            state.resultRows    = res.rows;
            state.resultMs      = res.executionMs;

            char buf[128];
            std::snprintf(buf, sizeof(buf), "%d rows  (%.1f ms)",
                          static_cast<int>(res.rows.size()), res.executionMs);
            state.status = buf;
            executed = true;
        }
    }

    if (ImGui::Button("Clear")) {
        state.buf[0] = '\0';
        state.error.clear();
        state.status.clear();
    }

    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (!state.error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "SQL Error: %s", state.error.c_str());
    else if (!state.status.empty())
        ImGui::TextDisabled("%s", state.status.c_str());

    return executed;
}

} // namespace UI

