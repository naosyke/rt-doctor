// Rendering check results for people (text) and tools (JSON).
#pragma once

#include <ostream>
#include <vector>

#include "rtdoctor/checks.hpp"

namespace rtdoctor {

void print_text(std::ostream& out, const std::vector<CheckResult>& results, bool color, bool verbose);
void print_json(std::ostream& out, const std::vector<CheckResult>& results);

std::string json_escape(const std::string& text);

}  // namespace rtdoctor
