#include "findit/DatasetConfig.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

namespace {

constexpr std::array<CanonicalField, kCanonicalFieldCount> kAllFields = {
    CanonicalField::SourceId, CanonicalField::ReportType, CanonicalField::Category,
    CanonicalField::Description, CanonicalField::Color, CanonicalField::Brand,
    CanonicalField::Location, CanonicalField::Reporter, CanonicalField::Timestamp,
};

const std::string kLostKey = "report_type.lost";
const std::string kFoundKey = "report_type.found";
const std::string kMissingKey = "missing_tokens";
const std::string kSourcePathKey = "source.path";

std::vector<std::string> lowercaseList(const KeyValueConfig& config, const std::string& key) {
    std::vector<std::string> values = config.getList(key);
    for (auto& value : values) {
        value = validation::toLower(value);
    }
    return values;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

const std::array<CanonicalField, kCanonicalFieldCount>& allCanonicalFields() {
    return kAllFields;
}

const char* canonicalName(CanonicalField field) {
    switch (field) {
        case CanonicalField::SourceId: return "source_id";
        case CanonicalField::ReportType: return "report_type";
        case CanonicalField::Category: return "category";
        case CanonicalField::Description: return "description";
        case CanonicalField::Color: return "color";
        case CanonicalField::Brand: return "brand";
        case CanonicalField::Location: return "location";
        case CanonicalField::Reporter: return "reporter";
        case CanonicalField::Timestamp: return "timestamp";
    }
    return "unknown";
}

bool isRequiredField(CanonicalField field) {
    return field != CanonicalField::SourceId && field != CanonicalField::Brand &&
           field != CanonicalField::Reporter;
}

std::size_t fieldIndex(CanonicalField field) {
    return static_cast<std::size_t>(field);
}

DatasetConfig DatasetConfig::fromKeyValues(const KeyValueConfig& config) {
    DatasetConfig result;
    std::set<std::string> knownKeys = {kLostKey, kFoundKey, kMissingKey, kSourcePathKey};
    std::set<std::string> mappedColumns;

    for (const CanonicalField field : kAllFields) {
        const std::string key = std::string("column.") + canonicalName(field);
        knownKeys.insert(key);
        const std::string column = config.getOr(key, "");
        if (column.empty() && isRequiredField(field)) {
            throw std::invalid_argument(config.sourceName() + ": required mapping '" + key +
                                        "' is missing");
        }
        if (!column.empty() && !mappedColumns.insert(column).second) {
            throw std::invalid_argument(config.sourceName() + ": source column '" + column +
                                        "' is mapped to more than one field");
        }
        result.columns_[fieldIndex(field)] = column;
    }
    for (const auto& entry : config.entries()) {
        if (knownKeys.count(entry.first) == 0) {
            throw std::invalid_argument(config.sourceName() + ": unknown key '" + entry.first + "'");
        }
    }

    result.sourcePath_ = config.getOr(kSourcePathKey, "");
    result.lostValues_ = lowercaseList(config, kLostKey);
    result.foundValues_ = lowercaseList(config, kFoundKey);
    result.missingTokens_ = lowercaseList(config, kMissingKey);
    if (result.lostValues_.empty() || result.foundValues_.empty()) {
        throw std::invalid_argument(config.sourceName() + ": '" + kLostKey + "' and '" + kFoundKey +
                                    "' must each list at least one value");
    }
    for (const auto& value : result.lostValues_) {
        if (contains(result.foundValues_, value)) {
            throw std::invalid_argument(config.sourceName() + ": value '" + value +
                                        "' is listed as both lost and found");
        }
    }
    return result;
}

DatasetConfig DatasetConfig::loadFile(const std::string& path) {
    return fromKeyValues(KeyValueConfig::loadFile(path));
}

const std::string& DatasetConfig::sourceColumn(CanonicalField field) const {
    return columns_[fieldIndex(field)];
}

bool DatasetConfig::isMissing(const std::string& value) const {
    const std::string normalized = validation::toLower(validation::collapseWhitespace(value));
    return normalized.empty() || contains(missingTokens_, normalized);
}

std::optional<ReportType> DatasetConfig::classify(const std::string& value) const {
    const std::string normalized = validation::toLower(validation::collapseWhitespace(value));
    if (contains(lostValues_, normalized)) {
        return ReportType::Lost;
    }
    if (contains(foundValues_, normalized)) {
        return ReportType::Found;
    }
    return std::nullopt;
}

}  // namespace findit
