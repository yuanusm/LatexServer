#pragma once

#include <nlohmann/json.hpp>

#include <asio.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace network::protocol {

using Json = nlohmann::json;

inline std::vector<std::uint8_t> frameMessage(const Json& message) {
    const std::string payload = message.dump();
    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());

    std::vector<std::uint8_t> framed(4 + payload.size());
    framed[0] = static_cast<std::uint8_t>((length >> 24) & 0xFF);
    framed[1] = static_cast<std::uint8_t>((length >> 16) & 0xFF);
    framed[2] = static_cast<std::uint8_t>((length >> 8) & 0xFF);
    framed[3] = static_cast<std::uint8_t>(length & 0xFF);
    std::copy(payload.begin(), payload.end(), framed.begin() + 4);
    return framed;
}

inline std::uint32_t decodeLengthPrefix(const std::array<std::uint8_t, 4>& header) {
    return (static_cast<std::uint32_t>(header[0]) << 24) |
           (static_cast<std::uint32_t>(header[1]) << 16) |
           (static_cast<std::uint32_t>(header[2]) << 8) |
           static_cast<std::uint32_t>(header[3]);
}

inline bool readFrameBlocking(asio::ip::tcp::socket& socket, Json& message, std::string& error) {
    std::array<std::uint8_t, 4> header{};
    asio::error_code ec;
    asio::read(socket, asio::buffer(header), ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    const std::uint32_t length = decodeLengthPrefix(header);
    std::vector<char> payload(length);
    asio::read(socket, asio::buffer(payload.data(), payload.size()), ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    try {
        message = Json::parse(payload.begin(), payload.end());
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }

    return true;
}

inline bool writeFrameBlocking(asio::ip::tcp::socket& socket, const Json& message, std::string& error) {
    asio::error_code ec;
    const auto bytes = frameMessage(message);
    asio::write(socket, asio::buffer(bytes.data(), bytes.size()), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

}  // namespace network::protocol
