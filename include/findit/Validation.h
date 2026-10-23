#pragma once

#include <cstddef>
#include <string>

// Stateless input-validation helpers shared by the domain classes.
namespace findit::validation {

constexpr std::size_t kDescriptionMinLength = 10;
constexpr std::size_t kDescriptionMaxLength = 500;
constexpr std::size_t kShortTextMaxLength = 60;

std::string trim(const std::string& text);
std::string toLower(std::string text);
// Trims, then collapses each internal run of whitespace into a single space.
std::string collapseWhitespace(const std::string& text);

// Report IDs look like R000001; user IDs look like U001. All-zero IDs are rejected.
bool isValidReportId(const std::string& id);
bool isValidUserId(const std::string& id);

// Returns the trimmed value, or throws std::invalid_argument naming the field.
std::string requireText(const std::string& value, const std::string& fieldName,
                        std::size_t minLength, std::size_t maxLength);

}  // namespace findit::validation
