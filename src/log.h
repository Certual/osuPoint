#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>

#ifdef ERROR
#undef ERROR
#endif

enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG = 1,
    INFO  = 2,
    WARN  = 3,
    ERROR = 4
};

inline void ous_log(LogLevel /*level*/, const char* tag, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("[%s] %s\n", tag, buf);
}
