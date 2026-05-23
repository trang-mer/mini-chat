// ============================================================
// server/server.cpp
// Implementation của Server
// ============================================================

#include "server.h"
#include <thread>

// ============================================================
// Constructor
// ============================================================
Server::Server(uint16_t port)
    : port_(port)
    , serverSocket_(INVALID_SOCKET)
    , running_(false)
{
    LOG_INFO("Server instance created (port: {})", port_);
}

// ============================================================
// Destructor: graceful shutdown
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
        LOG_ERROR("WSAStartup failed: error code {}", result);
        return false;
    }
    LOG_INFO("Winsock initialized (version 2.2)");

    // 2. Tạo server socket
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == INVALID_SOCKET) {
        LOG_ERROR("socket() failed: {}", wsaErrorString(WSAGetLastError()));
        WSACleanup();
        return false;
    }
    LOG_INFO("Server socket created");

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
        LOG_ERROR("bind() failed: {}", wsaErrorString(WSAGetLastError()));
        closesocket(serverSocket_);
        WSACleanup();
        return false;
    }
    LOG_INFO("Socket bound to port {}", port_);

    // 5. Bắt đầu lắng nghe
    result = listen(serverSocket_, SOMAXCONN);
    if (result == SOCKET_ERROR) {
        LOG_ERROR("listen() failed: {}", wsaErrorString(WSAGetLastError()));
        closesocket(serverSocket_);
        WSACleanup();
        return false;
    }
    LOG_INFO("Server listening on port {}...", port_);

    running_ = true;
    return true;
}

// ============================================================
// run: vòng lặp chính của server
// ============================================================
void Server::run() {
    if (!running_.load()) {
        LOG_ERROR("Server not initialized. Call init() first.");
        return;
    }

    LOG_INFO("Server is running. Press Ctrl+C to stop.");

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
            LOG_ERROR("select() failed: {}", wsaErrorString(WSAGetLastError()));
            break;
        }
        if (ready == 0) {
            // Timeout -> tiếp tục kiểm tra running_
            continue;
        }

        // Có kết nối mới -> accept
        acceptClient();
    }

    LOG_INFO("Server run loop ended.");
}

// ============================================================
// shutdown: dừng server gracefully
// ============================================================
void Server::shutdown() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO("Shutting down server...");

    running_ = false;

    // Đóng server socket để unblock accept()
    if (serverSocket_ != INVALID_SOCKET) {
        ::shutdown(serverSocket_, SD_BOTH);
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }

    // Xóa tất cả client
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& [socket, info] : clients_) {
            if (socket != INVALID_SOCKET) {
                closesocket(socket);
            }
        }
        clients_.clear();
    }

    WSACleanup();
    LOG_INFO("Server shutdown complete.");
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
            LOG_ERROR("accept() failed: {}", wsaErrorString(WSAGetLastError()));
        }
        return;
    }

    // Lấy thông tin client
    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
    uint16_t port = ntohs(clientAddr.sin_port);

    LOG_INFO("New connection from {}:{}", ipBuf, port);

    // Tạo ClientInfo
    ClientInfo info;
    info.socket     = clientSocket;
    info.ipAddress  = ipBuf;
    info.port        = port;
    info.connectTime = time(nullptr);
    info.nickname    = std::string(ipBuf) + ":" + std::to_string(port);

    // Thêm vào danh sách (mutex)
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_[clientSocket] = info;
    }

    // Tạo callbacks cho ClientHandler
    auto broadcastCb = [this](SOCKET sender, MessageType type,
                               const std::string& text, SOCKET exclude) {
        broadcastToClients(sender, type, text, exclude);
    };

    auto unregisterCb = [this](SOCKET socket) {
        unregisterClient(socket);
    };

    auto listCb = [this]() -> std::vector<ClientInfo> {
        return getClientList();
    };

    // Tạo ClientHandler trong shared_ptr để quản lý vòng đời
    auto handler = std::make_shared<ClientHandler>(
        clientSocket, broadcastCb, unregisterCb, listCb
    );

    // Tạo thread để chạy handler
    // Lưu thread trong map để quản lý (thay vì detach)
    // Thread chạy handler->start() và tự cleanup khi kết thúc
    std::thread([handler]() {
        handler->start();
    }).detach();

    LOG_INFO("Client {} connected. Total clients: {}",
             info.nickname, clients_.size());
}

// ============================================================
// broadcastToClients: gửi message cho tất cả client
//
// Thread safety:
//   1. Lock mutex -> copy danh sách sockets -> unlock
//   2. Gửi message cho từng socket (không mutex)
//   3. Nếu gửi thất bại -> unregister
//
// Lý do copy trước:
//   - Tránh giữ mutex quá lâu (chỉ giữ khi copy)
//   - Gửi message có thể lâu, không nên giữ mutex
//   - Nếu unregister xảy ra trong lúc gửi, không ảnh hưởng
//     (chúng ta chỉ gửi đến socket cũ, không lỗi)
// ============================================================
void Server::broadcastToClients(SOCKET sender, MessageType type,
                               const std::string& text, SOCKET exclude) {
    (void)sender;  // reserved for future use (e.g., logging sender info)
    // Copy danh sách sockets dưới mutex
    std::vector<SOCKET> targetSockets;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& [socket, info] : clients_) {
            if (socket != INVALID_SOCKET && socket != exclude) {
                targetSockets.push_back(socket);
            }
        }
    }
    // Mutex đã unlock ở đây -> có thể gửi message

    // Gửi cho từng client (không mutex)
    for (SOCKET targetSocket : targetSockets) {
        bool success = sendMessage(targetSocket, type, text);
        if (!success) {
            LOG_WARNING("Failed to send to client, removing...");
            unregisterClient(targetSocket);
        }
    }
}

// ============================================================
// unregisterClient: xóa client khỏi danh sách
// ============================================================
void Server::unregisterClient(SOCKET socket) {
    std::string nickname;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(socket);
        if (it != clients_.end()) {
            nickname = it->second.nickname;
            clients_.erase(it);
        }
    }

    if (!nickname.empty()) {
        closesocket(socket);
        LOG_INFO("Client {} disconnected. Total clients: {}",
                 nickname, clients_.size());
    }
}

// ============================================================
// getClientList: lấy danh sách client (cho /list)
// ============================================================
std::vector<ClientInfo> Server::getClientList() const {
    std::vector<ClientInfo> result;
    std::lock_guard<std::mutex> lock(clientsMutex_);
    result.reserve(clients_.size());
    for (const auto& [socket, info] : clients_) {
        result.push_back(info);
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
