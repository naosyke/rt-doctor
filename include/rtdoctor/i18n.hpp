// Report languages. Checks and advice are written in English; translate()
// maps them to another language through a catalog, falling back to the
// English text for anything not in it (so a new check never shows up empty).
#pragma once

#include <optional>
#include <string>

namespace rtdoctor {

enum class Lang { En, Ja };

std::optional<Lang> parse_lang(const std::string& name);  // "en", "ja"

// Check titles, values, explanations and fixes, trace advice and names of
// what delayed a task ("task hog (pid 7)", "idle", ...).
std::string translate(Lang lang, const std::string& english);

}  // namespace rtdoctor
