#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rtdoctor/analyzer.hpp"
#include "rtdoctor/trace_event.hpp"

using namespace rtdoctor;

TEST(TraceEvent, ParsesSchedSwitch) {
    const auto event = parse_trace_line(
        "           brake-1234    [001] d..2.  4410.303921: sched_switch: prev_comm=brake prev_pid=1234 "
        "prev_prio=19 prev_state=S ==> next_comm=swapper/1 next_pid=0 next_prio=120");
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->cpu, 1);
    EXPECT_DOUBLE_EQ(event->time, 4410.303921);
    EXPECT_EQ(event->name, "sched_switch");
    EXPECT_EQ(event->field("prev_comm"), "brake");
    EXPECT_EQ(event->int_field("next_pid"), 0);
    EXPECT_EQ(event->field("prev_state"), "S");
}

TEST(TraceEvent, ParsesIrqAndSoftirq) {
    const auto irq = parse_trace_line("  <idle>-0 [002] d.h1. 10.5: irq_handler_entry: irq=13 name=arch_timer");
    ASSERT_TRUE(irq.has_value());
    EXPECT_EQ(irq->name, "irq_handler_entry");
    EXPECT_EQ(irq->field("name"), "arch_timer");

    const auto softirq = parse_trace_line("  ksoftirqd/2-27 [002] ..s1. 10.6: softirq_entry: vec=1 [action=TIMER]");
    ASSERT_TRUE(softirq.has_value());
    EXPECT_EQ(softirq->field("action"), "TIMER");
}

TEST(TraceEvent, IgnoresHeadersAndGarbage) {
    EXPECT_FALSE(parse_trace_line("# tracer: nop").has_value());
    EXPECT_FALSE(parse_trace_line("").has_value());
    EXPECT_FALSE(parse_trace_line("CPU:1 [LOST 12 EVENTS]").has_value());
}

namespace {

// Builds trace lines with a compact helper.
struct Script {
    std::vector<std::string> lines;

    Script& waking(double t, int waker_cpu, int pid, const char* comm, int target_cpu) {
        char line[256];
        std::snprintf(line, sizeof(line), " x-1 [%03d] d..2. %.6f: sched_waking: comm=%s pid=%d prio=120 target_cpu=%03d",
                      waker_cpu, t, comm, pid, target_cpu);
        lines.push_back(line);
        return *this;
    }
    Script& switch_(double t, int cpu, const char* prev, int prev_pid, const char* state, const char* next,
                    int next_pid) {
        char line[256];
        std::snprintf(line, sizeof(line),
                      " x-1 [%03d] d..2. %.6f: sched_switch: prev_comm=%s prev_pid=%d prev_prio=120 prev_state=%s "
                      "==> next_comm=%s next_pid=%d next_prio=120",
                      cpu, t, prev, prev_pid, state, next, next_pid);
        lines.push_back(line);
        return *this;
    }
    Script& irq(double t, int cpu, bool entry, int number = 13, const char* name = "arch_timer") {
        char line[256];
        if (entry) {
            std::snprintf(line, sizeof(line), " x-1 [%03d] d.h1. %.6f: irq_handler_entry: irq=%d name=%s", cpu, t,
                          number, name);
        } else {
            std::snprintf(line, sizeof(line), " x-1 [%03d] d.h1. %.6f: irq_handler_exit: irq=%d ret=handled", cpu, t,
                          number);
        }
        lines.push_back(line);
        return *this;
    }

    std::vector<ThreadReport> run(AnalyzerOptions options = {}) const {
        LatencyAnalyzer analyzer([](int, const std::string& comm) { return comm == "brake"; }, options);
        for (const auto& line : lines) {
            if (const auto event = parse_trace_line(line)) analyzer.feed(*event);
        }
        return analyzer.reports();
    }
};

}  // namespace

TEST(Analyzer, AttributesWakeupLatencyToTheRunningTask) {
    // CPU 1 runs "hog". brake wakes at 1.000 but only runs at 1.003; it blocks at 1.005.
    Script script;
    script.switch_(0.900, 1, "swapper/1", 0, "R", "hog", 77)
        .waking(1.000, 0, 1234, "brake", 1)
        .switch_(1.003, 1, "hog", 77, "R", "brake", 1234)
        .switch_(1.005, 1, "brake", 1234, "S", "hog", 77);

    const auto reports = script.run({0.004, 5});
    ASSERT_EQ(reports.size(), 1u);
    const auto& r = reports[0];
    EXPECT_EQ(r.tid, 1234);
    EXPECT_EQ(r.activations, 1);
    EXPECT_NEAR(r.responses[0], 0.005, 1e-9);
    EXPECT_EQ(r.misses, 1);  // 5 ms > 4 ms deadline
    EXPECT_NEAR(r.waited_max, 0.003, 1e-9);
    ASSERT_EQ(r.delays.size(), 1u);
    EXPECT_EQ(r.delays[0].who, "task hog (pid 77)");
    EXPECT_NEAR(r.delays[0].seconds, 0.003, 1e-9);
    ASSERT_EQ(r.worst.size(), 1u);
    EXPECT_EQ(r.worst[0].cpu, 1);
    EXPECT_EQ(r.worst[0].delays[0].who, "task hog (pid 77)");
}

