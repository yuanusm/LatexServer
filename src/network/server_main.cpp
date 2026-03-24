#include "compiler.h"
#include "network/server.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    const fs::path argv0 = argc > 0 ? fs::path(argv[0]) : fs::current_path();
    const fs::path projectRoot = compiler::detectProjectRoot(argv0);

    std::uint16_t port = 9090;
    if (argc > 1) {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    try {
        network::CollabServer server(projectRoot, port);
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "Server failed: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
