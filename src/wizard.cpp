#include "rtdoctor/wizard.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <optional>
#include <sstream>
#include <thread>

#include "rtdoctor/checks.hpp"
#include "rtdoctor/session.hpp"
#include "rtdoctor/trace_report.hpp"

namespace rtdoctor {

namespace {

constexpr unsigned long kKernelThreadFlag = 0x00200000;  // PF_KTHREAD

// Fields of /proc/<pid>/stat after "pid (comm) ": [0] is field 3 (state).
std::vector<std::string> stat_fields(const std::string& stat) {
    std::vector<std::string> fields;
    const auto close = stat.rfind(')');
    if (close == std::string::npos) return fields;
    std::istringstream rest(stat.substr(close + 1));
    std::string field;
    while (rest >> field) fields.push_back(field);
    return fields;
}

long long status_value(const std::string& status, const std::string& key) {
    const auto at = status.find(key + ":");
    if (at == std::string::npos) return 0;
    return std::atoll(status.c_str() + at + key.size() + 1);
}

std::string policy_name(int policy) {
    switch (policy) {
        case 1: return "FIFO";
        case 2: return "RR";
        case 6: return "DEADLINE";
        default: return "";
    }
}

bool numeric(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

struct Words {
    Lang lang;
    std::string operator()(const std::string& en, const std::string& ja) const { return lang == Lang::Ja ? ja : en; }
};

std::string fixed(double value, int digits) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return text;
}

// Reads one answer; empty on end of input.
std::string ask(std::istream& in, std::ostream& out, const std::string& question) {
    out << question << std::flush;
    std::string answer;
    if (!std::getline(in, answer)) {
        out << "\n";
        return "";
    }
    answer.erase(0, answer.find_first_not_of(" \t"));
    answer.erase(answer.find_last_not_of(" \t\r") + 1);
    if (!isatty(STDIN_FILENO)) out << answer << "\n";  // Answers from a pipe or file: show them.
    return answer;
}

}  // namespace

std::vector<ProcessSample> sample_processes(const SystemView& system) {
    std::vector<ProcessSample> processes;
    for (const auto& entry : system.list("proc")) {
        if (!numeric(entry)) continue;
        const std::string base = "proc/" + entry;
        ProcessSample process;
        process.pid = std::atoi(entry.c_str());
        process.name = system.read_line(base + "/comm").value_or("");
        const auto fields = stat_fields(system.read(base + "/stat").value_or(""));
        if (fields.size() > 6) process.kernel_thread = (std::strtoul(fields[6].c_str(), nullptr, 10) & kKernelThreadFlag) != 0;
        for (const auto& tid : system.list(base + "/task")) {
            if (!numeric(tid)) continue;
            const std::string task = base + "/task/" + tid;
            ThreadSample thread;
            thread.tid = std::atoi(tid.c_str());
            thread.name = system.read_line(task + "/comm").value_or(process.name);
            thread.wakeups = status_value(system.read(task + "/status").value_or(""), "voluntary_ctxt_switches");
            const auto thread_fields = stat_fields(system.read(task + "/stat").value_or(""));
            if (thread_fields.size() > 38) {
                thread.rt_priority = std::atoi(thread_fields[37].c_str());
                thread.policy = std::atoi(thread_fields[38].c_str());
            }
            process.threads.push_back(thread);
        }
        if (!process.name.empty()) processes.push_back(std::move(process));
    }
    return processes;
}

std::vector<Candidate> rank_candidates(const std::vector<ProcessSample>& before,
                                       const std::vector<ProcessSample>& after, double seconds, int self,
                                       std::size_t limit) {
    std::map<int, long long> earlier;  // tid -> wakeups
    for (const auto& process : before) {
        for (const auto& thread : process.threads) earlier[thread.tid] = thread.wakeups;
    }

    std::vector<Candidate> candidates;
    for (const auto& process : after) {
        if (process.kernel_thread || process.pid == self || process.threads.empty()) continue;
        Candidate candidate;
        candidate.pid = process.pid;
        candidate.name = process.name;
        for (const auto& thread : process.threads) {
            const auto it = earlier.find(thread.tid);
            const double rate = it == earlier.end() || seconds <= 0 ? 0 : (thread.wakeups - it->second) / seconds;
            if (rate > candidate.wakeups_per_second || candidate.busiest_thread.empty()) {
                candidate.wakeups_per_second = std::max(rate, 0.0);
                candidate.busiest_thread = thread.name;
            }
            const auto policy = policy_name(thread.policy);
            if (!policy.empty() && thread.rt_priority >= candidate.rt_priority) {
                candidate.policy = policy;
                candidate.rt_priority = thread.rt_priority;
            }
        }
        if (!candidate.policy.empty() || candidate.wakeups_per_second >= 1) candidates.push_back(candidate);
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.policy.empty() != b.policy.empty()) return !a.policy.empty();
        if (a.rt_priority != b.rt_priority) return a.rt_priority > b.rt_priority;
        return a.wakeups_per_second > b.wakeups_per_second;
    });
    if (candidates.size() > limit) candidates.resize(limit);
    return candidates;
}

