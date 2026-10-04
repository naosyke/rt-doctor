#include "rtdoctor/checks.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace rtdoctor {

std::string_view to_string(Status status) {
    switch (status) {
        case Status::Pass: return "PASS";
        case Status::Info: return "INFO";
        case Status::Warn: return "WARN";
        case Status::Fail: return "FAIL";
    }
    return "?";
}

namespace {

bool contains(const std::string& text, std::string_view needle) { return text.find(needle) != std::string::npos; }

int to_int(const std::string& text, int fallback = 0) {
    try {
        return std::stoi(text);
    } catch (...) {
        return fallback;
    }
}

// Value of "name=" in the kernel command line, if present.
std::optional<std::string> cmdline_value(const SystemView& system, const std::string& name) {
    const auto cmdline = system.read_line("proc/cmdline");
    if (!cmdline) return std::nullopt;
    std::stringstream stream(*cmdline);
    std::string token;
    while (stream >> token) {
        if (token.rfind(name + "=", 0) == 0) return token.substr(name.size() + 1);
        if (token == name) return std::string();
    }
    return std::nullopt;
}

// The selected option of a sysfs/debugfs choice: "always [madvise] never" ->
// "madvise" (sysfs style), "none voluntary (full)" -> "full" (sched/preempt).
std::string selected(const std::string& text) {
    for (const auto& [open_char, close_char] : {std::pair{'[', ']'}, std::pair{'(', ')'}}) {
        const auto open = text.find(open_char), close = text.find(close_char);
        if (open != std::string::npos && close != std::string::npos && close > open) {
            return text.substr(open + 1, close - open - 1);
        }
    }
    return text;
}

std::vector<int> online_cpus(const SystemView& system) {
    if (const auto online = system.read_line("sys/devices/system/cpu/online")) return parse_cpu_list(*online);
    std::vector<int> cpus;
    for (const auto& name : system.list("sys/devices/system/cpu")) {
        if (name.rfind("cpu", 0) == 0 && name.size() > 3 && std::isdigit(static_cast<unsigned char>(name[3]))) {
            cpus.push_back(to_int(name.substr(3)));
        }
    }
    std::sort(cpus.begin(), cpus.end());
    return cpus;
}

CheckResult pass(std::string id, std::string title, std::string value) {
    return {std::move(id), std::move(title), Status::Pass, std::move(value), "", ""};
}

}  // namespace

std::vector<int> isolated_cpus(const SystemView& system) {
    std::set<int> cpus;
    for (const auto* parameter : {"isolcpus", "nohz_full"}) {
        if (const auto value = cmdline_value(system, parameter)) {
            for (const int cpu : parse_cpu_list(*value)) cpus.insert(cpu);
        }
    }
    return {cpus.begin(), cpus.end()};
}

CheckResult check_kernel_preemption(const SystemView& system) {
    const std::string id = "kernel_preemption", title = "Kernel preemption model";
    const auto version = system.read_line("proc/version").value_or("");
    if (system.read_line("sys/kernel/realtime").value_or("") == "1" || contains(version, "PREEMPT_RT")) {
        return pass(id, title, "PREEMPT_RT");
    }

    // Kernels with PREEMPT_DYNAMIC report the active mode in debugfs.
    std::string mode;
    if (const auto preempt = system.read_line("sys/kernel/debug/sched/preempt")) mode = selected(*preempt);
    if (mode.empty()) {
        mode = contains(version, "PREEMPT_LAZY")      ? "lazy"
               : contains(version, "PREEMPT_DYNAMIC") ? "dynamic (mode unknown)"
               : contains(version, "PREEMPT")         ? "full"
                                                      : "none or voluntary";
    }
    const std::string fix =
        "Use a PREEMPT_RT kernel (mainline since Linux 6.12: CONFIG_PREEMPT_RT=y; Ubuntu: linux-realtime; "
        "Debian: linux-image-rt-*). On PREEMPT_DYNAMIC kernels, preempt=full on the kernel command line is a "
        "first step.";
    if (mode == "full") {
        return {id, title, Status::Warn, "full preemption (not PREEMPT_RT)",
                "Most kernel code can be preempted, but interrupt handlers, softirqs and spinlocks still run "
                "without preemption; worst-case latencies are typically hundreds of microseconds to milliseconds.",
                fix};
    }
    return {id, title, Status::Fail, "preemption: " + mode,
            "Kernel code is not preempted (or only at explicit points), so a high-priority task can wait for "
            "long kernel operations; worst-case latencies of many milliseconds are normal.",
            fix};
}

