// `rtdoctor report`: check results (and optionally a trace) as one
// self-contained HTML file to keep, attach to a ticket or send to someone.
// No scripts and no external resources; it opens offline and prints well.
#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "rtdoctor/analyzer.hpp"
#include "rtdoctor/checks.hpp"
#include "rtdoctor/i18n.hpp"
#include "rtdoctor/system_view.hpp"
#include "rtdoctor/trace_report.hpp"

namespace rtdoctor {

struct HostInfo {
    std::string hostname;
    std::string kernel;     // Release, e.g. "6.8.0-1015-realtime".
    std::string build;      // Kernel build string; contains "PREEMPT_RT" on RT kernels.
    std::string cpu_model;  // Empty if unknown.
    std::string machine;    // e.g. "x86_64", "aarch64".
    int cpus = 0;           // Online CPUs.
    std::string generated;  // Time the report was made.
    std::string version;    // rt-doctor version.
};

// Fills what can be read from /proc and /sys; the caller adds machine,
// generated and version.
HostInfo read_host_info(const SystemView& system);

struct TraceResult {
    TraceSummary summary;
    std::vector<ThreadReport> threads;
    std::string error;  // Set if tracing could not run; the report says so.
};

void print_html(std::ostream& out, const HostInfo& host, const std::vector<CheckResult>& results,
                const std::optional<TraceResult>& trace, std::size_t max_threads = 3, Lang lang = Lang::En);

std::string html_escape(const std::string& text);

}  // namespace rtdoctor
