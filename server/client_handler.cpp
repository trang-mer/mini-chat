// ============================================================
// server/client_handler.cpp
// Implementation của ClientHandler
//
// Features:
//   - Chờ user nhập nickname khi vừa connect
//   - Xử lý /users, /join, /msg, /quit commands
//   - Broadcast tin nhắn trong room
//   - Thread-safe với mutex cho nickname/room
// ============================================================

#include "client_handler.h"
#include "server.h"
#include "../common/socket_wrapper.h"

// ============================================================
// Constructor
// ============================================================
ClientHandler::ClientHandler(SOCKET socket, std::shared_ptr<ServerBridge> bridge)
    : socket_(socket)
    , bridge_(bridge)
    , nickname_()
    , room_(Config::DEFAULT_ROOM)
    , running_(false)
{
}

// ============================================================
// start: bắt đầu xử lý client trong thread riêng
// ============================================================
void ClientHandler::start() {
    run();
}

// ============================================================
// stop: dừng xử lý
// ============================================================
void ClientHandler::stop() {
    running_ = false;
    // Đóng socket
    if (socket_ != INVALID_SOCKET) {
        ::shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

// ============================================================
// getNickname: lấy nickname (thread-safe)
// ============================================================
std::string ClientHandler::getNickname() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return nickname_;
}

// ============================================================
// getRoom: lấy room hiện tại (thread-safe)
// ============================================================
std::string ClientHandler::getRoom() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return room_;
}

// ============================================================
// hasNickname: kiểm tra đã có nickname chưa
// ============================================================
bool ClientHandler::hasNickname() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return !nickname_.empty();
}

// ============================================================
// isRunning: kiểm tra còn đang chạy không
// ============================================================
bool ClientHandler::isRunning() const {
    return running_.load();
}

// ============================================================
// run: vòng lặp nhận message từ client
// ============================================================
void ClientHandler::run() {
    running_ = true;

    // Bước 1: Chờ user nhập nickname
    bool gotNickname = waitForNickname();
    if (!gotNickname) {
        // Client disconnect trước khi nhập nickname
        cleanupAndRemove();
        return;
    }

    // Bước 2: Gửi thông báo chào mừng
    std::string welcome =
        "=== Welcome to Mini Chat C++ ===\n"
        "Commands:\n"
        "  /users       - List online users\n"
        "  /join <room> - Join a room\n"
        "  /msg <nick> <msg> - Send private message\n"
        "  /nick <name> - Change nickname\n"
        "  /quit        - Disconnect\n\n"
        "You are in room: " + getRoom() + "\n\n";
    sendMessage(socket_, MessageType::SYSTEM, welcome);

    // Bước 3: Broadcast user joined (nếu có bridge)
    if (auto bridge = bridge_.lock()) {
        bridge->broadcastRoomJoin(getNickname(), getRoom());
    }

    // Bước 4: Vòng lặp nhận message
    while (running_.load()) {
        // Non-blocking recv với select()
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);

        struct timeval timeout;
        timeout.tv_sec  = 1;
        timeout.tv_usec = 0;

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            break;
        }
        if (ready == 0) {
            continue;
        }

        // Nhận message
        Message msg = receiveMessage(socket_);
        if (msg.payload.empty() && msg.type == MessageType::NORMAL) {
            // Disconnect hoặc lỗi
            break;
        }

        // Xử lý message
        switch (msg.type) {
            case MessageType::COMMAND: {
                bool shouldQuit = handleCommand(msg.payload);
                if (shouldQuit) {
                    running_ = false;
                }
                break;
            }
            case MessageType::NORMAL:
            default:
                // Tin nhắn thường: broadcast trong room
                handleNormalMessage(msg.payload);
                break;
        }
    }

    // Cleanup khi disconnect
    cleanupAndRemove();
}

// ============================================================
// waitForNickname: chờ user nhập nickname
// Trả về: true = nhận được nickname, false = client disconnect
// ============================================================
bool ClientHandler::waitForNickname() {
    // Gửi prompt yêu cầu nhập nickname
    sendMessage(socket_, MessageType::SYSTEM, "Enter your nickname: ");

    // Non-blocking recv để nhận nickname
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket_, &readSet);

    struct timeval timeout;
    timeout.tv_sec  = 30;  // Timeout 30 giây để nhập nickname
    timeout.tv_usec = 0;

    int ready = select(0, &readSet, nullptr, nullptr, &timeout);
    if (ready == SOCKET_ERROR) {
        return false;
    }
    if (ready == 0) {
        // Timeout
        sendMessage(socket_, MessageType::SYSTEM, "Timeout waiting for nickname. Goodbye!");
        return false;
    }

    // Nhận nickname
    Message msg = receiveMessage(socket_);
    if (msg.payload.empty() && msg.type == MessageType::NORMAL) {
        return false;
    }

    // Parse nickname
    std::string nickname = trim(msg.payload);
    if (nickname.empty() || nickname.length() > Config::MAX_NICKNAME_LEN) {
        sendMessage(socket_, MessageType::SYSTEM,
                    "Invalid nickname. Disconnecting.");
        return false;
    }

    // Kiểm tra nickname đã tồn tại chưa
    if (auto bridge = bridge_.lock()) {
        if (bridge->nicknameExists(nickname, socket_)) {
            std::string err = "Nickname '" + nickname + "' is already taken. Disconnecting.";
            sendMessage(socket_, MessageType::SYSTEM, err);
            return false;
        }
    }

    // Đặt nickname
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        nickname_ = nickname;
    }

    // Xác nhận
    std::string confirm = "Your nickname is: " + nickname
                       + "\nRoom: " + getRoom()
                       + "\n\nType /help to see available commands.\n";
    sendMessage(socket_, MessageType::SYSTEM, confirm);

    return true;
}

