# Mini Chat C++

Multi-threaded TCP chat application written in modern C++ using Winsock2 and `std::thread`.

Dự án mô phỏng hệ thống chat realtime nhiều người dùng, tập trung vào các kiến thức System Programming như:

- Socket Programming
- Multithreading
- Synchronization
- Concurrent Client Handling
- Custom Binary Protocol
- Resource Management

---

# Features

## Core Features

- Multi-client chat server
- Thread-per-client architecture
- Room-based chat
- Broadcast messaging
- Private messaging
- Online user list
- Nickname management
- Graceful disconnect & reconnect
- Server logging system

## Commands

| Command                     | Description                    |
| --------------------------- | ------------------------------ |
| `/users`                    | Hiển thị danh sách user online |
| `/join <room>`              | Tham gia room                  |
| `/msg <nickname> <message>` | Gửi tin nhắn riêng             |
| `/nick <new_name>`          | Đổi nickname                   |
| `/quit`                     | Thoát chương trình             |

---

# Tech Stack

| Component       | Technology                                 |
| --------------- | ------------------------------------------ |
| Language        | C++17                                      |
| Networking      | Winsock2                                   |
| Concurrency     | `std::thread`, `std::mutex`, `std::atomic` |
| Synchronization | Mutex + Condition Variable                 |
| Protocol        | Custom Binary Protocol                     |

---

# Architecture

```text
                    +-------------------+
                    |      SERVER       |
                    +-------------------+
                              |
             +----------------+----------------+
             |                                 |
     +---------------+               +---------------+
     | Client Thread |               | Client Thread |
     +---------------+               +---------------+
             |                                 |
             +---------------+-----------------+
                             |
                     Shared Clients List
                           (Mutex)
```

## Threading Model

### Server

- Main thread:
  - Accept incoming connections
  - Handle socket events

- Client thread:
  - One thread per connected client
  - Receive and process messages
  - Handle commands

### Client

- Main thread:
  - Read user input

- Receive thread:
  - Listen messages from server continuously

---

# Project Structure

```text
mini-chat-cpp/
│
├── server/
│   ├── main.cpp
│   ├── server.cpp
│   ├── server.h
│   ├── client_handler.cpp
│   └── client_handler.h
│
├── client/
│   ├── main.cpp
│   ├── client.cpp
│   └── client.h
│
├── common/
│   ├── protocol.h
│   ├── utils.h
│   ├── logger.h
│   └── socket_wrapper.h
│
└── README.md
```

---

# Build & Run

## Requirements

- Windows
- MinGW-w64 or MSYS2
- C++17 compatible compiler

---

## Compile Server

```bash
cd server

g++ main.cpp server.cpp client_handler.cpp ^
-o server.exe ^
-lws2_32 ^
-std=c++17
```

## Compile Client

```bash
cd client

g++ main.cpp client.cpp ^
-o client.exe ^
-lws2_32 ^
-std=c++17
```

---

# Run Application

## Start Server

```bash
.\server\server.exe
```

Custom port:

```bash
.\server\server.exe 9000
```

---

## Start Client

```bash
.\client\client.exe
```

Connect to custom IP/Port:

```bash
.\client\client.exe 127.0.0.1 9000
```

---

# Example

## Client 1

```text
Enter nickname: Alice

> /join coding
> Hello everyone!
> /msg Bob Hi Bob
```

## Client 2

```text
Enter nickname: Bob

> /join coding
> Hi Alice!
> /users
```

---

# Protocol Design

## Message Format

```text
+------------+------------+-------------+
| Type(1B)   | Length(4B) | Payload(N)  |
+------------+------------+-------------+
```

## Message Types

| Type      | Value | Description         |
| --------- | ----- | ------------------- |
| NORMAL    | 0     | Normal room message |
| SYSTEM    | 1     | System message      |
| PRIVATE   | 2     | Private message     |
| COMMAND   | 3     | Client command      |
| USER_LIST | 4     | Online users list   |

---

# Thread Safety

| Resource      | Protection                |
| ------------- | ------------------------- |
| Clients List  | `std::mutex`              |
| Logger        | `std::mutex`              |
| Running State | `std::atomic`             |
| Message Queue | `std::condition_variable` |

---

# System Programming Concepts

Dự án minh họa nhiều kiến thức quan trọng trong System Programming:

- TCP Socket Programming
- Non-blocking I/O
- Concurrent Programming
- Mutex Synchronization
- Atomic Variables
- Thread Management
- RAII Resource Cleanup
- Binary Protocol Design
- Partial Send/Receive Handling
- Graceful Shutdown

---

# Future Improvements

- TLS/SSL encryption
- File transfer
- SQLite database
- Cross-platform support (Linux)
- GUI client
- Unit testing
- Docker support
- Authentication system
- Message history
- Voice chat

---

# Learning Goals

Mục tiêu của dự án:

- Hiểu cách hoạt động của TCP networking
- Xây dựng ứng dụng realtime
- Thực hành multithreading trong C++
- Làm quen synchronization và race condition
- Thiết kế protocol giao tiếp client/server
- Áp dụng kiến thức System Programming vào project thực tế

---

# Author

Developed for learning and practicing modern C++ System Programming.
