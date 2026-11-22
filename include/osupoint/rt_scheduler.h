#pragma once

#include <cstdint>
#include <time.h>

#include "osupoint/ipc_protocol.h"

bool SetRealtimePriority(int priority = 20) noexcept;
bool LockProcessMemory() noexcept;
bool SetThreadAffinity(int cpu_core_id) noexcept;

inline uint64_t GetMonotonicTimeNs() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

inline void FastSpinWait(uint32_t iterations) noexcept {
    for (uint32_t i = 0; i < iterations; ++i) {
        OSU_CPU_PAUSE();
    }
}
