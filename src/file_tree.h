#pragma once

#include "app_state.h"
#include "editor.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace file_tree {

struct FileTreeModule {
    bool showNewFileDialog = false;
    bool showNewFolderDialog = false;
    bool showCopyFileDialog = false;
    std::string newFilePath;
    std::string newFolderPath;
    std::string copySourcePath;
    std::string copyDestinationPath;
};

void initialize(FileTreeModule& module, const std::filesystem::path& projectRoot);
bool resolveProjectPath(const std::filesystem::path& projectRoot, const std::filesystem::path& candidate, std::filesystem::path& resolved, std::string& error);
void renderPanel(AppState& state, editor::EditorModule& editorModule, FileTreeModule& module, const ImVec2& size);

}  // namespace file_tree
