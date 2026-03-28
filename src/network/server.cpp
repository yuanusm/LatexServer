#include "network/server.hpp"

#include "compiler.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
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

struct FileEntry {
    std::string path;
    bool isDirectory = false;
    std::size_t size = 0;
};

bool isAllowedExtension(const fs::path& path) {
    static const std::vector<std::string> allowed{".tex", ".png", ".jpg", ".jpeg", ".pdf"};
    const std::string ext = path.extension().string();
    return std::find(allowed.begin(), allowed.end(), ext) != allowed.end();
}

std::vector<FileEntry> listEntriesRecursive(const fs::path& root) {
    std::vector<FileEntry> entries;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        FileEntry entry;
        entry.path = it->path().lexically_relative(root).generic_string();
        entry.isDirectory = it->is_directory(ec);
        if (!entry.isDirectory && it->is_regular_file(ec)) {
            if (!isAllowedExtension(it->path())) {
                continue;
            }
            entry.size = static_cast<std::size_t>(it->file_size(ec));
        }
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) { return a.path < b.path; });
    return entries;
}

std::vector<std::uint8_t> base64Decode(const std::string& input) {
    auto decodeBase64Char = [](unsigned char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };

    std::vector<std::uint8_t> output;
    output.reserve((input.size() / 4) * 3);
    std::array<int, 4> block{};
    std::size_t index = 0;
    for (unsigned char ch : input) {
        if (ch == '=') {
            block[index++] = -2;
        } else {
            const int value = decodeBase64Char(ch);
            if (value < 0) {
                continue;
            }
            block[index++] = value;
        }
        if (index == 4) {
            const int b0 = block[0];
            const int b1 = block[1];
            const int b2 = block[2];
            const int b3 = block[3];
            if (b0 >= 0 && b1 >= 0) output.push_back(static_cast<std::uint8_t>((b0 << 2) | (b1 >> 4)));
            if (b2 >= 0 && b0 >= 0 && b1 >= 0) output.push_back(static_cast<std::uint8_t>(((b1 & 0x0F) << 4) | (b2 >> 2)));
            if (b3 >= 0 && b2 >= 0) output.push_back(static_cast<std::uint8_t>(((b2 & 0x03) << 6) | b3));
            index = 0;
        }
    }
    return output;
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
int normalizedExitCode(int code) {
    return code;
}
#else
#include <sys/wait.h>
int normalizedExitCode(int code) {
    if (WIFEXITED(code)) {
        return WEXITSTATUS(code);
    }
    return code;
}
#endif

CollabServer::CompileOutcome runLatexmkCommand(const fs::path& projectRoot, const fs::path& mainTexPath) {
    CollabServer::CompileOutcome outcome;
    outcome.pdfPath = projectRoot / "main.pdf";

    const std::string command = "cd \"" + projectRoot.string() +
        "\" && latexmk -pdf -interaction=nonstopmode -halt-on-error -g main.tex 2>&1";

    log(LogLevel::INFO, "latexmk start: " + command);

#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif

    if (!pipe) {
        outcome.log = "Unable to launch latexmk process.";
        return outcome;
    }

    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        outcome.log += buffer.data();
    }

#if defined(_WIN32)
    const int rawExit = _pclose(pipe);
#else
    const int rawExit = pclose(pipe);
#endif
    outcome.exitCode = normalizedExitCode(rawExit);

    std::error_code ecPdf;
    const bool pdfExists = fs::exists(outcome.pdfPath, ecPdf) && !ecPdf;

    std::error_code ecTex;
    std::error_code ecPdfTime;
    const auto texTime = fs::last_write_time(mainTexPath, ecTex);
    const auto pdfTime = fs::last_write_time(outcome.pdfPath, ecPdfTime);
    const bool timestampValid = pdfExists && !ecTex && !ecPdfTime && (pdfTime >= texTime);

    if (outcome.exitCode == 0 && pdfExists && timestampValid) {
        outcome.success = true;
    } else if (outcome.exitCode != 0 && pdfExists && timestampValid) {
        outcome.success = true;
        log(LogLevel::WARN, "latexmk returned non-zero exit code but produced a valid PDF.");
    } else {
        outcome.success = false;
    }

    if (!pdfExists) {
        outcome.log += "\nPDF output was not generated.";
    } else if (!timestampValid) {
        outcome.log += "\nPDF output is older than main.tex.";
    }

    log(LogLevel::INFO, "latexmk end");
    log(LogLevel::INFO, "latexmk exit code: " + std::to_string(outcome.exitCode));

    return outcome;
}

}  // namespace

CollabServer::CollabServer(fs::path projectRoot, std::uint16_t port)
    : projectRoot_(fs::weakly_canonical(std::move(projectRoot))),
      mainTexPath_(projectRoot_ / "main.tex"),
      mainPdfPath_(projectRoot_ / "main.pdf"),
      acceptor_(ioContext_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    setLogComponent(LogComponent::SERVER);
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

    if (mainDocument_.empty()) {
        log(LogLevel::WARN, "main.tex is empty.");
    }
}

