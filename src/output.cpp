#include "rtdoctor/output.hpp"

#include <cstdio>
#include <map>

namespace rtdoctor {

namespace {

const char* color_of(Status status) {
    switch (status) {
        case Status::Pass: return "\x1b[32m";
        case Status::Info: return "\x1b[36m";
        case Status::Warn: return "\x1b[33m";
        case Status::Fail: return "\x1b[31m";
    }
    return "";
}

// Prints "label text" at `indent`, wrapping at ~100 columns with
// continuation lines aligned under the text.
void wrap(std::ostream& out, const std::string& label, const std::string& text, std::size_t indent = 8) {
    const std::size_t width = 100;
    std::string line = std::string(indent, ' ') + label;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        if (line.size() + word.size() + 1 > width && line.size() > indent + label.size()) {
            out << line << "\n";
            line = std::string(indent + label.size(), ' ');
        }
        line += (line.back() == ' ' ? "" : " ") + word;
        start = end + 1;
    }
    out << line << "\n";
}

}  // namespace

std::string json_escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    out += escaped;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

void print_summary(std::ostream& out, const std::vector<CheckResult>& results, bool color) {
    const auto findings = prioritized(results);
    out << "Summary\n";
    const bool major = !findings.empty() && findings.front().impact != Impact::Low;
    if (!major) {
        out << "  No major issues found" << (findings.empty() ? "." : "; only minor findings below.") << "\n\n";
        return;
    }
    const auto& top = findings.front();
    if (color) out << color_of(top.status);
    out << "  Biggest factor: " << top.title << " - " << top.value;
    if (color) out << "\x1b[0m";
    out << "\n";
    if (top.id == "virtualization") {
        wrap(out, "", "As long as this system runs in a virtual machine, the hypervisor can pause it at any time. "
                      "The other fixes help, but cannot bring deadline misses to zero; measure on bare metal.", 4);
    } else {
        wrap(out, "", "Fix this first: it has the largest effect on worst-case latency.", 4);
    }
    if (findings.size() > 1) {
        std::string next;
        for (std::size_t i = 1; i < findings.size(); ++i) {
            next += (i > 1 ? ", " : "") + findings[i].title + " (" + std::string(to_string(findings[i].impact)) + ")";
        }
        wrap(out, "Then:", next, 2);
    }
    out << "\n";
}

void print_text(std::ostream& out, const std::vector<CheckResult>& results, bool color, bool verbose) {
    print_summary(out, results, color);
    out << "Checks\n";
    std::map<Status, int> counts;
    for (const auto& result : results) {
        ++counts[result.status];
        const bool finding = result.status == Status::Warn || result.status == Status::Fail;
        const std::string tag = finding ? std::string(to_string(result.status)) + " " +
                                              std::string(to_string(result.impact))
                                        : std::string(to_string(result.status));
        char header[160];
        std::snprintf(header, sizeof(header), "  %-13s %-40s %s", tag.c_str(), result.title.c_str(),
                      result.value.c_str());
        if (color) out << color_of(result.status);
        out << header;
        if (color) out << "\x1b[0m";
        out << "\n";
        const bool explain = result.status == Status::Warn || result.status == Status::Fail || verbose;
        if (explain && !result.why.empty()) wrap(out, "Why:", result.why);
        if (explain && !result.fix.empty()) wrap(out, "Fix:", result.fix);
    }
    out << "\n  " << counts[Status::Fail] << " fail, " << counts[Status::Warn] << " warn, " << counts[Status::Pass]
        << " pass, " << counts[Status::Info] << " info\n";
}

void print_json(std::ostream& out, const std::vector<CheckResult>& results) {
    out << "{\"checks\":[";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        out << (i ? "," : "") << "{\"id\":\"" << json_escape(r.id) << "\",\"title\":\"" << json_escape(r.title)
            << "\",\"status\":\"" << to_string(r.status) << "\",\"impact\":\"" << to_string(r.impact)
            << "\",\"value\":\"" << json_escape(r.value)
            << "\",\"why\":\"" << json_escape(r.why) << "\",\"fix\":\"" << json_escape(r.fix) << "\"}";
    }
    out << "],\"priorities\":[";
    const auto findings = prioritized(results);
    for (std::size_t i = 0; i < findings.size(); ++i) out << (i ? "," : "") << "\"" << json_escape(findings[i].id) << "\"";
    out << "]}\n";
}

}  // namespace rtdoctor
