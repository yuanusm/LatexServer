#include "editor.h"
#include "file_watcher.h"
#include "pdf_renderer.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

namespace {
struct CompileState {
    bool autoCompile = true;
    bool busy = false;
    int lastExitCode = 0;
    std::string lastMessage = "Ready.";
};

bool ensureGeneratedDirectories() {
    std::error_code ec;
    std::filesystem::create_directories("build", ec);
    std::filesystem::create_directories("tikz_cache", ec);
    return !ec;
}

bool runLatexmk(CompileState& state) {
    ensureGeneratedDirectories();
    state.busy = true;
    state.lastMessage = "Compiling with latexmk...";
#if defined(_WIN32)
    const std::string command = "latexmk -pdf -interaction=nonstopmode -outdir=build main.tex";
#else
    const std::string command = "latexmk -pdf -interaction=nonstopmode -outdir=build main.tex >/tmp/local_latex_editor.log 2>&1";
#endif
    state.lastExitCode = std::system(command.c_str());
    state.busy = false;
    state.lastMessage = state.lastExitCode == 0 ? "Compile succeeded." : "Compile failed. Inspect build artifacts/logs.";
    return state.lastExitCode == 0;
}

void glfwErrorCallback(int, const char*) {}
}

int main() {
    ensureGeneratedDirectories();

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1600, 900, "Local LaTeX Editor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    TextEditorBuffer editor("main.tex");
    editor.load();

    FileWatcher texWatcher("main.tex");
    FileWatcher pdfWatcher("build/main.pdf");
    PdfRenderer pdfRenderer;
    CompileState compileState;

    runLatexmk(compileState);
    pdfRenderer.reloadIfChanged("build/main.pdf");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (compileState.autoCompile && editor.saveIfDirty()) {
            if (texWatcher.hasChanged() && !compileState.busy) {
                runLatexmk(compileState);
            }
        }
        if (pdfWatcher.hasChanged()) {
            pdfRenderer.reloadIfChanged("build/main.pdf");
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        ImGui::Begin("Editor");
        const float bottomBarHeight = 64.0f;
        const ImVec2 editorSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - bottomBarHeight);
        editor.draw("##latex_source", editorSize);
        ImGui::Separator();
        if (ImGui::Button("Compile")) {
            editor.saveIfDirty();
            runLatexmk(compileState);
            pdfWatcher.reset();
            pdfRenderer.reloadIfChanged("build/main.pdf");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-compile", &compileState.autoCompile);
        ImGui::SameLine();
        ImGui::TextUnformatted(compileState.lastMessage.c_str());
        ImGui::End();

        ImGui::Begin("PDF Preview");
        if (pdfRenderer.hasTexture()) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float aspect = static_cast<float>(pdfRenderer.width()) / static_cast<float>(pdfRenderer.height());
            ImVec2 size = avail;
            if (size.x / size.y > aspect) {
                size.x = size.y * aspect;
            } else {
                size.y = size.x / aspect;
            }
            ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(pdfRenderer.texture())), size, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::TextWrapped("No PDF preview available yet. %s", pdfRenderer.lastError().c_str());
        }
        ImGui::End();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
