// Test workload: keeps one CPU busy. Thread name: "hog".
//   cpu_hog CPU SECONDS
#include <sched.h>
#include <sys/prctl.h>
#include <time.h>

#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(std::atoi(argv[1]), &set);
    sched_setaffinity(0, sizeof(set), &set);
    prctl(PR_SET_NAME, "hog", 0, 0, 0);
    const time_t end = time(nullptr) + std::atoi(argv[2]);
    volatile unsigned long counter = 0;
    while (time(nullptr) < end) ++counter;
    return 0;
}
