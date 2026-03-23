#include "ui.h"

#include <imgui.h>

namespace ui {

void renderMainWindow(AppState& state) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("Main Window", nullptr, windowFlags);

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.5f;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float panelHeight = available.y - footerHeight;
    const float leftWidth = available.x * 0.60f;

    ImGui::BeginChild("EditorPanel", ImVec2(leftWidth, panelHeight), true);
    ImGui::TextUnformatted("main.tex editor");
    ImGui::Separator();
    const ImVec2 editorSize = ImGui::GetContentRegionAvail();
    if (ImGui::InputTextMultiline(
            "##editor",
            state.editorBuffer.data(),
            state.editorBuffer.size(),
            editorSize,
            ImGuiInputTextFlags_AllowTabInput)) {
        state.editorDirty = true;
        state.lastEditAt = std::chrono::steady_clock::now();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("PreviewPanel", ImVec2(0.0f, panelHeight), true);
    ImGui::TextUnformatted("PDF Preview");
    ImGui::Separator();
    ImGui::TextWrapped("PDF preview not implemented yet.");
    if (!state.lastPdfPath.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("Last PDF: %s", state.lastPdfPath.string().c_str());
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Compile")) {
        state.compileRequested = true;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto", &state.autoMode);

    ImGui::SameLine();
    if (ImGui::Button("Open PDF")) {
        state.openPdfRequested = true;
    }

    ImGui::SameLine();
    if (state.compileInProgress) {
        ImGui::TextUnformatted("Compiling...");
    } else {
        ImGui::TextUnformatted(state.lastCompileSuccess ? "Last compile: success" : "Last compile: idle/fail");
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Status: %s", state.status.c_str());
    if (!state.lastCommand.empty()) {
        ImGui::TextWrapped("Command: %s", state.lastCommand.c_str());
    }

    ImGui::End();
}

}  // namespace ui