TEST(Analyzer, InterruptsAreSeparatedFromTheTaskTheyInterrupt) {
    Script script;
    script.switch_(0.900, 1, "swapper/1", 0, "R", "hog", 77)
        .waking(1.000, 0, 1234, "brake", 1)
        .irq(1.001, 1, true)
        .irq(1.0015, 1, false)
        .switch_(1.002, 1, "hog", 77, "R", "brake", 1234)
        .switch_(1.004, 1, "brake", 1234, "S", "hog", 77);

    const auto r = script.run()[0];
    ASSERT_EQ(r.delays.size(), 2u);
    EXPECT_EQ(r.delays[0].who, "task hog (pid 77)");
    EXPECT_NEAR(r.delays[0].seconds, 0.0015, 1e-9);
    EXPECT_EQ(r.delays[1].who, "irq 13 arch_timer");
    EXPECT_NEAR(r.delays[1].seconds, 0.0005, 1e-9);
}

TEST(Analyzer, PreemptionCountsAsWaiting) {
    // brake runs, is preempted by "rt_other" for 2 ms, resumes and blocks.
    Script script;
    script.waking(1.000, 0, 1234, "brake", 1)
        .switch_(1.000, 1, "swapper/1", 0, "R", "brake", 1234)
        .switch_(1.001, 1, "brake", 1234, "R+", "rt_other", 99)
        .switch_(1.003, 1, "rt_other", 99, "S", "brake", 1234)
        .switch_(1.004, 1, "brake", 1234, "S", "swapper/1", 0);

    const auto r = script.run()[0];
    EXPECT_EQ(r.preemptions, 1);
    EXPECT_EQ(r.waits, 2);  // The (zero) wake-up wait and the preemption.
    EXPECT_NEAR(r.responses[0], 0.004, 1e-9);
    EXPECT_NEAR(r.worst[0].waited, 0.002, 1e-9);
    EXPECT_EQ(r.delays[0].who, "task rt_other (pid 99)");
}

TEST(Analyzer, IdleWakeUpIsReportedAsIdle) {
    // The CPU was idle; the target still only started 1 ms after its wake-up
    // (idle exit, or a paused virtual machine).
    Script script;
    script.switch_(0.900, 2, "brake", 1234, "S", "swapper/2", 0)
        .waking(1.000, 0, 1234, "brake", 2)
        .switch_(1.001, 2, "swapper/2", 0, "R", "brake", 1234)
        .switch_(1.002, 2, "brake", 1234, "S", "swapper/2", 0);

    const auto r = script.run()[0];
    EXPECT_EQ(r.activations, 1);
    ASSERT_EQ(r.delays.size(), 1u);
    EXPECT_EQ(r.delays[0].who, "idle");
    EXPECT_NEAR(r.delays[0].seconds, 0.001, 1e-9);
}

TEST(Analyzer, OnlyTargetThreadsAreReportedAndWorstIsKept) {
    Script script;
    for (int i = 0; i < 10; ++i) {
        const double t = 1.0 + i * 0.010;
        const double delay = i == 7 ? 0.004 : 0.0001;  // Activation 7 is the worst.
        script.waking(t, 0, 1234, "brake", 1)
            .switch_(t + delay, 1, "hog", 77, "R", "brake", 1234)
            .switch_(t + delay + 0.002, 1, "brake", 1234, "S", "hog", 77);
    }
    const auto reports = script.run({0, 3});
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].activations, 10);
    ASSERT_EQ(reports[0].worst.size(), 3u);
    EXPECT_NEAR(reports[0].worst[0].start, 1.07, 1e-9);
    EXPECT_NEAR(reports[0].worst[0].response, 0.006, 1e-9);
    EXPECT_EQ(reports[0].misses, 0);  // No deadline given.
}

TEST(Analyzer, MigrationMovesTheWait) {
    Script script;
    script.switch_(0.9, 1, "swapper/1", 0, "R", "hog", 77)
        .switch_(0.9, 3, "swapper/3", 0, "R", "other", 88)
        .waking(1.000, 0, 1234, "brake", 1);
    script.lines.push_back(" x-1 [000] d..2. 1.001000: sched_migrate_task: comm=brake pid=1234 prio=120 orig_cpu=1 dest_cpu=3");
    script.switch_(1.003, 3, "other", 88, "R", "brake", 1234).switch_(1.004, 3, "brake", 1234, "S", "other", 88);

    const auto r = script.run()[0];
    ASSERT_EQ(r.delays.size(), 2u);
    EXPECT_EQ(r.delays[0].who, "task other (pid 88)");  // 2 ms on CPU 3
    EXPECT_NEAR(r.delays[0].seconds, 0.002, 1e-9);
    EXPECT_EQ(r.delays[1].who, "task hog (pid 77)");    // 1 ms on CPU 1 before the move
    EXPECT_EQ(r.worst[0].cpu, 3);
}

TEST(Analyzer, ThreadRenamedAfterStartIsStillFound) {
    // The thread first runs under its parent's name, then names itself.
    LatencyAnalyzer analyzer([](int, const std::string& comm) { return comm == "control"; });
    Script script;
    script.switch_(0.9, 1, "swapper/1", 0, "R", "app", 50)
        .switch_(0.95, 1, "app", 50, "S", "swapper/1", 0)
        .waking(1.0, 0, 50, "control", 1)
        .switch_(1.0002, 1, "swapper/1", 0, "R", "control", 50)
        .switch_(1.001, 1, "control", 50, "S", "swapper/1", 0);
    for (const auto& line : script.lines) analyzer.feed(*parse_trace_line(line));
    const auto reports = analyzer.reports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].comm, "control");
    EXPECT_EQ(reports[0].activations, 1);
}
