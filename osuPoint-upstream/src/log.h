#pragma once
#include <cstdint>

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

void ous_log(LogLevel level, const char* tag, const char* fmt, ...);
