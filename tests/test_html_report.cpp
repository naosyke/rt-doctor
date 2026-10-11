#include <gtest/gtest.h>

#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "fake_system.hpp"
#include "rtdoctor/html_report.hpp"
#include "rtdoctor/i18n.hpp"

using namespace rtdoctor;

namespace {

std::size_t count(const std::string& text, const std::string& part) {
    std::size_t n = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) ++n;
    return n;
}

// Real check results: a kernel without full preemption (high impact) and SMT
// (low impact) as findings, and a passing clock source.
std::vector<CheckResult> findings() {
    FakeSystem fake;
    fake.file("proc/version", "Linux version 6.8.0 #1 SMP PREEMPT_DYNAMIC\n")
        .file("sys/kernel/debug/sched/preempt", "none (voluntary) full\n")
        .file("sys/devices/system/cpu/smt/active", "1\n")
        .file("sys/devices/system/cpu/clocksource/clocksource0/current_clocksource", "tsc\n");
    const SystemView system(fake.root());
    auto preemption = check_kernel_preemption(system);
    preemption.impact = Impact::High;
    auto smt = check_smt(system);
    smt.status = Status::Warn;  // Info by default; make it a (low) finding.
    smt.impact = Impact::Low;
    auto clock = check_clocksource(system);
    clock.impact = Impact::Low;
    return {preemption, smt, clock};
}

TraceResult traced() {
    TraceResult trace;
    trace.summary = {"threads named \"periodic\"", 5.0, 0.004, 0};
    ThreadReport t;
    t.tid = 42;
    t.comm = "periodic";
    t.activations = 3;
    t.misses = 1;
    t.responses = {0.002, 0.003, 0.006};
    t.waits = 3;
    t.waited_total = 0.004;
    t.waited_max = 0.004;
    t.delays = {{"task <hog> (pid 7)", 0.0039, 2}, {"irq 11 arch_timer", 0.0001, 3}};
    t.worst = {{0, 0.006, 0.004, 1, {{"task <hog> (pid 7)", 0.0039, 1}, {"irq 11 arch_timer", 0.0001, 1}}}};
    trace.threads = {t};
    return trace;
}

}  // namespace

TEST(HtmlReport, EscapesText) {
    EXPECT_EQ(html_escape("<a href=\"x\">'&'</a>"), "&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;");
}

TEST(HtmlReport, ReadsHostInfo) {
    FakeSystem fake;
    fake.file("proc/sys/kernel/hostname", "robot-01\n")
        .file("proc/sys/kernel/osrelease", "6.8.0-rt\n")
        .file("proc/sys/kernel/version", "#1 SMP PREEMPT_RT\n")
        .file("sys/devices/system/cpu/online", "0-3\n")
        .file("proc/cpuinfo", "processor\t: 0\nmodel name\t: Example CPU @ 3.0GHz\n");
    const auto host = read_host_info(SystemView(fake.root()));
    EXPECT_EQ(host.hostname, "robot-01");
    EXPECT_EQ(host.kernel, "6.8.0-rt");
    EXPECT_EQ(host.cpus, 4);
    EXPECT_EQ(host.cpu_model, "Example CPU @ 3.0GHz");
}

