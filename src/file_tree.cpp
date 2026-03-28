#include "file_tree.h"

#include "compiler.h"
#include "network/client.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace file_tree {
namespace {

bool startsWithPath(const fs::path& root, const fs::path& candidate) {
    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != root.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidate.end() || *rootIt != *candidateIt) {
            return false;
        }
    }
    return true;
}

fs::path normalizePotentiallyMissingPath(const fs::path& input) {
    std::error_code ec;
    if (fs::exists(input, ec)) {
        return fs::weakly_canonical(input, ec);
    }

    const fs::path parent = input.parent_path();
    const fs::path canonicalParent = parent.empty() ? fs::current_path() : fs::weakly_canonical(parent, ec);
    if (ec) {
        return input.lexically_normal();
    }
    return (canonicalParent / input.filename()).lexically_normal();
}

bool isTexFile(const fs::path& path) {
    return path.extension() == ".tex";
}

void copyStringToBuffer(const std::string& value, char* buffer, std::size_t size) {
    if (size == 0) {
        return;
    }
    std::snprintf(buffer, size, "%s", value.c_str());
}

bool createEmptyFile(const fs::path& path, std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to create file: " + path.string();
        return false;
    }
    return true;
}

void renderDirectoryNode(AppState& state, editor::EditorModule& editorModule, const fs::path& path) {
    std::error_code ec;
    std::vector<fs::directory_entry> directories;
    std::vector<fs::directory_entry> files;

    for (fs::directory_iterator it(path, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->is_directory(ec)) {
            directories.push_back(*it);
        } else if (it->is_regular_file(ec)) {
            files.push_back(*it);
        }
    }

    auto sorter = [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename().string() < b.path().filename().string();
    };
    std::sort(directories.begin(), directories.end(), sorter);
    std::sort(files.begin(), files.end(), sorter);

    for (const auto& entry : directories) {
        const std::string label = "[DIR] " + entry.path().filename().string();
        if (ImGui::TreeNode(label.c_str())) {
            renderDirectoryNode(state, editorModule, entry.path());
            ImGui::TreePop();
        }
    }

    for (const auto& entry : files) {
        const fs::path currentPath = fs::absolute(state.projectRoot / state.serverMainTexPath);
        const bool selected = fs::equivalent(entry.path(), currentPath, ec);
        if (ImGui::Selectable(entry.path().filename().string().c_str(), selected)) {
            std::string error;
            if (!editor::loadDocument(state, editorModule, entry.path(), error)) {
                state.status = error;
            }
        }
    }
}

void renderActionButtons(AppState& state, FileTreeModule& module) {
    if (ImGui::Button("Refresh")) {
        state.status = "Refreshed project file tree.";
    }
    ImGui::SameLine();
    if (ImGui::Button("New File")) {
        module.showNewFileDialog = true;
        ImGui::OpenPopup("CreateNewFile");
    }
    ImGui::SameLine();
    if (ImGui::Button("New Folder")) {
        module.showNewFolderDialog = true;
        ImGui::OpenPopup("CreateNewFolder");
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy File")) {
        module.showCopyFileDialog = true;
        ImGui::OpenPopup("CopyFileIntoProject");
    }
}

