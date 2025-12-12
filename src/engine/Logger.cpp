#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace myth {

void Logger::log(LogLevel level, const std::string& message, const std::source_location& loc) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream timestamp;
    timestamp << std::put_time(&tm_buf, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    const char* levelStr = "UNKNOWN";
    int color = 7;
    
    switch (level) {
        case LogLevel::Debug:   levelStr = "DEBUG"; color = 8; break;
        case LogLevel::Info:    levelStr = "INFO";  color = 10; break;
        case LogLevel::Warning: levelStr = "WARN";  color = 14; break;
        case LogLevel::Error:   levelStr = "ERROR"; color = 12; break;
        case LogLevel::Fatal:   levelStr = "FATAL"; color = 12; break;
    }
    
    std::string filename = loc.file_name();
    auto pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) filename = filename.substr(pos + 1);

#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
#endif

    std::cout << "[" << timestamp.str() << "] [" << levelStr << "] " << message << std::endl;

#ifdef _WIN32
    SetConsoleTextAttribute(hConsole, 7);
#endif
}

} // namespace myth
