// rtdoctor: checks how well a Linux system can run real-time work, and finds
// out what delays a given task.
//
//   rtdoctor check                 human-readable report
//   rtdoctor check --json          machine-readable report
//   rtdoctor check --fail-on warn  exit code 1 on any warning (for CI)
//   rtdoctor trace --pid 1234 --deadline-ms 4
//   rtdoctor trace --comm brake --duration 30
//   rtdoctor report -o report.html --comm brake --deadline-ms 4
//   rtdoctor                       guided mode (also: rtdoctor wizard)
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>

#include "rtdoctor/analyzer.hpp"
#include "rtdoctor/checks.hpp"
#include "rtdoctor/ftrace.hpp"
#include "rtdoctor/html_report.hpp"
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
                 "       rtdoctor report [-o FILE] [--pid PID | --comm NAME] [--duration SECONDS]\n"
                 "                       [--deadline-ms MS] [--threads N] [--lang en|ja] [--root DIR]\n"
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
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) output = argv[++i];
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

    std::optional<rtdoctor::TraceResult> traced;
    if (request.wanted()) {
        traced.emplace();
        if (!rtdoctor::run_trace(request, false, *traced)) return 2;
        if (!traced->error.empty()) std::cerr << "rtdoctor: tracing not possible: " << traced->error << "\n";
    }

    if (!rtdoctor::write_report(output, system, results, traced, request.max_threads, lang, RTDOCTOR_VERSION)) return 2;
    std::cerr << "Wrote " << output << "\n";
    return 0;
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
    if (argc >= 2 && std::strcmp(argv[1], "wizard") == 0) return wizard(argc - 2, argv + 2);
    // Without a command, someone at a terminal gets the guided mode.
    if (argc == 1 && isatty(STDIN_FILENO)) return wizard(0, argv);
    return usage();
}
