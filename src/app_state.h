#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef ERROR
#undef ERROR
#endif

namespace fs = std::filesystem;

namespace network {
class CollabClient;
}

struct AppState {
    enum class ClientState {
        IDLE,
        CONNECTED,
        EDITING,
        COMPILING,
        RECEIVING_PDF,
        ERROR
    };

    struct UserInfo {
        int id = 0;
        std::string name;
    };

    struct CollaborationState {
        struct FileEntry {
            std::string path;
            bool isDirectory = false;
            std::size_t size = 0;
        };

        std::string host = "127.0.0.1";
        int port = 9090;
        bool connected = false;
        bool suppressOutgoingSync = false;
        int syncDebounceMs = 400;
        std::unique_ptr<network::CollabClient> client;
        std::vector<UserInfo> users;
        std::vector<std::string> remoteFiles;
        std::vector<FileEntry> remoteFileEntries;
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
    bool showLogs = true;
    bool showUsersPopup = false;
    bool autoCompile = false;
    bool fontDirty = false;

    std::chrono::steady_clock::time_point lastEditAt = std::chrono::steady_clock::now();

    std::string status = "Ready.";
    std::string lastCompileLog;
    bool lastCompileSuccess = false;
    std::vector<std::string> logs;

    std::string incomingPdfBase64;
    std::size_t expectedPdfChunks = 0;
    std::size_t receivedPdfChunks = 0;
    bool receivedPdfLastFlag = false;

    ClientState clientState = ClientState::IDLE;

    CollaborationState collab;
};
