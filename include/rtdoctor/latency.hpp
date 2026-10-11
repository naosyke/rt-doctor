// `rtdoctor latency`: how late this system wakes up a real-time thread, per
// CPU, measured like cyclictest. One thread per CPU, pinned and SCHED_FIFO,
// sleeps until an absolute time on CLOCK_MONOTONIC and records how much later
// it actually woke up.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rtdoctor {

// Wake-up latencies of one CPU in a histogram: 1 us bins up to 1 ms, 10 us
// bins up to 10 ms, 100 us bins up to 100 ms, then one overflow bin.
class LatencyStats {
public:
    LatencyStats();
    void add(double microseconds);

    std::int64_t samples() const { return samples_; }
    double min() const { return samples_ ? min_ : 0; }
    double max() const { return samples_ ? max_ : 0; }
    double average() const { return samples_ ? sum_ / samples_ : 0; }
    // Upper bound of the bin holding that fraction of samples (0.99 = 99%);
    // the exact maximum for 1.0.
    double percentile(double fraction) const;
    std::int64_t count_over(double microseconds) const;

private:
    static std::size_t bin_of(double microseconds);
    static double upper_of(std::size_t bin);

    std::vector<std::int64_t> bins_;
    std::int64_t samples_ = 0;
    double min_ = 0, max_ = 0, sum_ = 0;
};

struct CpuLatency {
    int cpu = 0;
    LatencyStats stats;
};

struct LatencyOptions {
    std::vector<int> cpus;      // Empty: every CPU this process may run on.
    double duration = 10;       // Seconds.
    int interval_us = 1000;     // Timer period of each thread.
    int priority = 95;          // SCHED_FIFO priority.
};

struct LatencyResult {
    std::vector<CpuLatency> cpus;
    LatencyOptions options;
    double duration = 0;        // Seconds actually measured.
    bool realtime = true;       // False if SCHED_FIFO was not allowed (results then show normal scheduling).
    bool memory_locked = true;
};

// Thread names of the measuring threads, "rtd-lat/<cpu>", to find them in a trace.
std::string latency_thread_name(int cpu);
bool is_latency_thread(const std::string& name);

// Measures until the duration is over. `wait` is called on the calling
// thread while the measuring threads run; it gets the duration and returns
// early if it wants to stop (default: sleep, stopped by stop_latency()).
LatencyResult measure_latency(const LatencyOptions& options, const std::function<void(double)>& wait = {});
void stop_latency();

// A rule-of-thumb reading of the worst latency, e.g. "good: fine for 1 ms control loops".
std::string latency_verdict(double max_microseconds);

}  // namespace rtdoctor
