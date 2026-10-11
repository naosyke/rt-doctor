// Running a trace and writing a report: the steps shared by the `trace`,
// `report` and interactive (wizard) commands.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "rtdoctor/checks.hpp"
#include "rtdoctor/html_report.hpp"
#include "rtdoctor/i18n.hpp"

namespace rtdoctor {

// What to trace and for how long.
struct TraceRequest {
    int pid = 0;           // All threads of this process...
    std::string comm;      // ...or threads with this name.
    double duration = 10;  // Seconds.
    double deadline_ms = 0;
    std::size_t max_threads = 3;

    bool wanted() const { return pid > 0 || !comm.empty(); }
    bool valid() const { return (pid > 0) != !comm.empty() && duration > 0; }

    // Consumes a trace option at argv[i] (and its value); false if it is not one.
    bool parse(int argc, char** argv, int& i);
};

// True if this process sees the kernel's own process ids (not inside a
// container's PID namespace). On the host, pid 2 is always kthreadd.
bool sees_host_pids();

// Traces the target until the duration is over or Ctrl-C. Returns false
// (with a message on stderr) for a target that does not exist; a tracer that
// cannot start is reported in result.error instead. Progress messages go to
// stderr unless quiet.
bool run_trace(const TraceRequest& request, bool quiet, TraceResult& result);

// Writes the HTML report; false (with a message on stderr) if the file
// cannot be written.
bool write_report(const std::string& path, const SystemView& system, const std::vector<CheckResult>& results,
                  const std::optional<TraceResult>& trace, std::size_t max_threads, Lang lang,
                  const std::string& version, const std::optional<LatencyData>& latency = std::nullopt);

}  // namespace rtdoctor
