#pragma once

#include <cstddef>
#include <map>
#include <string>

#include "findit/KeyValueConfig.h"

namespace findit {

// Weights of the baseline score. The score is sum(weight * component) / sum(weights), so it
// always lies in [0, 1]; with the defaults (summing to 1) it is exactly
// 0.60*text + 0.20*location + 0.10*category + 0.10*colour.
struct BaselineWeights {
    double text = 0.60;
    double location = 0.20;
    double category = 0.10;
    double colour = 0.10;

    double total() const noexcept { return text + location + category + colour; }
    // Throws std::invalid_argument if any weight is negative/non-finite or all are zero.
    void validate() const;
};

enum class TextEngine { Keyword, Python };

// How to run the Python TF-IDF service (keys python.* in config/matching.conf).
struct PythonSettings {
    std::string executable = ".venv/bin/python";
    std::string modulePath = "ml";
    // Relative paths are placed in <data-dir>/store/ (AppContext::pythonLogPath).
    std::string logFile = "python_matcher.log";
    int timeoutSeconds = 120;
    bool fallbackToKeyword = true;  // Use the keyword baseline if Python is unavailable.
};

// Matching settings, loaded from config/matching.conf. Groups give partial credit (0.5) to
// related-but-different values; they are explicit lists, not learned.
struct MatchingConfig {
    BaselineWeights weights;
    std::map<std::string, std::string> locationZone;   // Lowercased location -> zone name.
    std::map<std::string, std::string> colourGroup;    // Lowercased colour -> group name.
    std::map<std::string, std::string> categoryGroup;  // Lowercased category -> group name.
    bool categoryBlocking = true;
    double threshold = 0.0;
    std::size_t topK = 5;
    int dateWindowDays = 0;  // 0 disables the date filter.
    TextEngine textEngine = TextEngine::Keyword;
    PythonSettings python;

    // Throws std::invalid_argument on unknown keys, bad numbers, or a value in two groups.
    static MatchingConfig fromKeyValues(const KeyValueConfig& config);
    static MatchingConfig loadFile(const std::string& path);
};

}  // namespace findit
