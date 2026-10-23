#include "findit/KeyValueConfig.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

KeyValueConfig KeyValueConfig::parse(std::istream& in, const std::string& sourceName) {
    KeyValueConfig config;
    config.sourceName_ = sourceName;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        const std::string trimmed = validation::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::string where = sourceName + ":" + std::to_string(lineNumber);
        const auto equals = trimmed.find('=');
        if (equals == std::string::npos) {
            throw std::invalid_argument(where + ": expected 'key = value'");
        }
        const std::string key = validation::trim(trimmed.substr(0, equals));
        if (key.empty()) {
            throw std::invalid_argument(where + ": empty key");
        }
        if (!config.entries_.emplace(key, validation::trim(trimmed.substr(equals + 1))).second) {
            throw std::invalid_argument(where + ": duplicate key '" + key + "'");
        }
    }
    return config;
}

KeyValueConfig KeyValueConfig::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open config file: " + path);
    }
    return parse(in, path);
}

bool KeyValueConfig::has(const std::string& key) const {
    return entries_.count(key) > 0;
}

std::string KeyValueConfig::getOr(const std::string& key, const std::string& fallback) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? fallback : it->second;
}

double KeyValueConfig::getDouble(const std::string& key, double fallback) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return fallback;
    }
    const char* begin = it->second.c_str();
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(begin, &end);
    if (it->second.empty() || errno != 0 || *end != '\0') {
        throw std::invalid_argument(sourceName_ + ": '" + key + "' must be a number");
    }
    return value;
}

long KeyValueConfig::getInt(const std::string& key, long fallback) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return fallback;
    }
    const char* begin = it->second.c_str();
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(begin, &end, 10);
    if (it->second.empty() || errno != 0 || *end != '\0') {
        throw std::invalid_argument(sourceName_ + ": '" + key + "' must be an integer");
    }
    return value;
}

bool KeyValueConfig::getBool(const std::string& key, bool fallback) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return fallback;
    }
    const std::string value = validation::toLower(it->second);
    if (value == "true" || value == "yes" || value == "1") {
        return true;
    }
    if (value == "false" || value == "no" || value == "0") {
        return false;
    }
    throw std::invalid_argument(sourceName_ + ": '" + key + "' must be true or false");
}

std::vector<std::string> KeyValueConfig::getList(const std::string& key) const {
    std::vector<std::string> items;
    std::stringstream stream(getOr(key, ""));
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = validation::trim(item);
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

std::vector<std::string> KeyValueConfig::keysWithPrefix(const std::string& prefix) const {
    std::vector<std::string> keys;
    for (const auto& entry : entries_) {
        if (entry.first.compare(0, prefix.size(), prefix) == 0) {
            keys.push_back(entry.first);
        }
    }
    return keys;
}

}  // namespace findit
