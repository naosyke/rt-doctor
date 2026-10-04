#include "rtdoctor/system_view.hpp"

#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace rtdoctor {

SystemView::SystemView(std::string root) : root_(std::move(root)) {
    if (root_.empty() || root_.back() != '/') root_ += '/';
}

std::string SystemView::full(const std::string& path) const { return root_ + path; }

std::optional<std::string> SystemView::read(const std::string& path) const {
    std::ifstream file(full(path));
    if (!file) return std::nullopt;
    std::stringstream content;
    content << file.rdbuf();
    return content.str();
}

std::optional<std::string> SystemView::read_line(const std::string& path) const {
    const auto content = read(path);
    if (!content) return std::nullopt;
    return content->substr(0, content->find('\n'));
}

bool SystemView::exists(const std::string& path) const {
    struct stat info {};
    return stat(full(path).c_str(), &info) == 0;
}

std::vector<std::string> SystemView::list(const std::string& path) const {
    std::vector<std::string> names;
    if (DIR* directory = opendir(full(path).c_str())) {
        while (const dirent* entry = readdir(directory)) {
            const std::string name = entry->d_name;
            if (name != "." && name != "..") names.push_back(name);
        }
        closedir(directory);
    }
    std::sort(names.begin(), names.end());
    return names;
}

ProcessLimits current_process_limits() {
    ProcessLimits limits;
    limits.root = geteuid() == 0;
    rlimit value{};
    if (getrlimit(RLIMIT_MEMLOCK, &value) == 0 && value.rlim_cur != RLIM_INFINITY) limits.memlock = value.rlim_cur;
    if (getrlimit(RLIMIT_RTPRIO, &value) == 0 && value.rlim_cur != RLIM_INFINITY) limits.rtprio = value.rlim_cur;
    return limits;
}

std::vector<int> parse_cpu_list(const std::string& text) {
    std::vector<int> cpus;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, ',')) {
        part.erase(std::remove_if(part.begin(), part.end(), ::isspace), part.end());
        // Kernel parameters may carry flags, e.g. isolcpus=domain,managed_irq,2-3.
        if (part.empty() || !std::isdigit(static_cast<unsigned char>(part[0]))) continue;
        const auto dash = part.find('-');
        if (dash == std::string::npos) {
            cpus.push_back(std::stoi(part));
        } else {
            for (int cpu = std::stoi(part.substr(0, dash)); cpu <= std::stoi(part.substr(dash + 1)); ++cpu) {
                cpus.push_back(cpu);
            }
        }
    }
    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    return cpus;
}

std::string format_cpu_list(std::vector<int> cpus) {
    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    std::string text;
    for (std::size_t i = 0; i < cpus.size();) {
        std::size_t j = i;
        while (j + 1 < cpus.size() && cpus[j + 1] == cpus[j] + 1) ++j;
        if (!text.empty()) text += ",";
        text += std::to_string(cpus[i]);
        if (j > i) text += "-" + std::to_string(cpus[j]);
        i = j + 1;
    }
    return text;
}

}  // namespace rtdoctor
