#include "network/server.hpp"

#include "compiler.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace network {
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

std::vector<std::string> listFilesRecursive(const fs::path& root) {
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        files.push_back(it->path().lexically_relative(root).generic_string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 2 < bytes.size()) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[index]) << 16) |
            (static_cast<std::uint32_t>(bytes[index + 1]) << 8) |
            static_cast<std::uint32_t>(bytes[index + 2]);
        encoded.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        encoded.push_back(kAlphabet[chunk & 0x3F]);
        index += 3;
    }

    const std::size_t remaining = bytes.size() - index;
    if (remaining == 1) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(bytes[index]) << 16;
        encoded.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        encoded.push_back('=');
        encoded.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(bytes[index]) << 16) |
            (static_cast<std::uint32_t>(bytes[index + 1]) << 8);
        encoded.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        encoded.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        encoded.push_back('=');
    }

    return encoded;
}

#ifdef _WIN32
network::CollabServer::CompileOutcome runLatexmkWindows(const fs::path& projectRoot) {
    network::CollabServer::CompileOutcome outcome;
    outcome.pdfPath = projectRoot / "main.pdf";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        outcome.log = "Failed to create output pipe for latexmk.";
        return outcome;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION processInfo{};
    std::string command = "latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex";
    std::vector<char> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back('\0');

    const std::string workingDirectory = projectRoot.string();

    const BOOL created = CreateProcessA(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.c_str(),
        &startupInfo,
        &processInfo);

    CloseHandle(writePipe);

    if (!created) {
        outcome.log = "CreateProcessA failed for latexmk.";
        CloseHandle(readPipe);
        return outcome;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    outcome.exitCode = static_cast<int>(exitCode);

    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        outcome.log.append(buffer, buffer + bytesRead);
    }

    CloseHandle(readPipe);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    outcome.success = (outcome.exitCode == 0) && fs::exists(outcome.pdfPath);
    if (outcome.log.empty()) {
        outcome.log = outcome.success ? "latexmk finished successfully." : "latexmk failed.";
    }

    return outcome;
}
#else
network::CollabServer::CompileOutcome runLatexmkWindows(const fs::path& projectRoot) {
    network::CollabServer::CompileOutcome outcome;
    outcome.pdfPath = projectRoot / "main.pdf";
    outcome.log = "Windows-only CreateProcessA compilation path unavailable on this platform.";
    return outcome;
}
#endif

}  // namespace

CollabServer::CollabServer(fs::path projectRoot, std::uint16_t port)
    : projectRoot_(fs::weakly_canonical(std::move(projectRoot))),
      mainTexPath_(projectRoot_ / "main.tex"),
      mainPdfPath_(projectRoot_ / "main.pdf"),
      acceptor_(ioContext_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    ensureMainDocument();
    compileWorker_ = std::thread([this]() { compileWorkerLoop(); });
}

CollabServer::~CollabServer() {
    {
        std::lock_guard<std::mutex> lock(compileMutex_);
        stopCompileWorker_ = true;
    }
    compileCv_.notify_all();
    if (compileWorker_.joinable()) {
        compileWorker_.join();
    }
}

void CollabServer::ensureMainDocument() {
    std::error_code ec;
    fs::create_directories(projectRoot_, ec);

    if (!fs::exists(mainTexPath_)) {
        std::string error;
        compiler::writeFile(mainTexPath_, "", error);
    }

    try {
        mainDocument_ = compiler::readFile(mainTexPath_);
    } catch (...) {
        mainDocument_.clear();
    }
}

void CollabServer::run() {
    std::cout << "Collab server listening on port " << acceptor_.local_endpoint().port() << "\n";
    for (;;) {
        auto socket = std::make_shared<asio::ip::tcp::socket>(ioContext_);
        acceptor_.accept(*socket);

        auto session = std::make_shared<ClientSession>();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session->id = nextUserId_++;
            session->name = "user" + std::to_string(session->id);
            session->socket = socket;
            clients_[session->id] = session;
        }

        sendTo(session, protocol::Json{{"type", "sync"}, {"content", mainDocument_}});
        sendTo(session, makeFileListMessage());
        broadcast(makeUsersMessage());

        std::thread([this, session]() { handleClient(session); }).detach();
    }
}

