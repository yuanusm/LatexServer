#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace network {
class CollabClient;
}

struct CompileRequest {
    fs::path texPath;
    fs::path buildDir;
    fs::path logPath;
};

struct CompileResult {
    bool success = false;
    fs::path pdfPath;
    fs::path logPath;
    int exitCode = -1;
    std::string command;
    std::string message;
};

struct AppState {
    struct UserInfo {
        int id = 0;
        std::string name;
    };

    struct CollaborationState {
        std::string host = "127.0.0.1";
        int port = 9090;
        bool connected = false;
        bool suppressOutgoingSync = false;
        int syncDebounceMs = 400;
        std::unique_ptr<network::CollabClient> client;
        std::vector<UserInfo> users;
        std::vector<std::string> remoteFiles;
        std::string lastSyncedContent;
        std::string lastOpenedRemotePath;
        std::chrono::steady_clock::time_point lastSyncSentAt = std::chrono::steady_clock::now();
    };

    fs::path projectRoot;
    fs::path texPath;
    fs::path buildDir;
    fs::path logPath;
    fs::path lastPdfPath;

    bool editorDirty = false;
    bool autoMode = false;
    bool saveRequested = false;
    bool compileRequested = false;
    bool compileInProgress = false;
    bool showPreferences = false;
    bool showOpenDialog = false;
    bool fontDirty = false;

    std::chrono::steady_clock::time_point lastEditAt = std::chrono::steady_clock::now();
    std::future<CompileResult> compileFuture;

    std::string status = "Ready.";
    std::string lastCommand;
    std::string openPathBuffer;
    bool lastCompileSuccess = false;

    CollaborationState collab;
};