CheckResult check_virtualization(const SystemView& system) {
    const std::string id = "virtualization", title = "Virtual machine";
    std::string found;
    const auto version = system.read_line("proc/version").value_or("");
    if (contains(version, "linuxkit")) found = "Docker Desktop (linuxkit)";
    else if (contains(version, "microsoft-standard")) found = "WSL 2";
    else if (const auto type = system.read_line("sys/hypervisor/type")) found = *type;
    else if (contains(system.read("proc/cpuinfo").value_or(""), " hypervisor")) found = "hypervisor (cpuinfo flag)";
    else if (const auto vendor = system.read_line("sys/class/dmi/id/sys_vendor")) {
        for (const auto* known : {"QEMU", "VMware", "innotek", "Xen", "Microsoft Corporation", "Parallels", "Amazon EC2"}) {
            if (contains(*vendor, known)) found = *vendor;
        }
    }
    if (found.empty()) {
        const auto compatible = system.read("sys/firmware/devicetree/base/compatible").value_or("");
        if (contains(compatible, "linux,dummy-virt")) found = "virtual machine (device tree)";
    }

    if (found.empty()) return pass(id, title, "bare metal (no hypervisor detected)");
    return {id, title, Status::Warn, found,
            "The hypervisor can pause this whole machine at any time; such pauses (often 1-50 ms) cannot be "
            "prevented from inside Linux and show up as latency spikes on every CPU.",
            "Measure and run real-time work on bare metal. If a VM is unavoidable, pin its vCPUs to dedicated "
            "host CPUs and use a real-time capable hypervisor."};
}

CheckResult check_cpu_isolation(const SystemView& system) {
    const std::string id = "cpu_isolation", title = "Isolated CPUs";
    const auto isolated = isolated_cpus(system);
    if (isolated.empty()) {
        return {id, title, Status::Warn, "none",
                "Real-time tasks share every CPU with the rest of the system: kernel threads, timers, RCU "
                "callbacks and ordinary processes.",
                "Reserve CPUs for real-time work, e.g. isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 on the kernel "
                "command line, and pin real-time tasks there (taskset or cgroup cpuset)."};
    }
    std::string value = format_cpu_list(isolated);
    const bool nohz = cmdline_value(system, "nohz_full").has_value();
    const bool rcu = cmdline_value(system, "rcu_nocbs").has_value();
    if (!nohz || !rcu) {
        return {id, title, Status::Info, value + (nohz ? "" : " (no nohz_full)") + (rcu ? "" : " (no rcu_nocbs)"),
                "Isolated CPUs still get the periodic scheduler tick and RCU callbacks unless nohz_full and "
                "rcu_nocbs cover them.",
                "Add nohz_full=" + value + " rcu_nocbs=" + value + " to the kernel command line."};
    }
    return pass(id, title, value + " (with nohz_full and rcu_nocbs)");
}

CheckResult check_irq_affinity(const SystemView& system) {
    const std::string id = "irq_affinity", title = "Interrupts on isolated CPUs";
    const auto isolated = isolated_cpus(system);
    if (isolated.empty()) return {id, title, Status::Info, "not applicable (no isolated CPUs)", "", ""};

    const std::set<int> isolated_set(isolated.begin(), isolated.end());
    std::vector<std::string> irqs;
    for (const auto& irq : system.list("proc/irq")) {
        if (!std::isdigit(static_cast<unsigned char>(irq[0]))) continue;
        const auto affinity = system.read_line("proc/irq/" + irq + "/effective_affinity_list")
                                  .value_or(system.read_line("proc/irq/" + irq + "/smp_affinity_list").value_or(""));
        for (const int cpu : parse_cpu_list(affinity)) {
            if (isolated_set.count(cpu)) {
                irqs.push_back(irq);
                break;
            }
        }
    }
    if (irqs.empty()) return pass(id, title, "no interrupts routed to " + format_cpu_list(isolated));

    std::string list;
    for (std::size_t i = 0; i < irqs.size() && i < 10; ++i) list += (i ? ", " : "") + irqs[i];
    if (irqs.size() > 10) list += ", ... (" + std::to_string(irqs.size()) + " total)";
    return {id, title, Status::Warn, "IRQs " + list,
            "Interrupt handlers on an isolated CPU preempt the real-time task running there.",
            "Move them to housekeeping CPUs: echo <cpus> > /proc/irq/<n>/smp_affinity_list (or "
            "irqaffinity=<cpus> on the kernel command line), and stop irqbalance from moving them back."};
}

