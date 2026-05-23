// ============================================================
// server/server.h
// TCP Server chính, quản lý nhiều client
//
// Kiến trúc:
//   - Server lắng nghe kết nối trên 1 thread (main thread)
//   - Mỗi client được tạo trong 1 thread riêng (ClientHandler)
//   - Danh sách client được bảo vệ bởi mutex
//   - Không dùng detach() - tất cả thread được quản lý trong vector
//
// Thread safety:
//   - clients_: unordered_map<SOCKET, shared_ptr<ClientHandler>>
//   - Mọi truy cập đều qua clientsMutex
// ============================================================

#ifndef SERVER_H
#define SERVER_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include "../common/protocol.h"
#include "../common/logger.h"
#include "../common/utils.h"
#include "client_handler.h"

class Server;

// ============================================================
// Server: TCP server chính
// ============================================================
class Server {
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
    [[nodiscard]] bool       isRunning()  const { return running_.load(); }
    [[nodiscard]] uint16_t   getPort()    const { return port_; }
    [[nodiscard]] size_t     getClientCount() const;

private:
    // Chấp nhận một client mới
    void acceptClient();

    // Xử lý broadcast message
    //   sender : socket của người gửi (INVALID_SOCKET = từ server)
    //   type   : loại message
    //   text   : nội dung message
    //   exclude: socket bỏ qua (thường là sender)
    void broadcastToClients(SOCKET sender, MessageType type,
                            const std::string& text, SOCKET exclude);

    // Xóa client khỏi danh sách
    void unregisterClient(SOCKET socket);

    // Lấy danh sách client (để /list)
    std::vector<ClientInfo> getClientList() const;

    uint16_t                  port_;              // Cổng lắng nghe
    SOCKET                    serverSocket_;       // Socket chờ kết nối
    std::atomic<bool>         running_;           // Server đang chạy không

    // Danh sách client: socket -> ClientInfo
    // Dùng unordered_map thay vì vector để lookup O(1)
    mutable std::mutex        clientsMutex_;
    std::unordered_map<SOCKET, ClientInfo> clients_;
};

#endif // SERVER_H
