#include "ui.h"

#include "compiler.h"
#include "file_tree.h"
#include "network/client.hpp"
#include "network/protocol.hpp"

#include <imgui.h>

#include <array>
#include <cstdint>
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
    if (ImGui::Button("Connect")) {
        std::string error;
        if (state.collab.client && state.collab.client->connect(state.collab.host, static_cast<std::uint16_t>(state.collab.port), error)) {
            state.collab.connected = true;
            state.status = "Connected to collaboration server.";
            state.collab.client->send({{"type", "file_list"}}, error);
        } else {
            state.status = "Connection failed: " + error;
            state.collab.connected = false;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Disconnect")) {
        if (state.collab.client) {
            state.collab.client->disconnect();
        }
        state.collab.connected = false;
        state.collab.users.clear();
        state.status = "Disconnected from collaboration server.";
    }

    ImGui::SameLine();
    char hostBuffer[128] = {};
    std::snprintf(hostBuffer, sizeof(hostBuffer), "%s", state.collab.host.c_str());
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::InputText("Host", hostBuffer, sizeof(hostBuffer))) {
        state.collab.host = hostBuffer;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Port", &state.collab.port);
    if (state.collab.port < 1) {
        state.collab.port = 1;
    }
    if (state.collab.port > 65535) {
        state.collab.port = 65535;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
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
            fs::path candidate;
            std::string error;
            if (file_tree::resolveProjectPath(state.projectRoot, state.openPathBuffer, candidate, error) &&
                editor::loadDocument(state, editorModule, candidate, error)) {
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

void renderMainWindow(AppState& state, editor::EditorModule& editorModule, file_tree::FileTreeModule& fileTreeModule) {
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
        state.fontDirty = true;
    }

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", state.status.c_str());
    if (!state.lastCommand.empty()) {
        ImGui::TextWrapped("latexmk: %s", state.lastCommand.c_str());
    }
    ImGui::TextUnformatted(state.compileInProgress ? "Compilation: running" : (state.lastCompileSuccess ? "Compilation: last run succeeded" : "Compilation: idle or last run failed"));

    ImGui::Separator();
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float treeWidth = contentSize.x * 0.28f;

    const float usersHeight = 170.0f;
    file_tree::renderPanel(state, editorModule, fileTreeModule, ImVec2(treeWidth, contentSize.y - usersHeight - 8.0f));
    ImGui::BeginChild("UsersPanel", ImVec2(treeWidth, usersHeight), true);
    ImGui::TextUnformatted("Users");
    ImGui::Separator();
    for (const auto& user : state.collab.users) {
        ImGui::BulletText("#%d %s", user.id, user.name.c_str());
    }
    if (state.collab.users.empty()) {
        ImGui::TextDisabled("No connected users");
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("EditorHostPanel", ImVec2(0.0f, contentSize.y), true);
    editor::renderEditor(state, editorModule, ImGui::GetContentRegionAvail());
    ImGui::EndChild();

    if (reloaded) {
        state.lastCompileSuccess = false;
    }

    ImGui::End();
}

}  // namespace ui