bool CollabServer::resolveInsideRoot(const fs::path& candidate, fs::path& resolved, std::string& error) const {
    const std::string raw = candidate.generic_string();
    if (raw.find("..") != std::string::npos) {
        error = "Path must not contain '..'.";
        return false;
    }

    fs::path combined = projectRoot_ / candidate;
    std::error_code ec;
    resolved = fs::weakly_canonical(combined, ec);
    if (ec) {
        const fs::path parent = fs::weakly_canonical(combined.parent_path(), ec);
        if (ec) {
            error = "Invalid path.";
            return false;
        }
        resolved = (parent / combined.filename()).lexically_normal();
    }

    if (!startsWithPath(projectRoot_, resolved)) {
        error = "Resolved path escapes project root.";
        return false;
    }

    return true;
}

protocol::Json CollabServer::makeUsersMessage() const {
    protocol::Json list = protocol::Json::array();
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, session] : clients_) {
        list.push_back({{"id", id}, {"name", session->name}});
    }
    return protocol::Json{{"type", "users"}, {"list", list}};
}

protocol::Json CollabServer::makeFileListMessage() const {
    const auto files = listFilesRecursive(projectRoot_);
    return protocol::Json{{"type", "file_list"}, {"files", files}};
}

void CollabServer::broadcast(const protocol::Json& message) {
    std::vector<std::shared_ptr<ClientSession>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [_, session] : clients_) {
            snapshot.push_back(session);
        }
    }

    for (const auto& session : snapshot) {
        sendTo(session, message);
    }
}

void CollabServer::sendTo(std::shared_ptr<ClientSession> session, const protocol::Json& message) {
    if (!session || !session->socket || !session->socket->is_open()) {
        return;
    }
    std::string error;
    if (!protocol::writeFrameBlocking(*session->socket, message, error)) {
        removeClient(session->id);
    }
}

void CollabServer::removeClient(int id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(id);
    }
    broadcast(makeUsersMessage());
}

void CollabServer::handleClient(std::shared_ptr<ClientSession> session) {
    while (session && session->socket && session->socket->is_open()) {
        protocol::Json message;
        std::string error;
        if (!protocol::readFrameBlocking(*session->socket, message, error)) {
            break;
        }
        handleMessage(session, message);
    }
    removeClient(session->id);
}

void CollabServer::updateMainDocument(const std::string& content) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mainDocument_ = content;
    }

    std::string writeError;
    if (!compiler::writeFile(mainTexPath_, content, writeError)) {
        std::cerr << "Failed to update main.tex: " << writeError << "\n";
    }
}

void CollabServer::enqueueCompile() {
    {
        std::lock_guard<std::mutex> lock(compileMutex_);
        ++pendingCompileRequests_;
    }
    compileCv_.notify_one();
}

void CollabServer::compileWorkerLoop() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(compileMutex_);
            compileCv_.wait(lock, [this]() { return stopCompileWorker_ || pendingCompileRequests_ > 0; });
            if (stopCompileWorker_) {
                return;
            }
            --pendingCompileRequests_;
        }

        const CompileOutcome outcome = runLatexmk();
        broadcastCompileOutcome(outcome);
    }
}

CollabServer::CompileOutcome CollabServer::runLatexmk() {
    return runLatexmkWindows(projectRoot_);
}

