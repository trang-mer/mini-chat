// ============================================================
// client/client.h
// TCP Client cho mini-chat-cpp
//
// Kiến trúc 2 thread:
//   - Main thread: nhận input từ console, gửi message
//   - Receive thread: nhận message từ server, hiển thị
//
// Đồng bộ:
//   - Receive thread gửi message -> main thread hiển thị
//   - Dùng mutex + condition variable để thread-safe
//   - Main thread kiểm tra running flag thường xuyên
// ============================================================

#ifndef CLIENT_H
#define CLIENT_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <queue>
#include <thread>
#include "../common/protocol.h"
#include "../common/logger.h"
#include "../common/utils.h"

class ChatClient {
public:
    // Constructor
    //   serverIP : địa chỉ IP của server (mặc định: 127.0.0.1)
    //   port     : cổng của server (mặc định: 8080)
    ChatClient(const std::string& serverIP = "127.0.0.1",
               uint16_t port = Config::DEFAULT_PORT);

    // Destructor: tự động cleanup
    ~ChatClient();

    // Không cho copy
    ChatClient(const ChatClient&)            = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    // Kết nối tới server
    // Trả về: true nếu thành công
    [[nodiscard]] bool connect(const std::string& nickname = "");

    // Chạy client (blocking - main loop)
    // Cho phép nhập message từ console và gửi đi
    void run();

    // Ngắt kết nối (gọi từ thread khác)
    void disconnect();

    // Kiểm tra còn kết nối không
    [[nodiscard]] bool isConnected() const { return connected_.load(); }

    // Getters
    [[nodiscard]] std::string getNickname() const { return nickname_; }

private:
    // Thread nhận message từ server
    void receiveThreadFunc();

    // Gửi message tới server
    // Trả về: true nếu gửi thành công
    bool sendToServer(MessageType type, const std::string& text);

    // Xử lý một message nhận được
    void handleIncomingMessage(const Message& msg);

    // In message ra console (thread-safe)
    void printMessage(const std::string& text);

    // Queue để truyền message từ receive thread -> main thread
    std::queue<std::string>  messageQueue_;
    mutable std::mutex       messageQueueMutex_;
    std::condition_variable  messageQueueCV_;

    std::string        serverIP_;       // IP của server
    uint16_t          serverPort_;     // Port của server
    SOCKET            serverSocket_;    // Socket kết nối
    std::string       nickname_;       // Nickname của user
    std::atomic<bool> connected_;      // Còn kết nối không
    std::atomic<bool> running_;        // Client đang chạy không
    std::thread       receiveThread_;  // Thread nhận message
};

#endif // CLIENT_H
