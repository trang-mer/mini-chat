// ============================================================
// client/main.cpp
// Entry point của client
//
// Cách compile:
//   cd client
//   g++ main.cpp client.cpp -o client.exe -lws2_32 -std=c++17
//
// Cách chạy:
//   client.exe [server_ip [port]]
//   client.exe 127.0.0.1 8080
//
// Features:
//   - Nickname: user nhập nickname khi vừa connect
//   - Rooms: /join roomName
//   - Private message: /msg nickname message
//   - Online users: /users
// ============================================================

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "client.h"

int main(int argc, char* argv[]) {
    std::cout << "===========================================\n"
              << "   Mini Chat C++ Client\n"
              << "   Nickname + Rooms + Private Message\n"
              << "===========================================\n\n";

    // Parse command line arguments
    std::string serverIP = "127.0.0.1";
    uint16_t    port    = Config::DEFAULT_PORT;

    if (argc >= 2) {
        serverIP = argv[1];
    }
    if (argc >= 3) {
        try {
            port = static_cast<uint16_t>(std::stoi(argv[2]));
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Invalid port: " << argv[2] << "\n";
            return 1;
        }
    }

    std::cout << "Connecting to " << serverIP << ":" << port << "...\n";

    // Tạo client
    ChatClient client(serverIP, port);

    // Kết nối
    if (!client.connect()) {
        std::cerr << "[ERROR] Failed to connect to server.\n";
        return 1;
    }

    std::cout << "[OK] Connected to server!\n\n";

    // === SETUP PHASE: chờ server gửi "Enter your nickname:" ===
    std::cout << "Waiting for server setup prompt...\n";
    bool gotPrompt = false;
    for (int i = 0; i < 50; ++i) {  // Đợi tối đa 5 giây
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::string msg;
        if (client.popMessage(msg)) {
            std::cout << msg << "\n";
            if (msg.find("nickname") != std::string::npos
                && msg.find("Enter") != std::string::npos) {
                gotPrompt = true;
                break;
            }
        }
    }

    if (!gotPrompt) {
        std::cout << "[WARNING] Did not receive nickname prompt from server.\n";
    }

    // Nhập nickname
    std::cout << "\n--- Setup ---\n";
    std::string nickname;
    while (true) {
        std::cout << "Enter your nickname: " << std::flush;
        if (!std::getline(std::cin, nickname)) {
            std::cout << "\n[INFO] Goodbye!\n";
            return 0;
        }
        nickname = trim(nickname);
        if (!nickname.empty()) {
            break;
        }
        std::cout << "[ERROR] Nickname cannot be empty.\n";
    }
    std::cout << "--------------\n\n";

    // Gửi nickname lên server
    client.sendToServer(MessageType::NORMAL, nickname);
    std::cout << "[OK] Nickname sent! Waiting for server confirmation...\n";

    // Đợi server xác nhận
    bool gotConfirmation = false;
    for (int i = 0; i < 50; ++i) {  // Đợi tối đa 5 giây
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::string msg;
        if (client.popMessage(msg)) {
            std::cout << msg << "\n";
            if (msg.find("nickname is") != std::string::npos
                || msg.find("Welcome") != std::string::npos) {
                gotConfirmation = true;
                break;
            }
        }
    }

    if (!gotConfirmation) {
        std::cout << "[WARNING] Did not receive server confirmation. Continuing anyway...\n";
    }

    std::cout << "\n[OK] Starting chat! Commands: /users, /join, /msg, /nick, /quit\n\n";

    // Chạy main loop
    client.run();

    std::cout << "\n[INFO] Goodbye!\n";
    return 0;
}
