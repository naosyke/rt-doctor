#include "rtdoctor/ftrace.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

namespace rtdoctor {

namespace {

const std::string kRoot = "/sys/kernel/tracing";

bool write_text(const std::string& path, const std::string& value) {
    FILE* file = std::fopen(path.c_str(), "w");
    if (!file) return false;
    const bool ok = std::fputs(value.c_str(), file) >= 0;
    return (std::fclose(file) == 0) && ok;
}

}  // namespace

const std::vector<std::string>& latency_events() {
    static const std::vector<std::string> events = {
        "sched/sched_switch",       "sched/sched_waking",    "sched/sched_migrate_task",
        "irq/irq_handler_entry",    "irq/irq_handler_exit",  "irq/softirq_entry",
        "irq/softirq_exit",
    };
    return events;
}

std::unique_ptr<FtraceSession> FtraceSession::start(const std::vector<std::string>& events, int buffer_kib,
                                                    std::string& error) {
    struct stat info {};
    if (stat((kRoot + "/instances").c_str(), &info) != 0) {
        mkdir(kRoot.c_str(), 0755);
        if (mount("nodev", kRoot.c_str(), "tracefs", 0, nullptr) != 0) {
            error = "cannot access ftrace (" + kRoot + "): " + std::strerror(errno) +
                    ". Run as root (in a container: --privileged).";
            return nullptr;
        }
    }
    std::random_device random;
    char name[40];
    std::snprintf(name, sizeof(name), "/instances/rtdoctor-%08x", random());
    const std::string directory = kRoot + name;
    if (mkdir(directory.c_str(), 0755) != 0) {
        error = "cannot create ftrace instance " + directory + ": " + std::strerror(errno);
        return nullptr;
    }
    std::unique_ptr<FtraceSession> session(new FtraceSession(directory));

    bool ok = write_text(directory + "/tracing_on", "0") && write_text(directory + "/trace_clock", "mono") &&
              write_text(directory + "/buffer_size_kb", std::to_string(buffer_kib));
    for (const auto& event : events) {
        if (!write_text(directory + "/events/" + event + "/enable", "1")) {
            // irq events may be missing on some kernels; the scheduler ones are required.
            if (event.rfind("sched/", 0) == 0) ok = false;
        }
    }
    session->pipe_ = open((directory + "/trace_pipe").c_str(), O_RDONLY | O_NONBLOCK);
    if (!ok || session->pipe_ < 0 || !write_text(directory + "/tracing_on", "1")) {
        error = "cannot configure ftrace instance " + directory + ": " + std::strerror(errno);
        return nullptr;
    }
    return session;
}

FtraceSession::FtraceSession(std::string directory) : directory_(std::move(directory)) {}

FtraceSession::~FtraceSession() {
    write_text(directory_ + "/tracing_on", "0");
    if (pipe_ >= 0) close(pipe_);
    rmdir(directory_.c_str());
}

void FtraceSession::read_available(std::string& pending, const std::function<void(const std::string&)>& on_line,
                                   int timeout_ms) {
    pollfd descriptor{pipe_, POLLIN, 0};
    if (poll(&descriptor, 1, timeout_ms) <= 0) return;
    char buffer[1 << 16];
    const ssize_t length = read(pipe_, buffer, sizeof(buffer));
    if (length <= 0) return;
    pending.append(buffer, static_cast<std::size_t>(length));
    std::size_t start = 0;
    for (auto end = pending.find('\n'); end != std::string::npos; end = pending.find('\n', start)) {
        on_line(pending.substr(start, end - start));
        start = end + 1;
    }
    pending.erase(0, start);
}

void FtraceSession::run(std::chrono::duration<double> duration,
                        const std::function<void(const std::string&)>& on_line,
                        const std::function<bool()>& keep_going) {
    const auto end = std::chrono::steady_clock::now() + duration;
    std::string pending;
    while (std::chrono::steady_clock::now() < end && keep_going()) {
        read_available(pending, on_line, 50);
    }
    write_text(directory_ + "/tracing_on", "0");
    // Drain what is still buffered.
    for (int idle = 0; idle < 3;) {
        const auto before = pending.size();
        read_available(pending, on_line, 20);
        idle = pending.size() == before ? idle + 1 : 0;
    }
}

long long FtraceSession::lost_events() const {
    long long lost = 0;
    for (int cpu = 0; cpu < 1024; ++cpu) {
        std::ifstream stats(directory_ + "/per_cpu/cpu" + std::to_string(cpu) + "/stats");
        if (!stats) break;
        std::string key;
        long long value = 0;
        while (stats >> key) {
            if (key == "overrun:" && stats >> value) lost += value;
        }
    }
    return lost;
}

}  // namespace rtdoctor
