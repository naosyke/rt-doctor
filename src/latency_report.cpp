#include "rtdoctor/latency_report.hpp"

#include <algorithm>
#include <cstdio>
#include <map>

#include "rtdoctor/output.hpp"
#include "rtdoctor/system_view.hpp"
#include "rtdoctor/trace_report.hpp"

namespace rtdoctor {

namespace {

std::string us(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), value < 10 ? "%.1f us" : "%.0f us", value);
    return text;
}

std::vector<int> cpu_numbers(const LatencyResult& result) {
    std::vector<int> cpus;
    for (const auto& c : result.cpus) cpus.push_back(c.cpu);
    return cpus;
}

}  // namespace

const char* const kTraceOverhead = "rtdoctor itself (reading the trace)";

LatencyCauses latency_causes(const std::vector<ThreadReport>& threads, int self, std::size_t worst) {
    const std::string own = "task rtdoctor (pid " + std::to_string(self) + ")";
    const auto name = [&](const std::string& who) { return self > 0 && who == own ? std::string(kTraceOverhead) : who; };
    LatencyCauses causes;
    std::map<std::string, Delay> totals;
    for (const auto& thread : threads) {
        for (const auto& delay : thread.delays) {
            auto& total = totals[name(delay.who)];
            total.who = name(delay.who);
            total.seconds += delay.seconds;
            total.count += delay.count;
        }
        for (auto activation : thread.worst) {
            for (auto& delay : activation.delays) delay.who = name(delay.who);
            causes.worst.push_back(activation);
        }
    }
    for (const auto& [who, delay] : totals) causes.delays.push_back(delay);
    std::sort(causes.delays.begin(), causes.delays.end(),
              [](const Delay& a, const Delay& b) { return a.seconds > b.seconds; });
    std::sort(causes.worst.begin(), causes.worst.end(),
              [](const Activation& a, const Activation& b) { return a.waited > b.waited; });
    if (causes.worst.size() > worst) causes.worst.resize(worst);
    return causes;
}

const CpuLatency* worst_cpu(const LatencyResult& result) {
    const CpuLatency* worst = nullptr;
    for (const auto& c : result.cpus) {
        if (c.stats.samples() > 0 && (!worst || c.stats.max() > worst->stats.max())) worst = &c;
    }
    return worst;
}

void print_latency_text(std::ostream& out, const LatencyResult& result, const std::optional<LatencyCauses>& causes) {
    const auto& o = result.options;
    char line[256];
    std::snprintf(line, sizeof(line), "rt-doctor latency - %.1f s, timer every %d us, %s on CPUs %s\n", result.duration,
                  o.interval_us, result.realtime ? ("SCHED_FIFO " + std::to_string(o.priority)).c_str() : "normal priority",
                  format_cpu_list(cpu_numbers(result)).c_str());
    out << line;
    if (!result.realtime) {
        out << "  Note: real-time priority not allowed (run as root); these are latencies of a normal task.\n";
    }
    if (!result.memory_locked) out << "  Note: memory could not be locked; page faults may add to the numbers.\n";

    out << "\n";
    std::snprintf(line, sizeof(line), "  %4s %10s %9s %9s %9s %9s %10s\n", "CPU", "wake-ups", "min", "avg", "99%",
                  "99.9%", "max");
    out << line;
    for (const auto& c : result.cpus) {
        const auto& s = c.stats;
        std::snprintf(line, sizeof(line), "  %4d %10lld %9s %9s %9s %9s %10s\n", c.cpu,
                      static_cast<long long>(s.samples()), us(s.min()).c_str(), us(s.average()).c_str(),
                      us(s.percentile(0.99)).c_str(), us(s.percentile(0.999)).c_str(), us(s.max()).c_str());
        out << line;
    }

    if (const auto* worst = worst_cpu(result)) {
        out << "\n  Worst: " << us(worst->stats.max()) << " on CPU " << worst->cpu << " -> "
            << latency_verdict(worst->stats.max()) << "\n";
    }

    if (causes) {
        if (causes->lost_events > 0) {
            out << "\n  Note: the kernel dropped " << causes->lost_events
                << " trace events; the causes below may be incomplete.\n";
        }
        out << "\n  What ran on the CPU while a measuring thread was woken but not yet running\n";
        if (causes->delays.empty()) {
            out << "    Nothing: once woken, the threads ran at once. The latency is in the wake-up itself\n"
                   "    (timer interrupt delayed, e.g. interrupts disabled or the CPU waking from idle).\n";
        }
        for (std::size_t i = 0; i < causes->delays.size() && i < 5; ++i) {
            const auto& d = causes->delays[i];
            std::snprintf(line, sizeof(line), "    %10s  %-44s %6d times\n", format_ms(d.seconds).c_str(), d.who.c_str(),
                          d.count);
            out << line;
        }
        if (!causes->delays.empty()) out << "    -> " << advice_for(causes->delays.front().who) << "\n";
        if (!causes->worst.empty()) {
            out << "\n  Slowest wake-ups\n";
            for (const auto& a : causes->worst) {
                std::snprintf(line, sizeof(line), "    CPU %d  waited %s", a.cpu, us(a.waited * 1e6).c_str());
                out << line;
                for (std::size_t d = 0; d < a.delays.size() && d < 3; ++d) {
                    out << (d ? ", " : ": ") << a.delays[d].who << " " << us(a.delays[d].seconds * 1e6);
                }
                out << "\n";
            }
        }
    }
}

void print_latency_json(std::ostream& out, const LatencyResult& result, const std::optional<LatencyCauses>& causes) {
    char number[64];
    const auto num = [&](double value) {
        std::snprintf(number, sizeof(number), "%.3f", value);
        return std::string(number);
    };
    out << "{\"duration_s\":" << num(result.duration) << ",\"interval_us\":" << result.options.interval_us
        << ",\"realtime\":" << (result.realtime ? "true" : "false") << ",\"priority\":" << result.options.priority
        << ",\"memory_locked\":" << (result.memory_locked ? "true" : "false") << ",\"cpus\":[";
    for (std::size_t i = 0; i < result.cpus.size(); ++i) {
        const auto& c = result.cpus[i];
        const auto& s = c.stats;
        out << (i ? "," : "") << "{\"cpu\":" << c.cpu << ",\"samples\":" << s.samples() << ",\"min_us\":" << num(s.min())
            << ",\"avg_us\":" << num(s.average()) << ",\"p99_us\":" << num(s.percentile(0.99))
            << ",\"p999_us\":" << num(s.percentile(0.999)) << ",\"max_us\":" << num(s.max()) << "}";
    }
    out << "]";
    if (const auto* worst = worst_cpu(result)) {
        out << ",\"worst\":{\"cpu\":" << worst->cpu << ",\"max_us\":" << num(worst->stats.max()) << ",\"verdict\":\""
            << json_escape(latency_verdict(worst->stats.max())) << "\"}";
    }
    if (causes) {
        out << ",\"causes\":[";
        for (std::size_t i = 0; i < causes->delays.size(); ++i) {
            const auto& d = causes->delays[i];
            out << (i ? "," : "") << "{\"who\":\"" << json_escape(d.who) << "\",\"us\":" << num(d.seconds * 1e6)
                << ",\"count\":" << d.count << "}";
        }
        out << "],\"lost_events\":" << causes->lost_events;
    }
    out << "}\n";
}

}  // namespace rtdoctor
