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

## Usage

```bash
rtdoctor check                  # report for people
rtdoctor check --verbose        # also explain PASS / INFO items
rtdoctor check --json           # report for tools (with "impact" and an ordered "priorities" list)
rtdoctor check --fail-on warn   # exit code 1 on any warning, for CI
```

Exit codes: 0 = no failures (or warnings with `--fail-on warn`), 1 = findings
at or above the threshold, 2 = usage error.

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

## Roadmap

* [x] `rtdoctor check` - system configuration
* [ ] `rtdoctor latency` - wake-up latency per CPU (built-in, no cyclictest needed)
* [ ] `rtdoctor trace --pid N` - when a task is late, **which other task or interrupt was running on its CPU**
* [ ] `rtdoctor report` - everything in one shareable HTML file
* [ ] Release binaries for x86-64 and arm64
