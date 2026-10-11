# rt-doctor

**Find out why your real-time Linux misses deadlines.**

`rtdoctor` checks a Linux system for everything that adds latency to
real-time work - the kernel's preemption model, CPU isolation, interrupts,
frequency scaling, idle states, memory settings, permissions, virtualization -
and tells you for each finding **why it matters and how to fix it**.

It is a single static binary with no dependencies: copy it onto a robot, a
controller or an embedded board and run it.

```text
$ sudo rtdoctor check
rt-doctor 0.1.0 - real-time readiness of this system

Summary
  Biggest factor: Virtual machine - Docker Desktop (linuxkit)
    As long as this system runs in a virtual machine, the hypervisor can pause it at any time. The
    other fixes help, but cannot bring deadline misses to zero; measure on bare metal.
  Then: Kernel preemption model (high), Isolated CPUs (medium), Transparent huge pages (low)

Checks
  WARN high     Kernel preemption model                  full preemption (not PREEMPT_RT)
        Why: Most kernel code can be preempted, but interrupt handlers, softirqs and spinlocks still
            run without preemption; worst-case latencies are typically hundreds of microseconds to
            milliseconds.
        Fix: Use a PREEMPT_RT kernel (mainline since Linux 6.12: CONFIG_PREEMPT_RT=y; Ubuntu:
            linux-realtime; Debian: linux-image-rt-*). ...
  WARN critical Virtual machine                          Docker Desktop (linuxkit)
  WARN medium   Isolated CPUs                            none
  INFO          Interrupts on isolated CPUs              not applicable (no isolated CPUs)
  INFO          CPU frequency scaling                    no cpufreq (fixed frequency, or managed by firmware/hypervisor)
  INFO          Deep CPU idle states                     no cpuidle states exposed
  PASS          Simultaneous multithreading              not supported
  INFO          Real-time throttling                     950000 us per 1000000 us
  WARN low      Transparent huge pages                   always
  INFO          Swap                                     on: /var/lib/swap (0 MiB used of 1023 MiB)
  PASS          Clock source                             arch_sys_counter
  PASS          Permission to use real-time priorities   root
  PASS          Permission to lock memory                root

  0 fail, 4 warn, 4 pass, 5 info
```

The summary puts the finding with the largest effect first. Each finding has
an impact:

| Impact | Meaning | Examples |
|---|---|---|
| critical | Cannot be fixed by tuning this system | Running in a virtual machine |
| high | Milliseconds or more, or real-time priorities unusable | Kernel without PREEMPT_RT, no permission for SCHED_FIFO |
| medium | Tens of microseconds to a few milliseconds | No isolated CPUs, interrupts on isolated CPUs, deep idle states, frequency scaling, swap in use |
| low | Only under particular conditions | Transparent huge pages, SMT, swap configured but unused |

Where it matters, a check looks at actual use, not only the configuration:
swap that is configured but unused is reported as INFO, swap in use as WARN.

## Who it is for

Teams that run time-critical work on Linux: robotics (ROS 2), motion
control, drones, industrial automation, audio, test and measurement. The
typical situation: "it misses a deadline now and then, and we don't know
why".

## Checks

| Check | Looks at |
|---|---|
| Kernel preemption model | `PREEMPT_RT`, full / voluntary / none preemption |
| Virtual machine | Docker Desktop, WSL 2, KVM/QEMU, VMware, Hyper-V, ... - hypervisor pauses cannot be fixed from inside |
| Isolated CPUs | `isolcpus`, `nohz_full`, `rcu_nocbs` |
| Interrupts on isolated CPUs | IRQ affinity of every interrupt |
| CPU frequency scaling | cpufreq governor per CPU |
| Deep CPU idle states | Exit latency of the deepest enabled cpuidle state |
| Simultaneous multithreading | SMT / hyper-threading siblings |
| Real-time throttling | `sched_rt_runtime_us` |
| Transparent huge pages | THP mode |
| Swap | Active swap devices |
| Clock source | TSC / architected timer vs. slow clocks |
| Permission to use real-time priorities | `RLIMIT_RTPRIO`, root |
| Permission to lock memory | `RLIMIT_MEMLOCK`, root |

Some values are only visible with root (e.g. the preemption mode of
`PREEMPT_DYNAMIC` kernels in debugfs); run with `sudo` for the full picture.

## Install

One static binary for x86-64 or arm64 Linux, with no dependencies: it runs on
any distribution (tested on Ubuntu, Debian 11, Fedora, Amazon Linux 2, Alpine
and BusyBox), on robots, boards and servers alike.

```bash
curl -fsSL https://github.com/naosyke/rt-doctor/releases/latest/download/install.sh | sh
```

