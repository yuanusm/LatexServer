#include "network/client.hpp"
#include "log.h"

namespace network {

CollabClient::CollabClient() {
    setLogComponent(LogComponent::CLIENT);
}

CollabClient::~CollabClient() {
    disconnect();
}

bool CollabClient::connect(const std::string& host, std::uint16_t port, std::string& error) {
    disconnect();

    try {
        asio::ip::tcp::resolver resolver(ioContext_);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        socket_ = std::make_unique<asio::ip::tcp::socket>(ioContext_);
        asio::connect(*socket_, endpoints);
        connected_.store(true);
        log(LogLevel::INFO, "Connected to server at " + host + ":" + std::to_string(port));
        readThread_ = std::thread([this]() { readLoop(); });
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        log(LogLevel::ERROR, "Connect failed: " + error);
        connected_.store(false);
        socket_.reset();
        return false;
    }
}

void CollabClient::disconnect() {
    const bool wasConnected = connected_.load();
    connected_.store(false);
    if (socket_) {
        asio::error_code ec;
        socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket_->close(ec);
    }

    if (readThread_.joinable()) {
        readThread_.join();
    }

    socket_.reset();
    std::lock_guard<std::mutex> lock(mutex_);
    inbox_.clear();
    if (wasConnected) {
        log(LogLevel::INFO, "Disconnected from server.");
    }
}

bool CollabClient::isConnected() const {
    return connected_.load();
}

bool CollabClient::send(const protocol::Json& message, std::string& error) {
    if (!socket_ || !connected_.load()) {
        error = "Not connected to collaboration server.";
        return false;
    }
    if (!protocol::writeFrameBlocking(*socket_, message, error)) {
        log(LogLevel::ERROR, "Failed to send message: " + error);
        return false;
    }
    return true;
}

std::vector<protocol::Json> CollabClient::pollIncoming() {
    std::vector<protocol::Json> messages;
    std::lock_guard<std::mutex> lock(mutex_);
    while (!inbox_.empty()) {
        messages.push_back(inbox_.front());
        inbox_.pop_front();
    }
    return messages;
}

void CollabClient::readLoop() {
    while (connected_.load() && socket_) {
        protocol::Json message;
        std::string error;
        if (!protocol::readFrameBlocking(*socket_, message, error)) {
            connected_.store(false);
            if (!error.empty()) {
                log(LogLevel::WARN, "Read loop stopped: " + error);
            }
            break;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        inbox_.push_back(std::move(message));
    }
}

}  // namespace network
