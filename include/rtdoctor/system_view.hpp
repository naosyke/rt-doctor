// Read-only access to the parts of /proc and /sys that rt-doctor inspects.
// The root is configurable, so checks can be tested against fake trees.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rtdoctor {

class SystemView {
public:
    explicit SystemView(std::string root = "/");

    // `path` is relative to the root, e.g. "proc/cmdline".
    std::optional<std::string> read(const std::string& path) const;
    // First line, without the trailing newline.
    std::optional<std::string> read_line(const std::string& path) const;
    bool exists(const std::string& path) const;
    // Entry names in a directory, sorted; empty if it does not exist.
    std::vector<std::string> list(const std::string& path) const;

private:
    std::string full(const std::string& path) const;
    std::string root_;
};

// Limits of the process that would run the real-time workload (here: the
// process running rt-doctor). Not part of the file system, so passed in.
struct ProcessLimits {
    bool root = false;                       // euid 0 (has every capability).
    std::optional<std::uint64_t> memlock;    // RLIMIT_MEMLOCK in bytes; nullopt = unlimited.
    std::optional<std::uint64_t> rtprio;     // RLIMIT_RTPRIO; nullopt = unlimited.
};

ProcessLimits current_process_limits();

// CPU list helpers: "0-2,5" <-> {0,1,2,5}.
std::vector<int> parse_cpu_list(const std::string& text);
std::string format_cpu_list(std::vector<int> cpus);

}  // namespace rtdoctor
