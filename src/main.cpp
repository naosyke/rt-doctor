// rtdoctor: checks how well a Linux system can run real-time work.
//
//   rtdoctor check                 human-readable report
//   rtdoctor check --json          machine-readable report
//   rtdoctor check --fail-on warn  exit code 1 on any warning (for CI)
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>

#include "rtdoctor/checks.hpp"
#include "rtdoctor/output.hpp"

#ifndef RTDOCTOR_VERSION
#define RTDOCTOR_VERSION "dev"
#endif

namespace {

int usage() {
    std::cerr << "usage: rtdoctor check [--json] [--verbose] [--fail-on warn|fail] [--root DIR]\n"
                 "       rtdoctor --version\n";
    return 2;
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
    return usage();
}
