// ============================================================
// client/client.cpp
// Implementation của ChatClient
// ============================================================

#include "client.h"

// ============================================================
// Constructor
// ============================================================
ChatClient::ChatClient(const std::string& serverIP, uint16_t port)
    : serverIP_(serverIP)
    , serverPort_(port)
    , serverSocket_(INVALID_SOCKET)
    , nickname_("Guest")
    , connected_(false)
    , running_(false)
{
    LOG_INFO("ChatClient instance created");
}

// ============================================================
// Destructor
// ============================================================
ChatClient::~ChatClient() {
    disconnect();
}

// ============================================================
// connect: kết nối tới server
// ============================================================
bool ChatClient::connect(const std::string& nickname) {
    if (connected_.load()) {
        LOG_WARNING("Already connected to server");
        return true;
    }

    // 1. Khởi tạo Winsock (nếu chưa)
    WSADATA wsaData;
    static bool wsaInitialized = false;
    if (!wsaInitialized) {
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            LOG_ERROR("WSAStartup failed: error code {}", result);
            return false;
        }
        wsaInitialized = true;
        LOG_INFO("Winsock initialized");
    }

    // 2. Tạo socket
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == INVALID_SOCKET) {
        LOG_ERROR("socket() failed: {}", wsaErrorString(WSAGetLastError()));
        return false;
    }
    LOG_INFO("Socket created");

    // 3. Kết nối tới server
    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(serverIP_.c_str());
    serverAddr.sin_port        = htons(serverPort_);

    int result = ::connect(serverSocket_, (struct sockaddr*)&serverAddr,
                        sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        LOG_ERROR("connect() failed: {}", wsaErrorString(WSAGetLastError()));
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
        return false;
    }
    LOG_INFO("Connected to {}:{}", serverIP_, serverPort_);

    // 4. Đặt nickname
    if (!nickname.empty()) {
        nickname_ = nickname;
    }

    // 5. Bắt đầu receive thread
    connected_ = true;
    running_   = true;
    receiveThread_ = std::thread(&ChatClient::receiveThreadFunc, this);

    return true;
}

// ============================================================
// disconnect: ngắt kết nối
// ============================================================
void ChatClient::disconnect() {
    if (!connected_.load()) {
        return;
    }

    LOG_INFO("Disconnecting from server...");

    running_   = false;
    connected_ = false;

    // Đóng socket để unblock recv()
    if (serverSocket_ != INVALID_SOCKET) {
        shutdown(serverSocket_, SD_BOTH);
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }

    // Đợi receive thread kết thúc
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }

    LOG_INFO("Disconnected from server");
}

// ============================================================
// run: main loop - nhận input và gửi đi
// ============================================================
void ChatClient::run() {
    if (!connected_.load()) {
        LOG_ERROR("Not connected to server. Call connect() first.");
        return;
    }

    LOG_INFO("Chat client running. Type /help for commands.");

    while (running_.load() && connected_.load()) {
        // Hiển thị prompt
        std::cout << "[" << nickname_ << "]> " << std::flush;

        // Non-blocking check cho message queue
        {
            std::lock_guard<std::mutex> lock(messageQueueMutex_);
            while (!messageQueue_.empty()) {
                std::string msg = messageQueue_.front();
                messageQueue_.pop();
                std::cout << msg << "\n";
                std::cout << "[" << nickname_ << "]> " << std::flush;
            }
        }

        // Sử dụng select() để non-blocking stdin
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(STDIN_FILENO, &readSet);  // stdin = file descriptor 0

        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 100000;  // 100ms

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            LOG_ERROR("select() on stdin failed");
            break;
        }
        if (ready == 0) {
            // Timeout -> kiểm tra message queue và tiếp tục
            continue;
        }

        // Có input từ user
        std::string line;
        if (!std::getline(std::cin, line)) {
            // EOF hoặc lỗi
            LOG_INFO("EOF received, disconnecting...");
            break;
        }

        // Xử lý input rỗng
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        // Kiểm tra command
        if (isCommand(line)) {
            auto [cmd, args] = parseCommand(line);

            if (cmd == "quit" || cmd == "exit") {
                LOG_INFO("User requested quit");
                sendToServer(MessageType::COMMAND, "/quit");
                break;
            }

            if (cmd == "nick") {
                if (args.empty()) {
                    printMessage("Usage: /nick <new_nickname>");
                } else {
                    nickname_ = args;
                    printMessage("Nickname changed to: " + nickname_);
                }
                continue;  // Command không cần gửi đi
            }

            if (cmd == "help" || cmd == "?") {
                printMessage("Commands: /nick <name>, /list, /quit, /help");
                continue;
            }

            if (cmd == "list") {
                // Gửi command tới server
                sendToServer(MessageType::COMMAND, line);
                continue;
            }

            // Các command khác: gửi cho server xử lý
            sendToServer(MessageType::COMMAND, line);
        } else {
            // Tin nhắn thường: gửi tới server
            sendToServer(MessageType::NORMAL, line);
        }
    }

    disconnect();
}

