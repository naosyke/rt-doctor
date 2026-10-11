// rtdoctor: checks how well a Linux system can run real-time work, and finds
// out what delays a given task.
//
//   rtdoctor check                 human-readable report
//   rtdoctor check --json          machine-readable report
//   rtdoctor check --fail-on warn  exit code 1 on any warning (for CI)
//   rtdoctor trace --pid 1234 --deadline-ms 4
//   rtdoctor trace --comm brake --duration 30
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

#include "rtdoctor/analyzer.hpp"
#include "rtdoctor/checks.hpp"
#include "rtdoctor/ftrace.hpp"
#include "rtdoctor/output.hpp"
#include "rtdoctor/trace_report.hpp"

#ifndef RTDOCTOR_VERSION
#define RTDOCTOR_VERSION "dev"
#endif

namespace {

int usage() {
    std::cerr << "usage: rtdoctor check [--json] [--verbose] [--fail-on warn|fail] [--root DIR]\n"
                 "       rtdoctor trace (--pid PID | --comm NAME) [--duration SECONDS] [--deadline-ms MS]\n"
                 "                      [--threads N] [--json]\n"
                 "       rtdoctor --version\n";
    return 2;
}

std::atomic<bool> stop_requested{false};

int trace(int argc, char** argv) {
    int pid = 0;
    std::string comm;
    double duration = 10, deadline_ms = 0;
    std::size_t max_threads = 3;
    bool json = false;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--pid" && i + 1 < argc) pid = std::atoi(argv[++i]);
        else if (arg == "--comm" && i + 1 < argc) comm = argv[++i];
        else if (arg == "--duration" && i + 1 < argc) duration = std::atof(argv[++i]);
        else if (arg == "--deadline-ms" && i + 1 < argc) deadline_ms = std::atof(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) max_threads = static_cast<std::size_t>(std::atoi(argv[++i]));
        else if (arg == "--json") json = true;
        else return usage();
    }
    if ((pid > 0) == !comm.empty() || duration <= 0) return usage();

    rtdoctor::TraceSummary summary;
    summary.deadline = deadline_ms / 1000.0;
    rtdoctor::LatencyAnalyzer::Filter filter;
    if (pid > 0) {
        // Trace events carry the kernel's (initial namespace) thread ids. In a
        // container's PID namespace the numbers differ and cannot be mapped,
        // so refuse instead of silently tracing nothing. On the host, pid 2 is
        // always the kernel thread kthreadd.
        const rtdoctor::SystemView proc("/proc");
        if (proc.read_line("2/comm").value_or("") != "kthreadd") {
            std::cerr << "rtdoctor: this shell is in a container's PID namespace, where process ids differ from\n"
                         "the ones the kernel traces. Run rtdoctor on the host (or with docker run --pid=host),\n"
                         "or select the target by thread name with --comm NAME.\n";
            return 2;
        }
        const auto name = proc.read_line(std::to_string(pid) + "/comm");
        if (!name) {
            std::cerr << "rtdoctor: no process with pid " << pid << "\n";
            return 2;
        }
        std::set<int> tids;
        for (const auto& tid : proc.list(std::to_string(pid) + "/task")) tids.insert(std::atoi(tid.c_str()));
        summary.target = "pid " + std::to_string(pid) + " (" + *name + ", " + std::to_string(tids.size()) + " threads)";
        filter = [tids](int tid, const std::string&) { return tids.count(tid) > 0; };
    } else {
        const std::string name = comm.substr(0, 15);  // The kernel keeps 15 characters.
        summary.target = "threads named \"" + name + "\"";
        filter = [name](int, const std::string& thread) { return thread == name; };
    }

    std::string error;
    auto session = rtdoctor::FtraceSession::start(rtdoctor::latency_events(), 16384, error);
    if (!session) {
        std::cerr << "rtdoctor: " << error << "\n";
        return 3;
    }
    signal(SIGINT, [](int) { stop_requested = true; });
    signal(SIGTERM, [](int) { stop_requested = true; });
    if (!json) std::cerr << "Tracing " << summary.target << " for " << duration << " s (Ctrl-C to stop early)...\n";

    rtdoctor::AnalyzerOptions options;
    options.deadline = summary.deadline;
    rtdoctor::LatencyAnalyzer analyzer(filter, options);
    const auto start = std::chrono::steady_clock::now();
    session->run(
        std::chrono::duration<double>(duration),
        [&](const std::string& line) {
            if (const auto event = rtdoctor::parse_trace_line(line)) analyzer.feed(*event);
        },
        [] { return !stop_requested; });
    summary.duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    summary.lost_events = session->lost_events();

    const auto reports = analyzer.reports();
    if (json) {
        rtdoctor::print_trace_json(std::cout, summary, reports);
    } else {
        rtdoctor::print_trace_text(std::cout, summary, reports, max_threads);
    }
    return 0;
}

int check(int argc, char** argv) {
    bool json = false, verbose = false;
    std::string fail_on = "fail", root = "/";
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") json = true;
        else if (arg == "--verbose" || arg == "-v") verbose = true;
        else if (arg == "--fail-on" && i + 1 < argc) fail_on = argv[++i];
        else if (arg == "--root" && i + 1 < argc) root = argv[++i];  // For tests and offline analysis.
        else return usage();
    }
    if (fail_on != "warn" && fail_on != "fail") return usage();

    const rtdoctor::SystemView system(root);
    const auto results = rtdoctor::run_checks(system, rtdoctor::current_process_limits());
    if (json) {
        rtdoctor::print_json(std::cout, results);
    } else {
        std::cout << "rt-doctor " << RTDOCTOR_VERSION << " - real-time readiness of this system\n\n";
        rtdoctor::print_text(std::cout, results, isatty(STDOUT_FILENO), verbose);
    }

    for (const auto& result : results) {
        if (result.status == rtdoctor::Status::Fail) return 1;
        if (fail_on == "warn" && result.status == rtdoctor::Status::Warn) return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << "rt-doctor " << RTDOCTOR_VERSION << "\n";
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "check") == 0) return check(argc - 2, argv + 2);
    if (argc >= 2 && std::strcmp(argv[1], "trace") == 0) return trace(argc - 2, argv + 2);
    return usage();
}
