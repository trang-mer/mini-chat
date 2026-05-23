// ============================================================
// server/server.h
// TCP Server chính, quản lý nhiều client
//
// Features:
//   - Nickname: mỗi client có nickname, server lưu trong ClientInfo
//   - Rooms: mỗi client thuộc 1 room, tin nhắn broadcast trong room
//   - Private message: gửi riêng từ sender tới target nickname
//   - Online users: /users trả danh sách user online
//   - Logging: ghi log ra server.log
//
// Kiến trúc:
//   - Server lắng nghe kết nối trên main thread
//   - Mỗi client được xử lý trong 1 thread riêng (ClientHandler)
//   - Danh sách client được bảo vệ bởi mutex
// ============================================================

#ifndef SERVER_H
#define SERVER_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <memory>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <thread>
#include "../common/protocol.h"
#include "../common/utils.h"
#include "client_handler.h"

// ============================================================
// Server: TCP server chính, implement ServerBridge
// ============================================================
class Server : public ServerBridge {
public:
    // Constructor
    //   port: cổng để lắng nghe (mặc định: 8080)
    explicit Server(uint16_t port = Config::DEFAULT_PORT);

    // Destructor: tự động cleanup
    ~Server();

    // Không cho copy
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // Khởi động server
    // Gọi: init() -> run()
    [[nodiscard]] bool init();

    // Chạy server (blocking)
    void run();

    // Dừng server (graceful shutdown)
    void shutdown();

    // Getters
    [[nodiscard]] bool     isRunning()    const { return running_.load(); }
    [[nodiscard]] uint16_t getPort()      const { return port_; }
    [[nodiscard]] size_t   getClientCount() const;

    // ============================================================
    // ServerBridge interface implementation
    // ============================================================

    // Gửi tin nhắn broadcast tới tất cả client trong cùng room
    void broadcastToRoom(SOCKET senderSocket,
                         const std::string& senderNickname,
                         const std::string& senderRoom,
                         const std::string& message) override;

    // Gửi tin nhắn riêng từ sender tới target
    void sendPrivateMessage(SOCKET senderSocket,
                            const std::string& senderNickname,
                            const std::string& targetNickname,
                            const std::string& message) override;

    // Gửi danh sách user online tới một client
    void sendUserList(SOCKET clientSocket) override;

    // Gửi thông báo cho client khi join room thành công
    void sendRoomJoined(SOCKET clientSocket, const std::string& roomName) override;

    // Xóa client khỏi danh sách (khi disconnect)
    // NOTE: ClientHandler KHÔNG đóng socket ở đây, chỉ xóa khỏi list
    void removeClient(SOCKET socket, const std::string& nickname) override;

    // Broadcast thông báo user joined room tới room
    void broadcastRoomJoin(const std::string& nickname, const std::string& room) override;

    // Cập nhật room của client trong danh sách (thread-safe)
    void updateClientRoom(SOCKET socket, const std::string& newRoom) override;

    // Cập nhật nickname của client trong danh sách (thread-safe)
    void updateClientNickname(SOCKET socket, const std::string& newNickname) override;

    // Kiểm tra nickname đã tồn tại chưa
    bool nicknameExists(const std::string& nickname, SOCKET excludeSocket) const override;

    // ============================================================
    // Logging helpers
    // ============================================================
    void logToFile(const std::string& prefix, const std::string& message);

private:
    // Chấp nhận một client mới
    void acceptClient();

    // Lấy danh sách client (dùng nội bộ)
    std::vector<ClientInfo> getClientList_() const;

    uint16_t                port_;           // Cổng lắng nghe
    SOCKET                  serverSocket_;    // Socket chờ kết nối
    std::atomic<bool>       running_;         // Server đang chạy không

    // Danh sách client: socket -> ClientInfo
    mutable std::mutex       clientsMutex_;
    std::vector<ClientInfo> clients_;

    // Mutex cho logging (bảo vệ log file từ nhiều thread)
    std::mutex              logMutex_;
    std::ofstream           logFile_;
};

#endif // SERVER_H
