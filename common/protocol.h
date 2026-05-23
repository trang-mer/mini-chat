// ============================================================
// common/protocol.h
// Định nghĩa protocol truyền tin giữa server và client
//
// Protocol: [uint32_t length (4 bytes, network byte order)][message bytes]
// - 4 bytes đầu: độ dài message (big-endian)
// - Tiếp theo: nội dung message (không null-terminated)
//
// Ví dụ: gửi "Hello" (5 bytes)
//   Byte 0-3: 0x00 0x00 0x00 0x05  (5 = 0x00000005)
//   Byte 4-8: 'H' 'e' 'l' 'l' 'o'
//
// Ưu điểm so với null-terminated:
// - Xử lý được binary data (có thể chứa '\0')
// - Biết chính xác bao nhiêu bytes cần đọc
// - Tránh buffer overflow
// ============================================================

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <string>

// ============================================================
// Config
// ============================================================
namespace Config {
    constexpr uint16_t DEFAULT_PORT     = 8080;
    constexpr int      RECV_BUFFER_SIZE = 8192;
    constexpr int      MAX_MESSAGE_SIZE = 4096;
    constexpr int      MAX_NICKNAME_LEN  = 32;
}

// ============================================================
// MessageType: các loại message trong protocol
// ============================================================
enum class MessageType : uint8_t {
    NORMAL       = 0,   // Tin nhắn thường (broadcast)
    SYSTEM       = 1,   // Tin nhắn hệ thống (server -> client)
    PRIVATE      = 2,   // Tin nhắn riêng tư (sender -> target)
    COMMAND      = 3,   // Lệnh client -> server (/nick, /quit, /list)
    NICKNAME     = 4,   // Thông báo đổi nickname
    BROADCAST    = 5,   // Server broadcast có prefix "[nickname]: "
};

// ============================================================
// MessageHeader: header của mỗi message
//   [MessageType: 1 byte][Length: 4 bytes][payload]
// ============================================================
struct MessageHeader {
    MessageType type;
    uint32_t    length;   // độ dài payload (network byte order = big-endian)

    // Đóng gói struct: không có padding giữa các field
    // Đảm bảo kích thước chính xác 5 bytes
} __attribute__((packed));

static_assert(sizeof(MessageHeader) == 5, "MessageHeader must be exactly 5 bytes");

// ============================================================
// Message: cấu trúc message hoàn chỉnh (header + payload)
// ============================================================
struct Message {
    MessageType type;
    std::string payload;   // nội dung message

    Message() : type(MessageType::NORMAL) {}
    Message(MessageType t, const std::string& p) : type(t), payload(p) {}
};

// ============================================================
// Helper functions: pack/unpack network messages
// ============================================================

// Chuyển uint32_t từ host byte order sang network byte order (big-endian)
[[nodiscard]] inline uint32_t htonl_safe(uint32_t hostlong) {
    return htonl(hostlong);
}

// Chuyển uint32_t từ network byte order sang host byte order
[[nodiscard]] inline uint32_t ntohl_safe(uint32_t netlong) {
    return ntohl(netlong);
}

// Đóng gói Message thành bytes để gửi qua socket
// Format: [1 byte type][4 bytes length][payload bytes]
[[nodiscard]] inline std::string packMessage(MessageType type, const std::string& payload) {
    std::string result;
    result.reserve(5 + payload.size());

    // Thêm type (1 byte)
    result.push_back(static_cast<char>(type));

    // Thêm length (4 bytes, big-endian)
    uint32_t len = htonl_safe(static_cast<uint32_t>(payload.size()));
    result.append(reinterpret_cast<const char*>(&len), 4);

    // Thêm payload
    result.append(payload);

    return result;
}

// Đóng gói tin nhắn thường (NORMAL)
[[nodiscard]] inline std::string makeNormalMessage(const std::string& text) {
    return packMessage(MessageType::NORMAL, text);
}

// Đóng gói tin nhắn hệ thống
[[nodiscard]] inline std::string makeSystemMessage(const std::string& text) {
    return packMessage(MessageType::SYSTEM, text);
}

// Đóng gói tin nhắn broadcast có prefix
[[nodiscard]] inline std::string makeBroadcastMessage(const std::string& nickname, const std::string& text) {
    return packMessage(MessageType::BROADCAST, nickname + ": " + text);
}

#endif // PROTOCOL_H
