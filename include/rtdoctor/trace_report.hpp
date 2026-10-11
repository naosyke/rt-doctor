// Rendering of `rtdoctor trace` results.
#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "rtdoctor/analyzer.hpp"

namespace rtdoctor {

struct TraceSummary {
    std::string target;      // e.g. "pid 1234 (brake)" or "comm brake"
    double duration = 0;     // Seconds traced.
    double deadline = 0;     // Seconds; 0 = none.
    long long lost_events = 0;
};

// What to do about a kind of delay, e.g. for "idle" or "irq ...".
std::string advice_for(const std::string& who);

double percentile(std::vector<double> values, double fraction);

void print_trace_text(std::ostream& out, const TraceSummary& summary, const std::vector<ThreadReport>& threads,
                      std::size_t max_threads);
void print_trace_json(std::ostream& out, const TraceSummary& summary, const std::vector<ThreadReport>& threads);

}  // namespace rtdoctor
