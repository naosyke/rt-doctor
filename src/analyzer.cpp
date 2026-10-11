#include "rtdoctor/analyzer.hpp"

#include <algorithm>
#include <cstdio>

namespace rtdoctor {

std::string format_ms(double seconds) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f ms", seconds * 1000.0);
    return text;
}

namespace {

std::vector<Delay> sorted(const std::map<std::string, Delay>& delays) {
    std::vector<Delay> list;
    for (const auto& [who, delay] : delays) list.push_back(delay);
    std::sort(list.begin(), list.end(), [](const Delay& a, const Delay& b) { return a.seconds > b.seconds; });
    return list;
}

std::string task_name(const std::string& comm, int pid) {
    if (pid == 0) return "idle";
    return "task " + comm + " (pid " + std::to_string(pid) + ")";
}

}  // namespace

LatencyAnalyzer::LatencyAnalyzer(Filter is_target, AnalyzerOptions options)
    : is_target_(std::move(is_target)), options_(options) {}

bool LatencyAnalyzer::target(int tid, const std::string& comm) {
    if (tid <= 0) return false;
    const auto cached = target_cache_.find(tid);
    if (cached != target_cache_.end()) return cached->second;
    const bool result = is_target_(tid, comm);
    target_cache_[tid] = result;
    if (result) {
        auto& thread = threads_[tid];
        thread.tid = tid;
        thread.comm = comm;
    }
    return result;
}

void LatencyAnalyzer::account(int cpu, double time) {
    auto& state = cpus_[cpu];
    if (state.last >= 0 && time > state.last) {
        const double dt = time - state.last;
        const std::string& who = state.interrupts.empty() ? state.running : state.interrupts.back();
        for (auto& [tid, thread] : threads_) {
            if (thread.state == State::Waiting && thread.episode.cpu == cpu) thread.episode.by_who[who] += dt;
        }
    }
    state.last = time;
}

void LatencyAnalyzer::start_waiting(Thread& thread, int cpu, double time) {
    thread.state = State::Waiting;
    thread.episode = Episode{time, cpu, {}};
}

void LatencyAnalyzer::stop_waiting(Thread& thread, double time) {
    const double waited = time - thread.episode.start;
    auto& report = thread.report;
    ++report.waits;
    report.waited_total += waited;
    report.waited_max = std::max(report.waited_max, waited);
    for (const auto& [who, seconds] : thread.episode.by_who) {
        auto& delay = thread.delays[who];
        delay.who = who;
        delay.seconds += seconds;
        ++delay.count;
        thread.current_delays[who] += seconds;
    }
    thread.current.waited += waited;
}

void LatencyAnalyzer::finish_activation(Thread& thread, double time) {
    auto& report = thread.report;
    auto activation = thread.current;
    activation.start = thread.activation_start;
    activation.response = time - thread.activation_start;
    activation.cpu = thread.cpu;
    for (const auto& [who, seconds] : thread.current_delays) activation.delays.push_back({who, seconds, 1});
    std::sort(activation.delays.begin(), activation.delays.end(),
              [](const Delay& a, const Delay& b) { return a.seconds > b.seconds; });

    ++report.activations;
    report.responses.push_back(activation.response);
    if (options_.deadline > 0 && activation.response > options_.deadline) ++report.misses;

    report.worst.push_back(activation);
    std::sort(report.worst.begin(), report.worst.end(),
              [](const Activation& a, const Activation& b) { return a.response > b.response; });
    if (report.worst.size() > options_.worst) report.worst.resize(options_.worst);

    thread.in_activation = false;
    thread.current = Activation{};
    thread.current_delays.clear();
}

void LatencyAnalyzer::feed(const TraceEvent& event) {
    const double t = event.time;
    const auto& name = event.name;

    if (name == "sched_waking" || name == "sched_wakeup") {
        const int tid = event.int_field("pid");
        if (!target(tid, event.field("comm"))) return;
        auto& thread = threads_[tid];
        if (thread.state == State::Running || thread.state == State::Waiting) return;  // Already awake.
        const int cpu = event.int_field("target_cpu", event.cpu);
        account(cpu, t);
        thread.in_activation = true;
        thread.activation_start = t;
        thread.current = Activation{};
        thread.current_delays.clear();
        thread.cpu = cpu;
        start_waiting(thread, cpu, t);
        return;
    }

    if (name == "sched_migrate_task") {
        const int tid = event.int_field("pid");
        const auto it = threads_.find(tid);
        if (it == threads_.end() || it->second.state != State::Waiting) return;
        const int destination = event.int_field("dest_cpu");
        account(it->second.episode.cpu, t);
        account(destination, t);
        it->second.episode.cpu = destination;
        it->second.cpu = destination;
        return;
    }

    if (name == "sched_switch") {
        const int cpu = event.cpu;
        account(cpu, t);
        const int prev = event.int_field("prev_pid");
        const int next = event.int_field("next_pid");

        if (target(prev, event.field("prev_comm"))) {
            auto& thread = threads_[prev];
            const auto& prev_state = event.field("prev_state");
            if (!prev_state.empty() && prev_state[0] == 'R') {
                // Preempted while runnable: it waits again on this CPU.
                if (thread.in_activation) ++thread.report.preemptions;
                start_waiting(thread, cpu, t);
            } else {
                if (thread.in_activation) finish_activation(thread, t);
                thread.state = State::Sleeping;
            }
        }
        if (target(next, event.field("next_comm"))) {
            auto& thread = threads_[next];
            if (thread.state == State::Waiting) stop_waiting(thread, t);
            thread.state = State::Running;
            thread.cpu = cpu;
        }
        cpus_[cpu].running = task_name(event.field("next_comm"), next);
        return;
    }

    if (name == "irq_handler_entry") {
        account(event.cpu, t);
        cpus_[event.cpu].interrupts.push_back("irq " + event.field("irq") + " " + event.field("name"));
        return;
    }
    if (name == "softirq_entry") {
        account(event.cpu, t);
        cpus_[event.cpu].interrupts.push_back("softirq " + event.field("action"));
        return;
    }
    if (name == "irq_handler_exit" || name == "softirq_exit") {
        account(event.cpu, t);
        auto& interrupts = cpus_[event.cpu].interrupts;
        if (!interrupts.empty()) interrupts.pop_back();
        return;
    }
}

std::vector<ThreadReport> LatencyAnalyzer::reports() const {
    std::vector<ThreadReport> reports;
    for (const auto& [tid, thread] : threads_) {
        if (thread.report.activations == 0) continue;
        auto report = thread.report;
        report.tid = tid;
        report.comm = thread.comm;
        report.delays = sorted(thread.delays);
        reports.push_back(std::move(report));
    }
    std::sort(reports.begin(), reports.end(), [](const ThreadReport& a, const ThreadReport& b) {
        const double worst_a = a.worst.empty() ? 0 : a.worst.front().response;
        const double worst_b = b.worst.empty() ? 0 : b.worst.front().response;
        return worst_a > worst_b;
    });
    return reports;
}

}  // namespace rtdoctor
