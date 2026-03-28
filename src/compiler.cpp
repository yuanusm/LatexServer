#include "compiler.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace compiler {
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

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool writeFile(const fs::path& path, const std::string& content, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

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

}  // namespace compiler
