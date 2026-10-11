// Turns a stream of scheduler and interrupt trace events into "why was my
// task late" statistics for selected threads.
//
// An activation of a thread runs from its wake-up (sched_waking) until it
// blocks again (sched_switch away with a sleeping state). Within it, the
// thread is either running or waiting for its CPU: after the wake-up, and
// after being preempted. Every moment of waiting is attributed to whatever
// ran on that CPU at the time: another task, an interrupt, a softirq, or the
// CPU still coming out of idle.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "rtdoctor/trace_event.hpp"

namespace rtdoctor {

struct Delay {
    std::string who;    // "task map_renderer (pid 88)", "irq 13 arch_timer", "softirq TIMER", "idle"
    double seconds = 0;
    int count = 0;      // In how many waits it appeared.
};

struct Activation {
    double start = 0;      // Wake-up time.
    double response = 0;   // Wake-up until it blocked again.
    double waited = 0;     // Ready but not running (wake-up latency + preemptions).
    int cpu = -1;          // CPU it ran on last.
    std::vector<Delay> delays;  // What ran instead while it waited, largest first.
};

struct ThreadReport {
    int tid = 0;
    std::string comm;
    int activations = 0;
    int misses = 0;                  // Activations with response > deadline (if a deadline is set).
    std::vector<double> responses;   // Seconds, in order.
    int waits = 0;                   // Times it had to wait (wake-ups and preemptions).
    double waited_total = 0;
    double waited_max = 0;
    int preemptions = 0;
    std::vector<Delay> delays;       // Totals over the whole trace, largest first.
    std::vector<Activation> worst;   // Longest responses, longest first.
};

struct AnalyzerOptions {
    double deadline = 0;     // Seconds; 0 = no deadline.
    std::size_t worst = 5;   // How many of the worst activations to keep per thread.
};

class LatencyAnalyzer {
public:
    using Filter = std::function<bool(int tid, const std::string& comm)>;

    LatencyAnalyzer(Filter is_target, AnalyzerOptions options = {});

    void feed(const TraceEvent& event);

    // Threads that had at least one activation, the most delayed first.
    std::vector<ThreadReport> reports() const;

private:
    enum class State { Unknown, Sleeping, Waiting, Running };

    struct Episode {  // One stretch of waiting.
        double start = 0;
        int cpu = -1;
        std::map<std::string, double> by_who;
    };

    struct Thread {
        int tid = 0;
        std::string comm;
        State state = State::Unknown;
        bool in_activation = false;
        double activation_start = 0;
        int cpu = -1;
        Episode episode;
        Activation current;               // Accumulates the running activation.
        std::map<std::string, double> current_delays;
        ThreadReport report;
        std::map<std::string, Delay> delays;
    };

    struct Cpu {
        double last = -1;
        std::string running = "idle";
        std::vector<std::string> interrupts;  // Nested irq / softirq contexts.
    };

    bool target(int tid, const std::string& comm);
    void account(int cpu, double time);
    void start_waiting(Thread& thread, int cpu, double time);
    void stop_waiting(Thread& thread, double time);
    void finish_activation(Thread& thread, double time);

    Filter is_target_;
    AnalyzerOptions options_;
    std::map<int, Thread> threads_;
    std::map<int, Cpu> cpus_;
    std::map<int, bool> target_cache_;
};

// "1.23 ms"
std::string format_ms(double seconds);

}  // namespace rtdoctor
