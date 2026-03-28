#include "ui.h"

#include "network/client.hpp"

#include <imgui.h>

#include <cstdint>
#include <cstdio>

namespace ui {
namespace {

const char* toClientStateText(AppState::ClientState state) {
    switch (state) {
        case AppState::ClientState::IDLE:
            return "IDLE";
        case AppState::ClientState::CONNECTED:
            return "CONNECTED";
        case AppState::ClientState::EDITING:
            return "EDITING";
        case AppState::ClientState::COMPILING:
            return "COMPILING";
        case AppState::ClientState::RECEIVING_PDF:
            return "RECEIVING_PDF";
        case AppState::ClientState::ERROR:
            return "ERROR";
    }
    return "IDLE";
}

void renderToolbar(AppState& state) {
    if (ImGui::Button("Compile")) {
        state.compileRequested = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Preferences")) {
        state.showPreferences = !state.showPreferences;
    }

    ImGui::SameLine();
    if (ImGui::Button("Connect")) {
        std::string error;
        if (state.collab.client && state.collab.client->connect(state.collab.host, static_cast<std::uint16_t>(state.collab.port), error)) {
            state.collab.connected = true;
            state.clientState = AppState::ClientState::CONNECTED;
            state.status = "Connected to collaboration server.";
        } else {
            state.status = "Connection failed: " + error;
            state.collab.connected = false;
            state.clientState = AppState::ClientState::ERROR;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Disconnect")) {
        if (state.collab.client) {
            state.collab.client->disconnect();
        }
        state.collab.connected = false;
        state.collab.users.clear();
        state.clientState = AppState::ClientState::IDLE;
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
    const bool fontChanged = renderPreferences(state, editorModule);
    if (fontChanged) {
        state.fontDirty = true;
    }

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", state.status.c_str());
    ImGui::TextWrapped("Client state: %s", toClientStateText(state.clientState));
    ImGui::TextUnformatted(state.compileInProgress ? "Compilation: waiting for server" : (state.lastCompileSuccess ? "Compilation: last run succeeded" : "Compilation: idle or last run failed"));
    ImGui::TextWrapped("Server main.tex: %s", state.serverMainTexPath.string().c_str());
    ImGui::TextWrapped("PDF output: %s", state.receivedPdfPath.string().c_str());

    ImGui::Separator();
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float usersWidth = contentSize.x * 0.28f;

    ImGui::BeginChild("UsersPanel", ImVec2(usersWidth, contentSize.y), true);
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

    ImGui::End();
}

}  // namespace ui
