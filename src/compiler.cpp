#include "compiler.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace compiler {
namespace {

int runCommand(const std::string& command) {
    return std::system(command.c_str());
}

}  // namespace

fs::path detectProjectRoot(const fs::path& argv0) {
    std::error_code ec;
    const fs::path executablePath = fs::weakly_canonical(fs::absolute(argv0), ec);
    if (!ec) {
        const fs::path candidate = executablePath.parent_path().parent_path();
        if (fs::exists(candidate / "assets") && fs::exists(candidate / "src")) {
            return candidate;
        }
    }

    const fs::path current = fs::current_path();
    if (fs::exists(current / "assets") && fs::exists(current / "src")) {
        return current;
    }

    return current;
}

std::string quotePath(const fs::path& path) {
    const std::string input = fs::absolute(path).string();
    std::string escaped;
    escaped.reserve(input.size() + 2);
    escaped.push_back('"');
    for (const char ch : input) {
        if (ch == '"') {
            escaped += '\\';
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string buildCommand(const std::string& executable, const std::vector<std::string>& args) {
    std::ostringstream command;
    command << executable;
    for (const std::string& arg : args) {
        command << ' ' << arg;
    }
    return command.str();
}

std::string buildOptionWithPath(const std::string& flag, const fs::path& path) {
    return flag + "=" + quotePath(path);
}

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool writeFile(const fs::path& path, const std::string& content, std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to save file: " + path.string();
        return false;
    }

    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        error = "Failed while writing file: " + path.string();
        return false;
    }

    return true;
}

CompileResult compilePdf(const AppState& state) {
    CompileResult result;

    std::error_code ec;
    const fs::path absoluteTexPath = fs::absolute(state.texPath);
    const fs::path absoluteBuildDir = fs::absolute(state.buildDir);
    const fs::path absoluteLogPath = fs::absolute(state.logPath);
    fs::create_directories(absoluteBuildDir, ec);
    fs::create_directories(absoluteLogPath.parent_path(), ec);

    const fs::path expectedPdf = absoluteBuildDir / (absoluteTexPath.stem().string() + ".pdf");

    std::vector<std::string> args = {
        "-pdf",
        "-interaction=nonstopmode",
        "-synctex=1",
        buildOptionWithPath("-outdir", absoluteBuildDir),
        quotePath(absoluteTexPath)
    };

    const std::string latexmkCommand = buildCommand("latexmk", args);
    result.command = latexmkCommand + " > " + quotePath(absoluteLogPath) + " 2>&1";
    result.exitCode = runCommand(result.command);
    result.logPath = absoluteLogPath;

    if (result.exitCode != 0) {
        result.message = "latexmk failed. See log: " + absoluteLogPath.string();
        return result;
    }

    if (!fs::exists(expectedPdf)) {
        result.message = "latexmk finished but the expected PDF was not found: " + expectedPdf.string();
        return result;
    }

    result.success = true;
    result.pdfPath = expectedPdf;
    result.message = "Compiled successfully: " + expectedPdf.string();
    return result;
}

bool openPdf(const AppState& state, std::string& status) {
    if (state.lastPdfPath.empty() || !fs::exists(state.lastPdfPath)) {
        status = "No generated PDF is available yet.";
        return false;
    }

    const std::string command = "start \"\" " + quotePath(state.lastPdfPath);
    if (runCommand(command) != 0) {
        status = "Failed to open the PDF with the default Windows viewer.";
        return false;
    }

    status = "Opened PDF: " + state.lastPdfPath.string();
    return true;
}

}  // namespace compiler
