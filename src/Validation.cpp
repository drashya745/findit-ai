#include "findit/Validation.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace findit::validation {

namespace {

bool hasPrefixAndDigits(const std::string& id, char prefix, std::size_t digitCount) {
    if (id.size() != digitCount + 1 || id[0] != prefix) {
        return false;
    }
    const bool allDigits = std::all_of(id.begin() + 1, id.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
    const bool allZero = std::all_of(id.begin() + 1, id.end(), [](char c) { return c == '0'; });
    return allDigits && !allZero;
}

}  // namespace

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string collapseWhitespace(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool pendingSpace = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pendingSpace = !result.empty();
            continue;
        }
        if (pendingSpace) {
            result += ' ';
            pendingSpace = false;
        }
        result += c;
    }
    return result;
}

bool isValidReportId(const std::string& id) {
    return hasPrefixAndDigits(id, 'R', 6);
}

bool isValidUserId(const std::string& id) {
    return hasPrefixAndDigits(id, 'U', 3);
}

std::string requireText(const std::string& value, const std::string& fieldName,
                        std::size_t minLength, std::size_t maxLength) {
    std::string trimmed = trim(value);
    if (trimmed.empty()) {
        throw std::invalid_argument(fieldName + " is required");
    }
    if (trimmed.size() < minLength) {
        throw std::invalid_argument(fieldName + " must be at least " + std::to_string(minLength) +
                                    " characters");
    }
    if (trimmed.size() > maxLength) {
        throw std::invalid_argument(fieldName + " must be at most " + std::to_string(maxLength) +
                                    " characters");
    }
    return trimmed;
}

}  // namespace findit::validation
