#include "findit/MatchingConfig.h"

#include <cmath>
#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

namespace {

void readGroups(const KeyValueConfig& config, const std::string& prefix,
                std::map<std::string, std::string>& target) {
    for (const auto& key : config.keysWithPrefix(prefix)) {
        const std::string group = key.substr(prefix.size());
        if (group.empty()) {
            throw std::invalid_argument(config.sourceName() + ": '" + key + "' needs a group name");
        }
        for (const auto& member : config.getList(key)) {
            const std::string value = validation::toLower(validation::collapseWhitespace(member));
            if (!target.emplace(value, group).second) {
                throw std::invalid_argument(config.sourceName() + ": '" + member +
                                            "' appears in more than one " + prefix + " group");
            }
        }
    }
}

}  // namespace

void BaselineWeights::validate() const {
    for (const double w : {text, location, category, colour}) {
        if (!std::isfinite(w) || w < 0.0) {
            throw std::invalid_argument("Weights must be finite and non-negative");
        }
    }
    if (total() <= 0.0) {
        throw std::invalid_argument("At least one weight must be positive");
    }
}

MatchingConfig MatchingConfig::fromKeyValues(const KeyValueConfig& config) {
    static const char* kScalarKeys[] = {"weight.text",         "weight.location",
                                        "weight.category",     "weight.colour",
                                        "category_blocking",   "threshold",
                                        "top_k",               "date_window_days",
                                        "text_engine",         "python.executable",
                                        "python.module_path",  "python.log_file",
                                        "python.timeout_seconds", "python.fallback"};
    for (const auto& entry : config.entries()) {
        const std::string& key = entry.first;
        bool known = key.rfind("zone.", 0) == 0 || key.rfind("colour_group.", 0) == 0 ||
                     key.rfind("category_group.", 0) == 0;
        for (const char* scalar : kScalarKeys) {
            known = known || key == scalar;
        }
        if (!known) {
            throw std::invalid_argument(config.sourceName() + ": unknown key '" + key + "'");
        }
    }

    MatchingConfig result;
    result.weights.text = config.getDouble("weight.text", result.weights.text);
    result.weights.location = config.getDouble("weight.location", result.weights.location);
    result.weights.category = config.getDouble("weight.category", result.weights.category);
    result.weights.colour = config.getDouble("weight.colour", result.weights.colour);
    result.weights.validate();

    result.categoryBlocking = config.getBool("category_blocking", result.categoryBlocking);
    result.threshold = config.getDouble("threshold", result.threshold);
    if (!(result.threshold >= 0.0 && result.threshold <= 1.0)) {
        throw std::invalid_argument(config.sourceName() + ": threshold must be between 0 and 1");
    }
    const long topK = config.getInt("top_k", static_cast<long>(result.topK));
    if (topK < 1 || topK > 100) {
        throw std::invalid_argument(config.sourceName() + ": top_k must be between 1 and 100");
    }
    result.topK = static_cast<std::size_t>(topK);
    const long window = config.getInt("date_window_days", result.dateWindowDays);
    if (window < 0 || window > 3650) {
        throw std::invalid_argument(config.sourceName() +
                                    ": date_window_days must be between 0 and 3650");
    }
    result.dateWindowDays = static_cast<int>(window);

    const std::string engine = validation::toLower(config.getOr("text_engine", "keyword"));
    if (engine == "keyword") {
        result.textEngine = TextEngine::Keyword;
    } else if (engine == "python") {
        result.textEngine = TextEngine::Python;
    } else {
        throw std::invalid_argument(config.sourceName() + ": text_engine must be keyword or python");
    }
    PythonSettings& python = result.python;
    python.executable = config.getOr("python.executable", python.executable);
    python.modulePath = config.getOr("python.module_path", python.modulePath);
    python.logFile = config.getOr("python.log_file", python.logFile);
    python.fallbackToKeyword = config.getBool("python.fallback", python.fallbackToKeyword);
    const long timeout = config.getInt("python.timeout_seconds", python.timeoutSeconds);
    if (timeout < 1 || timeout > 3600) {
        throw std::invalid_argument(config.sourceName() +
                                    ": python.timeout_seconds must be between 1 and 3600");
    }
    python.timeoutSeconds = static_cast<int>(timeout);
    if (python.executable.empty() || python.modulePath.empty()) {
        throw std::invalid_argument(config.sourceName() +
                                    ": python.executable and python.module_path must not be empty");
    }

    readGroups(config, "zone.", result.locationZone);
    readGroups(config, "colour_group.", result.colourGroup);
    readGroups(config, "category_group.", result.categoryGroup);
    return result;
}

MatchingConfig MatchingConfig::loadFile(const std::string& path) {
    return fromKeyValues(KeyValueConfig::loadFile(path));
}

}  // namespace findit