Lang lang_from_environment() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') continue;
        return std::string(value).rfind("ja", 0) == 0 ? Lang::Ja : Lang::En;
    }
    return Lang::En;
}

int run_wizard(std::istream& in, std::ostream& out, Lang lang, const std::string& version) {
    const Words w{lang};
    const SystemView system("/");
    out << "rt-doctor " << version << w(" - real-time readiness and what delays your program\n",
                                        " - リアルタイム性能の診断と、プログラムを遅らせる原因の調査\n");
    const bool root = geteuid() == 0;
    if (!root) {
        out << w("Note: not running as root. The system check works, but finding what delays a program needs\n"
                 "      root: run 'sudo rtdoctor'.\n",
                 "注意: root で実行されていません。システムの診断はできますが、遅れの原因調査には root が必要です\n"
                 "      （sudo rtdoctor で実行してください）。\n");
    }

    // 1. The system.
    out << "\n" << w("[1/3] Checking this system...", "[1/3] このシステムを診断しています…") << "\n";
    const auto results = run_checks(system, current_process_limits());
    const auto findings = prioritized(results);
    if (findings.empty() || findings.front().impact == Impact::Low) {
        out << "  " << w("No major issues found.", "大きな問題はありません。") << "\n";
    } else {
        for (std::size_t i = 0; i < findings.size() && i < 4; ++i) {
            const auto& f = findings[i];
            out << (i == 0 ? w("  Biggest factor: ", "  一番の要因: ") : w("  Then:           ", "  次に:       "))
                << translate(lang, f.title) << " - " << translate(lang, f.value) << "\n";
        }
        if (findings.front().id == "virtualization") {
            out << w("  This is a virtual machine: deadline misses cannot be brought to zero here.\n",
                     "  仮想マシン上なので、ここではどう調整しても期限切れはゼロにできません。\n");
        }
        out << w("  The report explains why each matters and how to fix it.\n",
                 "  理由と直し方は、最後に作るレポートに書いてあります。\n");
    }

    // 2. The program.
    std::optional<TraceResult> traced;
    std::size_t max_threads = 3;
    if (root) {
        out << "\n" << w("[2/3] Which program is late? Watching running programs for 1 second...",
                         "[2/3] 遅れを調べたいプログラムを選びます。動いているプログラムを1秒間観察しています…")
            << "\n";
        const auto before = sample_processes(system);
        const auto start = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto after = sample_processes(system);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto candidates = rank_candidates(before, after, seconds, getpid());

        if (candidates.empty()) {
            out << w("  No program with a real-time priority or periodic wake-ups was found.\n",
                     "  リアルタイム優先度で動くプログラムも、周期的に動くプログラムも見つかりませんでした。\n");
        } else {
            out << w("  Real-time priority first, then by how often they wake up:\n",
                     "  リアルタイム優先度のあるもの、次に起きる回数が多いものの順です:\n");
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto& c = candidates[i];
            char line[160];
            std::snprintf(line, sizeof(line), "  %2zu) %-16s pid %-7d ", i + 1, c.name.c_str(), c.pid);
            out << line;
            if (!c.policy.empty()) {
                out << w("real-time " + c.policy + " " + std::to_string(c.rt_priority) + ", ",
                         "リアルタイム " + c.policy + " " + std::to_string(c.rt_priority) + "、");
            }
            out << w("wakes " + fixed(c.wakeups_per_second, 0) + "/s", "1秒に " + fixed(c.wakeups_per_second, 0) + " 回起きる");
            if (c.wakeups_per_second >= 2) {
                out << w(" (every ~" + fixed(1000 / c.wakeups_per_second, 1) + " ms)",
                         "（約 " + fixed(1000 / c.wakeups_per_second, 1) + " ms ごと）");
            }
            if (c.busiest_thread != c.name) out << w(", thread ", "、スレッド ") << c.busiest_thread;
            out << "\n";
        }
        const auto answer = ask(in, out,
                                w("  Number, pid or name (Enter = skip): ", "  番号・pid・名前のどれかを入力（Enter で飛ばす）: "));

        TraceRequest request;
        const Candidate* chosen = nullptr;
        if (numeric(answer)) {
            const auto number = std::stoul(answer);
            if (number >= 1 && number <= candidates.size()) chosen = &candidates[number - 1];
            else request.pid = static_cast<int>(number);
        } else if (!answer.empty()) {
            for (const auto& c : candidates) {
                if (c.name == answer || c.busiest_thread == answer) chosen = &c;
            }
            if (!chosen) {
                for (const auto& p : after) {
                    if (!p.kernel_thread && p.name == answer) {
                        request.pid = p.pid;
                        break;
                    }
                }
                if (request.pid == 0) {
                    out << w("  No program named " + answer + "; skipping the trace.\n",
                             "  " + answer + " という名前のプログラムがないので、調査を飛ばします。\n");
                }
            }
        }
        if (chosen) request.pid = chosen->pid;

        if (request.pid > 0) {
            std::string suggestion;
            if (chosen && chosen->wakeups_per_second >= 2) suggestion = fixed(1000 / chosen->wakeups_per_second, 1);
            const auto deadline = ask(in, out,
                                      w("  Deadline in ms, usually its period" +
                                            (suggestion.empty() ? std::string(" (Enter = none)") : " [" + suggestion + "]") + ": ",
                                        "  期限は何 ms ですか？ふつうは周期と同じです" +
                                            (suggestion.empty() ? std::string("（Enter でなし）") : " [" + suggestion + "]") + ": "));
            request.deadline_ms = std::atof((deadline.empty() ? suggestion : deadline).c_str());
            const auto duration = ask(in, out, w("  Watch for how many seconds? [10]: ", "  何秒間見張りますか？ [10]: "));
            request.duration = duration.empty() ? 10 : std::max(1.0, std::atof(duration.c_str()));

            out << w("  Watching for " + fixed(request.duration, 0) + " s; keep the program doing its normal work "
                     "(Ctrl-C ends early)...\n",
                     "  " + fixed(request.duration, 0) + " 秒間見張ります。プログラムはいつもどおり動かしておいてください"
                     "（Ctrl+C で早めに終了）…\n");
            if (!sees_host_pids()) {
                out << w("  (Inside a container: matching the program's thread names.)\n",
                         "  （コンテナ内なので、プログラムのスレッド名で照合します）\n");
            }
            traced.emplace();
            if (!run_trace(request, true, *traced)) {
                traced.reset();
            } else if (!traced->error.empty()) {
                out << w("  Tracing was not possible: ", "  調査できませんでした: ") << traced->error << "\n";
            } else if (traced->threads.empty()) {
                out << w("  It never woke up and slept again while watched; is it a periodic program?\n",
                         "  見張っている間、一度も「起きて寝る」をしませんでした。周期的に動くプログラムですか？\n");
            } else {
                const auto& t = traced->threads.front();
                out << "\n  " << w("Thread ", "スレッド ") << t.comm << ": "
                    << w(std::to_string(t.activations) + " runs", std::to_string(t.activations) + " 回実行");
                if (request.deadline_ms > 0) {
                    out << w(", " + std::to_string(t.misses) + " over the deadline",
                             "、期限切れ " + std::to_string(t.misses) + " 回");
                }
                out << w(", worst ", "、最悪 ") << format_ms(percentile(t.responses, 1.0)) << "\n";
                if (!t.delays.empty()) {
                    out << "  " << w("Delayed most by: ", "一番の原因: ") << translate(lang, t.delays.front().who) << w(", ", "、")
                        << format_ms(t.delays.front().seconds) << "\n  "
                        << w("What to do: ", "対策: ") << translate(lang, advice_for(t.delays.front().who)) << "\n";
                }
            }
        }
    }

    // 3. The report.
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    const std::string file = std::string("rtdoctor-report-") + stamp + ".html";
    out << "\n" << w("[3/3] Writing the report...", "[3/3] レポートを作っています…") << "\n";
    if (!write_report(file, system, results, traced, max_threads, lang, version)) return 2;
    char cwd[4096];
    const std::string where = getcwd(cwd, sizeof(cwd)) ? std::string(cwd) + "/" + file : file;
    out << "  " << where << "\n"
        << w("  Open it in a browser. From another computer: scp <this-host>:" + where + " .\n",
             "  ブラウザで開いてください。別の PC で見るには: scp <このマシン>:" + where + " .\n");
    return 0;
}

}  // namespace rtdoctor
