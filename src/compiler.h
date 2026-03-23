#pragma once

#include "app_state.h"

#include <filesystem>
#include <string>
#include <vector>

namespace compiler {

std::filesystem::path detectProjectRoot(const std::filesystem::path& argv0);
std::string quotePath(const std::filesystem::path& path);
std::string buildCommand(const std::string& executable, const std::vector<std::string>& args);
std::string buildOptionWithPath(const std::string& flag, const std::filesystem::path& path);
std::string readFile(const std::filesystem::path& path);
bool writeFile(const std::filesystem::path& path, const std::string& content, std::string& error);
CompileResult compilePdf(const AppState& state);
bool openPdf(const AppState& state, std::string& status);

}  // namespace compiler
