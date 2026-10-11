#include "rtdoctor/html_report.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

#include "rtdoctor/i18n.hpp"

namespace rtdoctor {

namespace {

const char* const kStyle = R"CSS(
:root {
  --bg: #f6f7f9; --card: #ffffff; --text: #1d2330; --muted: #5d6677; --line: #e1e4ea;
  --pass: #1f8a4c; --info: #2a6fb0; --warn: #b7791f; --fail: #c53030;
  --pass-bg: #e6f4ec; --info-bg: #e7f0f9; --warn-bg: #fbf1e1; --fail-bg: #fbe7e7;
  --run: #9aa5b8; --task: #d08a1e; --irq: #7b5cc4; --softirq: #2f84c6; --idle: #2a9d8f; --other: #c9ced8;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #12151b; --card: #1b2029; --text: #e3e7ee; --muted: #9aa3b2; --line: #2c3340;
    --pass: #4cc082; --info: #66a8e6; --warn: #e3a948; --fail: #f07070;
    --pass-bg: #173326; --info-bg: #172a3d; --warn-bg: #3a2c14; --fail-bg: #3d1c1c;
    --run: #5b6578; --task: #e3a948; --irq: #a58bea; --softirq: #5aa6e0; --idle: #4cc0b0; --other: #3a4250;
  }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text);
  font: 15px/1.6 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial,
    "Hiragino Sans", "Hiragino Kaku Gothic ProN", "Noto Sans JP", "Yu Gothic", Meiryo, sans-serif; }
main { max-width: 960px; margin: 0 auto; padding: 32px 16px 48px; }
h1 { font-size: 24px; margin: 0 0 4px; }
h2 { font-size: 18px; margin: 36px 0 12px; }
h3 { font-size: 15px; margin: 20px 0 8px; }
.sub { color: var(--muted); margin: 0 0 20px; }
.card { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 18px 20px; margin: 12px 0; }
.facts { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 10px 24px; }
.facts div span, .tile span { display: block; color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .04em; }
.facts div b { font-weight: 600; overflow-wrap: anywhere; }
.verdict { border-left: 5px solid var(--muted); }
.verdict.fail { border-left-color: var(--fail); } .verdict.warn { border-left-color: var(--warn); }
.verdict.pass { border-left-color: var(--pass); }
.verdict .big { font-size: 18px; font-weight: 600; margin: 0 0 6px; }
.verdict p { margin: 6px 0; }
.pill { display: inline-block; min-width: 46px; text-align: center; padding: 1px 8px; border-radius: 999px;
  font-size: 12px; font-weight: 700; letter-spacing: .03em; }
.pill.PASS { color: var(--pass); background: var(--pass-bg); } .pill.INFO { color: var(--info); background: var(--info-bg); }
.pill.WARN { color: var(--warn); background: var(--warn-bg); } .pill.FAIL { color: var(--fail); background: var(--fail-bg); }
.impact { color: var(--muted); font-size: 12px; margin-left: 6px; }
.check { border-top: 1px solid var(--line); padding: 12px 0; }
.check:first-child { border-top: 0; padding-top: 0; }
.check .head { display: flex; flex-wrap: wrap; gap: 4px 10px; align-items: baseline; }
.check .title { font-weight: 600; }
.check .value { color: var(--muted); overflow-wrap: anywhere; }
.check dl { margin: 8px 0 0; display: grid; grid-template-columns: 44px 1fr; gap: 4px 8px; }
.check dt { color: var(--muted); font-size: 13px; }
.check dd { margin: 0; }
details summary { cursor: pointer; color: var(--muted); padding: 6px 0; }
code { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 13px; }
.tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 10px; margin: 8px 0 16px; }
.tile { border: 1px solid var(--line); border-radius: 8px; padding: 10px 12px; }
.tile b { font-size: 20px; font-variant-numeric: tabular-nums; }
.tile.bad b { color: var(--fail); } .tile.good b { color: var(--pass); }
svg.hist { width: 100%; height: auto; display: block; }
svg.hist text { fill: var(--muted); font-size: 11px; }
.who { display: grid; grid-template-columns: minmax(0, 1fr) 90px; gap: 2px 12px; align-items: center; margin: 6px 0; }
.who .name { overflow-wrap: anywhere; }
.who .ms { text-align: right; font-variant-numeric: tabular-nums; }
.who .bar { grid-column: 1 / -1; height: 8px; background: var(--line); border-radius: 4px; overflow: hidden; }
.who .bar i { display: block; height: 100%; border-radius: 4px; }
.who .count { grid-column: 1 / -1; color: var(--muted); font-size: 12px; }
.advice { background: var(--bg); border-radius: 8px; padding: 10px 12px; margin: 12px 0 0; }
.act { margin: 10px 0; }
.act .label { display: flex; justify-content: space-between; gap: 12px; font-size: 13px; flex-wrap: wrap; }
.act .label b { font-variant-numeric: tabular-nums; }
.track { position: relative; height: 16px; background: var(--bg); border-radius: 4px; margin-top: 4px; }
.seg { display: flex; height: 100%; border-radius: 4px; overflow: hidden; }
.seg i { display: block; height: 100%; }
.deadline { position: absolute; top: -3px; bottom: -3px; border-left: 2px dashed var(--fail); }
.legend { display: flex; flex-wrap: wrap; gap: 6px 16px; font-size: 12px; color: var(--muted); margin: 8px 0; }
.legend i { display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin-right: 5px; vertical-align: -1px; }
.note { color: var(--warn); }
.lat { margin: 12px 0 4px; font-variant-numeric: tabular-nums; }
.lat-row { display: grid; grid-template-columns: 36px repeat(5, minmax(52px, 72px)) minmax(60px, 1fr); gap: 8px;
  align-items: center; padding: 4px 0; border-top: 1px solid var(--line); font-size: 13px; }
