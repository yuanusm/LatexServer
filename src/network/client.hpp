#pragma once

#include "network/protocol.hpp"

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace network {

class CollabClient {
public:
    CollabClient();
    ~CollabClient();

    bool connect(const std::string& host, std::uint16_t port, std::string& error);
    void disconnect();

    bool isConnected() const;
    bool send(const protocol::Json& message, std::string& error);
    std::vector<protocol::Json> pollIncoming();

private:
    void readLoop();

    asio::io_context ioContext_;
    std::unique_ptr<asio::ip::tcp::socket> socket_;
    std::thread readThread_;
    std::atomic<bool> connected_{false};

    mutable std::mutex mutex_;
    std::deque<protocol::Json> inbox_;
};

}  // namespace network