CheckResult check_cpu_frequency(const SystemView& system) {
    const std::string id = "cpu_frequency", title = "CPU frequency scaling";
    std::map<std::string, std::vector<int>> by_governor;
    for (const int cpu : online_cpus(system)) {
        if (const auto governor =
                system.read_line("sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_governor")) {
            by_governor[*governor].push_back(cpu);
        }
    }
    if (by_governor.empty()) return {id, title, Status::Info, "no cpufreq (fixed frequency, or managed by firmware/hypervisor)", "", ""};

    std::string value;
    for (const auto& [governor, cpus] : by_governor) {
        value += (value.empty() ? "" : ", ") + governor + " on " + format_cpu_list(cpus);
    }
    if (by_governor.size() == 1 && by_governor.count("performance")) return pass(id, title, value);
    return {id, title, Status::Warn, value,
            "With a dynamic governor the CPU may run slower when a real-time task wakes up, and changing "
            "frequency takes time, so execution and response times vary.",
            "Use the performance governor on real-time CPUs: echo performance > "
            "/sys/devices/system/cpu/cpuN/cpufreq/scaling_governor (or cpupower frequency-set -g performance)."};
}

CheckResult check_cpu_idle(const SystemView& system) {
    const std::string id = "cpu_idle", title = "Deep CPU idle states";
    int worst_latency = -1;
    std::string worst_name;
    const std::string base = "sys/devices/system/cpu/cpu0/cpuidle";
    for (const auto& state : system.list(base)) {
        if (state.rfind("state", 0) != 0) continue;
        if (system.read_line(base + "/" + state + "/disable").value_or("0") == "1") continue;
        const int latency = to_int(system.read_line(base + "/" + state + "/latency").value_or("0"));
        if (latency > worst_latency) {
            worst_latency = latency;
            worst_name = system.read_line(base + "/" + state + "/name").value_or(state);
        }
    }
    if (worst_name.empty()) return {id, title, Status::Info, "no cpuidle states exposed", "", ""};
    const std::string value = "deepest enabled: " + worst_name + " (exit latency " + std::to_string(worst_latency) + " us)";
    if (worst_latency <= 10) return pass(id, title, value);
    return {id, title, Status::Warn, value,
            "A CPU waking from a deep idle state needs that long before it can run anything, which adds "
            "directly to the wake-up latency of real-time tasks.",
            "Limit idle states on real-time CPUs: disable deep states via "
            "/sys/devices/system/cpu/cpuN/cpuidle/stateM/disable, hold /dev/cpu_dma_latency open with a low "
            "value, or boot with processor.max_cstate=1 / intel_idle.max_cstate=0 (x86)."};
}

CheckResult check_smt(const SystemView& system) {
    const std::string id = "smt", title = "Simultaneous multithreading";
    const auto active = system.read_line("sys/devices/system/cpu/smt/active");
    if (!active || *active == "0") return pass(id, title, active ? "off" : "not supported");
    return {id, title, Status::Info, "on",
            "Two hardware threads share one core's execution units and caches; work on the sibling thread "
            "slows down and adds jitter to a real-time task.",
            "Keep the sibling of a real-time CPU idle (isolate both threads), or disable SMT with nosmt."};
}

CheckResult check_rt_throttling(const SystemView& system) {
    const std::string id = "rt_throttling", title = "Real-time throttling";
    const auto runtime = system.read_line("proc/sys/kernel/sched_rt_runtime_us");
    const auto period = system.read_line("proc/sys/kernel/sched_rt_period_us").value_or("1000000");
    if (!runtime) return {id, title, Status::Info, "unknown", "", ""};
    if (*runtime == "-1") {
        return {id, title, Status::Warn, "disabled (sched_rt_runtime_us = -1)",
                "A runaway real-time task can now take a CPU completely, starving kernel threads and possibly "
                "locking up the system.",
                "Keep a safety net unless every real-time task is known to sleep: echo 950000 > "
                "/proc/sys/kernel/sched_rt_runtime_us."};
    }
    return {id, title, Status::Info, *runtime + " us per " + period + " us",
            "Real-time tasks together may use at most this share of each period; a task that needs more is "
            "paused until the next period (a safety net against runaway tasks).",
            ""};
}

