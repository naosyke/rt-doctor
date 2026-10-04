#include <gtest/gtest.h>

#include "fake_system.hpp"
#include "rtdoctor/checks.hpp"

using namespace rtdoctor;

namespace {

// A machine set up for real-time work.
void tuned_machine(FakeSystem& fake) {
    fake.file("proc/version", "Linux version 6.12.0-rt5 (gcc) #1 SMP PREEMPT_RT\n")
        .file("sys/kernel/realtime", "1\n")
        .file("proc/cmdline", "quiet isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 irqaffinity=0-1\n")
        .file("proc/irq/16/effective_affinity_list", "0\n")
        .file("proc/irq/17/effective_affinity_list", "1\n")
        .file("sys/devices/system/cpu/online", "0-3\n")
        .file("sys/devices/system/cpu/smt/active", "0\n")
        .file("proc/sys/kernel/sched_rt_runtime_us", "950000\n")
        .file("proc/sys/kernel/sched_rt_period_us", "1000000\n")
        .file("sys/kernel/mm/transparent_hugepage/enabled", "always madvise [never]\n")
        .file("proc/swaps", "Filename\tType\tSize\tUsed\tPriority\n")
        .file("sys/devices/system/clocksource/clocksource0/current_clocksource", "tsc\n")
        .file("sys/devices/system/cpu/cpu0/cpuidle/state0/name", "POLL\n")
        .file("sys/devices/system/cpu/cpu0/cpuidle/state0/latency", "0\n")
        .file("sys/devices/system/cpu/cpu0/cpuidle/state1/name", "C6\n")
        .file("sys/devices/system/cpu/cpu0/cpuidle/state1/latency", "133\n")
        .file("sys/devices/system/cpu/cpu0/cpuidle/state1/disable", "1\n");
    for (int cpu = 0; cpu < 4; ++cpu) {
        fake.file("sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_governor", "performance\n");
    }
}

// An ordinary desktop distribution kernel and settings.
void desktop_machine(FakeSystem& fake) {
    fake.file("proc/version", "Linux version 6.8.0-45-generic (buildd) #45-Ubuntu SMP PREEMPT_DYNAMIC\n")
        .file("sys/kernel/debug/sched/preempt", "none (voluntary) full\n")
        .file("proc/cmdline", "BOOT_IMAGE=/vmlinuz quiet splash\n")
        .file("sys/devices/system/cpu/online", "0-1\n")
        .file("sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "powersave\n")
        .file("sys/devices/system/cpu/cpu1/cpufreq/scaling_governor", "powersave\n")
        .file("sys/devices/system/cpu/smt/active", "1\n")
        .file("sys/kernel/mm/transparent_hugepage/enabled", "[always] madvise never\n")
        .file("proc/swaps", "Filename\tType\tSize\tUsed\tPriority\n/swap.img file 2097148 0 -2\n")
        .file("sys/devices/system/cpu/cpu0/cpuidle/state1/name", "C10\n")
        .file("sys/devices/system/cpu/cpu0/cpuidle/state1/latency", "890\n")
        .file("sys/class/dmi/id/sys_vendor", "LENOVO\n");
}

const CheckResult& find(const std::vector<CheckResult>& results, const std::string& id) {
    for (const auto& result : results) {
        if (result.id == id) return result;
    }
    throw std::runtime_error("no check " + id);
}

}  // namespace

TEST(Checks, TunedMachinePassesEverything) {
    FakeSystem fake;
    tuned_machine(fake);
    ProcessLimits limits;
    limits.root = true;

    for (const auto& result : run_checks(SystemView(fake.root()), limits)) {
        EXPECT_TRUE(result.status == Status::Pass || result.status == Status::Info)
            << result.id << ": " << to_string(result.status) << " " << result.value;
    }
    EXPECT_EQ(check_cpu_isolation(SystemView(fake.root())).value, "2-3 (with nohz_full and rcu_nocbs)");
    EXPECT_EQ(check_cpu_idle(SystemView(fake.root())).value, "deepest enabled: POLL (exit latency 0 us)");
}

TEST(Checks, DesktopMachineGetsTheUsualFindings) {
    FakeSystem fake;
    desktop_machine(fake);
    const SystemView system(fake.root());

    const auto preemption = check_kernel_preemption(system);
    EXPECT_EQ(preemption.status, Status::Fail);
    EXPECT_EQ(preemption.value, "preemption: voluntary");
    EXPECT_FALSE(preemption.fix.empty());

    EXPECT_EQ(check_virtualization(system).status, Status::Pass);
    EXPECT_EQ(check_cpu_isolation(system).status, Status::Warn);
    EXPECT_EQ(check_irq_affinity(system).status, Status::Info);  // Not applicable.
    EXPECT_EQ(check_cpu_frequency(system).status, Status::Warn);
    EXPECT_EQ(check_cpu_frequency(system).value, "powersave on 0-1");
    EXPECT_EQ(check_cpu_idle(system).status, Status::Warn);
    EXPECT_EQ(check_smt(system).status, Status::Info);
    EXPECT_EQ(check_transparent_hugepages(system).status, Status::Warn);
    EXPECT_EQ(check_swap(system).value, "on: /swap.img");
}

TEST(Checks, FullPreemptionIsAWarningNotAFailure) {
    FakeSystem fake;
    fake.file("proc/version", "Linux version 6.8.0 #1 SMP PREEMPT_DYNAMIC\n")
        .file("sys/kernel/debug/sched/preempt", "none voluntary (full)\n");
    const auto result = check_kernel_preemption(SystemView(fake.root()));
    EXPECT_EQ(result.status, Status::Warn);
    EXPECT_EQ(result.value, "full preemption (not PREEMPT_RT)");
}

TEST(Checks, DetectsVirtualMachines) {
    {
        FakeSystem fake;
        fake.file("proc/version", "Linux version 6.10.14-linuxkit #1 SMP PREEMPT_LAZY\n");
        const auto result = check_virtualization(SystemView(fake.root()));
        EXPECT_EQ(result.status, Status::Warn);
        EXPECT_EQ(result.value, "Docker Desktop (linuxkit)");
    }
    {
        FakeSystem fake;
        fake.file("proc/cpuinfo", "flags\t\t: fpu vme de pse tsc hypervisor lahf_lm\n");
        EXPECT_EQ(check_virtualization(SystemView(fake.root())).status, Status::Warn);
    }
    {
        FakeSystem fake;
        fake.file("sys/class/dmi/id/sys_vendor", "QEMU\n");
        EXPECT_EQ(check_virtualization(SystemView(fake.root())).value, "QEMU");
    }
}

TEST(Checks, InterruptsOnIsolatedCpus) {
    FakeSystem fake;
    fake.file("proc/cmdline", "isolcpus=managed_irq,domain,2-3 nohz_full=2-3\n")
        .file("proc/irq/10/smp_affinity_list", "0-3\n")
        .file("proc/irq/11/effective_affinity_list", "3\n")
        .file("proc/irq/12/effective_affinity_list", "0\n");
    const SystemView system(fake.root());

    EXPECT_EQ(isolated_cpus(system), (std::vector<int>{2, 3}));
    const auto result = check_irq_affinity(system);
    EXPECT_EQ(result.status, Status::Warn);
    EXPECT_EQ(result.value, "IRQs 10, 11");

    const auto isolation = check_cpu_isolation(system);
    EXPECT_EQ(isolation.status, Status::Info);
    EXPECT_EQ(isolation.value, "2-3 (no rcu_nocbs)");
}

TEST(Checks, RealtimeThrottlingDisabledIsAWarning) {
    FakeSystem fake;
    fake.file("proc/sys/kernel/sched_rt_runtime_us", "-1\n");
    EXPECT_EQ(check_rt_throttling(SystemView(fake.root())).status, Status::Warn);
}

TEST(Checks, ProcessLimits) {
    ProcessLimits user;
    user.rtprio = 0;
    user.memlock = 8ull * 1024 * 1024;
    EXPECT_EQ(check_rt_priority_limit(user).status, Status::Fail);
    EXPECT_EQ(check_memory_lock_limit(user).status, Status::Warn);
    EXPECT_EQ(check_memory_lock_limit(user).value, "RLIMIT_MEMLOCK = 8192 KiB");

    ProcessLimits configured;
    configured.rtprio = 98;
    configured.memlock = std::nullopt;
    EXPECT_EQ(check_rt_priority_limit(configured).status, Status::Pass);
    EXPECT_EQ(check_memory_lock_limit(configured).status, Status::Pass);
}

TEST(Checks, SlowClocksource) {
    FakeSystem fake;
    fake.file("sys/devices/system/clocksource/clocksource0/current_clocksource", "hpet\n");
    EXPECT_EQ(check_clocksource(SystemView(fake.root())).status, Status::Warn);
}