void CollabServer::run() {
    log(LogLevel::INFO, "Collab server listening on port " + std::to_string(acceptor_.local_endpoint().port()));
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

        log(LogLevel::INFO, "Client connected: #" + std::to_string(session->id));

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
    const auto entries = listEntriesRecursive(projectRoot_);
    protocol::Json entriesJson = protocol::Json::array();
    for (const auto& entry : entries) {
        entriesJson.push_back({{"path", entry.path}, {"is_directory", entry.isDirectory}, {"size", entry.size}});
    }
    return protocol::Json{{"type", "file_list"}, {"files", files}, {"entries", entriesJson}};
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
        log(LogLevel::WARN, "Failed to send message to client #" + std::to_string(session->id) + ": " + error);
        removeClient(session->id);
    }
}

void CollabServer::removeClient(int id) {
    bool existed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        existed = clients_.erase(id) > 0;
    }
    if (existed) {
        log(LogLevel::INFO, "Client disconnected: #" + std::to_string(id));
    }
    broadcast(makeUsersMessage());
}

void CollabServer::handleClient(std::shared_ptr<ClientSession> session) {
    while (session && session->socket && session->socket->is_open()) {
        protocol::Json message;
        std::string error;
        if (!protocol::readFrameBlocking(*session->socket, message, error)) {
            if (!error.empty()) {
                log(LogLevel::WARN, "Read loop ended for client #" + std::to_string(session->id) + ": " + error);
            }
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
        log(LogLevel::ERROR, "Failed to update main.tex: " + writeError);
    }
}

bool CollabServer::tryEnqueueCompile() {
    std::lock_guard<std::mutex> lock(compileMutex_);
    if (compileRunning_ || pendingCompileRequests_ > 0) {
        return false;
    }
    ++pendingCompileRequests_;
    compileCv_.notify_one();
    return true;
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
            compileRunning_ = true;
        }

        const CompileOutcome outcome = runLatexmk();
        broadcastCompileOutcome(outcome);

        {
            std::lock_guard<std::mutex> lock(compileMutex_);
            compileRunning_ = false;
        }
    }
}

CollabServer::CompileOutcome CollabServer::runLatexmk() {
    return runLatexmkCommand(projectRoot_, mainTexPath_);
}

void CollabServer::broadcastCompileOutcome(const CompileOutcome& outcome) {
    if (!outcome.success) {
        log(LogLevel::ERROR, "Compilation failed.");
        broadcast(protocol::Json{{"type", "compile_result"}, {"success", false}, {"log", outcome.log}});
        return;
    }

    std::ifstream pdfFile(outcome.pdfPath, std::ios::binary);
    if (!pdfFile) {
        const std::string error = "Compilation succeeded but main.pdf could not be opened.";
        log(LogLevel::ERROR, error);
        broadcast(protocol::Json{{"type", "compile_result"}, {"success", false}, {"log", error}});
        return;
    }

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(pdfFile)), std::istreambuf_iterator<char>());

    constexpr std::size_t kChunkSize = 64 * 1024;
    constexpr std::size_t kSingleMessageThreshold = 5 * 1024 * 1024;
    const std::size_t effectiveChunkSize = bytes.size() < kSingleMessageThreshold ? bytes.size() + 1 : kChunkSize;
    const std::size_t chunkCount = bytes.empty() ? 1 : ((bytes.size() + effectiveChunkSize - 1) / effectiveChunkSize);

    broadcast(protocol::Json{{"type", "compile_result"}, {"success", true}, {"pdf_size", bytes.size()}, {"chunk_count", chunkCount}, {"log", outcome.log}});

    std::size_t chunkIndex = 0;
    for (std::size_t offset = 0; offset < bytes.size(); offset += effectiveChunkSize) {
        const std::size_t end = std::min(bytes.size(), offset + effectiveChunkSize);
        const std::vector<std::uint8_t> chunk(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(end));
        const bool last = (end == bytes.size());
        broadcast(protocol::Json{{"type", "pdf_chunk"}, {"data", base64Encode(chunk)}, {"last", last}, {"index", chunkIndex}, {"total", chunkCount}});
        ++chunkIndex;
    }

    if (bytes.empty()) {
        broadcast(protocol::Json{{"type", "pdf_chunk"}, {"data", ""}, {"last", true}, {"index", 0}, {"total", chunkCount}});
    }

    log(LogLevel::INFO, "PDF streaming complete. Chunks sent: " + std::to_string(chunkCount));
}

void CollabServer::handleMessage(std::shared_ptr<ClientSession> session, const protocol::Json& message) {
    const std::string type = message.value("type", "");
    if (type == "sync") {
        log(LogLevel::INFO, "Sync received from client #" + std::to_string(session->id));
        const std::string content = message.value("content", "");
        updateMainDocument(content);
        broadcast(protocol::Json{{"type", "sync"}, {"content", content}});
        return;
    }

    if (type == "compile_request") {
        log(LogLevel::INFO, "Compile request received from client #" + std::to_string(session->id));
        if (!tryEnqueueCompile()) {
            sendTo(session, protocol::Json{{"type", "compile_result"}, {"success", false}, {"log", "Compile already in progress. Please retry."}});
        }
        return;
    }

    if (type == "file_list" || type == "file_list_request") {
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
        if (!isAllowedExtension(resolved)) {
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
        if (!isAllowedExtension(resolved)) {
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
        if (!isAllowedExtension(resolved)) {
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
        if (!isAllowedExtension(resolved)) {
            return;
        }
        const std::vector<std::uint8_t> data = base64Decode(message.value("data", ""));
        std::string writeError;
        std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
        if (output) {
            output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        if (output) {
            broadcast(protocol::Json{{"type", "file_upload"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}});
            broadcast(makeFileListMessage());
        }
    }
}

}  // namespace network
