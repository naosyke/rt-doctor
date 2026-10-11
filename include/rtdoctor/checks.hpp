// System checks: is this Linux set up to run real-time work reliably?
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "rtdoctor/system_view.hpp"

namespace rtdoctor {

enum class Status {
    Pass,  // Good for real-time.
    Info,  // Worth knowing; not a problem by itself.
    Warn,  // Can cause latency spikes or misses.
    Fail,  // Real-time work will not be reliable.
};

std::string_view to_string(Status status);

// How much a finding typically adds to worst-case latency, used to order the
// findings so the ones that matter most come first.
enum class Impact {
    Unset,
    Low,     // Only under particular conditions (memory pressure, sibling load, ...).
    Medium,  // Tens of microseconds to a few milliseconds.
    High,    // Milliseconds or more, or real-time priorities unusable.
    Critical,  // Cannot be fixed by tuning this system at all (e.g. a virtual machine).
};

std::string_view to_string(Impact impact);

struct CheckResult {
    std::string id;      // Stable identifier, e.g. "kernel_preemption".
    std::string title;
    Status status;
    std::string value;   // What was found.
    std::string why;     // Why it matters (empty for Pass).
    std::string fix;     // How to improve it (empty for Pass).
    Impact impact = Impact::Unset;  // Set by run_checks() unless the check decides itself.
};

// Findings (Warn / Fail) ordered by impact, then severity, then check order.
std::vector<CheckResult> prioritized(const std::vector<CheckResult>& results);

std::vector<CheckResult> run_checks(const SystemView& system, const ProcessLimits& limits);

// Individual checks, exposed for tests.
CheckResult check_kernel_preemption(const SystemView& system);
CheckResult check_virtualization(const SystemView& system);
CheckResult check_cpu_isolation(const SystemView& system);
CheckResult check_irq_affinity(const SystemView& system);
CheckResult check_cpu_frequency(const SystemView& system);
CheckResult check_cpu_idle(const SystemView& system);
CheckResult check_smt(const SystemView& system);
CheckResult check_rt_throttling(const SystemView& system);
CheckResult check_transparent_hugepages(const SystemView& system);
CheckResult check_swap(const SystemView& system);
CheckResult check_clocksource(const SystemView& system);
CheckResult check_rt_priority_limit(const ProcessLimits& limits);
CheckResult check_memory_lock_limit(const ProcessLimits& limits);

// Isolated CPUs from the kernel command line (isolcpus= or nohz_full=).
std::vector<int> isolated_cpus(const SystemView& system);

}  // namespace rtdoctor
