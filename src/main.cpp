#include "app_state.h"
#include "compiler.h"
#include "ui.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string editorText(const AppState& state) {
    return std::string(state.editorBuffer.data());
}

void loadEditor(AppState& state) {
    const std::string content = compiler::readFile(state.texPath);
    state.editorBuffer.assign(std::max<std::size_t>(content.size() + 1, 64 * 1024), '\0');
    std::memcpy(state.editorBuffer.data(), content.c_str(), content.size());
    state.editorDirty = false;
}

bool saveEditor(const AppState& state, std::string& error) {
    return compiler::writeFile(state.texPath, editorText(state), error);
}

void startCompile(AppState& state) {
    if (state.compileInProgress) {
        return;
    }

    std::string error;
    if (!saveEditor(state, error)) {
        state.status = error;
        state.lastCompileSuccess = false;
        return;
    }

    state.compileInProgress = true;
    state.compileRequested = false;
    state.status = "Compiling with latexmk...";
    state.lastAutoCompileAt = std::chrono::steady_clock::now();
    state.compileFuture = std::async(std::launch::async, [state]() {
        return compiler::compilePdf(state);
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
        state.editorDirty = false;
    }
}

void handleOpenPdf(AppState& state) {
    if (!state.openPdfRequested) {
        return;
    }

    state.openPdfRequested = false;
    compiler::openPdf(state, state.status);
}

void processAutoCompile(AppState& state) {
    if (!state.autoMode || state.compileInProgress || !state.editorDirty) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - state.lastEditAt < std::chrono::milliseconds(700)) {
        return;
    }

    startCompile(state);
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

    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) {
        std::cerr << "Failed to initialize OpenGL loader.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    AppState state;
    state.projectRoot = projectRoot;
    state.texPath = fs::absolute(projectRoot / "assets/sample.tex");
    state.buildDir = fs::absolute(projectRoot / "build-output");
    state.logPath = state.buildDir / "latexmk.log";
    state.status = "Ready. Edit the LaTeX source and click Compile.";

    try {
        loadEditor(state);
    } catch (const std::exception& ex) {
        state.editorBuffer.assign(64 * 1024, '\0');
        state.status = std::string("Failed to load LaTeX source: ") + ex.what();
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        pollCompile(state);
        processAutoCompile(state);
        handleOpenPdf(state);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui::renderMainWindow(state);
        if (state.compileRequested) {
            startCompile(state);
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
