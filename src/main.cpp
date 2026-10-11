// rtdoctor: checks how well a Linux system can run real-time work, and finds
// out what delays a given task.
//
//   rtdoctor check                 human-readable report
//   rtdoctor check --json          machine-readable report
//   rtdoctor check --fail-on warn  exit code 1 on any warning (for CI)
//   rtdoctor trace --pid 1234 --deadline-ms 4
//   rtdoctor trace --comm brake --duration 30
//   rtdoctor report -o report.html --comm brake --deadline-ms 4
#include <signal.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "rtdoctor/analyzer.hpp"
#include "rtdoctor/checks.hpp"
#include "rtdoctor/ftrace.hpp"
#include "rtdoctor/html_report.hpp"
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
                 "       rtdoctor report [-o FILE] [--pid PID | --comm NAME] [--duration SECONDS]\n"
                 "                       [--deadline-ms MS] [--threads N] [--root DIR]\n"
                 "       rtdoctor --version\n";
    return 2;
}

std::atomic<bool> stop_requested{false};

// What to trace and for how long; shared by `trace` and `report`.
struct TraceRequest {
    int pid = 0;
    std::string comm;
    double duration = 10, deadline_ms = 0;
    std::size_t max_threads = 3;

    bool wanted() const { return pid > 0 || !comm.empty(); }
    bool valid() const { return (pid > 0) != !comm.empty() && duration > 0; }

    // Consumes a trace option at argv[i]; false if it is not one.
    bool parse(int argc, char** argv, int& i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) return false;
        if (arg == "--pid") pid = std::atoi(argv[++i]);
        else if (arg == "--comm") comm = argv[++i];
        else if (arg == "--duration") duration = std::atof(argv[++i]);
        else if (arg == "--deadline-ms") deadline_ms = std::atof(argv[++i]);
        else if (arg == "--threads") max_threads = static_cast<std::size_t>(std::atoi(argv[++i]));
        else return false;
        return true;
    }
};

// Traces the target. Returns the exit code for a usage error (2) or 0; an
// unavailable ftrace is reported in result.error.
int run_trace(const TraceRequest& request, bool quiet, rtdoctor::TraceResult& result) {
    auto& summary = result.summary;
    summary.deadline = request.deadline_ms / 1000.0;
    rtdoctor::LatencyAnalyzer::Filter filter;
    if (request.pid > 0) {
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
        const auto name = proc.read_line(std::to_string(request.pid) + "/comm");
        if (!name) {
            std::cerr << "rtdoctor: no process with pid " << request.pid << "\n";
            return 2;
        }
        std::set<int> tids;
        for (const auto& tid : proc.list(std::to_string(request.pid) + "/task")) tids.insert(std::atoi(tid.c_str()));
        summary.target =
            "pid " + std::to_string(request.pid) + " (" + *name + ", " + std::to_string(tids.size()) + " threads)";
        filter = [tids](int tid, const std::string&) { return tids.count(tid) > 0; };
    } else {
        const std::string name = request.comm.substr(0, 15);  // The kernel keeps 15 characters.
        summary.target = "threads named \"" + name + "\"";
        filter = [name](int, const std::string& thread) { return thread == name; };
    }

    auto session = rtdoctor::FtraceSession::start(rtdoctor::latency_events(), 16384, result.error);
    if (!session) return 0;
    signal(SIGINT, [](int) { stop_requested = true; });
    signal(SIGTERM, [](int) { stop_requested = true; });
    if (!quiet) {
        std::cerr << "Tracing " << summary.target << " for " << request.duration << " s (Ctrl-C to stop early)...\n";
    }

    rtdoctor::AnalyzerOptions options;
    options.deadline = summary.deadline;
    rtdoctor::LatencyAnalyzer analyzer(filter, options);
    const auto start = std::chrono::steady_clock::now();
    session->run(
        std::chrono::duration<double>(request.duration),
        [&](const std::string& line) {
            if (const auto event = rtdoctor::parse_trace_line(line)) analyzer.feed(*event);
        },
        [] { return !stop_requested; });
    summary.duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    summary.lost_events = session->lost_events();
    result.threads = analyzer.reports();
    return 0;
}

int trace(int argc, char** argv) {
    TraceRequest request;
    bool json = false;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) json = true;
        else if (!request.parse(argc, argv, i)) return usage();
    }
    if (!request.valid()) return usage();

    rtdoctor::TraceResult result;
    if (const int code = run_trace(request, json, result)) return code;
    if (!result.error.empty()) {
        std::cerr << "rtdoctor: " << result.error << "\n";
        return 3;
    }
    if (json) {
        rtdoctor::print_trace_json(std::cout, result.summary, result.threads);
    } else {
        rtdoctor::print_trace_text(std::cout, result.summary, result.threads, request.max_threads);
    }
    return 0;
}

int report(int argc, char** argv) {
    TraceRequest request;
    std::string output = "rtdoctor-report.html", root = "/";
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) output = argv[++i];
        else if (arg == "--root" && i + 1 < argc) root = argv[++i];
        else if (!request.parse(argc, argv, i)) return usage();
    }
    if (request.wanted() && !request.valid()) return usage();

    const rtdoctor::SystemView system(root);
    const auto results = rtdoctor::run_checks(system, rtdoctor::current_process_limits());

    std::optional<rtdoctor::TraceResult> traced;
    if (request.wanted()) {
        traced.emplace();
        if (const int code = run_trace(request, false, *traced)) return code;
        if (!traced->error.empty()) std::cerr << "rtdoctor: tracing not possible: " << traced->error << "\n";
    }

    auto host = rtdoctor::read_host_info(system);
    utsname name{};
    if (uname(&name) == 0) host.machine = name.machine;
    host.version = RTDOCTOR_VERSION;
    char generated[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(generated, sizeof(generated), "%Y-%m-%d %H:%M:%S %Z", std::localtime(&now));
    host.generated = generated;

    std::ofstream file(output);
    if (!file) {
        std::cerr << "rtdoctor: cannot write " << output << "\n";
        return 2;
    }
    rtdoctor::print_html(file, host, results, traced, request.max_threads);
    file.close();
    if (!file) {
        std::cerr << "rtdoctor: cannot write " << output << "\n";
        return 2;
    }
    std::cerr << "Wrote " << output << "\n";
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
    if (argc >= 2 && std::strcmp(argv[1], "report") == 0) return report(argc - 2, argv + 2);
    return usage();
}
