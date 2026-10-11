#include "rtdoctor/trace_report.hpp"

#include <algorithm>
#include <cstdio>

#include "rtdoctor/output.hpp"

namespace rtdoctor {

std::string advice_for(const std::string& who) {
    if (who == "idle") {
        return "The CPU was idle but the task still did not run: slow wake-ups from deep idle states, or a "
               "virtual machine being paused. See rtdoctor check (Deep CPU idle states, Virtual machine).";
    }
    if (who.rfind("rtdoctor itself", 0) == 0) {
        return "This is the cost of --trace: rtdoctor reads the trace on the measured CPUs. Compare with a run "
               "without --trace for the undisturbed numbers.";
    }
    if (who.rfind("irq ", 0) == 0) {
        return "An interrupt handler ran on the task's CPU. Route that interrupt to another CPU "
               "(/proc/irq/<n>/smp_affinity_list), or isolate the task's CPU.";
    }
    if (who.rfind("softirq ", 0) == 0) {
        return "Deferred interrupt work (softirq) ran on the task's CPU. A PREEMPT_RT kernel runs it in "
               "threads that a real-time task can preempt; otherwise move the interrupt source elsewhere.";
    }
    return "Another task ran on the task's CPU while it was ready. Move that task to another CPU (taskset, "
           "cgroup cpuset), or give the delayed task a real-time priority (chrt -f) so it preempts the other one.";
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(fraction * (values.size() - 1) + 0.5);
    return values[std::min(index, values.size() - 1)];
}

void print_trace_text(std::ostream& out, const TraceSummary& summary, const std::vector<ThreadReport>& threads,
                      std::size_t max_threads) {
    char line[512];
    std::snprintf(line, sizeof(line), "rt-doctor trace - %s, %.1f s", summary.target.c_str(), summary.duration);
    out << line;
    if (summary.deadline > 0) out << ", deadline " << format_ms(summary.deadline);
    out << "\n";
    if (summary.lost_events > 0) {
        out << "  Note: the kernel dropped " << summary.lost_events
            << " trace events (buffer full); numbers may be incomplete. Trace a shorter time.\n";
    }
    if (threads.empty()) {
        out << "\n  The target never woke up and went back to sleep during the trace (no activations).\n"
               "  Is it running, and does it sleep between its periodic work?\n";
        return;
    }

    for (std::size_t i = 0; i < threads.size() && i < max_threads; ++i) {
        const auto& t = threads[i];
        out << "\nThread " << t.comm << " (tid " << t.tid << "): " << t.activations << " activations";
        if (summary.deadline > 0) out << ", " << t.misses << " over the deadline";
        out << "\n";
        out << "  Response time (wake-up to sleep): median " << format_ms(percentile(t.responses, 0.5)) << ", 99th "
            << format_ms(percentile(t.responses, 0.99)) << ", max " << format_ms(percentile(t.responses, 1.0)) << "\n";
        out << "  Waiting for its CPU: " << t.waits << " times, " << format_ms(t.waited_total) << " in total, max "
            << format_ms(t.waited_max) << " (" << t.preemptions << " preemptions)\n";

        if (!t.delays.empty()) {
            out << "\n  Who delayed it (ran on its CPU while it was ready)\n";
            for (std::size_t d = 0; d < t.delays.size() && d < 5; ++d) {
                std::snprintf(line, sizeof(line), "    %10s  %-44s %6d times\n", format_ms(t.delays[d].seconds).c_str(),
                              t.delays[d].who.c_str(), t.delays[d].count);
                out << line;
            }
            out << "    -> " << advice_for(t.delays[0].who) << "\n";
        }

        out << "\n  Worst activations\n";
        for (const auto& a : t.worst) {
            std::snprintf(line, sizeof(line), "    CPU %d  response %s  waited %s", a.cpu, format_ms(a.response).c_str(),
                          format_ms(a.waited).c_str());
            out << line;
            for (std::size_t d = 0; d < a.delays.size() && d < 3; ++d) {
                out << (d ? ", " : ": ") << a.delays[d].who << " " << format_ms(a.delays[d].seconds);
            }
            out << "\n";
        }
    }
    if (threads.size() > max_threads) {
        out << "\n  (" << threads.size() - max_threads << " more threads; use --threads N or --json)\n";
    }
}

void print_trace_json(std::ostream& out, const TraceSummary& summary, const std::vector<ThreadReport>& threads) {
    char number[64];
    const auto ms = [&](double seconds) {
        std::snprintf(number, sizeof(number), "%.3f", seconds * 1000.0);
        return std::string(number);
    };
    out << "{\"target\":\"" << json_escape(summary.target) << "\",\"duration_s\":" << summary.duration
        << ",\"deadline_ms\":" << ms(summary.deadline) << ",\"lost_events\":" << summary.lost_events << ",\"threads\":[";
    for (std::size_t i = 0; i < threads.size(); ++i) {
        const auto& t = threads[i];
        out << (i ? "," : "") << "{\"tid\":" << t.tid << ",\"comm\":\"" << json_escape(t.comm)
            << "\",\"activations\":" << t.activations << ",\"misses\":" << t.misses
            << ",\"response_ms\":{\"p50\":" << ms(percentile(t.responses, 0.5))
            << ",\"p99\":" << ms(percentile(t.responses, 0.99)) << ",\"max\":" << ms(percentile(t.responses, 1.0))
            << "},\"waits\":" << t.waits << ",\"waited_total_ms\":" << ms(t.waited_total)
            << ",\"waited_max_ms\":" << ms(t.waited_max) << ",\"preemptions\":" << t.preemptions << ",\"delays\":[";
        for (std::size_t d = 0; d < t.delays.size(); ++d) {
            out << (d ? "," : "") << "{\"who\":\"" << json_escape(t.delays[d].who) << "\",\"ms\":"
                << ms(t.delays[d].seconds) << ",\"count\":" << t.delays[d].count << "}";
        }
        out << "],\"worst\":[";
        for (std::size_t w = 0; w < t.worst.size(); ++w) {
            const auto& a = t.worst[w];
            out << (w ? "," : "") << "{\"cpu\":" << a.cpu << ",\"response_ms\":" << ms(a.response)
                << ",\"waited_ms\":" << ms(a.waited) << ",\"delays\":[";
            for (std::size_t d = 0; d < a.delays.size(); ++d) {
                out << (d ? "," : "") << "{\"who\":\"" << json_escape(a.delays[d].who) << "\",\"ms\":"
                    << ms(a.delays[d].seconds) << "}";
            }
            out << "]}";
        }
        out << "]}";
    }
    out << "]}\n";
}

}  // namespace rtdoctor
