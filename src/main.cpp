// rtdoctor: checks how well a Linux system can run real-time work, and finds
// out what delays a given task.
//
//   rtdoctor check                 human-readable report
//   rtdoctor check --json          machine-readable report
//   rtdoctor check --fail-on warn  exit code 1 on any warning (for CI)
//   rtdoctor trace --pid 1234 --deadline-ms 4
//   rtdoctor trace --comm brake --duration 30
//   rtdoctor report -o report.html --comm brake --deadline-ms 4
//   rtdoctor latency --duration 60 --trace
//   rtdoctor                       guided mode (also: rtdoctor wizard)
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "rtdoctor/analyzer.hpp"
#include "rtdoctor/checks.hpp"
#include "rtdoctor/ftrace.hpp"
#include "rtdoctor/html_report.hpp"
#include "rtdoctor/latency.hpp"
#include "rtdoctor/latency_report.hpp"
#include "rtdoctor/output.hpp"
#include "rtdoctor/session.hpp"
#include "rtdoctor/trace_report.hpp"
#include "rtdoctor/wizard.hpp"

#ifndef RTDOCTOR_VERSION
#define RTDOCTOR_VERSION "dev"
#endif

namespace {

int usage() {
    std::cerr << "usage: rtdoctor                  (guided: check, pick a program, trace, report)\n"
                 "       rtdoctor wizard [--lang en|ja]\n"
                 "       rtdoctor check [--json] [--verbose] [--fail-on warn|fail] [--root DIR]\n"
                 "       rtdoctor trace (--pid PID | --comm NAME) [--duration SECONDS] [--deadline-ms MS]\n"
                 "                      [--threads N] [--json]\n"
                 "       rtdoctor latency [--duration SECONDS] [--interval-us US] [--priority 1-99] [--cpus LIST]\n"
                 "                        [--trace] [--fail-over-us US] [--json]\n"
                 "       rtdoctor report [-o FILE] [--pid PID | --comm NAME] [--duration SECONDS]\n"
                 "                       [--deadline-ms MS] [--threads N] [--latency SECONDS] [--lang en|ja] [--root DIR]\n"
                 "       rtdoctor --version\n";
    return 2;
}

int trace(int argc, char** argv) {
    rtdoctor::TraceRequest request;
    bool json = false;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) json = true;
        else if (!request.parse(argc, argv, i)) return usage();
    }
    if (!request.valid()) return usage();

    rtdoctor::TraceResult result;
    if (!rtdoctor::run_trace(request, json, result)) return 2;
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
    rtdoctor::TraceRequest request;
    std::string output = "rtdoctor-report.html", root = "/";
    rtdoctor::Lang lang = rtdoctor::Lang::En;
    double latency_seconds = 0;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) output = argv[++i];
        else if (arg == "--latency" && i + 1 < argc) latency_seconds = std::atof(argv[++i]);
        else if (arg == "--root" && i + 1 < argc) root = argv[++i];
        else if (arg == "--lang" && i + 1 < argc) {
            const auto parsed = rtdoctor::parse_lang(argv[++i]);
            if (!parsed) return usage();
            lang = *parsed;
        } else if (!request.parse(argc, argv, i)) return usage();
    }
    if (request.wanted() && !request.valid()) return usage();

    const rtdoctor::SystemView system(root);
    const auto results = rtdoctor::run_checks(system, rtdoctor::current_process_limits());

    std::optional<rtdoctor::LatencyData> latency;
    if (latency_seconds > 0) {
        std::cerr << "Measuring wake-up latency for " << latency_seconds << " s...\n";
        rtdoctor::LatencyOptions options;
        options.duration = latency_seconds;
        latency = rtdoctor::LatencyData{rtdoctor::measure_latency(options), std::nullopt};
    }

    std::optional<rtdoctor::TraceResult> traced;
    if (request.wanted()) {
        traced.emplace();
        if (!rtdoctor::run_trace(request, false, *traced)) return 2;
        if (!traced->error.empty()) std::cerr << "rtdoctor: tracing not possible: " << traced->error << "\n";
    }

    if (!rtdoctor::write_report(output, system, results, traced, request.max_threads, lang, RTDOCTOR_VERSION,
                                 latency)) {
        return 2;
    }
    std::cerr << "Wrote " << output << "\n";
    return 0;
}

