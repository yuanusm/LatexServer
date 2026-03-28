#include "network/server.hpp"

#include "compiler.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
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

struct FileEntry {
    std::string path;
    bool isDirectory = false;
    std::size_t size = 0;
};

bool isAllowedExtension(const fs::path& path) {
    static const std::vector<std::string> allowed{".tex", ".png", ".jpg", ".jpeg", ".pdf", ".bib", ".cls", ".sty"};
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return std::find(allowed.begin(), allowed.end(), ext) != allowed.end();
}

bool isTextTexFile(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".tex";
}

bool isForbiddenDirectory(const fs::path& path) {
    const std::string name = path.filename().string();
    if (name.empty()) {
        return false;
    }
    if (name[0] == '.') {
        return true;
    }
    return name == "build" || name == "_deps" || name == "client_temp" || name == ".git";
}

std::vector<FileEntry> scanProjectTree(const fs::path& root) {
    log(LogLevel::INFO, "Scanning project tree: " + root.string());
    std::vector<FileEntry> entries;
    std::set<std::string> uniquePaths;
    std::error_code rootEc;
    const fs::path canonicalRoot = fs::weakly_canonical(root, rootEc);
    if (rootEc) {
        log(LogLevel::ERROR, "Failed to resolve root for scan: " + root.string());
        return entries;
    }

    std::error_code ec;
    fs::recursive_directory_iterator it(canonicalRoot, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        if (it->is_directory(ec) && isForbiddenDirectory(it->path())) {
            log(LogLevel::INFO, "Skipping directory during scan: " + it->path().string());
            it.disable_recursion_pending();
            continue;
        }

        std::error_code canonicalEc;
        const fs::path canonicalPath = fs::weakly_canonical(it->path(), canonicalEc);
        if (canonicalEc || !startsWithPath(canonicalRoot, canonicalPath)) {
            log(LogLevel::WARN, "Skipping out-of-root path during scan: " + it->path().string());
            continue;
        }

        std::error_code relEc;
        const std::string relativePath = fs::relative(canonicalPath, canonicalRoot, relEc).generic_string();
        if (relEc || relativePath.empty() || relativePath == ".") {
            continue;
        }

        FileEntry entry;
        entry.path = relativePath;
        entry.isDirectory = it->is_directory(ec);
        if (!entry.isDirectory && it->is_regular_file(ec)) {
            if (!isAllowedExtension(canonicalPath)) {
                continue;
            }
            entry.size = static_cast<std::size_t>(it->file_size(ec));
        }

        if (uniquePaths.insert(entry.path).second) {
            entries.push_back(std::move(entry));
        }
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
        log(LogLevel::WARN, "Rejected path with traversal attempt: " + candidate.string());
        return false;
    }

    fs::path combined = projectRoot_ / candidate;
    std::error_code ec;
    resolved = fs::weakly_canonical(combined, ec);
    if (ec) {
        const fs::path parent = fs::weakly_canonical(combined.parent_path(), ec);
        if (ec) {
            error = "Invalid path.";
            log(LogLevel::WARN, "Rejected invalid path: " + candidate.string());
            return false;
        }
        resolved = (parent / combined.filename()).lexically_normal();
    }

    if (!startsWithPath(projectRoot_, resolved) || resolved.string().find(projectRoot_.string()) != 0) {
        error = "Resolved path escapes project root.";
        log(LogLevel::WARN, "Rejected invalid path: " + candidate.string());
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
    const auto entries = scanProjectTree(projectRoot_);
    std::vector<std::string> files;
    files.reserve(entries.size());
    protocol::Json entriesJson = protocol::Json::array();
    for (const auto& entry : entries) {
        if (!entry.isDirectory) {
            files.push_back(entry.path);
        }
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

    if (type == "op_insert" || type == "op_delete") {
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
            sendTo(session, protocol::Json{{"type", "error"}, {"message", error}});
            return;
        }
        if (!isTextTexFile(resolved)) {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", "Only .tex files can be opened in the editor."}});
            return;
        }
        try {
            const std::string content = compiler::readFile(resolved);
            sendTo(session, protocol::Json{{"type", "file_open"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}, {"content", content}});
        } catch (const std::exception& ex) {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", std::string("Failed to open file: ") + ex.what()}});
        }
        return;
    }

    if (type == "file_save") {
        if (!resolveInsideRoot(pathText, resolved, error)) {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", error}});
            return;
        }
        if (!isTextTexFile(resolved)) {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", "Only .tex files can be saved from the editor."}});
            return;
        }
        std::string writeError;
        if (compiler::writeFile(resolved, message.value("content", ""), writeError)) {
            broadcast(protocol::Json{{"type", "file_save"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}, {"content", message.value("content", "")}});
            broadcast(makeFileListMessage());
            if (fs::equivalent(resolved, mainTexPath_)) {
                std::lock_guard<std::mutex> lock(mutex_);
                mainDocument_ = message.value("content", "");
            }
        } else {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", std::string("Failed to save file: ") + writeError}});
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
        const std::string name = message.value("name", "");
        const std::string optionalSubfolder = message.value("path", "");
        if (name.empty()) {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", "Missing upload file name."}});
            return;
        }

        fs::path relativePath = fs::path(optionalSubfolder) / fs::path(name).filename();
        if (!resolveInsideRoot(relativePath, resolved, error)) {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", error}});
            return;
        }
        if (!isAllowedExtension(resolved)) {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", "Unsupported file extension. Allowed: .tex .png .jpg .jpeg .pdf .bib .cls .sty"}});
            return;
        }
        const std::vector<std::uint8_t> data = base64Decode(message.value("data", ""));
        constexpr std::size_t kMaxUploadBytes = 15 * 1024 * 1024;
        if (data.size() > kMaxUploadBytes) {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", "Upload rejected: file exceeds 15MB limit."}});
            return;
        }

        std::error_code ec;
        fs::create_directories(resolved.parent_path(), ec);
        std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
        if (output) {
            output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        if (output) {
            broadcast(protocol::Json{{"type", "file_upload"}, {"path", fs::relative(resolved, projectRoot_).generic_string()}});
            broadcast(makeFileListMessage());
        } else {
            sendTo(session, protocol::Json{{"type", "error"}, {"message", "Failed to save uploaded file on server."}});
        }
        return;
    }
}

}  // namespace network