void renderCreationPopups(AppState& state, editor::EditorModule& editorModule, FileTreeModule& module) {
    if (module.showNewFileDialog) {
        ImGui::OpenPopup("CreateNewFile");
        module.showNewFileDialog = false;
    }
    if (module.showNewFolderDialog) {
        ImGui::OpenPopup("CreateNewFolder");
        module.showNewFolderDialog = false;
    }
    if (module.showCopyFileDialog) {
        ImGui::OpenPopup("CopyFileIntoProject");
        module.showCopyFileDialog = false;
    }

    if (ImGui::BeginPopupModal("CreateNewFile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        char buffer[512] = {};
        copyStringToBuffer(module.newFilePath, buffer, sizeof(buffer));
        if (ImGui::InputText("Relative path", buffer, sizeof(buffer))) {
            module.newFilePath = buffer;
        }
        if (ImGui::Button("Create")) {
            fs::path target;
            std::string error;
            if (resolveProjectPath(state.projectRoot, module.newFilePath, target, error)) {
                if (!isTexFile(target)) {
                    error = "New files must use the .tex extension.";
                } else {
                    fs::create_directories(target.parent_path());
                    if (createEmptyFile(target, error)) {
                        if (state.collab.connected && state.collab.client) {
                            std::string sendError;
                            state.collab.client->send({{"type", "file_create"}, {"path", target.lexically_relative(state.projectRoot).generic_string()}}, sendError);
                        }
                        editor::loadDocument(state, editorModule, target, error);
                        module.newFilePath.clear();
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            if (!error.empty()) {
                state.status = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("CreateNewFolder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        char buffer[512] = {};
        copyStringToBuffer(module.newFolderPath, buffer, sizeof(buffer));
        if (ImGui::InputText("Folder path", buffer, sizeof(buffer))) {
            module.newFolderPath = buffer;
        }
        if (ImGui::Button("Create")) {
            fs::path target;
            std::string error;
            if (resolveProjectPath(state.projectRoot, module.newFolderPath, target, error)) {
                std::error_code ec;
                fs::create_directories(target, ec);
                if (ec) {
                    error = "Unable to create folder: " + target.string();
                } else {
                    state.status = "Created folder: " + target.string();
                    module.newFolderPath.clear();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!error.empty()) {
                state.status = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("CopyFileIntoProject", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        char sourceBuffer[512] = {};
        char destinationBuffer[512] = {};
        copyStringToBuffer(module.copySourcePath, sourceBuffer, sizeof(sourceBuffer));
        copyStringToBuffer(module.copyDestinationPath, destinationBuffer, sizeof(destinationBuffer));
        if (ImGui::InputText("Source file", sourceBuffer, sizeof(sourceBuffer))) {
            module.copySourcePath = sourceBuffer;
        }
        if (ImGui::InputText("Destination relative path", destinationBuffer, sizeof(destinationBuffer))) {
            module.copyDestinationPath = destinationBuffer;
        }
        if (ImGui::Button("Copy")) {
            std::string error;
            fs::path destination;
            if (resolveProjectPath(state.projectRoot, module.copyDestinationPath, destination, error)) {
                std::error_code ec;
                const fs::path source = fs::absolute(module.copySourcePath);
                if (!fs::exists(source, ec) || !fs::is_regular_file(source, ec)) {
                    error = "Source file does not exist: " + source.string();
                } else {
                    fs::create_directories(destination.parent_path(), ec);
                    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
                    if (ec) {
                        error = "Unable to copy file into project: " + destination.string();
                    } else {
                        if (state.collab.connected && state.collab.client) {
                            std::string fileContent = compiler::readFile(destination);
                            std::string sendError;
                            state.collab.client->send({{"type", "file_upload"}, {"path", destination.lexically_relative(state.projectRoot).generic_string()}, {"content", fileContent}}, sendError);
                        }
                        state.status = "Copied file into project: " + destination.string();
                        module.copySourcePath.clear();
                        module.copyDestinationPath.clear();
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            if (!error.empty()) {
                state.status = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace

void initialize(FileTreeModule& module, const fs::path& projectRoot) {
    module.newFilePath = "new-file.tex";
    module.newFolderPath = "chapter-assets";
    module.copyDestinationPath = (projectRoot / "images" / "copied-file.tex").lexically_relative(projectRoot).string();
}

bool resolveProjectPath(const fs::path& projectRoot, const fs::path& candidate, fs::path& resolved, std::string& error) {
    if (candidate.empty()) {
        error = "Path cannot be empty.";
        return false;
    }

    if (candidate.generic_string().find("..") != std::string::npos) {
        error = "Path must not contain '..'.";
        return false;
    }

    const fs::path root = fs::weakly_canonical(projectRoot);
    fs::path combined = candidate;
    if (combined.is_relative()) {
        combined = root / combined;
    }

    resolved = normalizePotentiallyMissingPath(combined);
    if (!startsWithPath(root, resolved)) {
        error = "File operations must stay inside the project root.";
        return false;
    }

    return true;
}

void renderPanel(AppState& state, editor::EditorModule& editorModule, FileTreeModule& module, const ImVec2& size) {
    ImGui::BeginChild("FileTreePanel", size, true);
    ImGui::TextUnformatted("Project Files");
    ImGui::TextWrapped("Root: %s", state.projectRoot.string().c_str());
    renderActionButtons(state, module);
    renderCreationPopups(state, editorModule, module);
    ImGui::Separator();

    const std::string rootLabel = state.projectRoot.filename().empty()
        ? state.projectRoot.string()
        : state.projectRoot.filename().string();
    if (ImGui::TreeNodeEx(rootLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        renderDirectoryNode(state, editorModule, state.projectRoot);
        ImGui::TreePop();
    }

    ImGui::EndChild();
}

}  // namespace file_tree