TEST(HtmlReport, ChecksOnlyReportLeadsWithTheBiggestFactor) {
    std::ostringstream out;
    print_html(out, HostInfo{"robot-01"}, findings(), std::nullopt);
    const auto html = out.str();
    EXPECT_NE(html.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(html.find("Biggest factor: Kernel preemption model"), std::string::npos);
    EXPECT_EQ(html.find("<h2>Trace"), std::string::npos);
    // The high-impact finding comes before the low one, and the pass is folded away.
    EXPECT_LT(html.find("id=\"kernel_preemption\""), html.find("id=\"smt\""));
    EXPECT_GT(html.find("id=\"clocksource\""), html.find("<details>"));
    // Self-contained: nothing loaded from elsewhere, no scripts.
    EXPECT_EQ(html.find("<script"), std::string::npos);
    EXPECT_EQ(html.find("http"), std::string::npos);
}

TEST(HtmlReport, TraceSectionShowsDelaysDeadlineAndEscapesNames) {
    std::ostringstream out;
    print_html(out, HostInfo{}, findings(), traced());
    const auto html = out.str();
    EXPECT_NE(html.find("<h2>Trace: threads named &quot;periodic&quot;</h2>"), std::string::npos);
    EXPECT_NE(html.find("task &lt;hog&gt; (pid 7)"), std::string::npos);
    EXPECT_EQ(html.find("<hog>"), std::string::npos);
    EXPECT_NE(html.find("deadline 4.00 ms"), std::string::npos);
    EXPECT_NE(html.find("var(--fail)\"><title>1 activations"), std::string::npos);  // The 6 ms one is late.
    EXPECT_NE(html.find("What to do:</b> Another task ran"), std::string::npos);
    EXPECT_EQ(count(html, "class=\"act\""), 1u);
}

TEST(HtmlReport, ExplainsWhenTracingWasNotPossible) {
    TraceResult trace;
    trace.summary.target = "pid 1";
    trace.error = "tracefs is not available";
    std::ostringstream out;
    print_html(out, HostInfo{}, findings(), trace);
    EXPECT_NE(out.str().find("Tracing was not possible: tracefs is not available"), std::string::npos);
}

TEST(HtmlReport, JapaneseReportTranslatesPageChecksAndAdvice) {
    std::ostringstream out;
    print_html(out, HostInfo{}, findings(), traced(), 3, Lang::Ja);
    const auto html = out.str();
    EXPECT_NE(html.find("<html lang=\"ja\">"), std::string::npos);
    EXPECT_NE(html.find("一番の要因: カーネルのプリエンプション方式"), std::string::npos);
    EXPECT_NE(html.find("<dt>対策</dt><dd>PREEMPT_RT カーネルを使ってください"), std::string::npos);
    EXPECT_NE(html.find("タスクの調査: 名前が「periodic」のスレッド"), std::string::npos);
    EXPECT_NE(html.find("タスク &lt;hog&gt; (pid 7)"), std::string::npos);
    EXPECT_NE(html.find("割り込み irq 11 arch_timer"), std::string::npos);
    EXPECT_NE(html.find("対策:</b> タスクが動ける状態なのに"), std::string::npos);
    EXPECT_EQ(html.find("Who delayed it"), std::string::npos);
}

TEST(I18n, EveryCheckTextOfTheTestMachinesHasAJapaneseVersion) {
    // Translated: some Japanese in it, and no English words of 3+ letters
    // left except technical terms (names, units, kernel parameters).
    const auto still_english = [](const std::string& text) {
        static const std::regex word("[A-Za-z]{3,}");
        static const std::set<std::string> technical = {"voluntary", "nohz_full", "rcu_nocbs", "swapfile",
                                                         "performance", "powersave", "MiB", "CPU"};
        for (auto it = std::sregex_iterator(text.begin(), text.end(), word); it != std::sregex_iterator(); ++it) {
            const auto w = it->str();
            bool known = false;
            for (const auto& term : technical) known = known || term.find(w) != std::string::npos;
            if (!known) return true;
        }
        return false;
    };
    for (const auto& text : {"Kernel preemption model", "Permission to lock memory", "preemption: none or voluntary",
                             "deepest enabled: C3 (exit latency 133 us)", "950000 us per 1000000 us",
                             "on: /swapfile (12 MiB used of 1024 MiB)", "performance on 0-3, powersave on 4-7",
                             "Add nohz_full=2-3 rcu_nocbs=2-3 to the kernel command line."}) {
        const auto japanese = translate(Lang::Ja, text);
        EXPECT_FALSE(still_english(japanese)) << text << " -> " << japanese;
    }
    EXPECT_EQ(translate(Lang::En, "Swap"), "Swap");
    EXPECT_EQ(translate(Lang::Ja, "Swap"), "スワップ");
    EXPECT_EQ(translate(Lang::Ja, "something new"), "something new");  // Falls back to English.
}
