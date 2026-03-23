#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

    std::vector<char> editorBuffer;
    bool editorDirty = false;
    bool autoMode = false;
    bool compileRequested = false;
    bool openPdfRequested = false;
    bool compileInProgress = false;

    std::chrono::steady_clock::time_point lastEditAt = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastAutoCompileAt = std::chrono::steady_clock::now();

    std::future<CompileResult> compileFuture;

    std::string status = "Ready.";
    std::string lastCommand;
    bool lastCompileSuccess = false;
};
