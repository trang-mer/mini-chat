// ============================================================
// server/client_handler.h
// Xử lý kết nối của một client trong thread riêng
//
// Features:
//   - Nickname: user đặt nickname khi connect
//   - Rooms: user có thể /join room khác, tin nhắn chỉ gửi trong room
//   - Private message: /msg nickname message
//   - Online users: /users xem danh sách user đang online
//
// Kiến trúc:
//   - Mỗi ClientHandler chạy trong 1 thread riêng
//   - Server quản lý danh sách clients qua mutex
//   - ClientHandler gửi callback về Server khi cần broadcast/remove
// ============================================================

#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <string>
#include <memory>
#include <functional>
#include "../common/protocol.h"
#include "../common/logger.h"
#include "../common/utils.h"

// ============================================================
// ClientInfo: thông tin về một client đang kết nối
// Lưu trong Server::clients (dưới mutex protection)
// ============================================================
struct ClientInfo {
    SOCKET     socket;      // Socket kết nối
    std::string nickname;   // Tên người dùng
    std::string room;       // Room hiện tại (mặc định: "general")
    std::string ipAddress;  // Địa chỉ IP
    uint16_t   port;        // Cổng của client
    time_t     connectTime; // Thời điểm kết nối

    ClientInfo()
        : socket(INVALID_SOCKET), port(0), connectTime(0) {}

    ClientInfo(SOCKET s, const std::string& ip, uint16_t p, time_t t)
        : socket(s), nickname(), room(Config::DEFAULT_ROOM),
          ipAddress(ip), port(p), connectTime(t) {}
};

// ============================================================
// ServerBridge: bridge giữa ClientHandler và Server
// Dùng weak_ptr để tránh circular reference
// ============================================================
class ServerBridge {
public:
    explicit ServerBridge() = default;

    // Broadcast message tới tất cả client trong cùng room
    virtual void broadcastToRoom(SOCKET senderSocket,
                                const std::string& senderNickname,
                                const std::string& senderRoom,
                                const std::string& message) = 0;

    // Gửi tin nhắn riêng từ sender tới target
    virtual void sendPrivateMessage(SOCKET senderSocket,
                                   const std::string& senderNickname,
                                   const std::string& targetNickname,
                                   const std::string& message) = 0;

    // Gửi danh sách user online tới một client
    virtual void sendUserList(SOCKET clientSocket) = 0;

    // Gửi thông báo cho client khi join room thành công
    virtual void sendRoomJoined(SOCKET clientSocket,
                                const std::string& roomName) = 0;

    // Xóa client khỏi danh sách (khi disconnect)
    // NOTE: Server KHÔNG đóng socket - ClientHandler đã đóng trước
    virtual void removeClient(SOCKET socket,
                              const std::string& nickname) = 0;

    // Broadcast thông báo user joined room tới room
    virtual void broadcastRoomJoin(const std::string& nickname,
                                 const std::string& room) = 0;

    // Cập nhật room của client trong danh sách
    virtual void updateClientRoom(SOCKET socket,
                                  const std::string& newRoom) = 0;

    // Cập nhật nickname của client trong danh sách
    virtual void updateClientNickname(SOCKET socket,
                                     const std::string& newNickname) = 0;

    // Kiểm tra nickname đã tồn tại chưa
    virtual bool nicknameExists(const std::string& nickname,
                                SOCKET excludeSocket) const = 0;

    virtual ~ServerBridge() = default;
};

// ============================================================
// ClientHandler: xử lý 1 client trong thread riêng
//
// Mỗi ClientHandler:
//   - Nhận nickname ngay khi connect
//   - Nhận message từ client
//   - Xử lý command (/users, /join, /msg, /quit)
//   - Broadcast message trong room
// ============================================================
class ClientHandler
    : public std::enable_shared_from_this<ClientHandler> {
public:
    // Constructor
    //   socket: socket của client
    //   bridge: shared_ptr tới ServerBridge (chính là Server)
    ClientHandler(SOCKET socket, std::shared_ptr<ServerBridge> bridge);

    // Không cho copy
    ClientHandler(const ClientHandler&)            = delete;
    ClientHandler& operator=(const ClientHandler&) = delete;

    // Bắt đầu xử lý client (chạy trong thread riêng)
    void start();

    // Dừng xử lý client
    void stop();

    // Getters
    [[nodiscard]] SOCKET      getSocket()    const { return socket_; }
    [[nodiscard]] std::string getNickname()  const;
    [[nodiscard]] std::string getRoom()      const;
    [[nodiscard]] bool        hasNickname()  const;
    [[nodiscard]] bool        isRunning()    const;

private:
    // Vòng lặp nhận message từ client
    void run();

    // Bước 1: chờ user nhập nickname
    bool waitForNickname();

    // Xử lý command
    // Trả về: true = tiếp tục, false = ngắt kết nối
    bool handleCommand(const std::string& text);

    // Xử lý tin nhắn thường (broadcast trong room)
    void handleNormalMessage(const std::string& text);

    // Kiểm tra client còn kết nối không
    [[nodiscard]] bool isAlive() const;

    // Cleanup khi client disconnect
    // NOTE: Đóng socket TRƯỚC, rồi mới gọi removeClient
    void cleanupAndRemove();

    SOCKET                          socket_;
    std::weak_ptr<ServerBridge>     bridge_;
    std::string                     nickname_;
    std::string                     room_;
    std::atomic<bool>               running_;
    mutable std::mutex              stateMutex_;  // Bảo vệ nickname_ và room_
};

#endif // CLIENT_HANDLER_H
