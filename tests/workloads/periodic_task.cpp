// Test workload: a periodic task that wakes every PERIOD ms, computes WORK ms
// of CPU time and sleeps again, pinned to one CPU. Thread name: "periodic".
//   periodic_task CPU PERIOD_MS WORK_MS SECONDS
#include <pthread.h>
#include <sched.h>
#include <sys/prctl.h>
#include <time.h>

#include <cstdlib>

namespace {
long long now_ns(clockid_t clock) {
    timespec t{};
    clock_gettime(clock, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    const int cpu = std::atoi(argv[1]);
    const long long period = std::atoll(argv[2]) * 1000000LL, work = std::atoll(argv[3]) * 1000000LL;
    const long long end = now_ns(CLOCK_MONOTONIC) + std::atoll(argv[4]) * 1000000000LL;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof(set), &set);
    prctl(PR_SET_NAME, "periodic", 0, 0, 0);

    long long next = now_ns(CLOCK_MONOTONIC);
    while (next < end) {
        const long long until = now_ns(CLOCK_THREAD_CPUTIME_ID) + work;
        while (now_ns(CLOCK_THREAD_CPUTIME_ID) < until) {
        }
        next += period;
        timespec wake{static_cast<time_t>(next / 1000000000LL), static_cast<long>(next % 1000000000LL)};
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, nullptr);
    }
    return 0;
}
