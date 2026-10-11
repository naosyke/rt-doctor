#include "rtdoctor/latency.hpp"

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace rtdoctor {

namespace {

constexpr std::size_t kFine = 1000;    // 0-1000 us in 1 us bins.
constexpr std::size_t kMedium = 900;   // 1-10 ms in 10 us bins.
constexpr std::size_t kCoarse = 900;   // 10-100 ms in 100 us bins.
constexpr std::size_t kBins = kFine + kMedium + kCoarse + 1;

std::atomic<bool> stopping{false};

std::int64_t to_ns(const timespec& t) { return std::int64_t(t.tv_sec) * 1000000000 + t.tv_nsec; }

timespec from_ns(std::int64_t ns) {
    timespec t{};
    t.tv_sec = static_cast<time_t>(ns / 1000000000);
    t.tv_nsec = static_cast<long>(ns % 1000000000);
    return t;
}

struct Worker {
    int cpu = 0;
    int interval_us = 1000;
    LatencyStats stats;
    bool realtime = false;
    pthread_t thread{};
};

void* measure(void* arg) {
    auto& worker = *static_cast<Worker*>(arg);
    pthread_setname_np(pthread_self(), latency_thread_name(worker.cpu).c_str());
    const std::int64_t interval = std::int64_t(worker.interval_us) * 1000;
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    std::int64_t next = to_ns(now) + interval;
    while (!stopping.load(std::memory_order_relaxed)) {
        const timespec wake = from_ns(next);
        if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, nullptr) != 0) continue;  // Interrupted.
        clock_gettime(CLOCK_MONOTONIC, &now);
        const std::int64_t late = to_ns(now) - next;
        worker.stats.add(late / 1000.0);
        next += interval;
        if (next <= to_ns(now)) next = to_ns(now) + interval;  // Fell behind by whole periods: skip them.
    }
    return nullptr;
}

std::vector<int> allowed_cpus() {
    cpu_set_t set;
    CPU_ZERO(&set);
    std::vector<int> cpus;
    if (sched_getaffinity(0, sizeof(set), &set) != 0) return cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &set)) cpus.push_back(cpu);
    }
    return cpus;
}

bool start(Worker& worker, int priority, bool realtime) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);  // Small: the stacks are locked in memory.
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(worker.cpu, &set);
    pthread_attr_setaffinity_np(&attr, sizeof(set), &set);
    if (realtime) {
        sched_param param{};
        param.sched_priority = priority;
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        pthread_attr_setschedparam(&attr, &param);
    }
    const int error = pthread_create(&worker.thread, &attr, measure, &worker);
    pthread_attr_destroy(&attr);
    worker.realtime = realtime && error == 0;
    return error == 0;
}

}  // namespace

LatencyStats::LatencyStats() : bins_(kBins, 0) {}

std::size_t LatencyStats::bin_of(double us) {
    if (us < 0) us = 0;
    if (us < 1000) return static_cast<std::size_t>(us);
    if (us < 10000) return kFine + static_cast<std::size_t>((us - 1000) / 10);
    if (us < 100000) return kFine + kMedium + static_cast<std::size_t>((us - 10000) / 100);
    return kBins - 1;
}

double LatencyStats::upper_of(std::size_t bin) {
    if (bin < kFine) return bin + 1.0;
    if (bin < kFine + kMedium) return 1000 + (bin - kFine + 1) * 10.0;
    if (bin < kBins - 1) return 10000 + (bin - kFine - kMedium + 1) * 100.0;
    return INFINITY;
}

void LatencyStats::add(double us) {
    if (samples_ == 0 || us < min_) min_ = us;
    if (samples_ == 0 || us > max_) max_ = us;
    sum_ += us;
    ++samples_;
    ++bins_[bin_of(us)];
}

double LatencyStats::percentile(double fraction) const {
    if (samples_ == 0) return 0;
    if (fraction >= 1) return max_;
    const auto wanted = static_cast<std::int64_t>(std::ceil(fraction * samples_));
    std::int64_t seen = 0;
    for (std::size_t bin = 0; bin < bins_.size(); ++bin) {
        seen += bins_[bin];
        if (seen >= wanted) return std::min(upper_of(bin), max_);
    }
    return max_;
}

std::int64_t LatencyStats::count_over(double us) const {
    std::int64_t count = 0;
    for (std::size_t bin = bin_of(us) + 1; bin < bins_.size(); ++bin) count += bins_[bin];
    return count;
}

std::string latency_thread_name(int cpu) { return "rtd-lat/" + std::to_string(cpu); }

bool is_latency_thread(const std::string& name) { return name.rfind("rtd-lat/", 0) == 0; }

void stop_latency() { stopping = true; }

LatencyResult measure_latency(const LatencyOptions& options, const std::function<void(double)>& wait) {
    LatencyResult result;
    result.options = options;
    if (result.options.cpus.empty()) result.options.cpus = allowed_cpus();

    // Page faults while measuring would show up as latency. MCL_ONFAULT
    // locks pages as they are touched instead of the whole address space at
    // once (older kernels: fall back to locking everything).
    result.memory_locked = mlockall(MCL_CURRENT | MCL_FUTURE | MCL_ONFAULT) == 0 ||
                           mlockall(MCL_CURRENT | MCL_FUTURE) == 0;

    stopping = false;
    std::vector<Worker> workers(result.options.cpus.size());
    bool realtime = true;
    for (std::size_t i = 0; i < workers.size(); ++i) {
        workers[i].cpu = result.options.cpus[i];
        workers[i].interval_us = std::max(50, result.options.interval_us);
        if (!start(workers[i], result.options.priority, realtime)) {
            // Not allowed to use SCHED_FIFO: measure with normal scheduling.
            realtime = false;
            start(workers[i], 0, false);
        }
    }
    if (!realtime) {
        // Keep it comparable: every thread with the same (normal) policy.
        sched_param normal{};
        for (auto& worker : workers) pthread_setschedparam(worker.thread, SCHED_OTHER, &normal);
    }

    const auto begin = std::chrono::steady_clock::now();
    if (wait) {
        wait(result.options.duration);
    } else {
        const auto end = begin + std::chrono::duration<double>(result.options.duration);
        while (!stopping && std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    stopping = true;
    for (auto& worker : workers) pthread_join(worker.thread, nullptr);
    result.duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    if (result.memory_locked) munlockall();

    result.realtime = realtime;
    for (auto& worker : workers) result.cpus.push_back({worker.cpu, std::move(worker.stats)});
    return result;
}

std::string latency_verdict(double max_us) {
    if (max_us <= 50) return "excellent: typical of a tuned PREEMPT_RT system, fine for loops down to about 100 us";
    if (max_us <= 150) return "good: fine for 1 ms control loops";
    if (max_us <= 1000) return "fair: fine for 10 ms loops, but 1 ms loops can miss their deadline";
    return "poor: wake-ups can be milliseconds late, so even 10 ms loops can miss; see rtdoctor check";
}

}  // namespace rtdoctor