.lat-head { color: var(--muted); font-size: 12px; border-top: 0; }
.lat-row .track { height: 10px; margin-top: 0; }
.p99 { position: absolute; top: -3px; bottom: -3px; border-left: 2px solid var(--text); opacity: .6; }
@media (max-width: 560px) { .lat-row { grid-template-columns: 28px repeat(3, 1fr); } .lat-row > :nth-child(2),
  .lat-row > :nth-child(3), .lat-row > :nth-child(5) { display: none; } .lat-row .track { grid-column: 1 / -1; } }
footer { color: var(--muted); font-size: 12px; margin-top: 40px; }
@media print { body { background: #fff; } .card { break-inside: avoid; } details { display: block; } }
)CSS";

std::string fmt(const char* format, double value) {
    char text[64];
    std::snprintf(text, sizeof(text), format, value);
    return text;
}

std::string ms(double seconds) { return format_ms(seconds); }

// Picks the English or the Japanese wording of the page itself.
struct Words {
    Lang lang;
    std::string operator()(const std::string& en, const std::string& ja) const { return lang == Lang::Ja ? ja : en; }
    std::string tr(const std::string& english) const { return translate(lang, english); }
    std::string impact(Impact impact) const {
        if (lang == Lang::En) return std::string(to_string(impact));
        switch (impact) {
            case Impact::Critical: return "致命的";
            case Impact::High: return "大";
            case Impact::Medium: return "中";
            case Impact::Low: return "小";
            case Impact::Unset: break;
        }
        return "";
    }
};

// The color of a kind of delay: another task, an interrupt, a softirq, idle.
std::string color_of(const std::string& who) {
    if (who == "idle") return "var(--idle)";
    if (who.rfind("irq ", 0) == 0) return "var(--irq)";
    if (who.rfind("softirq ", 0) == 0) return "var(--softirq)";
    return "var(--task)";
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// 1, 2 or 5 times a power of ten, so that `range` gets about `ticks` steps.
double nice_step(double range, int ticks) {
    const double raw = range / ticks;
    const double power = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double m : {1.0, 2.0, 5.0}) {
        if (raw <= m * power) return m * power;
    }
    return 10 * power;
}

void print_host(std::ostream& out, const HostInfo& host, const Words& w) {
    out << "<div class=\"card facts\">";
    const auto fact = [&](const std::string& label, const std::string& value) {
        if (!value.empty()) out << "<div><span>" << label << "</span><b>" << html_escape(value) << "</b></div>";
    };
    fact(w("Host", "ホスト"), host.hostname);
    fact(w("Kernel", "カーネル"), host.kernel);
    fact("CPU", host.cpu_model.empty() ? host.machine : host.cpu_model + " (" + host.machine + ")");
    fact(w("Online CPUs", "使用中のCPU数"), host.cpus > 0 ? std::to_string(host.cpus) : "");
    fact(w("Kernel build", "カーネルのビルド"), host.build);
    fact(w("Generated", "作成日時"), host.generated);
    out << "</div>\n";
}

void print_verdict(std::ostream& out, const std::vector<CheckResult>& results, const Words& w) {
    const auto findings = prioritized(results);
    const bool major = !findings.empty() && findings.front().impact != Impact::Low;
    if (!major) {
        out << "<div class=\"card verdict pass\"><p class=\"big\">" << w("No major issues found", "大きな問題はありません")
            << "</p><p>"
            << (findings.empty() ? w("Every check passed.", "すべてのチェックに合格しました。")
                                 : w("Only minor findings; see the checks below.",
                                     "軽微な指摘のみです。下のチェック結果を見てください。"))
            << "</p></div>\n";
        return;
    }
    const auto& top = findings.front();
    out << "<div class=\"card verdict " << lower(to_string(top.status)) << "\"><p class=\"big\">"
        << w("Biggest factor: ", "一番の要因: ") << html_escape(w.tr(top.title)) << "</p><p>"
        << html_escape(w.tr(top.value)) << "</p><p>";
    if (top.id == "virtualization") {
        out << html_escape(w.tr("As long as this system runs in a virtual machine, the hypervisor can pause it at any "
                                "time. The other fixes help, but cannot bring deadline misses to zero; measure on "
                                "bare metal."));
    } else {
        out << w("Fix this first: it has the largest effect on worst-case latency.",
                 "まずこれを直してください。最悪の遅延に一番大きく影響します。");
    }
    out << "</p>";
    if (findings.size() > 1) {
        out << "<p>" << w("Then: ", "次に: ");
        for (std::size_t i = 1; i < findings.size(); ++i) {
            out << (i > 1 ? w(", ", "、") : "") << html_escape(w.tr(findings[i].title)) << " <span class=\"impact\">"
                << w.impact(findings[i].impact) << "</span>";
        }
        out << "</p>";
    }
    out << "</div>\n";
}

void print_check(std::ostream& out, const CheckResult& r, const Words& w) {
    const bool finding = r.status == Status::Warn || r.status == Status::Fail;
    out << "<div class=\"check\" id=\"" << html_escape(r.id) << "\"><div class=\"head\"><span class=\"pill "
        << to_string(r.status) << "\">" << to_string(r.status) << "</span>";
    if (finding) {
        out << "<span class=\"impact\">" << w(w.impact(r.impact) + " impact", "影響: " + w.impact(r.impact)) << "</span>";
    }
    out << "<span class=\"title\">" << html_escape(w.tr(r.title)) << "</span><span class=\"value\">"
        << html_escape(w.tr(r.value)) << "</span></div>";
    if (!r.why.empty() || !r.fix.empty()) {
        out << "<dl>";
        if (!r.why.empty()) out << "<dt>" << w("Why", "理由") << "</dt><dd>" << html_escape(w.tr(r.why)) << "</dd>";
        if (!r.fix.empty()) out << "<dt>" << w("Fix", "対策") << "</dt><dd>" << html_escape(w.tr(r.fix)) << "</dd>";
        out << "</dl>";
    }
    out << "</div>\n";
}

void print_checks(std::ostream& out, const std::vector<CheckResult>& results, const Words& w) {
    std::map<Status, int> counts;
    for (const auto& r : results) ++counts[r.status];
    const auto n = [&](Status s) { return std::to_string(counts[s]); };
    out << "<h2>" << w("System checks", "システムチェック") << "</h2><p class=\"sub\">"
        << w(n(Status::Fail) + " fail, " + n(Status::Warn) + " warn, " + n(Status::Pass) + " pass, " +
                 n(Status::Info) + " info. Findings are ordered by their typical effect on worst-case latency.",
             "FAIL " + n(Status::Fail) + " 件、WARN " + n(Status::Warn) + " 件、PASS " + n(Status::Pass) + " 件、INFO " +
                 n(Status::Info) + " 件。指摘は、最悪の遅延への影響が大きい順に並んでいます。")
        << "</p>\n";

    out << "<div class=\"card\">";
    const auto findings = prioritized(results);
    if (findings.empty()) out << "<p>" << w("No findings.", "指摘はありません。") << "</p>";
    for (const auto& r : findings) print_check(out, r, w);
    out << "</div>\n";

    const auto rest = std::to_string(counts[Status::Pass] + counts[Status::Info]);
    out << "<div class=\"card\"><details><summary>"
        << w(rest + " checks without findings", "指摘のないチェック " + rest + " 件") << "</summary>";
    for (const auto& r : results) {
        if (r.status == Status::Pass || r.status == Status::Info) print_check(out, r, w);
    }
    out << "</details></div>\n";
}

// Histogram of response times with the deadline marked. Counts are on a
// log scale so that the rare slow activations stay visible next to the
// thousands of normal ones.
void print_histogram(std::ostream& out, const std::vector<double>& responses, double deadline, const Words& w) {
    if (responses.empty()) return;
    const double width = 720, height = 190, left = 36, bottom = 26, top = 14;
    const double plot_w = width - left - 10, plot_h = height - bottom - top;
    const double largest = *std::max_element(responses.begin(), responses.end());
    const double x_max = std::max(largest, deadline) * 1.08 * 1000;  // ms
    const int bins = 60;
    std::vector<int> counts(bins, 0);
    for (const double r : responses) {
        const int bin = std::min(bins - 1, static_cast<int>(r * 1000 / x_max * bins));
        ++counts[bin];
    }
    const int peak = *std::max_element(counts.begin(), counts.end());
    const double y_max = std::log10(peak + 1.0);
    const auto x = [&](double value_ms) { return left + value_ms / x_max * plot_w; };

    out << "<svg class=\"hist\" viewBox=\"0 0 " << width << " " << height << "\" role=\"img\" aria-label=\""
        << w("Histogram of response times", "応答時間の分布") << "\">";
    // Y grid: 1, 10, 100, ... activations.
    for (int power = 0; std::pow(10, power) <= peak; ++power) {
        const double y = top + plot_h - std::log10(std::pow(10, power) + 1.0) / y_max * plot_h;
        out << "<line x1=\"" << left << "\" x2=\"" << width - 10 << "\" y1=\"" << y << "\" y2=\"" << y
            << "\" stroke=\"var(--line)\"/><text x=\"" << left - 6 << "\" y=\"" << y + 4
            << "\" text-anchor=\"end\">" << static_cast<long>(std::pow(10, power)) << "</text>";
    }
    const double bar_w = plot_w / bins;
    for (int i = 0; i < bins; ++i) {
        if (counts[i] == 0) continue;
        const double h = std::max(1.5, std::log10(counts[i] + 1.0) / y_max * plot_h);
        const double start_ms = x_max * i / bins;
        const bool late = deadline > 0 && start_ms + x_max / bins > deadline * 1000;
        const std::string range = fmt("%.2f", start_ms) + "-" + fmt("%.2f", start_ms + x_max / bins) + " ms";
        out << "<rect x=\"" << fmt("%.1f", left + i * bar_w + 0.5) << "\" y=\"" << fmt("%.1f", top + plot_h - h)
            << "\" width=\"" << fmt("%.1f", std::max(1.0, bar_w - 1)) << "\" height=\"" << fmt("%.1f", h)
            << "\" rx=\"1\" fill=\"" << (late ? "var(--fail)" : "var(--info)") << "\"><title>"
            << w(std::to_string(counts[i]) + " activations, " + range, range + ": " + std::to_string(counts[i]) + " 回")
            << "</title></rect>";
    }
    const double step = nice_step(x_max, 6);
    for (double t = 0; t <= x_max + 1e-9; t += step) {
        out << "<text x=\"" << fmt("%.1f", x(t)) << "\" y=\"" << height - 8 << "\" text-anchor=\"middle\">"
            << fmt(step < 1 ? "%.1f" : "%.0f", t) << "</text>";
    }
    out << "<text x=\"" << width - 10 << "\" y=\"" << top - 2 << "\" text-anchor=\"end\">"
        << w("response time (ms)", "横軸: 応答時間 (ms)、縦軸: 回数") << "</text>";
    out << "<line x1=\"" << left << "\" x2=\"" << width - 10 << "\" y1=\"" << top + plot_h << "\" y2=\""
        << top + plot_h << "\" stroke=\"var(--muted)\"/>";
    if (deadline > 0) {
        const double dx = x(deadline * 1000);
        out << "<line x1=\"" << fmt("%.1f", dx) << "\" x2=\"" << fmt("%.1f", dx) << "\" y1=\"" << top - 4
            << "\" y2=\"" << top + plot_h << "\" stroke=\"var(--fail)\" stroke-width=\"2\" stroke-dasharray=\"5 4\"/>"
            << "<text x=\"" << fmt("%.1f", dx + 5) << "\" y=\"" << top + 10 << "\" style=\"fill:var(--fail)\">"
            << w("deadline ", "期限 ") << html_escape(ms(deadline)) << "</text>";
    }
    out << "</svg>\n";
}

void print_legend(std::ostream& out, const Words& w) {
    out << "<div class=\"legend\"><span><i style=\"background:var(--run)\"></i>" << w("running itself", "自分の処理")
        << "</span><span><i style=\"background:var(--task)\"></i>" << w("another task", "他のタスク")
        << "</span><span><i style=\"background:var(--irq)\"></i>" << w("interrupt", "割り込み")
        << "</span><span><i style=\"background:var(--softirq)\"></i>" << w("softirq", "softirq（割り込みの後処理）")
        << "</span><span><i style=\"background:var(--idle)\"></i>" << w("CPU waking from idle", "CPU の休止からの復帰")
        << "</span></div>\n";
}

void print_thread(std::ostream& out, const ThreadReport& t, double deadline, const Words& w) {
    out << "<div class=\"card\"><h3>" << w("Thread ", "スレッド ") << html_escape(t.comm)
        << " <span class=\"impact\">tid " << t.tid << "</span></h3><div class=\"tiles\">";
    const auto tile = [&](const std::string& label, const std::string& value, const char* cls = "") {
        out << "<div class=\"tile " << cls << "\"><span>" << label << "</span><b>" << html_escape(value) << "</b></div>";
    };
    tile(w("Activations", "実行回数"), std::to_string(t.activations));
    if (deadline > 0) tile(w("Over the deadline", "期限切れ"), std::to_string(t.misses), t.misses > 0 ? "bad" : "good");
    tile(w("Median response", "応答時間の中央値"), ms(percentile(t.responses, 0.5)));
    tile(w("99th percentile", "99パーセンタイル"), ms(percentile(t.responses, 0.99)));
    tile(w("Worst response", "最悪の応答時間"), ms(percentile(t.responses, 1.0)),
         deadline > 0 ? (percentile(t.responses, 1.0) > deadline ? "bad" : "good") : "");
    tile(w("Waited for its CPU", "CPU の空き待ち合計"), ms(t.waited_total));
    out << "</div>";
    print_histogram(out, t.responses, deadline, w);

    if (!t.delays.empty()) {
        const auto waits = std::to_string(t.waits), preempted = std::to_string(t.preemptions);
        out << "<h3>" << w("Who delayed it", "誰が遅らせたか") << "</h3><p class=\"sub\">"
            << w("What ran on its CPU while it was ready to run, over the whole trace. Waited " + waits + " times, " +
                     ms(t.waited_total) + " in total, longest " + ms(t.waited_max) + ", preempted " + preempted +
                     " times.",
                 "動ける状態なのに待たされていた間、その CPU で何が動いていたか（計測期間全体）。待ちは " + waits +
                     " 回、合計 " + ms(t.waited_total) + "、最長 " + ms(t.waited_max) + "、実行中に横取りされた回数 " +
                     preempted + " 回。")
            << "</p>";
        const double largest = t.delays.front().seconds;
        for (std::size_t d = 0; d < t.delays.size() && d < 6; ++d) {
            const auto& delay = t.delays[d];
            const auto count = std::to_string(delay.count);
            out << "<div class=\"who\"><span class=\"name\">" << html_escape(w.tr(delay.who))
                << "</span><span class=\"ms\">" << html_escape(ms(delay.seconds))
                << "</span><span class=\"bar\"><i style=\"width:"
                << fmt("%.1f", largest > 0 ? std::max(0.5, delay.seconds / largest * 100) : 0) << "%;background:"
                << color_of(delay.who) << "\"></i></span><span class=\"count\">"
                << w("in " + count + " waits", count + " 回の待ちで発生") << "</span></div>";
        }
        out << "<p class=\"advice\"><b>" << w("What to do:", "対策:") << "</b> "
            << html_escape(w.tr(advice_for(t.delays.front().who))) << "</p>";
    }

    if (!t.worst.empty()) {
        out << "<h3>" << w("Worst activations", "時間がかかった回ワースト") << "</h3><p class=\"sub\">"
            << w("Each bar is one activation from wake-up to sleep: the time it ran itself and what it waited for.",
                 "1本の棒が1回分（起きてから寝るまで）です。自分の処理の時間と、何に待たされたかを表します。"
                 "赤い点線が期限です。")
            << "</p>";
        print_legend(out, w);
        double scale = std::max(t.worst.front().response, deadline);
        for (const auto& a : t.worst) {
            const auto pct = [&](double seconds) { return fmt("%.2f", std::max(0.0, seconds / scale * 100)); };
            out << "<div class=\"act\"><div class=\"label\"><span>CPU " << a.cpu << " &middot; "
                << w("waited ", "待ち ") << html_escape(ms(a.waited)) << "</span><b>" << html_escape(ms(a.response))
                << "</b></div><div class=\"track\"><div class=\"seg\">";
            out << "<i style=\"width:" << pct(a.response - a.waited) << "%;background:var(--run)\" title=\""
                << w("running ", "自分の処理 ") << html_escape(ms(a.response - a.waited)) << "\"></i>";
            double attributed = 0;
            for (const auto& d : a.delays) {
                attributed += d.seconds;
                out << "<i style=\"width:" << pct(d.seconds) << "%;background:" << color_of(d.who) << "\" title=\""
                    << html_escape(w.tr(d.who) + " " + ms(d.seconds)) << "\"></i>";
            }
            if (a.waited - attributed > 0) {
                out << "<i style=\"width:" << pct(a.waited - attributed) << "%;background:var(--other)\"></i>";
            }
            out << "</div>";
            if (deadline > 0) out << "<span class=\"deadline\" style=\"left:" << pct(deadline) << "%\"></span>";
            out << "</div><div class=\"label\"><span>";
            for (std::size_t d = 0; d < a.delays.size() && d < 3; ++d) {
                out << (d ? w(", ", "、") : "") << html_escape(w.tr(a.delays[d].who)) << " "
                    << html_escape(ms(a.delays[d].seconds));
            }
            out << "</span></div></div>\n";
        }
    }
    out << "</div>\n";
}

std::string us(double value) { return fmt(value < 10 ? "%.1f us" : "%.0f us", value); }

void print_latency(std::ostream& out, const LatencyData& latency, const Words& w) {
    const auto& r = latency.result;
    out << "<h2>" << w("Wake-up latency per CPU", "CPU ごとの起床遅延") << "</h2><p class=\"sub\">"
        << w("A thread on each CPU asked to wake up every " + std::to_string(r.options.interval_us) +
                 " us and recorded how late it actually woke up, for " + fmt("%.1f", r.duration) + " s" +
                 (r.realtime ? " with real-time priority " + std::to_string(r.options.priority) : "") + ".",
             "各 CPU で、" + std::to_string(r.options.interval_us) + " us ごとに起こしてもらう約束をしたスレッドが、実際に何 us "
                 "遅れて起きたかを " + fmt("%.1f", r.duration) + " 秒間記録しました" +
                 (r.realtime ? "（リアルタイム優先度 " + std::to_string(r.options.priority) + "）" : "") + "。")
        << "</p>";
    if (!r.realtime) {
        out << "<p class=\"note\">" << w("Real-time priority was not allowed (not root): these are latencies of a normal task.",
                                         "リアルタイム優先度が使えなかったため（root でない）、普通の優先度での値です。")
            << "</p>";
    }
    const auto* worst = worst_cpu(r);
    if (!worst) return;
    out << "<div class=\"card\"><div class=\"tiles\">";
    const auto tile = [&](const std::string& label, const std::string& value, const char* cls = "") {
        out << "<div class=\"tile " << cls << "\"><span>" << label << "</span><b>" << html_escape(value) << "</b></div>";
    };
    const double max = worst->stats.max();
    tile(w("Worst wake-up", "最悪の遅れ"), us(max), max > 1000 ? "bad" : max <= 150 ? "good" : "");
    tile(w("On CPU", "その CPU"), std::to_string(worst->cpu));
    std::int64_t samples = 0;
    for (const auto& c : r.cpus) samples += c.stats.samples();
    tile(w("Wake-ups measured", "測定回数"), std::to_string(samples));
    out << "</div><p class=\"advice\"><b>" << w("Reading:", "目安:") << "</b> " << html_escape(w.tr(latency_verdict(max)))
        << "</p>";

    // One row per CPU; the bar is the maximum, the tick the 99th percentile.
    out << "<div class=\"lat\"><div class=\"lat-row lat-head\"><span>CPU</span><span>min</span><span>"
        << w("avg", "平均") << "</span><span>99%</span><span>99.9%</span><span>max</span><span></span></div>";
    for (const auto& c : r.cpus) {
        const auto& st = c.stats;
        const auto pct = [&](double v) { return fmt("%.1f", max > 0 ? std::min(100.0, v / max * 100) : 0); };
        out << "<div class=\"lat-row\"><span>" << c.cpu << "</span><span>" << us(st.min()) << "</span><span>"
            << us(st.average()) << "</span><span>" << us(st.percentile(0.99)) << "</span><span>"
            << us(st.percentile(0.999)) << "</span><span><b>" << us(st.max()) << "</b></span><span class=\"track\">"
            << "<span class=\"seg\"><i style=\"width:" << pct(st.max()) << "%;background:"
            << (st.max() > 1000 ? "var(--fail)" : st.max() > 150 ? "var(--warn)" : "var(--pass)")
            << "\"></i></span><span class=\"p99\" style=\"left:" << pct(st.percentile(0.99)) << "%\"></span></span></div>";
    }
    out << "</div><p class=\"sub\">" << w("Bar: maximum (relative to the worst CPU). Tick: 99th percentile.",
                                           "棒: 最大値（一番悪い CPU を基準にした長さ）。縦線: 99パーセンタイル。")
        << "</p>";

    if (latency.causes && !latency.causes->delays.empty()) {
        const auto& causes = *latency.causes;
        out << "<h3>" << w("What ran instead when a measuring thread was woken",
                           "測定スレッドが起こされてから動くまでの間に、その CPU で動いていたもの")
            << "</h3>";
        const double largest = causes.delays.front().seconds;
        for (std::size_t d = 0; d < causes.delays.size() && d < 6; ++d) {
            const auto& delay = causes.delays[d];
            const auto count = std::to_string(delay.count);
            out << "<div class=\"who\"><span class=\"name\">" << html_escape(w.tr(delay.who))
                << "</span><span class=\"ms\">" << html_escape(ms(delay.seconds)) << "</span><span class=\"bar\"><i style=\"width:"
                << fmt("%.1f", largest > 0 ? std::max(0.5, delay.seconds / largest * 100) : 0) << "%;background:"
                << color_of(delay.who) << "\"></i></span><span class=\"count\">"
                << w("in " + count + " waits", count + " 回の待ちで発生") << "</span></div>";
        }
        out << "<p class=\"advice\"><b>" << w("What to do:", "対策:") << "</b> "
            << html_escape(w.tr(advice_for(causes.delays.front().who))) << "</p>";
    }
    out << "</div>\n";
}

void print_trace(std::ostream& out, const TraceResult& trace, std::size_t max_threads, const Words& w) {
    const auto& s = trace.summary;
    out << "<h2>" << w("Trace: ", "タスクの調査: ") << html_escape(w.tr(s.target)) << "</h2>";
    if (!trace.error.empty()) {
        out << "<div class=\"card\"><p class=\"note\">" << w("Tracing was not possible: ", "調査できませんでした: ")
            << html_escape(trace.error) << "</p><p>"
            << w("Run rtdoctor report as root (or in a container with --privileged).",
                 "rtdoctor report を root で実行してください（コンテナなら --privileged 付きで）。")
            << "</p></div>\n";
        return;
    }
    out << "<p class=\"sub\">"
        << w(fmt("%.1f", s.duration) + " s traced" + (s.deadline > 0 ? ", deadline " + ms(s.deadline) : "") +
                 ". An activation runs from the task's wake-up until it sleeps again.",
             fmt("%.1f", s.duration) + " 秒間の計測" + (s.deadline > 0 ? "、期限 " + ms(s.deadline) : "") +
                 "。「1回の実行」は、タスクが起きてから再び寝るまでです。")
        << "</p>";
    if (s.lost_events > 0) {
        const auto lost = std::to_string(s.lost_events);
        out << "<p class=\"note\">"
            << w("The kernel dropped " + lost +
                     " trace events (buffer full); the numbers may be incomplete. Trace a shorter time.",
                 "カーネルが記録を " + lost +
                     " 件取りこぼしました（バッファが満杯）。数値が不完全な可能性があります。計測時間を短くしてください。")
            << "</p>";
    }
    if (trace.threads.empty()) {
        out << "<div class=\"card\"><p>"
            << w("The target never woke up and went back to sleep during the trace (no activations). Is it running, "
                 "and does it sleep between its periodic work?",
                 "計測中、対象が「起きて、また寝る」ことが一度もありませんでした。対象は動いていますか？周期処理の"
                 "合間に休んでいますか？")
            << "</p></div>\n";
        return;
    }
    for (std::size_t i = 0; i < trace.threads.size() && i < max_threads; ++i) {
        print_thread(out, trace.threads[i], s.deadline, w);
    }
    if (trace.threads.size() > max_threads) {
        const auto more = std::to_string(trace.threads.size() - max_threads);
        out << "<p class=\"sub\">"
            << w(more + " more threads not shown (use --threads N).",
                 "ほかに " + more + " 個のスレッドがあります（--threads N で表示）。")
            << "</p>";
    }
}

}  // namespace

