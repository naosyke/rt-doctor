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

void wrap(std::ostream& out, const std::string& label, const std::string& text) {
    // Indent continuation lines under the text, wrapping at ~100 columns.
    const std::size_t indent = 8, width = 100;
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

void print_text(std::ostream& out, const std::vector<CheckResult>& results, bool color, bool verbose) {
    std::map<Status, int> counts;
    for (const auto& result : results) {
        ++counts[result.status];
        char header[160];
        std::snprintf(header, sizeof(header), "  %-4s  %-40s %s", std::string(to_string(result.status)).c_str(),
                      result.title.c_str(), result.value.c_str());
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
            << "\",\"status\":\"" << to_string(r.status) << "\",\"value\":\"" << json_escape(r.value)
            << "\",\"why\":\"" << json_escape(r.why) << "\",\"fix\":\"" << json_escape(r.fix) << "\"}";
    }
    out << "]}\n";
}

}  // namespace rtdoctor
