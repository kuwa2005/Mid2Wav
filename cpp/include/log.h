#pragma once
#include <iostream>
#include <sstream>

enum class LogLevel { Progress = 0, Info = 1, Debug = 2 };

namespace Log {

inline int g_maxLevel = 0;

inline void configure(bool verbose, bool debug) {
    if (debug) g_maxLevel = static_cast<int>(LogLevel::Debug);
    else if (verbose) g_maxLevel = static_cast<int>(LogLevel::Info);
    else g_maxLevel = static_cast<int>(LogLevel::Progress);
}

inline bool enabled(LogLevel level) {
    return static_cast<int>(level) <= g_maxLevel;
}

class Line {
public:
    explicit Line(LogLevel lvl, std::ostream& os = std::cout, bool always = false)
        : active_(always || enabled(lvl)), os_(os) {}
    ~Line() { if (active_ && !ss_.str().empty()) os_ << ss_.str() << '\n'; }
    template<typename T> Line& operator<<(const T& v) { if (active_) ss_ << v; return *this; }
    Line& operator<<(std::ostream& (*manip)(std::ostream&)) {
        if (active_) {
            os_ << ss_.str();
            ss_.str("");
            ss_.clear();
            manip(os_);
        }
        return *this;
    }
private:
    bool active_;
    std::ostream& os_;
    std::ostringstream ss_;
};

class Raw {
public:
    explicit Raw(LogLevel lvl, std::ostream& os = std::cout, bool always = false)
        : active_(always || enabled(lvl)), os_(os) {}
    template<typename T> Raw& operator<<(const T& v) { if (active_) os_ << v; return *this; }
    Raw& operator<<(std::ostream& (*manip)(std::ostream&)) {
        if (active_) manip(os_);
        return *this;
    }
private:
    bool active_;
    std::ostream& os_;
};

} // namespace Log

#define LOG_PROGRESS() ::Log::Line(::LogLevel::Progress)
#define LOG_INFO() ::Log::Line(::LogLevel::Info)
#define LOG_DEBUG() ::Log::Line(::LogLevel::Debug)
#define LOG_WARN() ::Log::Line(::LogLevel::Progress, std::cerr, true)
#define LOG_ERROR() ::Log::Line(::LogLevel::Progress, std::cerr, true)
#define LOG_RAW_PROGRESS() ::Log::Raw(::LogLevel::Progress)
