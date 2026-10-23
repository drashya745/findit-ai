#pragma once

#include <istream>
#include <map>
#include <string>
#include <vector>

namespace findit {

// Minimal "key = value" configuration file. Lines starting with '#' are comments.
// Keys are unique; values are trimmed. Lists are comma-separated.
class KeyValueConfig {
public:
    // Throw std::invalid_argument (with file and line) on malformed lines or duplicate keys.
    static KeyValueConfig parse(std::istream& in, const std::string& sourceName);
    static KeyValueConfig loadFile(const std::string& path);

    bool has(const std::string& key) const;
    std::string getOr(const std::string& key, const std::string& fallback) const;
    double getDouble(const std::string& key, double fallback) const;
    long getInt(const std::string& key, long fallback) const;
    bool getBool(const std::string& key, bool fallback) const;
    // Empty list if the key is absent; empty items are dropped.
    std::vector<std::string> getList(const std::string& key) const;
    std::vector<std::string> keysWithPrefix(const std::string& prefix) const;

    const std::map<std::string, std::string>& entries() const noexcept { return entries_; }
    const std::string& sourceName() const noexcept { return sourceName_; }

private:
    std::map<std::string, std::string> entries_;
    std::string sourceName_;
};

}  // namespace findit
