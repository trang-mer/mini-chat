// ============================================================
// common/protocol.h
// Định nghĩa protocol truyền tin giữa server và client
//
// Protocol: [1 byte type][4 bytes length (big-endian)][payload]
//
// PHASE 4 - Thêm:
//   - PRIVATE: tin nhắn riêng tư
//   - ROOM_JOIN: thông báo user join room
//   - ROOM_LEAVE: thông báo user rời room
//   - USER_LIST: danh sách user online
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
    constexpr int      MAX_ROOM_NAME_LEN = 32;
    constexpr char     DEFAULT_ROOM[]    = "general";
}

// ============================================================
// MessageType: các loại message trong protocol
// ============================================================
enum class MessageType : uint8_t {
    NORMAL     = 0,   // Tin nhắn thường (broadcast trong room)
    SYSTEM     = 1,   // Tin nhắn hệ thống (server -> client)
    PRIVATE    = 2,   // Tin nhắn riêng tư (sender -> target)
    COMMAND    = 3,   // Lệnh client -> server (/users, /join, /msg, /quit)
    ROOM_JOIN  = 4,   // Thông báo user join room
    ROOM_LEAVE = 5,   // Thông báo user rời room
    USER_LIST  = 6,   // Danh sách user online
};

// ============================================================
// MessageHeader: header của mỗi message
//   [MessageType: 1 byte][Length: 4 bytes][payload]
// ============================================================
#pragma pack(push, 1)
struct MessageHeader {
    MessageType type;
    uint32_t    length;   // độ dài payload (network byte order = big-endian)
};
#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 5, "MessageHeader must be exactly 5 bytes");

// ============================================================
// Message: cấu trúc message hoàn chỉnh
// ============================================================
struct Message {
    MessageType type;
    std::string payload;

    Message() : type(MessageType::NORMAL) {}
    Message(MessageType t, const std::string& p) : type(t), payload(p) {}
};

// ============================================================
// Helper functions: pack/unpack network messages
// ============================================================

// Chuyển uint32_t sang network byte order (big-endian)
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

// Đóng gói tin nhắn broadcast trong room
// Format hiển thị: [roomName] nickname: message
[[nodiscard]] inline std::string makeRoomBroadcast(const std::string& room,
                                                   const std::string& nickname,
                                                   const std::string& text) {
    return packMessage(MessageType::NORMAL, "[" + room + "] " + nickname + ": " + text);
}

// Đóng gói tin nhắn riêng tư
// Format hiển thị: [Private] senderNickname: message
[[nodiscard]] inline std::string makePrivateMessage(const std::string& senderNickname,
                                                     const std::string& text) {
    return packMessage(MessageType::PRIVATE, "[Private] " + senderNickname + ": " + text);
}

// Đóng gói thông báo join room
[[nodiscard]] inline std::string makeRoomJoinMessage(const std::string& nickname,
                                                      const std::string& room) {
    return packMessage(MessageType::ROOM_JOIN, nickname + " joined room: " + room);
}

// Đóng gói thông báo leave room
[[nodiscard]] inline std::string makeRoomLeaveMessage(const std::string& nickname,
                                                       const std::string& room) {
    return packMessage(MessageType::ROOM_LEAVE, nickname + " left room: " + room);
}

// Đóng gói danh sách user online
[[nodiscard]] inline std::string makeUserListMessage(const std::string& listText) {
    return packMessage(MessageType::USER_LIST, listText);
}

#endif // PROTOCOL_H
