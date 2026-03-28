#pragma once

#include <filesystem>
#include <string>

namespace compiler {

std::filesystem::path detectProjectRoot(const std::filesystem::path& argv0);
std::string readFile(const std::filesystem::path& path);
bool writeFile(const std::filesystem::path& path, const std::string& content, std::string& error);

}  // namespace compiler
