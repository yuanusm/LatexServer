#include "app_state.h"
#include "editor.h"
#include "ui.h"

#include "TextEditor.h"
#include "core/editor_sync.hpp"
#include "core/file_sync.hpp"
#include "network/client.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int decodeBase64Char(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

std::vector<std::uint8_t> base64Decode(const std::string& input) {
    std::vector<std::uint8_t> output;
    output.reserve((input.size() / 4) * 3);

    std::array<int, 4> block{};
    std::size_t index = 0;

    for (unsigned char ch : input) {
        if (ch == '=') {
            block[index++] = -2;
        } else {
            const int value = decodeBase64Char(ch);
            if (value < 0) {
                continue;
            }
            block[index++] = value;
        }

        if (index == 4) {
            const int b0 = block[0];
            const int b1 = block[1];
            const int b2 = block[2];
            const int b3 = block[3];

            if (b0 >= 0 && b1 >= 0) {
                output.push_back(static_cast<std::uint8_t>((b0 << 2) | (b1 >> 4)));
            }
            if (b2 >= 0 && b0 >= 0 && b1 >= 0) {
                output.push_back(static_cast<std::uint8_t>(((b1 & 0x0F) << 4) | (b2 >> 2)));
            }
            if (b3 >= 0 && b2 >= 0) {
                output.push_back(static_cast<std::uint8_t>(((b2 & 0x03) << 6) | b3));
            }
            index = 0;
        }
    }

    return output;
}

void requestServerCompile(AppState& state) {
    if (!state.compileRequested || !state.collab.connected || !state.collab.client) {
        return;
    }

    state.compileRequested = false;
    state.compileInProgress = true;
    state.lastCompileSuccess = false;
    state.status = "Compile request sent to server...";
    std::string error;
    if (!state.collab.client->send({{"type", "compile_request"}}, error)) {
        state.compileInProgress = false;
        state.status = "Compile request failed: " + error;
    }
}

void pollCollaboration(AppState& state, editor::EditorModule& editorModule) {
    if (!state.collab.client || !state.collab.connected) {
        return;
    }

    for (const auto& message : state.collab.client->pollIncoming()) {
        const std::string type = message.value("type", "");
        if (type == "sync") {
            if (editorModule.textEditor) {
                const std::string content = message.value("content", "");
                if (content != editorModule.textEditor->GetText()) {
                    state.collab.suppressOutgoingSync = true;
                    editorModule.textEditor->SetText(content);
                    state.collab.suppressOutgoingSync = false;
                }
                state.collab.lastSyncedContent = content;
                state.editorDirty = false;
                state.status = "Document synchronized from server main.tex.";
            }
            continue;
        }

        if (type == "compile_result") {
            state.compileInProgress = false;
            const bool success = message.value("success", false);
            state.lastCompileSuccess = success;
            if (success) {
                state.incomingPdfBase64.clear();
                state.status = "Server compilation succeeded. Receiving PDF...";
            } else {
                state.lastCompileLog = message.value("log", "Server compilation failed.");
                state.status = state.lastCompileLog;
            }
            continue;
        }

        if (type == "pdf_chunk") {
            const std::string data = message.value("data", "");
            state.incomingPdfBase64 += data;
            const bool last = message.value("last", false);
            if (last) {
                const std::vector<std::uint8_t> bytes = base64Decode(state.incomingPdfBase64);
                std::error_code ec;
                fs::create_directories(state.receivedPdfPath.parent_path(), ec);
                std::ofstream out(state.receivedPdfPath, std::ios::binary | std::ios::trunc);
                if (!out) {
                    state.status = "Failed to store PDF locally: " + state.receivedPdfPath.string();
                } else {
                    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    if (out) {
                        state.status = "PDF received from server: " + state.receivedPdfPath.string();
                    } else {
                        state.status = "Failed while writing PDF to: " + state.receivedPdfPath.string();
                    }
                }
                state.incomingPdfBase64.clear();
            }
            continue;
        }

        if (type == "users") {
            state.collab.users.clear();
            if (message.contains("list") && message["list"].is_array()) {
                for (const auto& item : message["list"]) {
                    AppState::UserInfo user;
                    user.id = item.value("id", 0);
                    user.name = item.value("name", "");
                    state.collab.users.push_back(std::move(user));
                }
            }
            continue;
        }

        core::file_sync::applyServerMessage(state, message);
    }

    if (!state.collab.client->isConnected()) {
        state.collab.connected = false;
        state.status = "Disconnected from collaboration server.";
    }
}

void rebuildFontsIfNeeded(AppState& state, editor::EditorModule& editorModule, ImGuiIO& io) {
    if (!state.fontDirty) {
        return;
    }

    editor::ensureFonts(editorModule, io);
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    state.fontDirty = false;
}

}  // namespace

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW.\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1400, 900, "LatexServer", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create the GLFW window.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    AppState state;
    state.collab.client = std::make_unique<network::CollabClient>();
    state.projectRoot = fs::current_path();
    state.serverMainTexPath = "main.tex";
    state.receivedPdfPath = state.projectRoot / "client_temp" / "main.pdf";
    state.status = "Ready. Connect to server, edit main.tex, and request compile.";

    editor::EditorModule editorModule;
    editor::initialize(state, editorModule, io);
    rebuildFontsIfNeeded(state, editorModule, io);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        core::editor_sync::processOutgoingSync(state, editorModule);
        requestServerCompile(state);
        pollCollaboration(state, editorModule);
        rebuildFontsIfNeeded(state, editorModule, io);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui::renderMainWindow(state, editorModule);

        ImGui::Render();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    editor::shutdown(editorModule);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
