// ============================================================
// common/socket_wrapper.h
// RAII wrapper cho Winsock SOCKET
//
// RAII = Resource Acquisition Is Initialization
// Nguyên tắc: tài nguyên được quản lý tự động qua destructor
// Khi đối tượng bị hủy (ra khỏi scope, exception, v.v.)
// destructor sẽ được gọi tự động -> đóng socket
//
// Ưu điểm so với raw SOCKET:
//
//   // Cách cũ (raw socket) - dễ quên closesocket
//   SOCKET s = socket(...);
//   if (error) return;
//   do_something(s);
//   closesocket(s);  // Quên dòng này = resource leak!
//
//   // Cách mới (RAII wrapper) - tự động
//   SocketWrapper s(socket(...));
//   if (!s) return;
//   do_something(s.get());  // Hoặc dùng trực tiếp như SOCKET
//   // closesocket() được gọi tự động khi s ra khỏi scope
// ============================================================

#ifndef SOCKET_WRAPPER_H
#define SOCKET_WRAPPER_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

// ============================================================
// SocketWrapper: RAII wrapper cho SOCKET
// ============================================================
class SocketWrapper {
public:
    // Constructor mặc định: tạo socket INVALID_SOCKET
    SocketWrapper() : socket_(INVALID_SOCKET) {}

    // Constructor nhận vào SOCKET đã tạo sẵn
    explicit SocketWrapper(SOCKET s) : socket_(s) {}

    // Move constructor: chuyển ownership từ wrapper khác
    SocketWrapper(SocketWrapper&& other) noexcept : socket_(other.socket_) {
        other.socket_ = INVALID_SOCKET;
    }

    // Move assignment: chuyển ownership
    SocketWrapper& operator=(SocketWrapper&& other) noexcept {
        if (this != &other) {
            close();
            socket_ = other.socket_;
            other.socket_ = INVALID_SOCKET;
        }
        return *this;
    }

    // Copy constructor/assignment: không cho phép (tránh double close)
    SocketWrapper(const SocketWrapper&)            = delete;
    SocketWrapper& operator=(const SocketWrapper&) = delete;

    // Destructor: tự động đóng socket
    // Đây là phần quan trọng nhất của RAII pattern
    ~SocketWrapper() { close(); }

    // Kiểm tra socket có hợp lệ không
    [[nodiscard]] bool isValid() const { return socket_ != INVALID_SOCKET; }

    // Operator bool: cho phép dùng trong if (!socket)
    explicit operator bool() const { return isValid(); }

    // Lấy raw SOCKET (để dùng với các Winsock API)
    [[nodiscard]] SOCKET get() const { return socket_; }

    // Lấy raw SOCKET (để dùng như SOCKET trực tiếp)
    [[nodiscard]] operator SOCKET() const { return socket_; }

    // Gán một SOCKET mới (đóng socket cũ nếu có)
    void reset(SOCKET s = INVALID_SOCKET) {
        close();
        socket_ = s;
    }

    // Đóng socket và giải phóng tài nguyên
    void close() {
        if (socket_ != INVALID_SOCKET) {
            // shutdown() thông báo cho phía kia biết là mình sẽ đóng
            // SD_SEND = không gửi nữa, nhưng vẫn nhận được
            // Giúp phía kia có thể gửi dữ liệu cuối trước khi đóng
            shutdown(socket_, SD_SEND);
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

    // Cập nhật socket (dùng nội bộ)
    void attach(SOCKET s) { socket_ = s; }

private:
    SOCKET socket_;
};

#endif // SOCKET_WRAPPER_H
