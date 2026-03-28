#include "ui.h"

#include "TextEditor.h"
#include "log.h"
#include "network/client.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 2 < bytes.size()) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[index]) << 16) |
            (static_cast<std::uint32_t>(bytes[index + 1]) << 8) |
            static_cast<std::uint32_t>(bytes[index + 2]);
        encoded.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        encoded.push_back(kAlphabet[chunk & 0x3F]);
        index += 3;
    }

    const std::size_t remaining = bytes.size() - index;
    if (remaining == 1) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(bytes[index]) << 16;
        encoded.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        encoded.push_back('=');
        encoded.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[index]) << 16) |
            (static_cast<std::uint32_t>(bytes[index + 1]) << 8);
        encoded.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        encoded.push_back('=');
    }

    return encoded;
}

std::string fileNameFromPath(const std::string& path) {
    const std::size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool isDirectChildOf(const std::string& parent, const std::string& candidate) {
    if (parent.empty()) {
        return candidate.find('/') == std::string::npos;
    }
    const std::string prefix = parent + "/";
    if (candidate.rfind(prefix, 0) != 0) {
        return false;
    }
    return candidate.find('/', prefix.size()) == std::string::npos;
}

void requestRemoteFileList(AppState& state) {
    if (!state.collab.connected || !state.collab.client) {
        return;
    }
    std::string error;
    state.collab.client->send({{"type", "file_list_request"}}, error);
}

void openRemoteFile(AppState& state, editor::EditorModule& editorModule, const std::string& path) {
    if (!state.collab.connected || !state.collab.client) {
        return;
    }

    std::string error;
    if (!state.collab.lastOpenedRemotePath.empty() && editorModule.textEditor) {
        const std::string currentText = editorModule.textEditor->GetText();
        const network::protocol::Json saveMessage{
            {"type", "file_save"},
            {"path", state.collab.lastOpenedRemotePath},
            {"content", currentText}
        };
        state.collab.client->send(
            saveMessage,
            error);
    }

    error.clear();
    if (!state.collab.client->send({{"type", "file_open"}, {"path", path}}, error)) {
        state.status = "Failed to open remote file: " + error;
        state.clientState = AppState::ClientState::ERROR;
    } else {
        state.collab.lastOpenedRemotePath = path;
    }
}

void renderDirectoryRecursive(
    AppState& state,
    editor::EditorModule& editorModule,
    const std::vector<std::string>& directories,
    const std::vector<AppState::CollaborationState::FileEntry>& files,
    const std::string& parent) {

    for (const auto& dir : directories) {
        if (!isDirectChildOf(parent, dir)) {
            continue;
        }

        const std::string label = "[DIR] " + fileNameFromPath(dir);
        if (ImGui::TreeNode(label.c_str())) {
            renderDirectoryRecursive(state, editorModule, directories, files, dir);

            for (const auto& file : files) {
                if (isDirectChildOf(dir, file.path)) {
                    const bool selected = (state.collab.lastOpenedRemotePath == file.path);
                    if (ImGui::Selectable(fileNameFromPath(file.path).c_str(), selected)) {
                        openRemoteFile(state, editorModule, file.path);
                    }
                }
            }

            ImGui::TreePop();
        }
    }

    for (const auto& file : files) {
        if (isDirectChildOf(parent, file.path)) {
            const bool selected = (state.collab.lastOpenedRemotePath == file.path);
            if (ImGui::Selectable(fileNameFromPath(file.path).c_str(), selected)) {
                openRemoteFile(state, editorModule, file.path);
            }
        }
    }
}

void renderToolbar(AppState& state) {
    if (ImGui::Button("Connect")) {
        std::string error;
        if (state.collab.client && state.collab.client->connect(state.collab.host, static_cast<std::uint16_t>(state.collab.port), error)) {
            state.collab.connected = true;
            state.clientState = AppState::ClientState::CONNECTED;
            state.status = "Connected to collaboration server.";
            requestRemoteFileList(state);
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
    if (ImGui::Button("Compile")) {
        state.compileRequested = true;
    }

    ImGui::SameLine();
    ImGui::Checkbox("AutoCompile", &state.autoCompile);

    ImGui::SameLine();
    ImGui::Checkbox("Logs", &state.showLogs);

    ImGui::SameLine();
    if (ImGui::Button("Users")) {
        state.showUsersPopup = true;
        ImGui::OpenPopup("UsersPopup");
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

    if (ImGui::BeginPopup("UsersPopup")) {
        if (state.collab.users.empty()) {
            ImGui::TextDisabled("No connected users");
        } else {
            for (const auto& user : state.collab.users) {
                ImGui::BulletText("#%d %s", user.id, user.name.c_str());
            }
        }
        ImGui::EndPopup();
    }
}

void renderFileTreePanel(AppState& state, editor::EditorModule& editorModule, const ImVec2& size) {
    ImGui::BeginChild("LeftFileTree", size, true);
    ImGui::TextUnformatted("Project Files");

    if (ImGui::Button("Refresh Files")) {
        requestRemoteFileList(state);
    }

    ImGui::Separator();

    static char uploadSource[512] = "";
    static char uploadDestination[512] = "images/upload.png";

    ImGui::InputText("Source", uploadSource, sizeof(uploadSource));
    ImGui::InputText("Target", uploadDestination, sizeof(uploadDestination));
    if (ImGui::Button("Upload Image") && state.collab.connected && state.collab.client) {
        std::ifstream in(uploadSource, std::ios::binary);
        std::vector<std::uint8_t> bytes;
        if (!in) {
            state.status = "Unable to open upload file.";
        } else {
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            const std::string encoded = base64Encode(bytes);
            std::string error;
            if (state.collab.client->send({{"type", "file_upload"}, {"path", uploadDestination}, {"data", encoded}}, error)) {
                state.status = "Image uploaded: " + std::string(uploadDestination);
                requestRemoteFileList(state);
            } else {
                state.status = "Upload failed: " + error;
            }
        }
    }

    ImGui::Separator();

    std::vector<std::string> directories;
    std::vector<AppState::CollaborationState::FileEntry> files;
    for (const auto& entry : state.collab.remoteFileEntries) {
        if (entry.path.empty()) {
            continue;
        }
        if (entry.isDirectory) {
            directories.push_back(entry.path);
        } else {
            files.push_back(entry);
            fs::path current(entry.path);
            fs::path parent = current.parent_path();
            while (!parent.empty()) {
                directories.push_back(parent.generic_string());
                parent = parent.parent_path();
            }
        }
    }

    std::sort(directories.begin(), directories.end());
    directories.erase(std::unique(directories.begin(), directories.end()), directories.end());
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path < b.path; });

    renderDirectoryRecursive(state, editorModule, directories, files, "");

    ImGui::EndChild();
}

void renderLogPanel(AppState& state, const ImVec2& size) {
    if (!state.showLogs) {
        return;
    }

    ImGui::BeginChild("BottomLogsPanel", size, true);
    ImGui::TextUnformatted("Compiler / Runtime Logs");
    ImGui::Separator();

    if (ImGui::BeginChild("LogsScrollRegion", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& line : state.logs) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
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

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", state.status.c_str());
    ImGui::TextWrapped("Client state: %s", toClientStateText(state.clientState));

    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float leftWidth = 300.0f;
    const float logsHeight = state.showLogs ? 180.0f : 0.0f;

    ImGui::BeginChild("CenterLayout", ImVec2(available.x, available.y - logsHeight), false);

    ImGui::BeginChild("LeftPanel", ImVec2(leftWidth, 0.0f), true);
    renderFileTreePanel(state, editorModule, ImGui::GetContentRegionAvail());
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("EditorPanel", ImVec2(0.0f, 0.0f), true);
    editor::renderEditor(state, editorModule, ImGui::GetContentRegionAvail());
    ImGui::EndChild();

    ImGui::EndChild();

    if (state.showLogs) {
        renderLogPanel(state, ImVec2(available.x, logsHeight));
    }

    ImGui::End();
}

}  // namespace ui
