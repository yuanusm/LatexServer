#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <string>

namespace fs = std::filesystem;

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
};
