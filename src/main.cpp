#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

struct AppState {
    fs::path texPath = "assets/sample.tex";
    fs::path buildDir = "build-output";
    fs::path lastPdfPath;
    std::string status = "Presiona B para compilar el PDF. Presiona O para abrir el ultimo PDF generado.";
    std::chrono::steady_clock::time_point statusUpdatedAt = std::chrono::steady_clock::now();
};

std::string shellEscape(const fs::path& path) {
    std::string input = path.string();
    std::string output;
    output.reserve(input.size() + 2);
    output.push_back('\'');
    for (char ch : input) {
        if (ch == '\'') {
            output += "'\\''";
        } else {
            output.push_back(ch);
        }
    }
    output.push_back('\'');
    return output;
}

std::string detectOpenCommand(const fs::path& pdfPath) {
#if defined(_WIN32)
    return "start \"\" " + shellEscape(pdfPath);
#elif defined(__APPLE__)
    return "open " + shellEscape(pdfPath);
#else
    return "xdg-open " + shellEscape(pdfPath) + " >/dev/null 2>&1 &";
#endif
}

void updateWindowTitle(GLFWwindow* window, const std::string& status) {
    std::string title = "LatexServer - sin render interno | " + status;
    glfwSetWindowTitle(window, title.c_str());
}

bool runCommand(const std::string& command) {
    return std::system(command.c_str()) == 0;
}

bool compilePdf(AppState& state) {
    fs::create_directories(state.buildDir);

    const fs::path outputPdf = state.buildDir / (state.texPath.stem().string() + ".pdf");
    std::ostringstream command;
    command << "pdflatex"
            << " -interaction=nonstopmode"
            << " -halt-on-error"
            << " -output-directory " << shellEscape(state.buildDir)
            << " " << shellEscape(state.texPath);

    if (!runCommand(command.str())) {
        state.status = "Fallo la compilacion. Revisa la terminal para ver el error de pdflatex.";
        return false;
    }

    if (!fs::exists(outputPdf)) {
        state.status = "pdflatex termino sin generar el PDF esperado.";
        return false;
    }

    state.lastPdfPath = outputPdf;
    state.status = "PDF generado en " + outputPdf.string() + ". Presiona O para abrirlo externamente.";
    return true;
}

bool openPdf(const AppState& state, std::string& status) {
    if (state.lastPdfPath.empty() || !fs::exists(state.lastPdfPath)) {
        status = "Todavia no hay un PDF generado para abrir.";
        return false;
    }

    if (!runCommand(detectOpenCommand(state.lastPdfPath))) {
        status = "No se pudo abrir el PDF con la aplicacion del sistema.";
        return false;
    }

    status = "PDF abierto externamente: " + state.lastPdfPath.string();
    return true;
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) {
        return;
    }

    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) {
        return;
    }

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_B:
            compilePdf(*state);
            break;
        case GLFW_KEY_O:
            openPdf(*state, state->status);
            break;
        default:
            break;
    }

    state->statusUpdatedAt = std::chrono::steady_clock::now();
    updateWindowTitle(window, state->status);
}

int main() {
    if (!glfwInit()) {
        std::cerr << "No se pudo inicializar GLFW.\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(960, 540, "LatexServer - sin render interno", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "No se pudo crear la ventana GLFW.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    AppState state;
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, keyCallback);
    updateWindowTitle(window, state.status);

    while (!glfwWindowShouldClose(window)) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