// ============================================================
// handleCommand: xử lý lệnh từ client
// Trả về: true = tiếp tục, false = ngắt kết nối
// ============================================================
bool ClientHandler::handleCommand(const std::string& text) {
    auto [cmd, args] = parseCommand(text);

    if (cmd == "users" || cmd == "list") {
        // /users - hiển thị danh sách user online
        if (auto bridge = bridge_.lock()) {
            bridge->sendUserList(socket_);
        }
        return true;
    }

    if (cmd == "join") {
        // /join <roomName>
        if (args.empty()) {
            sendMessage(socket_, MessageType::SYSTEM, "Usage: /join <roomName>");
            return true;
        }

        std::string newRoom = trim(args);
        if (newRoom.length() > Config::MAX_ROOM_NAME_LEN) {
            sendMessage(socket_, MessageType::SYSTEM, "Room name too long.");
            return true;
        }

        // Cập nhật room
        std::string oldRoom = getRoom();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            room_ = newRoom;
        }

        // Cập nhật trong server
        if (auto bridge = bridge_.lock()) {
            bridge->updateClientRoom(socket_, newRoom);
            bridge->sendRoomJoined(socket_, newRoom);
            bridge->broadcastRoomJoin(getNickname(), newRoom);
        }

        return true;
    }

    if (cmd == "msg" || cmd == "dm" || cmd == "w") {
        // /msg <nickname> <message>
        // args format: "nickname message"
        size_t spacePos = args.find(' ');
        if (spacePos == std::string::npos) {
            sendMessage(socket_, MessageType::SYSTEM,
                        "Usage: /msg <nickname> <message>");
            return true;
        }

        std::string targetNick = trim(args.substr(0, spacePos));
        std::string privMsg    = trim(args.substr(spacePos + 1));

        if (targetNick.empty() || privMsg.empty()) {
            sendMessage(socket_, MessageType::SYSTEM,
                        "Usage: /msg <nickname> <message>");
            return true;
        }

        if (auto bridge = bridge_.lock()) {
            bridge->sendPrivateMessage(socket_, getNickname(), targetNick, privMsg);
        }

        return true;
    }

    if (cmd == "quit" || cmd == "exit" || cmd == "q") {
        // /quit - ngắt kết nối
        sendMessage(socket_, MessageType::SYSTEM, "Goodbye!");
        return false;
    }

    if (cmd == "help" || cmd == "?") {
        std::string help =
            "=== Commands ===\n"
            "  /users           - List online users\n"
            "  /join <room>     - Join a room\n"
            "  /msg <n> <msg>   - Send private message\n"
            "  /nick <name>     - Change your nickname\n"
            "  /quit            - Disconnect\n";
        sendMessage(socket_, MessageType::SYSTEM, help);
        return true;
    }

    if (cmd == "nick" || cmd == "rename") {
        // /nick <newName> - đổi nickname
        if (args.empty()) {
            sendMessage(socket_, MessageType::SYSTEM, "Usage: /nick <newNickname>");
            return true;
        }
        std::string newNick = trim(args);
        if (newNick.length() > Config::MAX_NICKNAME_LEN) {
            sendMessage(socket_, MessageType::SYSTEM, "Nickname too long.");
            return true;
        }
        if (auto bridge = bridge_.lock()) {
            if (bridge->nicknameExists(newNick, socket_)) {
                sendMessage(socket_, MessageType::SYSTEM,
                            "Nickname '" + newNick + "' is already taken.");
                return true;
            }
        }
        std::string oldNick = getNickname();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            nickname_ = newNick;
        }
        // Cập nhật nickname trong server
        if (auto bridge = bridge_.lock()) {
            bridge->updateClientNickname(socket_, newNick);
        }
        sendMessage(socket_, MessageType::SYSTEM,
                    "Nickname changed from " + oldNick + " to " + newNick);
        return true;
    }

    // Lệnh không hợp lệ
    sendMessage(socket_, MessageType::SYSTEM,
                "Unknown command: /" + cmd + ". Type /help for help.");
    return true;
}

// ============================================================
// handleNormalMessage: xử lý tin nhắn thường (broadcast trong room)
// ============================================================
void ClientHandler::handleNormalMessage(const std::string& text) {
    if (text.empty()) {
        return;
    }

    if (auto bridge = bridge_.lock()) {
        bridge->broadcastToRoom(socket_, getNickname(), getRoom(), text);
    }
}

// ============================================================
// isAlive: kiểm tra client còn kết nối không
// ============================================================
bool ClientHandler::isAlive() const {
    return socket_ != INVALID_SOCKET;
}

// ============================================================
// cleanupAndRemove: cleanup khi client disconnect
//
// THỨ TỰ QUAN TRỌNG:
//   1. Đóng socket TRƯỚC
//   2. Gọi removeClient SAU (server chỉ xóa khỏi list, không đóng socket)
// ============================================================
void ClientHandler::cleanupAndRemove() {
    // NOTE: Lấy nickname TRƯỚC khi đóng socket
    std::string nick = getNickname();
    SOCKET deadSocket = INVALID_SOCKET;

    // Bước 1: Đóng socket
    if (socket_ != INVALID_SOCKET) {
        deadSocket = socket_;
        ::shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    // Bước 2: Xóa khỏi danh sách server
    // NOTE: Pass deadSocket (đã invalid), server chỉ xóa khỏi list
    if (auto bridge = bridge_.lock()) {
        bridge->removeClient(deadSocket, nick);
    }

    running_ = false;
}
