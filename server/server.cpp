// ============================================================
// server/server.cpp
// Implementation của Server
//
// Features: nickname, rooms, private message, logging
// ============================================================

#include "server.h"

// ============================================================
// Constructor
// ============================================================
Server::Server(uint16_t port)
    : port_(port)
    , serverSocket_(INVALID_SOCKET)
    , running_(false)
{
}

// ============================================================
// Destructor
// ============================================================
Server::~Server() {
    if (running_.load()) {
        shutdown();
    }
}

// ============================================================
// init: khởi tạo Winsock và tạo server socket
// ============================================================
bool Server::init() {
    // 1. Khởi tạo Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "[ERROR] WSAStartup failed: error code " << result << "\n";
        return false;
    }
    std::cout << "[OK] Winsock initialized (version 2.2)\n";

    // 2. Tạo server socket
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == INVALID_SOCKET) {
        std::cerr << "[ERROR] socket() failed: " << wsaErrorString(WSAGetLastError()) << "\n";
        WSACleanup();
        return false;
    }
    std::cout << "[OK] Server socket created\n";

    // 3. Cho phép reuse address (tránh lỗi "Address already in use" khi restart)
    int reuseAddr = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));

    // 4. Bind socket tới port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(port_);

    result = bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        std::cerr << "[ERROR] bind() failed: " << wsaErrorString(WSAGetLastError()) << "\n";
        closesocket(serverSocket_);
        WSACleanup();
        return false;
    }
    std::cout << "[OK] Socket bound to port " << port_ << "\n";

    // 5. Bắt đầu lắng nghe
    result = listen(serverSocket_, SOMAXCONN);
    if (result == SOCKET_ERROR) {
        std::cerr << "[ERROR] listen() failed: " << wsaErrorString(WSAGetLastError()) << "\n";
        closesocket(serverSocket_);
        WSACleanup();
        return false;
    }

    // 6. Mở file log
    logFile_.open("server.log", std::ios::out | std::ios::trunc);
    if (!logFile_.is_open()) {
        std::cerr << "[WARNING] Cannot open server.log for writing\n";
    }

    // 7. Log server started
    logToFile("[INFO]", "Server started on port " + std::to_string(port_));

    running_ = true;
    return true;
}

// ============================================================
// run: vòng lặp chính của server
// ============================================================
void Server::run() {
    if (!running_.load()) {
        std::cerr << "[ERROR] Server not initialized. Call init() first.\n";
        return;
    }

    std::cout << "[OK] Server listening on port " << port_ << "...\n";
    std::cout << "[INFO] Press Ctrl+C to stop.\n\n";

    while (running_.load()) {
        // Sử dụng select() để non-blocking accept
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket_, &readSet);

        struct timeval timeout;
        timeout.tv_sec  = 1;   // Check mỗi 1 giây (để kiểm tra running_)
        timeout.tv_usec = 0;

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            if (running_.load()) {
                std::cerr << "[ERROR] select() failed: " << wsaErrorString(WSAGetLastError()) << "\n";
            }
            break;
        }
        if (ready == 0) {
            // Timeout -> tiếp tục kiểm tra running_
            continue;
        }

        // Có kết nối mới -> accept
        acceptClient();
    }

    logToFile("[INFO]", "Server run loop ended");
}

// ============================================================
// shutdown: dừng server gracefully
// ============================================================
void Server::shutdown() {
    if (!running_.load()) {
        return;
    }

    std::cout << "\n[INFO] Shutting down server...\n";
    logToFile("[INFO]", "Server shutting down");

    running_ = false;

    // Đóng server socket để unblock accept()
    if (serverSocket_ != INVALID_SOCKET) {
        ::shutdown(serverSocket_, SD_BOTH);
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }

    // Xóa tất cả client (chỉ xóa khỏi list, KHÔNG đóng socket)
    // ClientHandler đã tự đóng socket khi nhận shutdown notification
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.clear();
    }

    // Đóng file log
    if (logFile_.is_open()) {
        logFile_.close();
    }

    WSACleanup();
    std::cout << "[INFO] Server shutdown complete.\n";
    logToFile("[INFO]", "Server shutdown complete");
}

