// ============================================================
// client/client.h
// TCP Client cho mini-chat-cpp
//
// Features:
//   - 2 threads: receive thread (nhận từ server) + main thread (gửi/nhập)
//   - Hỗ trợ /users, /join, /msg, /quit
//   - Hiển thị tin nhắn theo format: [room] nickname: message
//   - Private message format: [Private] nickname: message
//   - Setup phase: nhập nickname trước khi chat
// ============================================================

#ifndef CLIENT_H
#define CLIENT_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <mutex>
#include <string>
#include <queue>
#include <thread>
#include <condition_variable>
#include <functional>
#include "../common/protocol.h"
#include "../common/logger.h"
#include "../common/utils.h"

class ChatClient {
public:
    // Constructor
    ChatClient(const std::string& serverIP = "127.0.0.1",
               uint16_t port = Config::DEFAULT_PORT);

    ~ChatClient();

    ChatClient(const ChatClient&)            = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    // Kết nối tới server
    [[nodiscard]] bool connect();

    // Chạy client (blocking - main loop)
    void run();

    // Ngắt kết nối
    void disconnect();

    [[nodiscard]] bool isConnected() const { return connected_.load(); }

    // Getters
    [[nodiscard]] std::string getServerIP()   const { return serverIP_; }
    [[nodiscard]] uint16_t    getServerPort() const { return serverPort_; }

    // Gửi message tới server (public để main.cpp gửi nickname)
    bool sendToServer(MessageType type, const std::string& text);

    // Đọc 1 message từ queue (blocking, dùng cho setup phase)
    bool waitForMessage(std::string& outMsg, int timeoutMs = 5000);

    // Kiểm tra queue có message không (non-blocking)
    bool hasMessage();

    // Lấy 1 message từ queue (non-blocking)
    bool popMessage(std::string& outMsg);

    // Đăng ký callback được gọi khi có message mới
    void setMessageCallback(std::function<void(const std::string&)> cb);

private:
    // Thread nhận message từ server
    void receiveThreadFunc();

    // Xử lý một message nhận được
    void handleIncomingMessage(const Message& msg);

    std::string        serverIP_;
    uint16_t          serverPort_;
    SOCKET            serverSocket_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;

    // Queue để truyền message từ receive thread -> main thread
    std::queue<std::string>  messageQueue_;
    mutable std::mutex       messageQueueMutex_;
    std::condition_variable  messageQueueCV_;

    // Callback khi có message mới
    std::function<void(const std::string&)> messageCallback_;

    std::thread       receiveThread_;
};

#endif // CLIENT_H
