#include "app_state.h"
#include "compiler.h"
#include "editor.h"
#include "ui.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

bool saveEditor(AppState& state, const editor::EditorModule& editorModule) {
    std::string error;
    if (!editor::saveDocument(state, editorModule, error)) {
        state.status = error;
        state.lastCompileSuccess = false;
        return false;
    }
    return true;
}

void startCompile(AppState& state, const editor::EditorModule& editorModule) {
    if (state.compileInProgress) {
        return;
    }

    if (!saveEditor(state, editorModule)) {
        return;
    }

    state.compileInProgress = true;
    state.compileRequested = false;
    state.status = "Compiling with latexmk...";

    const CompileRequest request{state.texPath, state.buildDir, state.logPath};
    state.compileFuture = std::async(std::launch::async, [request]() {
        return compiler::compilePdf(request);
    });
}

void pollCompile(AppState& state) {
    if (!state.compileInProgress) {
        return;
    }

    if (state.compileFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        return;
    }

    const CompileResult result = state.compileFuture.get();
    state.compileInProgress = false;
    state.lastCommand = result.command;
    state.lastCompileSuccess = result.success;
    state.status = result.message;
    if (result.success) {
        state.lastPdfPath = result.pdfPath;
    }
}

void processAutoCompile(AppState& state, const editor::EditorModule& editorModule) {
    if (!state.autoMode || state.compileInProgress || !state.editorDirty) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - state.lastEditAt < std::chrono::milliseconds(700)) {
        return;
    }

    startCompile(state, editorModule);
}

void processSaveRequest(AppState& state, const editor::EditorModule& editorModule) {
    if (!state.saveRequested) {
        return;
    }
    state.saveRequested = false;
    saveEditor(state, editorModule);
}

void rebuildFontsIfNeeded(AppState& state, editor::EditorModule& editorModule, ImGuiIO& io) {
    if (!state.requestFontReload) {
        return;
    }

    editor::ensureFonts(editorModule, io);
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    state.requestFontReload = false;
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path argv0 = argc > 0 ? fs::path(argv[0]) : fs::current_path();
    const fs::path projectRoot = compiler::detectProjectRoot(argv0);
    fs::current_path(projectRoot);

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
    state.projectRoot = projectRoot;
    state.texPath = fs::absolute(projectRoot / "assets/sample.tex");
    state.buildDir = fs::absolute(projectRoot / "build-output");
    state.logPath = state.buildDir / "latexmk.log";
    state.openPathBuffer = state.texPath.string();
    state.status = "Ready. Open a .tex file, edit it, then save or compile.";

    editor::EditorModule editorModule;
    editor::initialize(state, editorModule, io);
    rebuildFontsIfNeeded(state, editorModule, io);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        pollCompile(state);
        processSaveRequest(state, editorModule);
        processAutoCompile(state, editorModule);
        rebuildFontsIfNeeded(state, editorModule, io);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui::renderMainWindow(state, editorModule);
        if (state.compileRequested) {
            startCompile(state, editorModule);
        }

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

    if (state.compileInProgress) {
        state.compileFuture.wait();
    }

    editor::shutdown(editorModule);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
