#include "core/file_sync.hpp"

#include "compiler.h"
#include "editor.h"
#include "file_tree.h"

#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace core::file_sync {

void applyServerMessage(AppState& state, const network::protocol::Json& message) {
    const std::string type = message.value("type", "");
    if (type == "file_save") {
        fs::path resolved;
        std::string error;
        if (file_tree::resolveProjectPath(state.projectRoot, message.value("path", ""), resolved, error)) {
            std::string writeError;
            compiler::writeFile(resolved, message.value("content", ""), writeError);
        }
        return;
    }

    if (type == "file_create") {
        fs::path resolved;
        std::string error;
        if (file_tree::resolveProjectPath(state.projectRoot, message.value("path", ""), resolved, error)) {
            std::string writeError;
            compiler::writeFile(resolved, "", writeError);
        }
        return;
    }

    if (type == "file_delete") {
        fs::path resolved;
        std::string error;
        if (file_tree::resolveProjectPath(state.projectRoot, message.value("path", ""), resolved, error)) {
            std::error_code ec;
            fs::remove_all(resolved, ec);
        }
        return;
    }

    if (type == "file_rename") {
        fs::path oldResolved;
        fs::path newResolved;
        std::string error;
        if (file_tree::resolveProjectPath(state.projectRoot, message.value("old", ""), oldResolved, error) &&
            file_tree::resolveProjectPath(state.projectRoot, message.value("new", ""), newResolved, error)) {
            std::error_code ec;
            fs::create_directories(newResolved.parent_path(), ec);
            fs::rename(oldResolved, newResolved, ec);
        }
        return;
    }

    if (type == "file_upload") {
        fs::path resolved;
        std::string error;
        if (file_tree::resolveProjectPath(state.projectRoot, message.value("path", ""), resolved, error)) {
            std::string writeError;
            compiler::writeFile(resolved, message.value("content", ""), writeError);
        }
        return;
    }

    if (type == "file_open") {
        const std::string path = message.value("path", "");
        state.collab.lastOpenedRemotePath = path;
    }

    if (type == "file_list") {
        state.collab.remoteFiles.clear();
        state.collab.remoteFileEntries.clear();
        std::set<std::string> seen;
        if (message.contains("files") && message["files"].is_array()) {
            for (const auto& item : message["files"]) {
                const std::string path = item.get<std::string>();
                if (seen.insert("f:" + path).second) {
                    state.collab.remoteFiles.push_back(path);
                }
            }
        }
        if (message.contains("entries") && message["entries"].is_array()) {
            for (const auto& item : message["entries"]) {
                AppState::CollaborationState::FileEntry entry;
                entry.path = item.value("path", "");
                entry.isDirectory = item.value("is_directory", false);
                entry.size = item.value("size", static_cast<std::size_t>(0));
                if (seen.insert("e:" + entry.path).second) {
                    state.collab.remoteFileEntries.push_back(std::move(entry));
                }
            }
        }
    }
}

}  // namespace core::file_sync
