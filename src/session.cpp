#include "rtdoctor/session.hpp"

#include <signal.h>
#include <sys/utsname.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <set>

#include "rtdoctor/analyzer.hpp"
#include "rtdoctor/ftrace.hpp"

namespace rtdoctor {

namespace {

std::atomic<bool> stop_requested{false};

}  // namespace

bool TraceRequest::parse(int argc, char** argv, int& i) {
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

bool sees_host_pids() { return SystemView("/proc").read_line("2/comm").value_or("") == "kthreadd"; }

bool run_trace(const TraceRequest& request, bool quiet, TraceResult& result) {
    auto& summary = result.summary;
    summary.deadline = request.deadline_ms / 1000.0;
    LatencyAnalyzer::Filter filter;
    if (request.pid > 0) {
        const SystemView proc("/proc");
        const auto pid = std::to_string(request.pid);
        const auto name = proc.read_line(pid + "/comm");
        if (!name) {
            std::cerr << "rtdoctor: no process with pid " << request.pid << "\n";
            return false;
        }
        std::set<int> tids;
        std::set<std::string> names;
        for (const auto& tid : proc.list(pid + "/task")) {
            tids.insert(std::atoi(tid.c_str()));
            if (const auto thread = proc.read_line(pid + "/task/" + tid + "/comm")) names.insert(*thread);
        }
        if (sees_host_pids()) {
            summary.target = "pid " + pid + " (" + *name + ", " + std::to_string(tids.size()) + " threads)";
            filter = [tids](int tid, const std::string&) { return tids.count(tid) > 0; };
        } else {
            // Inside a container the kernel traces other ids than this
            // container shows, and they cannot be mapped. Match the
            // process's thread names instead (which may also match threads
            // of the same name in other processes).
            std::string list;
            for (const auto& thread : names) list += (list.empty() ? "\"" : ", \"") + thread + "\"";
            summary.target = "threads named " + list + " (pid " + pid + " in a container)";
            if (!quiet) {
                std::cerr << "rtdoctor: inside a container; matching the thread names of pid " << pid
                          << " instead of its ids.\n";
            }
            filter = [names](int, const std::string& thread) { return names.count(thread) > 0; };
        }
    } else {
        const std::string name = request.comm.substr(0, 15);  // The kernel keeps 15 characters.
        summary.target = "threads named \"" + name + "\"";
        filter = [name](int, const std::string& thread) { return thread == name; };
    }

    auto session = FtraceSession::start(latency_events(), 16384, result.error);
    if (!session) return true;
    stop_requested = false;
    signal(SIGINT, [](int) { stop_requested = true; });
    signal(SIGTERM, [](int) { stop_requested = true; });
    if (!quiet) {
        std::cerr << "Tracing " << summary.target << " for " << request.duration << " s (Ctrl-C to stop early)...\n";
    }

    AnalyzerOptions options;
    options.deadline = summary.deadline;
    LatencyAnalyzer analyzer(filter, options);
    const auto start = std::chrono::steady_clock::now();
    session->run(
        std::chrono::duration<double>(request.duration),
        [&](const std::string& line) {
            if (const auto event = parse_trace_line(line)) analyzer.feed(*event);
        },
        [] { return !stop_requested; });
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    summary.duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    summary.lost_events = session->lost_events();
    result.threads = analyzer.reports();
    return true;
}

bool write_report(const std::string& path, const SystemView& system, const std::vector<CheckResult>& results,
                  const std::optional<TraceResult>& trace, std::size_t max_threads, Lang lang,
                  const std::string& version) {
    auto host = read_host_info(system);
    utsname name{};
    if (uname(&name) == 0) host.machine = name.machine;
    host.version = version;
    char generated[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(generated, sizeof(generated), "%Y-%m-%d %H:%M:%S %Z", std::localtime(&now));
    host.generated = generated;

    std::ofstream file(path);
    if (file) {
        print_html(file, host, results, trace, max_threads, lang);
        file.close();
    }
    if (!file) {
        std::cerr << "rtdoctor: cannot write " << path << "\n";
        return false;
    }
    return true;
}

}  // namespace rtdoctor
