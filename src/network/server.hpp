#pragma once

#include "network/protocol.hpp"

#include <asio.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace network {

class CollabServer {
public:
    CollabServer(std::filesystem::path projectRoot, std::uint16_t port);
    void run();

private:
    struct ClientSession {
        int id = 0;
        std::string name;
        std::shared_ptr<asio::ip::tcp::socket> socket;
    };

    bool resolveInsideRoot(const std::filesystem::path& candidate, std::filesystem::path& resolved, std::string& error) const;
    protocol::Json makeUsersMessage() const;
    protocol::Json makeFileListMessage() const;
    void broadcast(const protocol::Json& message);
    void sendTo(std::shared_ptr<ClientSession> session, const protocol::Json& message);
    void removeClient(int id);
    void handleClient(std::shared_ptr<ClientSession> session);
    void handleMessage(std::shared_ptr<ClientSession> session, const protocol::Json& message);

    std::filesystem::path projectRoot_;
    asio::io_context ioContext_;
    asio::ip::tcp::acceptor acceptor_;

    mutable std::mutex mutex_;
    int nextUserId_ = 1;
    std::string document_;
    std::unordered_map<int, std::shared_ptr<ClientSession>> clients_;
};

}  // namespace network