// ============================================================
// acceptClient: chấp nhận một client mới
// ============================================================
void Server::acceptClient() {
    struct sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);

    SOCKET clientSocket = accept(
        serverSocket_,
        (struct sockaddr*)&clientAddr,
        &clientAddrLen
    );

    if (clientSocket == INVALID_SOCKET) {
        if (running_.load()) {
            std::cerr << "[ERROR] accept() failed: " << wsaErrorString(WSAGetLastError()) << "\n";
        }
        return;
    }

    // Lấy thông tin client
    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
    uint16_t port = ntohs(clientAddr.sin_port);

    std::cout << "[INFO] New connection from " << ipBuf << ":" << port << "\n";
    logToFile("[INFO]", std::string("New connection from ") + ipBuf + ":" + std::to_string(port));

    // Tạo ClientInfo (chưa có nickname, sẽ đợi client nhập)
    ClientInfo info;
    info.socket      = clientSocket;
    info.ipAddress   = ipBuf;
    info.port        = port;
    info.connectTime = time(nullptr);
    info.nickname    = "";        // Chưa có nickname
    info.room        = Config::DEFAULT_ROOM;

    // Thêm vào danh sách (mutex)
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.push_back(info);
    }

    // Tạo shared_ptr tới Server (dùng this, Server tồn tại đến khi main kết thúc)
    std::shared_ptr<Server> serverPtr(this, [](Server*){});  // no-op deleter

    // Tạo ClientHandler trong shared_ptr
    auto handler = std::make_shared<ClientHandler>(clientSocket, serverPtr);

    // Tạo thread để chạy handler
    // Handler chạy trong thread riêng, tự cleanup khi kết thúc
    std::thread([handler]() {
        handler->start();
    }).detach();

    std::cout << "[INFO] Client thread started. Waiting for nickname...\n";
}

// ============================================================
// broadcastToRoom: gửi message cho tất cả client trong cùng room
// ============================================================
void Server::broadcastToRoom(SOCKET senderSocket,
                            const std::string& senderNickname,
                            const std::string& senderRoom,
                            const std::string& message) {
    // Copy danh sách sockets trong cùng room
    std::vector<SOCKET> targetSockets;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& client : clients_) {
            if (client.socket != INVALID_SOCKET
                && client.socket != senderSocket
                && client.room == senderRoom) {
                targetSockets.push_back(client.socket);
            }
        }
    }
    // Mutex đã unlock -> gửi message

    // Log chat message
    logToFile("[CHAT]", "[" + senderRoom + "] " + senderNickname + ": " + message);

    // Gửi cho từng client
    for (SOCKET targetSocket : targetSockets) {
        std::string fullMessage = "[" + senderRoom + "] " + senderNickname + ": " + message;
        bool success = sendMessage(targetSocket, MessageType::NORMAL, fullMessage);
        if (!success) {
            // Gửi thất bại -> tìm nickname để log rồi remove
            std::string nick = "(unknown)";
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                for (const auto& c : clients_) {
                    if (c.socket == targetSocket) {
                        nick = c.nickname;
                        break;
                    }
                }
            }
            std::cout << "[WARNING] Failed to send to client " << nick << ", removing...\n";
            removeClient(targetSocket, nick);
        }
    }
}

// ============================================================
// sendPrivateMessage: gửi tin nhắn riêng từ sender tới target
// ============================================================
void Server::sendPrivateMessage(SOCKET senderSocket,
                                const std::string& senderNickname,
                                const std::string& targetNickname,
                                const std::string& message) {
    SOCKET targetSocket = INVALID_SOCKET;

    // Tìm socket của target nickname
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& client : clients_) {
            if (client.nickname == targetNickname && client.socket != senderSocket) {
                targetSocket = client.socket;
                break;
            }
        }
    }

    if (targetSocket == INVALID_SOCKET) {
        // Không tìm thấy user -> gửi thông báo cho sender
        sendMessage(senderSocket, MessageType::SYSTEM, "User not found.");
        return;
    }

    // Gửi tin nhắn riêng cho target
    std::string fullMessage = "[Private] " + senderNickname + ": " + message;
    bool success = sendMessage(targetSocket, MessageType::PRIVATE, fullMessage);

    if (success) {
        logToFile("[PRIVATE]", senderNickname + " -> " + targetNickname + ": " + message);
    } else {
        // Target đã disconnect -> remove
        removeClient(targetSocket, targetNickname);
    }
}

