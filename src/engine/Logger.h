#pragma once

#include <string>
#include <format>
#include <source_location>

namespace myth {

enum class LogLevel { Debug, Info, Warning, Error, Fatal };

class Logger {
public:
    static void log(LogLevel level, const std::string& message, 
                    const std::source_location& loc = std::source_location::current());
    
    template<typename... Args>
    static void debugf(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    static void infof(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    static void warnf(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Warning, std::format(fmt, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    static void errorf(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    static void debug(const std::string& msg) { log(LogLevel::Debug, msg); }
    static void info(const std::string& msg) { log(LogLevel::Info, msg); }
    static void warn(const std::string& msg) { log(LogLevel::Warning, msg); }
    static void error(const std::string& msg) { log(LogLevel::Error, msg); }
    static void fatal(const std::string& msg) { log(LogLevel::Fatal, msg); }
};

} // namespace myth
