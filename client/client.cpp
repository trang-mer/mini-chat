// ============================================================
// client/client.cpp
// Implementation của ChatClient
//
// Features:
//   - receive thread nhận message liên tục, cho vào queue
//   - main thread nhập message và gửi lên server
//   - Hỗ trợ /users, /join, /msg, /quit, /nick
// ============================================================

#include "client.h"

// ============================================================
// Constructor
// ============================================================
ChatClient::ChatClient(const std::string& serverIP, uint16_t port)
    : serverIP_(serverIP)
    , serverPort_(port)
    , serverSocket_(INVALID_SOCKET)
    , connected_(false)
    , running_(false)
{
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
bool ChatClient::connect() {
    if (connected_.load()) {
        return true;
    }

    // 1. Khởi tạo Winsock (static để tránh init nhiều lần)
    WSADATA wsaData;
    static bool wsaInitialized = false;
    if (!wsaInitialized) {
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "[ERROR] WSAStartup failed: error code " << result << "\n";
            return false;
        }
        wsaInitialized = true;
    }

    // 2. Tạo socket
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == INVALID_SOCKET) {
        std::cerr << "[ERROR] socket() failed: " << wsaErrorString(WSAGetLastError()) << "\n";
        return false;
    }

    // 3. Kết nối tới server
    struct sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(serverIP_.c_str());
    serverAddr.sin_port        = htons(serverPort_);

    int result = ::connect(serverSocket_, (struct sockaddr*)&serverAddr,
                        sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        std::cerr << "[ERROR] connect() failed: " << wsaErrorString(WSAGetLastError()) << "\n";
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
        return false;
    }

    // 4. Bắt đầu receive thread
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

    running_   = false;
    connected_ = false;

    if (serverSocket_ != INVALID_SOCKET) {
        shutdown(serverSocket_, SD_BOTH);
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

// ============================================================
// run: main loop - nhận input và gửi đi
// ============================================================
void ChatClient::run() {
    if (!connected_.load()) {
        std::cerr << "[ERROR] Not connected to server. Call connect() first.\n";
        return;
    }

    while (running_.load() && connected_.load()) {
        // Hiển thị message từ queue trước
        {
            std::string msg;
            while (popMessage(msg)) {
                std::cout << msg << "\n";
            }
        }

        // Hiển thị prompt
        std::cout << "> " << std::flush;

        // Đọc input từ user (blocking với timeout ngắn)
        std::string line;
        bool hasInput = false;

        // Non-blocking check với select() cho stdin
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(STDIN_FILENO, &readSet);

        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 100000;  // 100ms

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready > 0) {
            if (std::getline(std::cin, line)) {
                hasInput = true;
            }
        }

        // Nếu không có input, kiểm tra xem có message nào không
        if (!hasInput) {
            // Tiếp tục vòng lặp để hiển thị message
            continue;
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        // Kiểm tra command
        if (isCommand(line)) {
            auto [cmd, args] = parseCommand(line);

            if (cmd == "quit" || cmd == "exit" || cmd == "q") {
                sendToServer(MessageType::COMMAND, "/quit");
                break;
            }

            if (cmd == "users" || cmd == "list") {
                sendToServer(MessageType::COMMAND, "/users");
                continue;
            }

            if (cmd == "join") {
                if (args.empty()) {
                    std::cout << "[SYSTEM] Usage: /join <roomName>\n";
                } else {
                    sendToServer(MessageType::COMMAND, "/join " + args);
                }
                continue;
            }

            if (cmd == "msg" || cmd == "dm" || cmd == "w") {
                if (args.empty()) {
                    std::cout << "[SYSTEM] Usage: /msg <nickname> <message>\n";
                } else {
                    sendToServer(MessageType::COMMAND, "/msg " + args);
                }
                continue;
            }

            if (cmd == "nick" || cmd == "rename") {
                if (args.empty()) {
                    std::cout << "[SYSTEM] Usage: /nick <newNickname>\n";
                } else {
                    sendToServer(MessageType::COMMAND, "/nick " + args);
                }
                continue;
            }

            if (cmd == "help" || cmd == "?") {
                std::cout << "Commands:\n"
                          << "  /users           - List online users\n"
                          << "  /join <room>    - Join a room\n"
                          << "  /msg <n> <msg> - Send private message\n"
                          << "  /nick <name>   - Change your nickname\n"
                          << "  /quit           - Disconnect\n";
                continue;
            }

            // Các command khác -> gửi cho server xử lý
            sendToServer(MessageType::COMMAND, line);
        } else {
            // Tin nhắn thường -> broadcast trong room
            sendToServer(MessageType::NORMAL, line);
        }
    }

    disconnect();
}

// ============================================================
// receiveThreadFunc: thread nhận message từ server
// ============================================================
void ChatClient::receiveThreadFunc() {
    while (running_.load() && connected_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket_, &readSet);

        struct timeval timeout;
        timeout.tv_sec  = 1;
        timeout.tv_usec = 0;

        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            break;
        }
        if (ready == 0) {
            continue;
        }

        Message msg = receiveMessage(serverSocket_);
        if (msg.payload.empty() && msg.type == MessageType::NORMAL) {
            // Disconnect
            break;
        }

        handleIncomingMessage(msg);
    }

    connected_ = false;
    running_   = false;

    // Thông báo disconnect
    {
        std::lock_guard<std::mutex> lock(messageQueueMutex_);
        messageQueue_.push("\n*** Disconnected from server ***\n");
    }
    messageQueueCV_.notify_one();
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
        case MessageType::NORMAL:
        case MessageType::PRIVATE:
        case MessageType::ROOM_JOIN:
        case MessageType::ROOM_LEAVE:
            output = msg.payload;
            break;

        case MessageType::USER_LIST:
            output = "\n" + msg.payload + "\n";
            break;

        default:
            output = msg.payload;
            break;
    }

    // Cho vào queue để main thread hiển thị
    {
        std::lock_guard<std::mutex> lock(messageQueueMutex_);
        messageQueue_.push(output);
    }
    messageQueueCV_.notify_one();

    // Gọi callback nếu có
    if (messageCallback_) {
        messageCallback_(output);
    }
}

// ============================================================
// waitForMessage: đợi message trong queue (blocking)
// ============================================================
bool ChatClient::waitForMessage(std::string& outMsg, int timeoutMs) {
    std::unique_lock<std::mutex> lock(messageQueueMutex_);

    bool ok = messageQueueCV_.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMs),
        [this] { return !messageQueue_.empty() || !running_.load(); }
    );

    if (ok && !messageQueue_.empty()) {
        outMsg = messageQueue_.front();
        messageQueue_.pop();
        return true;
    }
    return false;
}

// ============================================================
// hasMessage: kiểm tra queue có message không (non-blocking)
// ============================================================
bool ChatClient::hasMessage() {
    std::lock_guard<std::mutex> lock(messageQueueMutex_);
    return !messageQueue_.empty();
}

// ============================================================
// popMessage: lấy 1 message từ queue (non-blocking)
// ============================================================
bool ChatClient::popMessage(std::string& outMsg) {
    std::lock_guard<std::mutex> lock(messageQueueMutex_);
    if (messageQueue_.empty()) {
        return false;
    }
    outMsg = messageQueue_.front();
    messageQueue_.pop();
    return true;
}

// ============================================================
// setMessageCallback: đăng ký callback
// ============================================================
void ChatClient::setMessageCallback(std::function<void(const std::string&)> cb) {
    messageCallback_ = std::move(cb);
}
