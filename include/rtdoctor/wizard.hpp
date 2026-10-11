// Interactive mode: `sudo rtdoctor` without arguments walks through checking
// the system, picking a program, tracing it and writing the report, so no
// options have to be known.
#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "rtdoctor/i18n.hpp"
#include "rtdoctor/system_view.hpp"

namespace rtdoctor {

struct ThreadSample {
    int tid = 0;
    std::string name;
    long long wakeups = 0;  // voluntary_ctxt_switches: times it went to sleep (and so woke up again).
    int policy = 0;         // 0 normal, 1 FIFO, 2 RR, 3 batch, 5 idle, 6 deadline.
    int rt_priority = 0;
};

struct ProcessSample {
    int pid = 0;
    std::string name;
    bool kernel_thread = false;
    std::vector<ThreadSample> threads;
};

// Every process in <root>/proc with its threads.
std::vector<ProcessSample> sample_processes(const SystemView& system);

// A program worth tracing: it runs with a real-time policy (someone made it
// time-critical) or wakes up often (periodic work, like a control loop).
struct Candidate {
    int pid = 0;
    std::string name;
    std::string policy;            // "FIFO", "RR", "DEADLINE" or "" (normal).
    int rt_priority = 0;
    std::string busiest_thread;    // The thread that woke up most often.
    double wakeups_per_second = 0;  // Of that thread.
};

// Compares two samples taken `seconds` apart. Kernel threads and `self` are
// left out; real-time programs come first (highest priority first), then the
// ones that wake up most often.
std::vector<Candidate> rank_candidates(const std::vector<ProcessSample>& before,
                                       const std::vector<ProcessSample>& after, double seconds, int self,
                                       std::size_t limit = 10);

// The report language from the locale ($LC_ALL, $LC_MESSAGES, $LANG).
Lang lang_from_environment();

int run_wizard(std::istream& in, std::ostream& out, Lang lang, const std::string& version);

}  // namespace rtdoctor