std::string html_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

HostInfo read_host_info(const SystemView& system) {
    HostInfo host;
    host.hostname = system.read_line("proc/sys/kernel/hostname").value_or("");
    host.kernel = system.read_line("proc/sys/kernel/osrelease").value_or("");
    host.build = system.read_line("proc/sys/kernel/version").value_or("");
    if (const auto online = system.read_line("sys/devices/system/cpu/online")) {
        host.cpus = static_cast<int>(parse_cpu_list(*online).size());
    }
    if (const auto cpuinfo = system.read("proc/cpuinfo")) {
        std::istringstream lines(*cpuinfo);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.rfind("model name", 0) != 0 && line.rfind("Model", 0) != 0) continue;
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            host.cpu_model = line.substr(line.find_first_not_of(" \t", colon + 1));
            break;
        }
    }
    if (host.cpu_model.empty()) {
        // Arm boards: the devicetree names the board instead.
        auto model = system.read_line("proc/device-tree/model").value_or("");
        model.erase(std::find(model.begin(), model.end(), '\0'), model.end());
        host.cpu_model = model;
    }
    return host;
}

void print_html(std::ostream& out, const HostInfo& host, const std::vector<CheckResult>& results,
                const std::optional<TraceResult>& trace, std::size_t max_threads, Lang lang,
                const std::optional<LatencyData>& latency) {
    const Words w{lang};
    const std::string name = html_escape(host.hostname.empty() ? w("this system", "このシステム") : host.hostname);
    out << "<!DOCTYPE html>\n<html lang=\"" << w("en", "ja") << "\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
           "<meta name=\"color-scheme\" content=\"light dark\"><title>"
        << w("rt-doctor report", "rt-doctor レポート") << (host.hostname.empty() ? "" : " - " + html_escape(host.hostname))
        << "</title><style>" << kStyle << "</style></head><body><main>\n";
    out << "<h1>" << w("rt-doctor report", "rt-doctor レポート") << "</h1><p class=\"sub\">"
        << w("Real-time readiness of " + name + (trace ? " and what delayed the traced task." : "."),
             name + " のリアルタイム性能の診断" + (trace ? "と、指定したタスクを遅らせた原因の調査。" : "。"))
        << "</p>\n";
    print_host(out, host, w);
    print_verdict(out, results, w);
    if (latency) print_latency(out, *latency, w);
    if (trace) print_trace(out, *trace, max_threads, w);
    print_checks(out, results, w);
    out << "<footer>"
        << w("Generated by rt-doctor " + html_escape(host.version) +
                 ". The checks read /proc and /sys; the trace uses a private ftrace instance.",
             "rt-doctor " + html_escape(host.version) +
                 " で作成。チェックは /proc と /sys を読み取り、調査は専用の ftrace インスタンスを使っています。")
        << "</footer>\n</main></body></html>\n";
}

}  // namespace rtdoctor