// ============================================================
// receiveThreadFunc: thread nhận message từ server
//
// Chạy trong receiveThread_ riêng
// Nhận message từ server, cho vào queue để main thread hiển thị
// ============================================================
void ChatClient::receiveThreadFunc() {
    LOG_INFO("Receive thread started");

    while (running_.load() && connected_.load()) {
        // Non-blocking recv với select()
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket_, &readSet);

        struct timeval timeout;
        timeout.tv_sec  = 1;
        timeout.tv_usec = 0;

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            if (running_.load()) {
                LOG_ERROR("select() failed in receive thread: {}",
                         wsaErrorString(WSAGetLastError()));
            }
            break;
        }
        if (ready == 0) {
            // Timeout -> tiếp tục vòng lặp
            continue;
        }

        // Nhận message
        Message msg = receiveMessage(serverSocket_);
        if (msg.payload.empty() && msg.type == MessageType::NORMAL) {
            // Disconnect hoặc lỗi
            LOG_INFO("Server disconnected");
            break;
        }

        // Xử lý message
        handleIncomingMessage(msg);
    }

    connected_ = false;
    running_   = false;

    // Thông báo cho main thread
    printMessage("\n*** Disconnected from server ***\n");

    LOG_INFO("Receive thread ended");
}

// ============================================================
// sendToServer: gửi message tới server
// ============================================================
bool ChatClient::sendToServer(MessageType type, const std::string& text) {
    if (!connected_.load() || serverSocket_ == INVALID_SOCKET) {
        return false;
    }

    bool success = sendMessage(serverSocket_, type, text);
    if (!success) {
        LOG_ERROR("sendMessage failed");
        connected_ = false;
    }

    return success;
}

// ============================================================
// handleIncomingMessage: xử lý message nhận được
// ============================================================
void ChatClient::handleIncomingMessage(const Message& msg) {
    std::string output;

    switch (msg.type) {
        case MessageType::SYSTEM:
            output = "[SYSTEM] " + msg.payload;
            break;

        case MessageType::BROADCAST:
            output = msg.payload;
            break;

        case MessageType::NORMAL:
            output = msg.payload;
            break;

        case MessageType::NICKNAME:
            output = "[NICK] " + msg.payload;
            break;

        default:
            output = "[?] " + msg.payload;
            break;
    }

    // Cho vào queue để main thread hiển thị (thread-safe)
    {
        std::lock_guard<std::mutex> lock(messageQueueMutex_);
        messageQueue_.push(output);
    }
}

// ============================================================
// printMessage: in message ra console (thread-safe)
// ============================================================
void ChatClient::printMessage(const std::string& text) {
    std::lock_guard<std::mutex> lock(messageQueueMutex_);
    messageQueue_.push(text);
}
