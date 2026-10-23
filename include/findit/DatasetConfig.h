#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "findit/KeyValueConfig.h"
#include "findit/Types.h"

namespace findit {

// Internal fields every dataset is mapped onto.
enum class CanonicalField {
    SourceId,
    ReportType,
    Category,
    Description,
    Color,
    Brand,
    Location,
    Reporter,
    Timestamp,
};
constexpr std::size_t kCanonicalFieldCount = 9;

const std::array<CanonicalField, kCanonicalFieldCount>& allCanonicalFields();
const char* canonicalName(CanonicalField field);  // e.g. "report_type"
// Required fields must be mapped and present on every row; optional ones may be absent.
bool isRequiredField(CanonicalField field);
std::size_t fieldIndex(CanonicalField field);

// Source-column mapping and value vocabulary for one dataset, loaded from a config file
// so source column names are not hardcoded in the application.
class DatasetConfig {
public:
    // Throws std::invalid_argument for unknown keys, unmapped required fields,
    // empty or overlapping lost/found vocabularies, or two fields mapped to one column.
    static DatasetConfig fromKeyValues(const KeyValueConfig& config);
    static DatasetConfig loadFile(const std::string& path);

    const std::string& sourceColumn(CanonicalField field) const;  // Empty if unmapped.
    const std::string& sourcePath() const noexcept { return sourcePath_; }  // May be empty.
    const std::vector<std::string>& lostValues() const noexcept { return lostValues_; }
    const std::vector<std::string>& foundValues() const noexcept { return foundValues_; }
    const std::vector<std::string>& missingTokens() const noexcept { return missingTokens_; }

    // True for empty/whitespace values and configured tokens such as "None" (case-insensitive).
    bool isMissing(const std::string& value) const;
    // nullopt when the value is in neither vocabulary.
    std::optional<ReportType> classify(const std::string& value) const;

private:
    std::array<std::string, kCanonicalFieldCount> columns_;
    std::string sourcePath_;
    std::vector<std::string> lostValues_;
    std::vector<std::string> foundValues_;
    std::vector<std::string> missingTokens_;
};

}  // namespace findit