std::atomic<bool> interrupted{false};

int latency(int argc, char** argv) {
    rtdoctor::LatencyOptions options;
    bool json = false, traced = false;
    double fail_over_us = 0;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") json = true;
        else if (arg == "--trace") traced = true;
        else if (arg == "--duration" && i + 1 < argc) options.duration = std::atof(argv[++i]);
        else if (arg == "--interval-us" && i + 1 < argc) options.interval_us = std::atoi(argv[++i]);
        else if (arg == "--priority" && i + 1 < argc) options.priority = std::atoi(argv[++i]);
        else if (arg == "--cpus" && i + 1 < argc) options.cpus = rtdoctor::parse_cpu_list(argv[++i]);
        else if (arg == "--fail-over-us" && i + 1 < argc) fail_over_us = std::atof(argv[++i]);
        else return usage();
    }
    if (options.duration <= 0 || options.interval_us < 50 || options.priority < 1 || options.priority > 99) {
        return usage();
    }

    signal(SIGINT, [](int) { interrupted = true; });
    signal(SIGTERM, [](int) { interrupted = true; });
    const auto keep_going = [] { return !interrupted.load(); };

    // With --trace, record the scheduler while measuring and find out what
    // ran instead of the measuring threads when they were woken.
    std::unique_ptr<rtdoctor::FtraceSession> session;
    std::optional<rtdoctor::LatencyCauses> causes;
    if (traced) {
        std::string error;
        session = rtdoctor::FtraceSession::start(rtdoctor::latency_events(), 16384, error);
        if (!session) {
            std::cerr << "rtdoctor: " << error << "\n";
            return 3;
        }
    }
    rtdoctor::LatencyAnalyzer analyzer(
        [](int, const std::string& name) { return rtdoctor::is_latency_thread(name); });
    if (!json) {
        std::cerr << "Measuring wake-up latency for " << options.duration << " s"
                  << (traced ? " with a trace" : "") << " (Ctrl-C to stop early)...\n";
    }
    const auto result = rtdoctor::measure_latency(options, [&](double duration) {
        if (session) {
            session->run(
                std::chrono::duration<double>(duration),
                [&](const std::string& line) {
                    if (const auto event = rtdoctor::parse_trace_line(line)) analyzer.feed(*event);
                },
                keep_going);
        } else {
            const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(duration);
            while (keep_going() && std::chrono::steady_clock::now() < end) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    });
    if (session) {
        causes = rtdoctor::latency_causes(analyzer.reports(), getpid());
        causes->lost_events = session->lost_events();
    }

    if (json) rtdoctor::print_latency_json(std::cout, result, causes);
    else rtdoctor::print_latency_text(std::cout, result, causes);
    const auto* worst = rtdoctor::worst_cpu(result);
    return fail_over_us > 0 && worst && worst->stats.max() > fail_over_us ? 1 : 0;
}

int wizard(int argc, char** argv) {
    rtdoctor::Lang lang = rtdoctor::lang_from_environment();
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            const auto parsed = rtdoctor::parse_lang(argv[++i]);
            if (!parsed) return usage();
            lang = *parsed;
        } else {
            return usage();
        }
    }
    return rtdoctor::run_wizard(std::cin, std::cout, lang, RTDOCTOR_VERSION);
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
    if (argc >= 2 && std::strcmp(argv[1], "latency") == 0) return latency(argc - 2, argv + 2);
    if (argc >= 2 && std::strcmp(argv[1], "wizard") == 0) return wizard(argc - 2, argv + 2);
    // Without a command, someone at a terminal gets the guided mode.
    if (argc == 1 && isatty(STDIN_FILENO)) return wizard(0, argv);
    return usage();
}
