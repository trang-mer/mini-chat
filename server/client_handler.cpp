// ============================================================
// server/client_handler.cpp
// Implementation của ClientHandler
// ============================================================

#include "client_handler.h"
#include "../common/socket_wrapper.h"

// ============================================================
// Constructor
// ============================================================
ClientHandler::ClientHandler(
    SOCKET                 socket,
    BroadcastCallback      broadcast,
    UnregisterCallback     unregister,
    ListCallback           list
)
    : socket_(socket)
    , nickname_()
    , ip_()
    , port_(0)
    , connectTime_(0)
    , running_(true)
    , broadcastCallback_(std::move(broadcast))
    , unregisterCallback_(std::move(unregister))
    , listCallback_(std::move(list))
{
    // Lấy thông tin IP và port
    struct sockaddr_in addr;
    int addrLen = sizeof(addr);
    if (getpeername(socket_, (struct sockaddr*)&addr, &addrLen) == 0) {
        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf));
        ip_   = ipBuf;
        port_ = ntohs(addr.sin_port);
    } else {
        ip_   = "unknown";
        port_ = 0;
    }

    connectTime_ = time(nullptr);

    // Tạo nickname mặc định: "IP:port"
    nickname_ = ip_ + ":" + std::to_string(port_);
}

// ============================================================
// start: bắt đầu xử lý client trong thread riêng
// ============================================================
void ClientHandler::start() {
    // Chạy vòng lặp nhận message
    run();
}

// ============================================================
// stop: dừng xử lý (gọi từ thread khác)
// ============================================================
void ClientHandler::stop() {
    running_ = false;
    // Đóng socket để recv() trong run() trả về
    shutdown(socket_, SD_BOTH);
    closesocket(socket_);
}

// ============================================================
// setNickname: đặt nickname mới
// ============================================================
void ClientHandler::setNickname(const std::string& name) {
    std::string oldNick = nickname_;
    nickname_ = name;
    LOG_INFO("Client {} renamed to '{}'", oldNick, nickname_);
}

// ============================================================
// run: vòng lặp nhận message từ client
// ============================================================
void ClientHandler::run() {
    LOG_INFO("Client handler started for {}", nickname_);

    // Gửi thông báo chào mừng
    std::string welcome =
        "=== Welcome to Mini Chat C++ ===\n"
        "Commands:\n"
        "  /nick <name>  - Set your nickname\n"
        "  /list        - List all connected users\n"
        "  /help        - Show this help\n"
        "  /quit        - Disconnect\n"
        "Type a message to broadcast to all users.\n";
    sendMessage(socket_, MessageType::SYSTEM, welcome);

    // Thông báo cho tất cả client khác biết có người mới
    broadcastCallback_(socket_, MessageType::SYSTEM,
                       nickname_ + " has joined the chat.", INVALID_SOCKET);

    // Vòng lặp chính: nhận message
    while (running_) {
        // Sử dụng select() để kiểm tra non-blocking
        // Đây là cách an toàn để thoát khỏi recv() blocking
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);

        struct timeval timeout;
        timeout.tv_sec  = 1;   // Check mỗi 1 giây
        timeout.tv_usec = 0;

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            LOG_ERROR("select() failed: {}", wsaErrorString(WSAGetLastError()));
            break;
        }
        if (ready == 0) {
            // Timeout -> tiếp tục vòng lặp
            continue;
        }

        // Có dữ liệu để đọc
        Message msg = receiveMessage(socket_);
        if (msg.payload.empty() && msg.type == MessageType::NORMAL) {
            // receiveMessage trả về message rỗng = lỗi hoặc disconnect
            LOG_INFO("Client {} disconnected (recv returned empty)", nickname_);
            break;
        }

        // Xử lý message
        switch (msg.type) {
            case MessageType::NORMAL: {
                handleNormalMessage(msg.payload);
                break;
            }
            case MessageType::COMMAND: {
                bool shouldQuit = handleCommand(msg.payload);
                if (shouldQuit) {
                    running_ = false;
                }
                break;
            }
            default:
                LOG_WARNING("Unknown message type {} from {}", static_cast<int>(msg.type), nickname_);
                break;
        }
    }

    // Cleanup khi disconnect
    LOG_INFO("Cleaning up client {}", nickname_);

    // Thông báo cho các client khác biết người này đã rời đi
    broadcastCallback_(socket_, MessageType::SYSTEM,
                       nickname_ + " has left the chat.", INVALID_SOCKET);

    // Đóng socket và yêu cầu unregister
    closesocket(socket_);
    unregisterCallback_(socket_);

    LOG_INFO("Client handler finished for {}", nickname_);
}

// ============================================================
// handleCommand: xử lý lệnh từ client
// ============================================================
bool ClientHandler::handleCommand(const std::string& text) {
    auto [cmd, args] = parseCommand(text);

    if (cmd == "nick") {
        // /nick <name>
        if (args.empty()) {
            sendMessage(socket_, MessageType::SYSTEM, "Usage: /nick <name>");
        } else {
            std::string oldNick = nickname_;
            setNickname(args);
            sendMessage(socket_, MessageType::SYSTEM,
                        "Your nickname is now: " + nickname_);
            // Broadcast thay đổi nickname
            broadcastCallback_(socket_, MessageType::NICKNAME,
                               oldNick + " -> " + nickname_, INVALID_SOCKET);
        }
        return true;
    }

    if (cmd == "list") {
        // /list - hiển thị danh sách client
        auto clients = listCallback_();
        std::ostringstream oss;
        oss << "=== Connected users (" << clients.size() << ") ===\n";
        for (const auto& client : clients) {
            std::string marker = (client.socket == socket_) ? " (you)" : "";
            oss << "  - " << client.nickname << marker << "\n";
        }
        sendMessage(socket_, MessageType::SYSTEM, oss.str());
        return true;
    }

    if (cmd == "help" || cmd == "?") {
        std::string help =
            "=== Commands ===\n"
            "  /nick <name>  - Change your nickname\n"
            "  /list         - List all connected users\n"
            "  /help         - Show this help\n"
            "  /quit         - Disconnect\n";
        sendMessage(socket_, MessageType::SYSTEM, help);
        return true;
    }

    if (cmd == "quit" || cmd == "exit") {
        sendMessage(socket_, MessageType::SYSTEM, "Goodbye!");
        return false;  // Ngắt kết nối
    }

    // Lệnh không hợp lệ
    sendMessage(socket_, MessageType::SYSTEM, "Unknown command: /" + cmd + ". Type /help for help.");
    return true;
}

// ============================================================
// handleNormalMessage: xử lý tin nhắn thường (broadcast)
// ============================================================
void ClientHandler::handleNormalMessage(const std::string& text) {
    if (text.empty()) {
        return;
    }

    LOG_INFO("Broadcast from {}: {}", nickname_, text);

    // Broadcast cho tất cả client khác
    // Format: "[nickname]: message"
    broadcastCallback_(socket_, MessageType::BROADCAST, text, socket_);
}

// ============================================================
// isAlive: kiểm tra client còn kết nối không
// ============================================================
bool ClientHandler::isAlive() const {
    return socket_ != INVALID_SOCKET;
}
