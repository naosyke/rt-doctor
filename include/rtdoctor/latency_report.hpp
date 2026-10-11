// Rendering of `rtdoctor latency` results, and what delayed the measuring
// threads when the measurement ran with a trace (--trace).
#pragma once

#include <optional>
#include <ostream>
#include <vector>

#include "rtdoctor/analyzer.hpp"
#include "rtdoctor/latency.hpp"

namespace rtdoctor {

// What ran on a CPU while a measuring thread was woken but not yet running,
// over all measuring threads: the totals and the slowest wake-ups.
struct LatencyCauses {
    std::vector<Delay> delays;        // Largest first.
    std::vector<Activation> worst;    // Longest wait first.
    long long lost_events = 0;
};

// `self` is rtdoctor's own pid: its time is shown as "rtdoctor itself
// (reading the trace)", the cost of --trace, not a cause on the system.
LatencyCauses latency_causes(const std::vector<ThreadReport>& threads, int self = 0, std::size_t worst = 5);

extern const char* const kTraceOverhead;  // "rtdoctor itself (reading the trace)"

// The CPU with the highest maximum (nullptr if nothing was measured).
const CpuLatency* worst_cpu(const LatencyResult& result);

void print_latency_text(std::ostream& out, const LatencyResult& result, const std::optional<LatencyCauses>& causes);
void print_latency_json(std::ostream& out, const LatencyResult& result, const std::optional<LatencyCauses>& causes);

}  // namespace rtdoctor
