#include "osupoint/rt_scheduler.h"
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <algorithm>

bool SetRealtimePriority(int priority) noexcept {
    struct rlimit rl{};
    if (getrlimit(RLIMIT_RTPRIO, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        if (rl.rlim_cur > 0 && priority > static_cast<int>(rl.rlim_cur)) {
            priority = static_cast<int>(rl.rlim_cur);
        }
    }
    const int max_prio = sched_get_priority_max(SCHED_FIFO);
    if (max_prio > 0 && priority > max_prio) priority = max_prio;
    const int min_prio = sched_get_priority_min(SCHED_FIFO);
    if (min_prio > 0 && priority < min_prio) priority = min_prio;

    struct sched_param sp{};
    sp.sched_priority = priority;

    int res = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (res == 0) {
        return true;
    }

    // Only the input worker calls this. Do not promote the GUI or starve IRQs.
    errno = 0;
    if (setpriority(PRIO_PROCESS, 0, -5) == 0 && errno == 0) {
        return true;
    }

    return false;
}

bool LockProcessMemory() noexcept {
    int res = mlockall(MCL_CURRENT | MCL_FUTURE);
    return (res == 0);
}

bool SetThreadAffinity(int cpu_core_id) noexcept {
    if (cpu_core_id < 0 || cpu_core_id >= CPU_SETSIZE) return false;
    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0 || !CPU_ISSET(cpu_core_id, &allowed)) return false;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_core_id, &cpuset);

    int res = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    return (res == 0);
}
