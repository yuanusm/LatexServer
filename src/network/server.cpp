#include "network/server.hpp"

#include "compiler.h"

#include <algorithm>
#include <iostream>
#include <thread>

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

}  // namespace

CollabServer::CollabServer(fs::path projectRoot, std::uint16_t port)
    : projectRoot_(fs::weakly_canonical(std::move(projectRoot))),
      acceptor_(ioContext_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {}

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

        sendTo(session, protocol::Json{{"type", "sync"}, {"content", document_}});
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

void CollabServer::handleMessage(std::shared_ptr<ClientSession> session, const protocol::Json& message) {
    const std::string type = message.value("type", "");
    if (type == "sync") {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            document_ = message.value("content", "");
        }
        broadcast(protocol::Json{{"type", "sync"}, {"content", document_}});
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
        return;
    }
}

}  // namespace network
