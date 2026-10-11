// A private ftrace instance (/sys/kernel/tracing/instances/rtdoctor-<random>)
// with the scheduler and interrupt events rtdoctor needs. Other tracing on
// the system is not affected; the instance is removed on destruction.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rtdoctor {

class FtraceSession {
public:
    static std::unique_ptr<FtraceSession> start(const std::vector<std::string>& events, int buffer_kib,
                                                std::string& error);
    ~FtraceSession();

    FtraceSession(const FtraceSession&) = delete;
    FtraceSession& operator=(const FtraceSession&) = delete;

    // Calls `on_line` for every trace line until `duration` has passed or
    // `keep_going` returns false, then stops tracing and drains the buffer.
    void run(std::chrono::duration<double> duration, const std::function<void(const std::string&)>& on_line,
             const std::function<bool()>& keep_going);

    // Events the kernel dropped because the buffer was full.
    long long lost_events() const;

private:
    explicit FtraceSession(std::string directory);
    void read_available(std::string& pending, const std::function<void(const std::string&)>& on_line, int timeout_ms);

    std::string directory_;
    int pipe_ = -1;
};

// Events used by `rtdoctor trace`.
const std::vector<std::string>& latency_events();

}  // namespace rtdoctor
