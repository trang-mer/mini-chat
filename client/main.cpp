// ============================================================
// client/main.cpp
// Entry point của client
//
// Cách chạy:
//   cd client
//   g++ main.cpp client.cpp -o client.exe -lws2_32 -std=c++17
//   ./client.exe [server_ip [port]]
//   ./client.exe 127.0.0.1 8080
//   ./client.exe Alice          (connect với nickname Alice)
// ============================================================

#include <iostream>
#include <string>
#include "client.h"

void printBanner() {
    std::cout << "===========================================\n"
              << "   Mini Chat C++ Client (Refactored)\n"
              << "===========================================\n"
              << "Commands: /nick, /list, /quit, /help\n"
              << "===========================================\n\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    // Format: client.exe [server_ip] [port] [nickname]
    std::string serverIP  = "127.0.0.1";
    uint16_t    port      = Config::DEFAULT_PORT;
    std::string nickname  = "Guest";

    if (argc >= 2) {
        serverIP = argv[1];
    }
    if (argc >= 3) {
        try {
            port = static_cast<uint16_t>(std::stoi(argv[2]));
        } catch (const std::exception& e) {
            std::cerr << "Invalid port: " << argv[2] << "\n";
            return 1;
        }
    }
    if (argc >= 4) {
        nickname = argv[3];
    }

    printBanner();

    // Tạo client
    ChatClient client(serverIP, port);

    // Kết nối
    std::cout << "Connecting to " << serverIP << ":" << port << " as \"" << nickname << "\"...\n";

    if (!client.connect(nickname)) {
        std::cerr << "Failed to connect to server.\n";
        return 1;
    }

    std::cout << "Connected! Start typing messages.\n\n";

    // Chạy main loop
    client.run();

    std::cout << "\nGoodbye!\n";
    return 0;
}