void CollabServer::broadcastCompileOutcome(const CompileOutcome& outcome) {
    if (!outcome.success) {
        broadcast(protocol::Json{{"type", "compile_result"}, {"success", false}, {"log", outcome.log}});
        return;
    }

    std::ifstream pdfFile(outcome.pdfPath, std::ios::binary);
    if (!pdfFile) {
        broadcast(protocol::Json{{"type", "compile_result"}, {"success", false}, {"log", "Compilation succeeded but main.pdf could not be opened."}});
        return;
    }

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(pdfFile)), std::istreambuf_iterator<char>());

    broadcast(protocol::Json{{"type", "compile_result"}, {"success", true}, {"pdf_size", bytes.size()}});

    constexpr std::size_t kChunkSize = 32 * 1024;
    for (std::size_t offset = 0; offset < bytes.size(); offset += kChunkSize) {
        const std::size_t end = std::min(bytes.size(), offset + kChunkSize);
        const std::vector<std::uint8_t> chunk(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(end));
        const bool last = (end == bytes.size());
        broadcast(protocol::Json{{"type", "pdf_chunk"}, {"data", base64Encode(chunk)}, {"last", last}});
    }

    if (bytes.empty()) {
        broadcast(protocol::Json{{"type", "pdf_chunk"}, {"data", ""}, {"last", true}});
    }
}

void CollabServer::handleMessage(std::shared_ptr<ClientSession> session, const protocol::Json& message) {
    const std::string type = message.value("type", "");
    if (type == "sync") {
        const std::string content = message.value("content", "");
        updateMainDocument(content);
        broadcast(protocol::Json{{"type", "sync"}, {"content", content}});
        return;
    }

    if (type == "compile_request") {
        enqueueCompile();
        return;
    }

    if (type == "file_list") {
        sendTo(session, makeFileListMessage());
        return;
    }

    const std::string pathText = message.value("path", "");
    fs::path resolved;
    std::string error;

    if (type == "file_open") {
        if (!resolveInsideRoot(pathText, resolved, error)) {
            return;
        }
        try {
            const std::string content = compiler::readFile(resolved);
            sendTo(session, protocol::Json{{"type", "file_open"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}, {"content", content}});
        } catch (...) {
        }
        return;
    }

    if (type == "file_save") {
        if (!resolveInsideRoot(pathText, resolved, error)) {
            return;
        }
        std::string writeError;
        if (compiler::writeFile(resolved, message.value("content", ""), writeError)) {
            broadcast(protocol::Json{{"type", "file_save"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}, {"content", message.value("content", "")}});
            broadcast(makeFileListMessage());
        }
        return;
    }

    if (type == "file_create") {
        if (!resolveInsideRoot(pathText, resolved, error)) {
            return;
        }
        std::string writeError;
        if (compiler::writeFile(resolved, "", writeError)) {
            broadcast(protocol::Json{{"type", "file_create"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}});
            broadcast(makeFileListMessage());
        }
        return;
    }

    if (type == "file_delete") {
        if (!resolveInsideRoot(pathText, resolved, error)) {
            return;
        }
        std::error_code ec;
        fs::remove_all(resolved, ec);
        if (!ec) {
            broadcast(protocol::Json{{"type", "file_delete"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}});
            broadcast(makeFileListMessage());
        }
        return;
    }

    if (type == "file_rename") {
        fs::path oldResolved;
        fs::path newResolved;
        if (!resolveInsideRoot(message.value("old", ""), oldResolved, error) || !resolveInsideRoot(message.value("new", ""), newResolved, error)) {
            return;
        }
        std::error_code ec;
        fs::create_directories(newResolved.parent_path(), ec);
        fs::rename(oldResolved, newResolved, ec);
        if (!ec) {
            broadcast(protocol::Json{{"type", "file_rename"}, {"old", fs::relative(oldResolved, projectRoot_).generic_string()}, {"new", fs::relative(newResolved, projectRoot_).generic_string()}});
            broadcast(makeFileListMessage());
        }
        return;
    }

    if (type == "file_upload") {
        if (!resolveInsideRoot(pathText, resolved, error)) {
            return;
        }
        std::string writeError;
        if (compiler::writeFile(resolved, message.value("content", ""), writeError)) {
            broadcast(protocol::Json{{"type", "file_upload"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}});
            broadcast(makeFileListMessage());
        }
    }
}

}  // namespace network
