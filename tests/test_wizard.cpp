#include <gtest/gtest.h>

#include <string>

#include "fake_system.hpp"
#include "rtdoctor/wizard.hpp"

using namespace rtdoctor;

namespace {

// /proc/<pid>/stat with the fields the wizard reads: flags (field 9),
// rt_priority (40) and policy (41).
std::string stat(int pid, const std::string& comm, unsigned long flags, int rt_priority, int policy) {
    std::string line = std::to_string(pid) + " (" + comm + ") S";
    for (int field = 4; field <= 41; ++field) {
        line += " " + std::to_string(field == 9 ? flags : field == 40 ? rt_priority : field == 41 ? policy : 0);
    }
    return line + "\n";
}

// One process with one thread per (tid, name, wakeups, rt priority, policy).
void process(FakeSystem& fake, int pid, const std::string& name, unsigned long flags,
             std::initializer_list<std::tuple<int, const char*, long long, int, int>> threads) {
    const std::string base = "proc/" + std::to_string(pid);
    fake.file(base + "/comm", name + "\n").file(base + "/stat", stat(pid, name, flags, 0, 0));
    for (const auto& [tid, thread, wakeups, priority, policy] : threads) {
        const std::string task = base + "/task/" + std::to_string(tid);
        fake.file(task + "/comm", std::string(thread) + "\n")
            .file(task + "/stat", stat(tid, thread, flags, priority, policy))
            .file(task + "/status", "Name:\t" + std::string(thread) + "\nvoluntary_ctxt_switches:\t" +
                                        std::to_string(wakeups) + "\nnonvoluntary_ctxt_switches:\t3\n");
    }
}

// The same machine one second apart: wakeup counters grow at given rates.
void machine(FakeSystem& fake, int second) {
    process(fake, 2, "kthreadd", 0x00200000, {{2, "kthreadd", 500L * second, 0, 0}});
    process(fake, 300, "ros2_node", 0, {{300, "ros2_node", 2L * second, 0, 0},
                                        {301, "control_loop", 1000L * second, 80, 1}});
    process(fake, 400, "logger", 0, {{400, "logger", 50L * second, 0, 0}});
    process(fake, 500, "sshd", 0, {{500, "sshd", 0, 0, 0}});
    process(fake, 600, "planner", 0, {{600, "planner", 100L * second, 0, 0}});
    process(fake, 700, "rtdoctor", 0, {{700, "rtdoctor", 900L * second, 0, 0}});
}

}  // namespace

TEST(Wizard, ReadsThreadsWithPolicyAndWakeups) {
    FakeSystem fake;
    machine(fake, 3);
    const auto processes = sample_processes(SystemView(fake.root()));
    ASSERT_EQ(processes.size(), 6u);
    const auto& node = processes[1];
    EXPECT_EQ(node.name, "ros2_node");
    EXPECT_FALSE(node.kernel_thread);
    ASSERT_EQ(node.threads.size(), 2u);
    EXPECT_EQ(node.threads[1].name, "control_loop");
    EXPECT_EQ(node.threads[1].wakeups, 3000);
    EXPECT_EQ(node.threads[1].policy, 1);
    EXPECT_EQ(node.threads[1].rt_priority, 80);
    EXPECT_TRUE(processes[0].kernel_thread);
}

TEST(Wizard, RealTimeFirstThenByWakeupsWithoutKernelThreadsOrItself) {
    FakeSystem before, after;
    machine(before, 1);
    machine(after, 2);
    const auto candidates =
        rank_candidates(sample_processes(SystemView(before.root())), sample_processes(SystemView(after.root())), 1.0, 700);
    ASSERT_EQ(candidates.size(), 3u);  // Not kthreadd (kernel), rtdoctor (itself) or sshd (idle).
    EXPECT_EQ(candidates[0].name, "ros2_node");
    EXPECT_EQ(candidates[0].policy, "FIFO");
    EXPECT_EQ(candidates[0].rt_priority, 80);
    EXPECT_EQ(candidates[0].busiest_thread, "control_loop");
    EXPECT_DOUBLE_EQ(candidates[0].wakeups_per_second, 1000);
    EXPECT_EQ(candidates[1].name, "planner");
    EXPECT_EQ(candidates[2].name, "logger");
}

TEST(Wizard, LanguageFollowsTheLocale) {
    setenv("LC_ALL", "ja_JP.UTF-8", 1);
    EXPECT_EQ(lang_from_environment(), Lang::Ja);
    setenv("LC_ALL", "C.UTF-8", 1);
    EXPECT_EQ(lang_from_environment(), Lang::En);
    unsetenv("LC_ALL");
}