// ============================================================
// sendUserList: gửi danh sách user online tới một client
// ============================================================
void Server::sendUserList(SOCKET clientSocket) {
    std::ostringstream oss;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        size_t count = clients_.size();
        oss << "Online users (" << count << "):\n";
        for (const auto& client : clients_) {
            if (!client.nickname.empty()) {
                oss << "- " << client.nickname << " in room " << client.room << "\n";
            }
        }
    }

    sendMessage(clientSocket, MessageType::USER_LIST, oss.str());
}

// ============================================================
// sendRoomJoined: gửi thông báo cho client khi join room thành công
// ============================================================
void Server::sendRoomJoined(SOCKET clientSocket, const std::string& roomName) {
    std::string msg = "You joined room: " + roomName;
    sendMessage(clientSocket, MessageType::SYSTEM, msg);
}

// ============================================================
// removeClient: xóa client khỏi danh sách
// NOTE: Chỉ xóa khỏi list, KHÔNG đóng socket
// Socket đã được ClientHandler đóng trước khi gọi hàm này
// ============================================================
void Server::removeClient(SOCKET socket, const std::string& nickname) {
    bool found = false;
    std::string nick = nickname;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto it = clients_.begin(); it != clients_.end(); ++it) {
            if (it->socket == socket) {
                nick = it->nickname;
                clients_.erase(it);
                found = true;
                break;
            }
        }
    }

    if (found) {
        if (!nick.empty()) {
            std::cout << "[INFO] " << nick << " disconnected\n";
            logToFile("[INFO]", nick + " disconnected");
        } else {
            std::cout << "[INFO] Client disconnected\n";
            logToFile("[INFO]", "Client disconnected");
        }
    }
}

// ============================================================
// broadcastRoomJoin: broadcast thông báo user joined room
// ============================================================
void Server::broadcastRoomJoin(const std::string& nickname, const std::string& room) {
    std::vector<SOCKET> targetSockets;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& client : clients_) {
            if (client.socket != INVALID_SOCKET && client.room == room) {
                targetSockets.push_back(client.socket);
            }
        }
    }

    for (SOCKET targetSocket : targetSockets) {
        std::string msg = nickname + " joined room " + room;
        sendMessage(targetSocket, MessageType::SYSTEM, msg);
    }

    logToFile("[INFO]", nickname + " joined room " + room);
}

// ============================================================
// updateClientRoom: cập nhật room của client trong danh sách
// ============================================================
void Server::updateClientRoom(SOCKET socket, const std::string& newRoom) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& client : clients_) {
        if (client.socket == socket) {
            client.room = newRoom;
            break;
        }
    }
}

// ============================================================
// updateClientNickname: cập nhật nickname của client trong danh sách
// ============================================================
void Server::updateClientNickname(SOCKET socket, const std::string& newNickname) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& client : clients_) {
        if (client.socket == socket) {
            client.nickname = newNickname;
            break;
        }
    }
}

// ============================================================
// nicknameExists: kiểm tra nickname đã tồn tại chưa
// ============================================================
bool Server::nicknameExists(const std::string& nickname, SOCKET excludeSocket) const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& client : clients_) {
        if (client.nickname == nickname && client.socket != excludeSocket
            && !client.nickname.empty()) {
            return true;
        }
    }
    return false;
}

// ============================================================
// getClientList_: lấy danh sách client (dùng nội bộ)
// ============================================================
std::vector<ClientInfo> Server::getClientList_() const {
    std::vector<ClientInfo> result;
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& client : clients_) {
        result.push_back(client);
    }
    return result;
}

// ============================================================
// getClientCount: số lượng client đang kết nối
// ============================================================
size_t Server::getClientCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_.size();
}

// ============================================================
// logToFile: ghi log ra console và file server.log
// Thread-safe (dùng logMutex_)
// ============================================================
void Server::logToFile(const std::string& prefix, const std::string& message) {
    // Lấy thời gian hiện tại
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&time));

    std::string logLine = std::string(timeBuf) + " " + prefix + " " + message;

    // In ra console
    std::cout << logLine << "\n";

    // Ghi ra file (thread-safe)
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logFile_.is_open()) {
        logFile_ << logLine << "\n";
        logFile_.flush();
    }
}