The script picks the build for the machine, checks it against the published
SHA256SUMS and installs it to `/usr/local/bin` (`PREFIX=...` to change). Or
download `rtdoctor-linux-x86_64.tar.gz` / `rtdoctor-linux-arm64.tar.gz` from
the [releases](https://github.com/naosyke/rt-doctor/releases) yourself.

## Usage

```bash
rtdoctor check                  # report for people
rtdoctor check --verbose        # also explain PASS / INFO items
rtdoctor check --json           # report for tools (with "impact" and an ordered "priorities" list)
rtdoctor check --fail-on warn   # exit code 1 on any warning, for CI
```

Exit codes: 0 = no failures (or warnings with `--fail-on warn`), 1 = findings
at or above the threshold, 2 = usage error, 3 = tracing unavailable (`trace`; `report` notes it in the
page instead).

## Trace: who delayed my task?

`rtdoctor check` says what is wrong with the system. `rtdoctor trace` watches
a running task and says **what ran on its CPU while it was ready but had to
wait** - another task, an interrupt, a softirq, or the CPU waking from idle.

```bash
sudo rtdoctor trace --pid 1826 --duration 5 --deadline-ms 4
```

```
rt-doctor trace - pid 1826 (periodic, 1 threads), 5.1 s, deadline 4.00 ms

Thread periodic (tid 1826): 500 activations, 3 over the deadline
  Response time (wake-up to sleep): median 2.92 ms, 99th 3.91 ms, max 5.92 ms
  Waiting for its CPU: 504 times, 464.06 ms in total, max 3.00 ms (4 preemptions)

  Who delayed it (ran on its CPU while it was ready)
     460.23 ms  task hog (pid 1825)                             502 times
       2.16 ms  irq 11 arch_timer                               503 times
       0.51 ms  irq 6 IPI                                       189 times
    -> Another task ran on the task's CPU while it was ready. Move that task to
       another CPU (taskset, cgroup cpuset), or give the delayed task a
       real-time priority (chrt -f) so it preempts the other one.
```

Following the advice (`chrt -f 50`) on the same machine:

| | before | after `chrt -f 50` |
|---|---|---|
| over the 4 ms deadline | 3 of 500 | 0 of 500 |
| response max | 5.92 ms | 2.07 ms |
| waiting in total | 464 ms | 4.2 ms |
| time taken by `hog` | 460 ms | 3.3 ms |

How it works: a private ftrace instance (it does not disturb other tracers)
records `sched_switch`, `sched_waking`, `sched_migrate_task` and the
irq / softirq entry and exit events. Each wait of the target is split among
whatever was running on its CPU at the time. The report also lists the worst
activations with their own breakdown.

```bash
rtdoctor trace --pid N            # all threads of a process
rtdoctor trace --comm NAME        # threads by name (as in /proc/*/task/*/comm)
  --duration 10 --deadline-ms 2 --threads 3 --json
```

Needs root (or tracefs access). Inside a container, select by `--comm` or
run the container with `--pid=host`: process ids in a container's PID
namespace are not the ones the kernel traces.

## Report: one file to share

```bash
sudo rtdoctor report -o report.html                                   # checks only
sudo rtdoctor report -o report.html --pid 1826 --duration 10 --deadline-ms 4   # checks + trace
sudo rtdoctor report -o report.html --lang ja                         # in Japanese (日本語)
```

`report.html` is a single self-contained page (no scripts, nothing loaded
from the internet; light and dark mode; prints well) to attach to a ticket or
send to a customer or colleague:

* the machine (host, kernel, CPU) and the **biggest factor** first
* for the traced task: response time histogram with the deadline, **who
  delayed it** as bars with what to do, and the worst activations split into
  running time and what it waited for
* every check, findings first, ordered by impact

`--lang ja` writes the whole page in Japanese, including every explanation,
fix and piece of advice.

## Build

```bash
docker build --target base -t rtdoctor-base .
docker run --rm -v "$PWD":/workspace rtdoctor-base bash -c \
  "cmake -S . -B build -G Ninja -DRTDOCTOR_STATIC=ON && cmake --build build && ctest --test-dir build"
```

The result is `build/rtdoctor`, a static binary (about 2.5 MB). The checks
are tested against fake `/proc` and `/sys` trees of typical machines (a tuned
real-time machine, a desktop, Docker Desktop, a machine with interrupts on
isolated CPUs).

### Releasing

CI builds and tests on x86-64 and arm64 for every push (as root, so the ftrace
end-to-end tests run as well). To publish a release, set the version in
`CMakeLists.txt` and push a matching tag:

```bash
git tag v0.1.0
git push origin v0.1.0
```

The release workflow builds both binaries with `scripts/package.sh` (which
refuses a tag that does not match the version), and publishes them with
`SHA256SUMS` and `install.sh`.

## Roadmap

* [x] `rtdoctor check` - system configuration
* [ ] `rtdoctor latency` - wake-up latency per CPU (built-in, no cyclictest needed)
* [x] `rtdoctor trace --pid N` - when a task is late, **which other task or interrupt was running on its CPU**
* [x] `rtdoctor report` - everything in one shareable HTML file
* [x] Release binaries for x86-64 and arm64 (built by GitHub Actions on tags)

## License

rt-doctor is open source under the [Apache License 2.0](LICENSE): free to
use, modify and ship, also commercially. Copyright 2026 naosyke.

Built only on public Linux interfaces and documentation (procfs, sysfs,
ftrace, kernel docs). Independent personal project, not affiliated with any
employer.