CheckResult check_transparent_hugepages(const SystemView& system) {
    const std::string id = "transparent_hugepages", title = "Transparent huge pages";
    const auto enabled = system.read_line("sys/kernel/mm/transparent_hugepage/enabled");
    if (!enabled) return {id, title, Status::Info, "not available", "", ""};
    const auto mode = selected(*enabled);
    if (mode != "always") return pass(id, title, mode);
    return {id, title, Status::Warn, "always",
            "The kernel may stall a task to compact memory into huge pages, and khugepaged works in the "
            "background; both add unpredictable delays.",
            "echo madvise > /sys/kernel/mm/transparent_hugepage/enabled (or never)."};
}

CheckResult check_swap(const SystemView& system) {
    const std::string id = "swap", title = "Swap";
    const auto swaps = system.read("proc/swaps");
    if (!swaps) return {id, title, Status::Info, "unknown", "", ""};
    std::stringstream lines(*swaps);
    std::string line;
    std::getline(lines, line);  // Header
    std::vector<std::string> devices;
    while (std::getline(lines, line)) {
        std::stringstream fields(line);
        std::string device;
        if (fields >> device) devices.push_back(device);
    }
    if (devices.empty()) return pass(id, title, "off");
    std::string value;
    for (const auto& device : devices) value += (value.empty() ? "" : ", ") + device;
    return {id, title, Status::Warn, "on: " + value,
            "A page of a real-time task that was swapped out takes milliseconds to read back on next access.",
            "Lock real-time memory with mlockall(MCL_CURRENT | MCL_FUTURE), or disable swap (swapoff -a)."};
}

CheckResult check_clocksource(const SystemView& system) {
    const std::string id = "clocksource", title = "Clock source";
    const auto source = system.read_line("sys/devices/system/clocksource/clocksource0/current_clocksource");
    if (!source) return {id, title, Status::Info, "unknown", "", ""};
    if (*source == "tsc" || *source == "arch_sys_counter") return pass(id, title, *source);
    if (*source == "kvm-clock" || *source == "hyperv_clocksource_tsc_page") {
        return {id, title, Status::Info, *source, "Paravirtual clock: fine for a VM, but timing is only as good as the host.", ""};
    }
    return {id, title, Status::Warn, *source,
            "Reading this clock is slow (an I/O access instead of a CPU register), which adds overhead to every "
            "timestamp and timer.",
            "Check why the TSC / architected timer is not used (dmesg | grep -i clocksource), e.g. tsc=reliable."};
}

CheckResult check_rt_priority_limit(const ProcessLimits& limits) {
    const std::string id = "rt_priority_limit", title = "Permission to use real-time priorities";
    if (limits.root) return pass(id, title, "root");
    if (!limits.rtprio || *limits.rtprio >= 99) return pass(id, title, "RLIMIT_RTPRIO unlimited");
    if (*limits.rtprio > 0) return pass(id, title, "up to priority " + std::to_string(*limits.rtprio));
    return {id, title, Status::Fail, "RLIMIT_RTPRIO = 0",
            "This user cannot switch a task to SCHED_FIFO / SCHED_RR; sched_setscheduler() fails with EPERM.",
            "Allow it in /etc/security/limits.conf (e.g. '@realtime - rtprio 98') or give the program "
            "CAP_SYS_NICE (setcap cap_sys_nice+ep <program>)."};
}

CheckResult check_memory_lock_limit(const ProcessLimits& limits) {
    const std::string id = "memory_lock_limit", title = "Permission to lock memory";
    if (limits.root) return pass(id, title, "root");
    if (!limits.memlock) return pass(id, title, "RLIMIT_MEMLOCK unlimited");
    const auto mib = *limits.memlock / (1024 * 1024);
    if (mib >= 256) return pass(id, title, std::to_string(mib) + " MiB");
    return {id, title, Status::Warn, "RLIMIT_MEMLOCK = " + std::to_string(*limits.memlock / 1024) + " KiB",
            "mlockall() of a real-time process fails or stops locking new memory beyond this limit, leaving "
            "it exposed to page faults.",
            "Raise it in /etc/security/limits.conf (e.g. '@realtime - memlock unlimited') or give the "
            "program CAP_IPC_LOCK."};
}

std::vector<CheckResult> run_checks(const SystemView& system, const ProcessLimits& limits) {
    return {
        check_kernel_preemption(system),
        check_virtualization(system),
        check_cpu_isolation(system),
        check_irq_affinity(system),
        check_cpu_frequency(system),
        check_cpu_idle(system),
        check_smt(system),
        check_rt_throttling(system),
        check_transparent_hugepages(system),
        check_swap(system),
        check_clocksource(system),
        check_rt_priority_limit(limits),
        check_memory_lock_limit(limits),
    };
}

}  // namespace rtdoctor
