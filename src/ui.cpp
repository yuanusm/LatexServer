#include "ui.h"

#include "compiler.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace ui {
namespace {

std::string currentDocumentLabel(const AppState& state) {
    return state.texPath.empty() ? std::string{"No file loaded"} : state.texPath.filename().string();
}

void renderToolbar(AppState& state) {
    if (ImGui::Button("Open")) {
        state.showOpenDialog = true;
        if (state.openPathBuffer.empty()) {
            state.openPathBuffer = state.texPath.empty() ? std::string{} : state.texPath.string();
        }
        ImGui::OpenPopup("OpenLaTeXFile");
    }

    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        state.saveRequested = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Compile")) {
        state.compileRequested = true;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto", &state.autoMode);

    ImGui::SameLine();
    if (ImGui::Button("Preferences")) {
        state.showPreferences = !state.showPreferences;
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    ImGui::TextUnformatted(currentDocumentLabel(state).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(state.editorDirty ? "• modified" : "• saved");
}

bool renderOpenDialog(AppState& state, editor::EditorModule& editorModule) {
    bool loaded = false;
    if (state.showOpenDialog) {
        ImGui::OpenPopup("OpenLaTeXFile");
        state.showOpenDialog = false;
    }

    if (ImGui::BeginPopupModal("OpenLaTeXFile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Enter the absolute or project-relative path to a .tex file.");
        char pathBuffer[1024] = {};
        std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", state.openPathBuffer.c_str());
        if (ImGui::InputText("Path", pathBuffer, sizeof(pathBuffer))) {
            state.openPathBuffer = pathBuffer;
        }

        if (ImGui::Button("Load")) {
            fs::path candidate = state.openPathBuffer;
            if (candidate.is_relative()) {
                candidate = state.projectRoot / candidate;
            }
            std::string error;
            if (editor::loadDocument(state, editorModule, candidate, error)) {
                loaded = true;
                ImGui::CloseCurrentPopup();
            } else {
                state.status = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return loaded;
}

bool renderPreferences(AppState& state, editor::EditorModule& editorModule) {
    bool fontChanged = false;
    if (!state.showPreferences) {
        return fontChanged;
    }

    if (ImGui::Begin("Preferences", &state.showPreferences, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& prefs = editorModule.preferences;

        ImGui::TextUnformatted("Font");
        fontChanged |= ImGui::SliderFloat("Font size", &prefs.fontSize, 12.0f, 32.0f, "%.1f pt");

        const char* currentFont = editorModule.fontOptions.empty()
            ? "Default ImGui Font"
            : editorModule.fontOptions[prefs.selectedFontIndex].label.c_str();
        if (ImGui::BeginCombo("Font family", currentFont)) {
            for (int index = 0; index < static_cast<int>(editorModule.fontOptions.size()); ++index) {
                const bool selected = prefs.selectedFontIndex == index;
                if (ImGui::Selectable(editorModule.fontOptions[index].label.c_str(), selected)) {
                    prefs.selectedFontIndex = index;
                    fontChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Colors");
        ImGui::ColorEdit4("Keyword color", &prefs.keywordColor.x);
        ImGui::ColorEdit4("Comment color", &prefs.commentColor.x);
        ImGui::ColorEdit4("String/math color", &prefs.stringColor.x);
        ImGui::ColorEdit4("Background color", &prefs.backgroundColor.x);
        ImGui::ColorEdit4("Text color", &prefs.textColor.x);

        ImGui::Separator();
        ImGui::TextUnformatted("Editor");
        ImGui::Checkbox("Show line numbers", &prefs.showLineNumbers);
        ImGui::Checkbox("Show whitespace", &prefs.showWhitespace);
        ImGui::SliderInt("Tab size", &prefs.tabSize, 2, 8);

        editor::applyPreferences(editorModule);
    }
    ImGui::End();
    return fontChanged;
}

}  // namespace

void renderMainWindow(AppState& state, editor::EditorModule& editorModule) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("Main Window", nullptr, windowFlags);

    renderToolbar(state);
    const bool reloaded = renderOpenDialog(state, editorModule);
    const bool fontChanged = renderPreferences(state, editorModule);
    if (fontChanged) {
        state.requestFontReload = true;
    }

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", state.status.c_str());
    if (!state.lastCommand.empty()) {
        ImGui::TextWrapped("latexmk: %s", state.lastCommand.c_str());
    }
    ImGui::TextUnformatted(state.compileInProgress ? "Compilation: running" : (state.lastCompileSuccess ? "Compilation: last run succeeded" : "Compilation: idle or last run failed"));

    ImGui::Separator();
    const ImVec2 editorSize = ImGui::GetContentRegionAvail();
    editor::renderEditor(state, editorModule, editorSize);

    if (reloaded) {
        state.lastCompileSuccess = false;
    }

    ImGui::End();
}

}  // namespace ui
