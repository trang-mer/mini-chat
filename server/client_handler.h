// ============================================================
// server/client_handler.h
// Xử lý kết nối của một client trong thread riêng
//
// Mỗi ClientHandler quản lý 1 client:
//   - Nhận message từ client
//   - Xử lý command (/nick, /list, /quit)
//   - Broadcast message cho các client khác
//   - Tự động cleanup khi client disconnect
//
// Lưu ý về thread safety:
//   - ClientHandler được tạo trong 1 thread riêng
//   - Server gọi register/unregister để quản lý danh sách
//   - Tất cả thao tác trên clients map đều qua mutex
// ============================================================

#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <memory>
#include <functional>
#include "../common/protocol.h"
#include "../common/logger.h"
#include "../common/utils.h"

// ============================================================
// ClientInfo: thông tin về một client
// ============================================================
struct ClientInfo {
    SOCKET         socket;      // Socket kết nối
    std::string    nickname;    // Tên người dùng (mặc định: IP:port)
    std::string    ipAddress;   // Địa chỉ IP
    uint16_t       port;       // Cổng của client
    time_t         connectTime; // Thời điểm kết nối

    ClientInfo() : socket(INVALID_SOCKET), port(0), connectTime(0) {}

    ClientInfo(SOCKET s, const std::string& ip, uint16_t p, time_t t)
        : socket(s), ipAddress(ip), port(p), connectTime(t) {
        // Tạo nickname mặc định từ IP:port
        nickname = ip + ":" + std::to_string(p);
    }
};

// ============================================================
// ClientHandler: xử lý 1 client trong thread riêng
//
// Đặc điểm:
//   - Kế thừa std::enable_shared_from_this để có thể share con trỏ
//   - Chạy trong thread riêng
//   - Callback để thông báo cho Server khi cần register/unregister
// ============================================================
class ClientHandler
    : public std::enable_shared_from_this<ClientHandler> {
public:
    // Callback types
    using BroadcastCallback  = std::function<void(SOCKET, MessageType, const std::string&, SOCKET)>;
    using UnregisterCallback = std::function<void(SOCKET)>;
    using ListCallback       = std::function<std::vector<ClientInfo>()>;

    // Constructor
    //   socket    : socket của client
    //   broadcast : callback để broadcast message
    //   unregister: callback để xóa client khỏi server
    //   list      : callback để lấy danh sách client
    ClientHandler(
        SOCKET                 socket,
        BroadcastCallback      broadcast,
        UnregisterCallback     unregister,
        ListCallback           list
    );

    // Không cho copy
    ClientHandler(const ClientHandler&)            = delete;
    ClientHandler& operator=(const ClientHandler&) = delete;

    // Bắt đầu xử lý client (chạy trong thread riêng)
    void start();

    // Dừng xử lý client (gọi từ thread khác)
    void stop();

    // Getter
    [[nodiscard]] SOCKET        getSocket()  const { return socket_; }
    [[nodiscard]] std::string   getNickname() const { return nickname_; }
    [[nodiscard]] std::string getIPAddress() const { return ip_; }
    [[nodiscard]] time_t         getConnectTime() const { return connectTime_; }

    // Setter nickname
    void setNickname(const std::string& name);

private:
    // Vòng lặp nhận message từ client
    void run();

    // Xử lý command (/nick, /list, /quit, /help)
    // Trả về: true = tiếp tục, false = ngắt kết nối
    bool handleCommand(const std::string& text);

    // Xử lý tin nhắn thường (broadcast)
    void handleNormalMessage(const std::string& text);

    // Kiểm tra client còn sống (non-blocking check)
    bool isAlive() const;

    SOCKET             socket_;
    std::string        nickname_;
    std::string        ip_;
    uint16_t           port_;
    time_t             connectTime_;
    bool               running_;
    BroadcastCallback  broadcastCallback_;
    UnregisterCallback unregisterCallback_;
    ListCallback       listCallback_;
};

#endif // CLIENT_HANDLER_H
