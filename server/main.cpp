// ============================================================
// server/main.cpp
// Entry point của server
//
// Cách compile:
//   cd server
//   g++ main.cpp server.cpp client_handler.cpp -o server.exe -lws2_32 -std=c++17
//
// Cách chạy:
//   server.exe [port]
//   server.exe 8080
//
// Features: Nickname, Rooms, Private Message, Online Users, Logging
// ============================================================

#include <iostream>
#include <csignal>
#include <cstdlib>
#include <thread>
#include <chrono>
#include "server.h"

// ============================================================
// Flag để graceful shutdown khi nhận Ctrl+C
// ============================================================
static volatile std::sig_atomic_t gRunning = 1;

// ============================================================
// Signal handler cho Ctrl+C
// ============================================================
static void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGBREAK) {
        gRunning = 0;
    }
}

// ============================================================
// Console command thread
// Cho phép nhập lệnh từ console trong khi server đang chạy
// ============================================================
void consoleCommandThread(Server* server) {
    while (gRunning) {
        std::string line;
        std::cout << "[Console] ";
        if (!std::getline(std::cin, line)) {
            // EOF hoặc lỗi
            break;
        }

        line = trim(line);
        if (line.empty()) continue;

        if (line == "/quit" || line == "/exit" || line == "q") {
            std::cout << "[Console] Shutting down...\n";
            gRunning = 0;
            break;
        }

        if (line == "/stats") {
            std::cout << "[Console] Connected clients: " << server->getClientCount() << "\n";
        } else if (line == "/help") {
            std::cout << "Available commands:\n"
                      << "  /stats    - Show number of connected clients\n"
                      << "  /quit     - Shutdown server\n"
                      << "  /help     - Show this help\n";
        } else {
            std::cout << "Unknown command. Type /help for available commands.\n";
        }
    }
}

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[]) {
    std::cout << "===========================================\n"
              << "   Mini Chat C++ Server\n"
              << "   Nickname + Rooms + Private Message\n"
              << "===========================================\n";

    // Parse port từ command line
    uint16_t port = Config::DEFAULT_PORT;
    if (argc > 1) {
        try {
            port = static_cast<uint16_t>(std::stoi(argv[1]));
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Invalid port number: " << argv[1] << "\n";
            return 1;
        }
    }

    std::cout << "Port: " << port << "\n"
              << "Log file: server.log\n"
              << "Commands: /stats, /quit, /help\n"
              << "Press Ctrl+C to stop.\n"
              << "===========================================\n\n";

    // Setup signal handler cho Ctrl+C
    std::signal(SIGINT, signalHandler);
    std::signal(SIGBREAK, signalHandler);

    // Tạo và khởi tạo server
    Server server(port);

    if (!server.init()) {
        std::cerr << "[ERROR] Failed to initialize server\n";
        return 1;
    }

    // Chạy console command thread
    std::thread consoleThread(consoleCommandThread, &server);

    // Chạy server (blocking)
    server.run();

    // Shutdown
    server.shutdown();

    // Đợi console thread kết thúc
    if (consoleThread.joinable()) {
        consoleThread.join();
    }

    std::cout << "\nServer exited.\n";
    return 0;
}
