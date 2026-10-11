#include "rtdoctor/trace_event.hpp"

#include <cstdlib>

namespace rtdoctor {

const std::string& TraceEvent::field(const std::string& key) const {
    static const std::string empty;
    const auto it = fields.find(key);
    return it == fields.end() ? empty : it->second;
}

int TraceEvent::int_field(const std::string& key, int fallback) const {
    const auto& value = field(key);
    if (value.empty()) return fallback;
    char* end = nullptr;
    const long number = std::strtol(value.c_str(), &end, 10);
    return end == value.c_str() ? fallback : static_cast<int>(number);
}

std::optional<TraceEvent> parse_trace_line(const std::string& line) {
    if (line.empty() || line[0] == '#') return std::nullopt;

    // "<comm>-<pid> [CPU] flags TIMESTAMP: EVENT: payload"
    const auto open = line.find(" [");
    if (open == std::string::npos) return std::nullopt;
    const auto close = line.find(']', open);
    if (close == std::string::npos) return std::nullopt;

    TraceEvent event;
    event.cpu = std::atoi(line.c_str() + open + 2);

    const auto event_end = line.find(": ", close);
    if (event_end == std::string::npos) return std::nullopt;
    const auto time_start = line.rfind(' ', event_end - 1);
    if (time_start == std::string::npos || time_start < close) return std::nullopt;
    event.time = std::strtod(line.c_str() + time_start + 1, nullptr);

    const auto name_start = event_end + 2;
    const auto name_end = line.find(':', name_start);
    if (name_end == std::string::npos) return std::nullopt;
    event.name = line.substr(name_start, name_end - name_start);

    // Payload: key=value pairs separated by spaces; "==>" and "[action=X]" are tolerated.
    std::size_t position = name_end + 1;
    while (position < line.size()) {
        while (position < line.size() && line[position] == ' ') ++position;
        auto token_end = line.find(' ', position);
        if (token_end == std::string::npos) token_end = line.size();
        std::string token = line.substr(position, token_end - position);
        position = token_end;
        if (!token.empty() && token.front() == '[') token.erase(0, 1);
        if (!token.empty() && token.back() == ']') token.pop_back();
        const auto equals = token.find('=');
        if (equals == std::string::npos || equals == 0) continue;
        event.fields[token.substr(0, equals)] = token.substr(equals + 1);
    }
    return event;
}

}  // namespace rtdoctor
