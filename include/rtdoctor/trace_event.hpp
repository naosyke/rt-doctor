// One event of the ftrace text output, e.g.
//   "  brake-1234  [001] d..2.  4410.303921: sched_switch: prev_comm=brake prev_pid=1234 ..."
#pragma once

#include <map>
#include <optional>
#include <string>

namespace rtdoctor {

struct TraceEvent {
    int cpu = -1;
    double time = 0;                          // Seconds (trace clock).
    std::string name;                         // e.g. "sched_switch".
    std::map<std::string, std::string> fields;  // key=value pairs.

    const std::string& field(const std::string& key) const;
    int int_field(const std::string& key, int fallback = -1) const;
};

std::optional<TraceEvent> parse_trace_line(const std::string& line);

}  // namespace rtdoctor
