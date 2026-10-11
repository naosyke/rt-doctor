#include "rtdoctor/i18n.hpp"

#include <map>
#include <regex>
#include <utility>
#include <vector>

namespace rtdoctor {

namespace {

// Fixed texts: check titles, explanations, fixes and advice.
const std::map<std::string, std::string>& japanese() {
    static const std::map<std::string, std::string> catalog = {
        // Titles.
        {"Kernel preemption model", "カーネルのプリエンプション方式"},
        {"Virtual machine", "仮想マシン"},
        {"Isolated CPUs", "隔離CPU"},
        {"Interrupts on isolated CPUs", "隔離CPUへの割り込み"},
        {"CPU frequency scaling", "CPU周波数の制御"},
        {"Deep CPU idle states", "CPUの深い休止状態"},
        {"Simultaneous multithreading", "SMT（ハイパースレッディング）"},
        {"Real-time throttling", "リアルタイム処理の上限（スロットリング）"},
        {"Transparent huge pages", "Transparent Huge Pages（THP）"},
        {"Swap", "スワップ"},
        {"Clock source", "クロックソース"},
        {"Permission to use real-time priorities", "リアルタイム優先度の使用権限"},
        {"Permission to lock memory", "メモリロックの権限"},

        // Values.
        {"full preemption (not PREEMPT_RT)", "フルプリエンプション（PREEMPT_RTではない）"},
        {"bare metal (no hypervisor detected)", "実機（ハイパーバイザーなし）"},
        {"hypervisor (cpuinfo flag)", "ハイパーバイザー（cpuinfo のフラグ）"},
        {"virtual machine (device tree)", "仮想マシン（デバイスツリー）"},
        {"none", "なし"},
        {"not applicable (no isolated CPUs)", "対象外（隔離CPUなし）"},
        {"no cpufreq (fixed frequency, or managed by firmware/hypervisor)",
         "cpufreq なし（固定周波数、またはファームウェア／ハイパーバイザーが管理）"},
        {"no cpuidle states exposed", "cpuidle の状態が公開されていない"},
        {"on", "有効"},
        {"off", "無効"},
        {"not supported", "非対応"},
        {"unknown", "不明"},
        {"not available", "利用不可"},
        {"disabled (sched_rt_runtime_us = -1)", "無効（sched_rt_runtime_us = -1）"},
        {"RLIMIT_RTPRIO unlimited", "RLIMIT_RTPRIO 無制限"},
        {"RLIMIT_MEMLOCK unlimited", "RLIMIT_MEMLOCK 無制限"},

        // Kernel preemption.
        {"Use a PREEMPT_RT kernel (mainline since Linux 6.12: CONFIG_PREEMPT_RT=y; Ubuntu: linux-realtime; "
         "Debian: linux-image-rt-*). On PREEMPT_DYNAMIC kernels, preempt=full on the kernel command line is a "
         "first step.",
         "PREEMPT_RT カーネルを使ってください（Linux 6.12 以降は本体に統合: CONFIG_PREEMPT_RT=y、Ubuntu: "
         "linux-realtime、Debian: linux-image-rt-*）。PREEMPT_DYNAMIC カーネルなら、まずカーネルのコマンドラインに "
         "preempt=full を付けるのが第一歩です。"},
        {"Most kernel code can be preempted, but interrupt handlers, softirqs and spinlocks still run "
         "without preemption; worst-case latencies are typically hundreds of microseconds to milliseconds.",
         "カーネルの大部分は横取り（プリエンプト）できますが、割り込みハンドラ・softirq・スピンロックの区間は横取り"
         "できません。最悪の遅延は数百マイクロ秒〜数ミリ秒になるのが普通です。"},
        {"Kernel code is not preempted (or only at explicit points), so a high-priority task can wait for "
         "long kernel operations; worst-case latencies of many milliseconds are normal.",
         "カーネルの処理は途中で横取りされない（または決まった地点でしか横取りされない）ため、優先度の高いタスクでも"
         "長いカーネル処理の終わりを待たされます。最悪で数ミリ秒以上の遅延が普通に起きます。"},

        // Virtualization.
        {"The hypervisor can pause this whole machine at any time; such pauses (often 1-50 ms) cannot be "
         "prevented from inside Linux and show up as latency spikes on every CPU.",
         "ハイパーバイザーは、この仮想マシン全体をいつでも一時停止できます。この停止（1〜50 ms 程度が多い）は "
         "Linux の中からは防げず、すべての CPU で遅延の急増として現れます。"},
        {"Measure and run real-time work on bare metal. If a VM is unavoidable, pin its vCPUs to dedicated "
         "host CPUs and use a real-time capable hypervisor.",
         "リアルタイム処理の測定と実行は実機で行ってください。仮想マシンが避けられない場合は、仮想CPUをホストの専用CPU"
         "に固定し、リアルタイム対応のハイパーバイザーを使ってください。"},
        {"As long as this system runs in a virtual machine, the hypervisor can pause it at any time. The other "
         "fixes help, but cannot bring deadline misses to zero; measure on bare metal.",
         "このシステムが仮想マシン上で動いている限り、ハイパーバイザーがいつでも止める可能性があります。ほかの対策も"
         "効果はありますが、期限切れをゼロにはできません。実機で測定してください。"},

        // CPU isolation and interrupts.
        {"Real-time tasks share every CPU with the rest of the system: kernel threads, timers, RCU "
         "callbacks and ordinary processes.",
         "リアルタイムタスクが、カーネルスレッド・タイマー・RCU コールバック・普通のプロセスなど、システムの他の"
         "すべての処理と CPU を共有しています。"},
        {"Reserve CPUs for real-time work, e.g. isolcpus=2-3 nohz_full=2-3 rcu_nocbs=2-3 on the kernel "
         "command line, and pin real-time tasks there (taskset or cgroup cpuset).",
         "リアルタイム処理専用の CPU を確保してください（例: カーネルのコマンドラインに isolcpus=2-3 nohz_full=2-3 "
         "rcu_nocbs=2-3）。そのうえでリアルタイムタスクをその CPU に固定します（taskset または cgroup の cpuset）。"},
        {"Isolated CPUs still get the periodic scheduler tick and RCU callbacks unless nohz_full and "
         "rcu_nocbs cover them.",
         "nohz_full と rcu_nocbs で指定しない限り、隔離した CPU にも定期的なスケジューラのティックと RCU "
         "コールバックが届きます。"},
        {"Interrupt handlers on an isolated CPU preempt the real-time task running there.",
         "隔離した CPU で割り込みハンドラが動くと、そこで動いているリアルタイムタスクが中断されます。"},
        {"Move them to housekeeping CPUs: echo <cpus> > /proc/irq/<n>/smp_affinity_list (or "
         "irqaffinity=<cpus> on the kernel command line), and stop irqbalance from moving them back.",
         "割り込みを雑用担当の CPU に移してください: echo <cpus> > /proc/irq/<n>/smp_affinity_list（または"
         "カーネルのコマンドラインに irqaffinity=<cpus>）。irqbalance が元に戻さないよう止めることも必要です。"},

        // CPU frequency and idle.
        {"With a dynamic governor the CPU may run slower when a real-time task wakes up, and changing "
         "frequency takes time, so execution and response times vary.",
         "周波数を動的に変える設定では、リアルタイムタスクが起きたときに CPU が低速で動いていることがあり、周波数の"
         "切り替えにも時間がかかるため、実行時間と応答時間がばらつきます。"},
        {"Use the performance governor on real-time CPUs: echo performance > "
         "/sys/devices/system/cpu/cpuN/cpufreq/scaling_governor (or cpupower frequency-set -g performance).",
         "リアルタイム用の CPU は performance に固定してください: echo performance > "
         "/sys/devices/system/cpu/cpuN/cpufreq/scaling_governor（または cpupower frequency-set -g performance）。"},
        {"A CPU waking from a deep idle state needs that long before it can run anything, which adds "
         "directly to the wake-up latency of real-time tasks.",
         "深い休止状態の CPU は、起きて何かを実行できるまでにこれだけの時間がかかり、それがそのままリアルタイム"
         "タスクの起床遅延に上乗せされます。"},
        {"Limit idle states on real-time CPUs: disable deep states via "
         "/sys/devices/system/cpu/cpuN/cpuidle/stateM/disable, hold /dev/cpu_dma_latency open with a low "
         "value, or boot with processor.max_cstate=1 / intel_idle.max_cstate=0 (x86).",
         "リアルタイム用の CPU では深い休止を制限してください: /sys/devices/system/cpu/cpuN/cpuidle/stateM/disable "
         "で無効化する、/dev/cpu_dma_latency に小さい値を書いて開いたままにする、または processor.max_cstate=1 / "
         "intel_idle.max_cstate=0（x86）で起動します。"},

        // SMT, throttling, memory, clock.
        {"Two hardware threads share one core's execution units and caches; work on the sibling thread "
         "slows down and adds jitter to a real-time task.",
         "2つのハードウェアスレッドが1つのコアの演算器とキャッシュを共有しています。相方のスレッドで処理が動くと、"
         "リアルタイムタスクが遅くなり、ばらつきが増えます。"},
        {"Keep the sibling of a real-time CPU idle (isolate both threads), or disable SMT with nosmt.",
         "リアルタイム用 CPU の相方スレッドは空けておく（両方を隔離する）か、nosmt で SMT を無効にしてください。"},
        {"A runaway real-time task can now take a CPU completely, starving kernel threads and possibly "
         "locking up the system.",
         "暴走したリアルタイムタスクが CPU を完全に独占でき、カーネルスレッドが動けなくなってシステムが固まる"
         "おそれがあります。"},
        {"Keep a safety net unless every real-time task is known to sleep: echo 950000 > "
         "/proc/sys/kernel/sched_rt_runtime_us.",
         "すべてのリアルタイムタスクが必ず休むと分かっている場合を除き、安全装置を残してください: echo 950000 > "
         "/proc/sys/kernel/sched_rt_runtime_us"},
        {"Real-time tasks together may use at most this share of each period; a task that needs more is "
         "paused until the next period (a safety net against runaway tasks).",
         "リアルタイムタスクは合計で各周期のこの割合までしか使えず、超えた分は次の周期まで止められます（暴走への"
         "安全装置）。"},
        {"The kernel may stall a task to compact memory into huge pages, and khugepaged works in the "
         "background; both add unpredictable delays.",
         "カーネルがメモリを大きなページにまとめるためにタスクを止めることがあり、khugepaged もバックグラウンドで"
         "動きます。どちらも予測できない遅延になります。"},
        {"echo madvise > /sys/kernel/mm/transparent_hugepage/enabled (or never).",
         "echo madvise > /sys/kernel/mm/transparent_hugepage/enabled（または never）"},
        {"Lock real-time memory with mlockall(MCL_CURRENT | MCL_FUTURE), or disable swap (swapoff -a).",
         "リアルタイム処理のメモリを mlockall(MCL_CURRENT | MCL_FUTURE) で固定するか、スワップを無効にして"
         "ください（swapoff -a）。"},
        {"Swap is configured but unused right now; it only matters once memory runs short, when "
         "pages of a real-time task could be swapped out and take milliseconds to read back.",
         "スワップは設定されていますが、今は使われていません。問題になるのはメモリが足りなくなったときで、"
         "リアルタイムタスクのメモリがディスクに追い出されると、読み戻すのに数ミリ秒かかります。"},
        {"Memory is being swapped out. A swapped-out page of a real-time task takes milliseconds to "
         "read back on its next access.",
         "メモリがディスクに追い出されています。リアルタイムタスクのメモリが追い出されていると、次に触れたときに"
         "読み戻しで数ミリ秒かかります。"},
        {"Paravirtual clock: fine for a VM, but timing is only as good as the host.",
         "仮想マシン用の時計です。仮想マシンとしては問題ありませんが、時間の正確さはホスト次第です。"},
        {"Reading this clock is slow (an I/O access instead of a CPU register), which adds overhead to every "
         "timestamp and timer.",
         "この時計は読むのが遅く（CPU のレジスタではなく I/O アクセス）、時刻の取得やタイマーのたびに"
         "余計な時間がかかります。"},
        {"Check why the TSC / architected timer is not used (dmesg | grep -i clocksource), e.g. tsc=reliable.",
         "TSC／アーキテクチャタイマーが使われない理由を確認してください（dmesg | grep -i clocksource）。"
         "例: tsc=reliable"},
        {"This user cannot switch a task to SCHED_FIFO / SCHED_RR; sched_setscheduler() fails with EPERM.",
         "このユーザーはタスクを SCHED_FIFO／SCHED_RR に切り替えられません（sched_setscheduler() が EPERM で失敗"
         "します）。"},
        {"Allow it in /etc/security/limits.conf (e.g. '@realtime - rtprio 98') or give the program "
         "CAP_SYS_NICE (setcap cap_sys_nice+ep <program>).",
         "/etc/security/limits.conf で許可する（例: '@realtime - rtprio 98'）か、プログラムに CAP_SYS_NICE を"
         "与えてください（setcap cap_sys_nice+ep <program>）。"},
        {"mlockall() of a real-time process fails or stops locking new memory beyond this limit, leaving "
         "it exposed to page faults.",
         "この上限を超えると、リアルタイムプロセスの mlockall() が失敗するか新しいメモリを固定できなくなり、"
         "ページフォールトによる遅延が起きえます。"},
        {"Raise it in /etc/security/limits.conf (e.g. '@realtime - memlock unlimited') or give the "
         "program CAP_IPC_LOCK.",
         "/etc/security/limits.conf で上限を上げる（例: '@realtime - memlock unlimited'）か、プログラムに "
         "CAP_IPC_LOCK を与えてください。"},

        // Trace advice.
        {"The CPU was idle but the task still did not run: slow wake-ups from deep idle states, or a "
         "virtual machine being paused. See rtdoctor check (Deep CPU idle states, Virtual machine).",
         "CPU は空いていたのに、タスクがすぐ動けませんでした。深い休止状態からの復帰が遅いか、仮想マシンが止められて"
         "いた可能性があります。システムチェックの「CPUの深い休止状態」「仮想マシン」を確認してください。"},
        {"An interrupt handler ran on the task's CPU. Route that interrupt to another CPU "
         "(/proc/irq/<n>/smp_affinity_list), or isolate the task's CPU.",
         "タスクの CPU で割り込みハンドラが動いていました。その割り込みを別の CPU に回す"
         "（/proc/irq/<n>/smp_affinity_list）か、タスクの CPU を隔離してください。"},
        {"Deferred interrupt work (softirq) ran on the task's CPU. A PREEMPT_RT kernel runs it in "
         "threads that a real-time task can preempt; otherwise move the interrupt source elsewhere.",
         "割り込みの後処理（softirq）がタスクの CPU で動いていました。PREEMPT_RT カーネルなら、これをリアルタイム"
         "タスクが横取りできるスレッドで実行します。そうでなければ、割り込み元を別の CPU に移してください。"},
        {"Another task ran on the task's CPU while it was ready. Move that task to another CPU (taskset, "
         "cgroup cpuset), or give the delayed task a real-time priority (chrt -f) so it preempts the other one.",
         "タスクが動ける状態なのに、同じ CPU で別のタスクが動いていました。そのタスクを別の CPU に移す（taskset、"
         "cgroup の cpuset）か、遅れたタスクにリアルタイム優先度を付けて（chrt -f）割り込めるようにしてください。"},
    };
    return catalog;
}

// Texts with numbers or names in them.
const std::vector<std::pair<std::regex, std::string>>& japanese_patterns() {
    static const std::vector<std::pair<std::regex, std::string>> patterns = {
        {std::regex(R"(^preemption: (.*)$)"), "プリエンプション: $1"},
        {std::regex(R"(none or voluntary$)"), "なし、または voluntary（決まった地点でのみ）"},
        {std::regex(R"(dynamic \(mode unknown\)$)"), "dynamic（モード不明）"},
        {std::regex(R"( \(no nohz_full\))"), "（nohz_full なし）"},
        {std::regex(R"( \(no rcu_nocbs\))"), "（rcu_nocbs なし）"},
        {std::regex(R"( \(with nohz_full and rcu_nocbs\)$)"), "（nohz_full と rcu_nocbs あり）"},
        {std::regex(R"(^Add nohz_full=(\S+) rcu_nocbs=(\S+) to the kernel command line\.$)"),
         "カーネルのコマンドラインに nohz_full=$1 rcu_nocbs=$2 を追加してください。"},
        {std::regex(R"(^no interrupts routed to (.*)$)"), "CPU $1 への割り込みなし"},
        {std::regex(R"(^IRQs (.*)$)"), "IRQ $1"},
        {std::regex(R"(, \.\.\. \((\d+) total\))"), "、…（計 $1 個）"},
        {std::regex(R"((\w+) on ([0-9]+(?:[-,][0-9]+)*)(, )?)"), "$1（CPU $2）"},
        {std::regex(R"(^deepest enabled: (.*) \(exit latency (-?\d+) us\)$)"), "最も深い状態: $1（復帰に $2 us）"},
        {std::regex(R"(^(-?\d+) us per (\d+) us$)"), "$2 us ごとに $1 us まで"},
        {std::regex(R"(^on: (.*) \((\d+ MiB) used of (\d+ MiB)\)$)"), "有効: $1（$3 のうち $2 使用中）"},
        {std::regex(R"(^up to priority (\d+)$)"), "優先度 $1 まで"},
        // What delayed a traced task, and trace targets.
        {std::regex(R"(^idle$)"), "idle（CPU の休止中）"},
        {std::regex(R"(^task (.*) \(pid (\d+)\)$)"), "タスク $1 (pid $2)"},
        {std::regex(R"(^irq (\d+) (.*)$)"), "割り込み irq $1 $2"},
        {std::regex(R"(^pid (\d+) \((.*), (\d+) threads\)$)"), "pid $1（$2、スレッド $3 個）"},
        {std::regex(R"re(^threads named "(.*)"$)re"), "名前が「$1」のスレッド"},
    };
    return patterns;
}

}  // namespace

std::optional<Lang> parse_lang(const std::string& name) {
    if (name == "en") return Lang::En;
    if (name == "ja") return Lang::Ja;
    return std::nullopt;
}

std::string translate(Lang lang, const std::string& english) {
    if (lang == Lang::En || english.empty()) return english;
    const auto& catalog = japanese();
    if (const auto it = catalog.find(english); it != catalog.end()) return it->second;
    std::string text = english;
    for (const auto& [pattern, replacement] : japanese_patterns()) text = std::regex_replace(text, pattern, replacement);
    return text;
}

}  // namespace rtdoctor
