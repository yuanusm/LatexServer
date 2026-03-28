#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace network {
class CollabClient;
}

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
    fs::path serverMainTexPath;
    fs::path receivedPdfPath;

    bool editorDirty = false;
    bool compileRequested = false;
    bool compileInProgress = false;
    bool showPreferences = false;
    bool fontDirty = false;

    std::chrono::steady_clock::time_point lastEditAt = std::chrono::steady_clock::now();

    std::string status = "Ready.";
    std::string lastCompileLog;
    bool lastCompileSuccess = false;

    std::string incomingPdfBase64;

    CollaborationState collab;
};
