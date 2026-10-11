#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "fake_system.hpp"
#include "rtdoctor/html_report.hpp"

using namespace rtdoctor;

namespace {

std::size_t count(const std::string& text, const std::string& part) {
    std::size_t n = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) ++n;
    return n;
}

std::vector<CheckResult> findings() {
    return {
        {"kernel_preemption", "Kernel preemption", Status::Warn, "voluntary", "Long kernel paths.", "Use PREEMPT_RT.",
         Impact::High},
        {"smt", "Simultaneous multithreading", Status::Warn, "on", "Siblings share a core.", "Turn it off.", Impact::Low},
        {"clocksource", "Clock source", Status::Pass, "tsc", "", "", Impact::Low},
    };
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
    EXPECT_NE(html.find("Biggest factor: Kernel preemption"), std::string::npos);
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
