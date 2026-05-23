// ============================================================
// common/logger.h
// Logger đơn giản, thread-safe, có thể log ra console và file
//
// Các mức log:
//   QUIET   - Tắt log hoàn toàn
//   DBG     - Thông tin chi tiết, chỉ khi cần debug
//   INF     - Thông tin thông thường
//   WRN     - Cảnh báo, có thể có vấn đề
//   ERR     - Lỗi đã xảy ra
//   FAT     - Lỗi nghiêm trọng, chương trình sẽ dừng
//
// Cách dùng:
//   LOG_INFO("User {} joined", username);
//   LOG_ERROR("Failed to connect: {}", WSAGetLastError());
//
// Lưu ý: ERROR trong LogLevel đổi thành ERR để tránh trùng với
// Windows macro ERROR (defined in winerror.h / windows.h)
// ============================================================

#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <sstream>
#include <mutex>
#include <iomanip>
#include <chrono>
#include <ctime>

// ============================================================
// LogLevel: các mức log
// Lưu ý: không dùng tên "ERROR" vì trùng với Windows macro ERROR
// ============================================================
enum class LogLevel {
    QUIET  = 0,   // Tắt log hoàn toàn
    DBG    = 1,   // Debug
    INF    = 2,   // Info
    WRN    = 3,   // Warning
    ERR    = 4,   // Error
    FAT    = 5,   // Fatal
};

// ============================================================
// Logger: singleton logger
// ============================================================
class Logger {
public:
    // Lấy instance duy nhất (singleton)
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    // Thiết lập mức log tối thiểu
    void setLevel(LogLevel level) { minLevel_ = level; }

    // Bật/tắt timestamp
    void setTimestamp(bool enable) { showTimestamp_ = enable; }

    // Bật/tắt log ra file
    void setFile(const std::string& filename);

    // ============================================================
    // Instance methods: gọi thông qua instance()
    // Dùng cho trường hợp muốn ghi log từ bên ngoài class
    // ============================================================
    template<typename... Args>
    void debug(const std::string& fmt, Args&&... args) {
        log(LogLevel::DBG, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(const std::string& fmt, Args&&... args) {
        log(LogLevel::INF, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warning(const std::string& fmt, Args&&... args) {
        log(LogLevel::WRN, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(const std::string& fmt, Args&&... args) {
        log(LogLevel::ERR, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void fatal(const std::string& fmt, Args&&... args) {
        log(LogLevel::FAT, fmt, std::forward<Args>(args)...);
    }

private:
    Logger() : minLevel_(LogLevel::INF), showTimestamp_(true), fileStream_(nullptr) {}

    ~Logger() {
        if (fileStream_) {
            fflush(fileStream_);
            fclose(fileStream_);
        }
    }

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    // Lấy chuỗi mức log
    [[nodiscard]] static const char* levelString(LogLevel level) {
        switch (level) {
            case LogLevel::DBG:  return "DBG ";
            case LogLevel::INF:  return "INF ";
            case LogLevel::WRN:  return "WRN ";
            case LogLevel::ERR:  return "ERR ";
            case LogLevel::FAT:  return "FAT ";
            default:             return "????";
        }
    }

    // Lấy thời gian hiện tại dạng chuỗi
    [[nodiscard]] std::string currentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    // Format message đơn giản: thay {} bằng argument
    // Ví dụ: format("Hello {}", "world") -> "Hello world"
    [[nodiscard]] static std::string format(const std::string& fmt) {
        return fmt;
    }

    template<typename T, typename... Args>
    [[nodiscard]] static std::string format(const std::string& fmt, T&& value, Args&&... args) {
        size_t pos = fmt.find("{}");
        if (pos == std::string::npos) {
            return fmt;
        }
        std::ostringstream oss;
        oss << fmt.substr(0, pos) << value;
        oss << format(fmt.substr(pos + 2), std::forward<Args>(args)...);
        return oss.str();
    }

    // Log thực sự
    template<typename... Args>
    void log(LogLevel level, const std::string& fmt, Args&&... args) {
        if (static_cast<int>(level) < static_cast<int>(minLevel_)) {
            return;
        }

        std::ostringstream oss;
        if (showTimestamp_) {
            oss << '[' << currentTimestamp() << "] ";
        }
        oss << '[' << levelString(level) << "] ";
        oss << format(fmt, std::forward<Args>(args)...);

        std::string line = oss.str();

        std::lock_guard<std::mutex> lock(mutex_);

        // In ra console (stderr cho error/fatal, stdout cho others)
        if (level >= LogLevel::ERR) {
            std::cerr << line << '\n' << std::flush;
        } else {
            std::cout << line << '\n' << std::flush;
        }

        // Ghi ra file nếu có
        if (fileStream_) {
            fprintf(fileStream_, "%s\n", line.c_str());
        }
    }

    LogLevel    minLevel_;
    bool        showTimestamp_;
    FILE*       fileStream_;
    std::mutex  mutex_;
};

// ============================================================
// Macros: gọi nhanh Logger::instance().xxx(...)
// Cho phép viết: LOG_INFO("message {}", arg)
// Thay vì:     Logger::instance().info("message {}", arg)
// ============================================================
#define LOG_DEBUG   Logger::instance().debug
#define LOG_INFO    Logger::instance().info
#define LOG_WARNING Logger::instance().warning
#define LOG_ERROR   Logger::instance().error
#define LOG_FATAL   Logger::instance().fatal

#endif // LOGGER_H
