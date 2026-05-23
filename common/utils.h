// ============================================================
// common/utils.h
// Các hàm utility dùng chung cho server và client
//
// Bao gồm:
//   - Network helpers (send/recv an toàn)
//   - String helpers (trim, split)
//   - Time helpers
// ============================================================

#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <ctime>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "protocol.h"

// ============================================================
// Network helpers
// ============================================================

// Gửi đầy đủ N bytes qua socket (khắc phục partial send)
// send() có thể gửi ÍT hơn số bytes yêu cầu
// Hàm này đảm bảo GỬI HẾT hoặc báo lỗi
// Trả về: số bytes đã gửi, hoặc SOCKET_ERROR
[[nodiscard]] inline int sendAll(SOCKET s, const char* data, int len) {
    int totalSent = 0;
    while (totalSent < len) {
        int sent = send(s, data + totalSent, len - totalSent, 0);
        if (sent == SOCKET_ERROR) return SOCKET_ERROR;
        totalSent += sent;
    }
    return totalSent;
}

// Gửi message theo protocol [length][payload]
// Đảm bảo cả header + payload được gửi đầy đủ
// Trả về: true nếu gửi thành công
inline bool sendMessage(SOCKET s, MessageType type, const std::string& payload) {
    std::string packed = packMessage(type, payload);
    int sent = sendAll(s, packed.data(), static_cast<int>(packed.size()));
    return sent != SOCKET_ERROR;
}

// Nhận đầy đủ N bytes từ socket (khắc phục partial recv)
// recv() có thể nhận ÍT hơn số bytes yêu cầu
// Hàm này đảm bảo NHẬN ĐỦ hoặc báo lỗi/disconnect
// Trả về: số bytes đã nhận, 0 = disconnect, SOCKET_ERROR = lỗi
[[nodiscard]] inline int recvAll(SOCKET s, char* buffer, int len) {
    int totalRecv = 0;
    while (totalRecv < len) {
        int recved = recv(s, buffer + totalRecv, len - totalRecv, 0);
        if (recved == SOCKET_ERROR) return SOCKET_ERROR;
        if (recved == 0) break;  // Client đóng kết nối
        totalRecv += recved;
    }
    return totalRecv;
}

// Nhận 1 message theo protocol
// Bước 1: đọc header (5 bytes: 1 type + 4 length)
// Bước 2: đọc payload (length bytes)
// Trả về: Message rỗng nếu lỗi/disconnect
[[nodiscard]] inline Message receiveMessage(SOCKET s) {
    Message result;

    // Bước 1: Đọc header (5 bytes)
    char headerBuf[sizeof(MessageHeader)];
    int headerLen = recvAll(s, headerBuf, static_cast<int>(sizeof(headerBuf)));
    if (headerLen != static_cast<int>(sizeof(headerBuf))) {
        return result;  // Lỗi hoặc disconnect
    }

    // Parse header
    result.type = static_cast<MessageType>(headerBuf[0]);
    uint32_t length = ntohl_safe(
        *reinterpret_cast<uint32_t*>(&headerBuf[1])
    );

    // Sanity check
    if (length == 0 || length > Config::MAX_MESSAGE_SIZE) {
        return result;
    }

    // Bước 2: Đọc payload
    std::string payload(length, '\0');
    int payloadLen = recvAll(s, &payload[0], static_cast<int>(length));
    if (payloadLen != static_cast<int>(length)) {
        return result;  // Lỗi hoặc disconnect
    }

    result.payload = std::move(payload);
    return result;
}

// ============================================================
// String helpers
// ============================================================

// Trim whitespace ở đầu và cuối chuỗi
[[nodiscard]] inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Tách chuỗi theo delimiter
[[nodiscard]] inline std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Chuyển chuỗi thành chữ thường
[[nodiscard]] inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ============================================================
// Time helpers
// ============================================================

// Lấy thời gian hiện tại dạng chuỗi
[[nodiscard]] inline std::string currentTimeString() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm   = *std::localtime(&time);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return std::string(buf);
}

// ============================================================
// Winsock helpers
// ============================================================

// Lấy mô tả lỗi Winsock
[[nodiscard]] inline std::string wsaErrorString(int errorCode) {
    char* msgBuf = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(errorCode),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msgBuf),
        0,
        nullptr
    );
    std::string result;
    if (msgBuf) {
        result = msgBuf;
        LocalFree(msgBuf);
        while (!result.empty()
               && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    } else {
        result = "Unknown error";
    }
    return result;
}

// Kiểm tra xem có phải command không (bắt đầu bằng '/')
[[nodiscard]] inline bool isCommand(const std::string& text) {
    return !text.empty() && text[0] == '/';
}

// Parse command: "/nick Alice" -> {"nick", "Alice"}
[[nodiscard]] inline std::pair<std::string, std::string> parseCommand(
    const std::string& text) {
    std::string trimmed = trim(text);
    if (trimmed.empty() || trimmed[0] != '/') {
        return {"", ""};
    }
    size_t spacePos = trimmed.find(' ');
    if (spacePos == std::string::npos) {
        return {toLower(trimmed.substr(1)), ""};
    }
    std::string cmd  = toLower(trimmed.substr(1, spacePos - 1));
    std::string args = trim(trimmed.substr(spacePos + 1));
    return {cmd, args};
}

#endif // UTILS_H
