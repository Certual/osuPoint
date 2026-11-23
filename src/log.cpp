#include "log.h"
#include <cstdio>
#include <cstdarg>
#include <mutex>

static std::mutex s_logMutex;

void ous_log(LogLevel /*level*/, const char* tag, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(s_logMutex);
    printf("[%s] %s\n", tag, buf);
}
