#include "findit/DatasetAdapter.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

#include "findit/Date.h"
#include "findit/Validation.h"

namespace findit {

namespace {

bool isTwoDigits(const std::string& text, std::size_t pos, int maxValue) {
    if (pos + 2 > text.size() || !std::isdigit(static_cast<unsigned char>(text[pos])) ||
        !std::isdigit(static_cast<unsigned char>(text[pos + 1]))) {
        return false;
    }
    return std::stoi(text.substr(pos, 2)) <= maxValue;
}

// Accepts "YYYY-MM-DD", optionally followed by ' ' or 'T' and "HH:MM" or "HH:MM:SS".
bool splitTimestamp(const std::string& text, std::string& date, std::string& time,
                    std::string& error) {
    if (text.size() < 10) {
        error = "Timestamp '" + text + "' is too short (expected YYYY-MM-DD[ HH:MM[:SS]])";
        return false;
    }
    date = text.substr(0, 10);
    try {
        (void)Date::parse(date);
    } catch (const std::invalid_argument& e) {
        error = std::string("Invalid timestamp '") + text + "': " + e.what();
        return false;
    }
    time.clear();
    if (text.size() == 10) {
        return true;
    }
    const std::string rest = text.substr(11);
    const bool separatorOk = text[10] == ' ' || text[10] == 'T';
    const bool hhmm = rest.size() >= 5 && isTwoDigits(rest, 0, 23) && rest[2] == ':' &&
                      isTwoDigits(rest, 3, 59);
    const bool shapeOk = rest.size() == 5 || (rest.size() == 8 && rest[5] == ':' &&
                                              isTwoDigits(rest, 6, 59));
    if (!separatorOk || !hhmm || !shapeOk) {
        error = "Invalid time in timestamp '" + text + "' (expected HH:MM or HH:MM:SS)";
        return false;
    }
    time = rest;
    return true;
}

AdaptOutcome rejected(std::string code, std::string detail, std::size_t whitespaceFixes) {
    AdaptOutcome outcome;
    outcome.reject = {std::move(code), std::move(detail)};
    outcome.whitespaceNormalizedFields = whitespaceFixes;
    return outcome;
}

}  // namespace

DatasetAdapter::DatasetAdapter(DatasetConfig config, const std::vector<std::string>& header)
    : config_(std::move(config)) {
    std::set<std::string> seen;
    for (const auto& column : header) {
        const std::string name = validation::trim(column);
        if (!seen.insert(name).second) {
            throw std::invalid_argument("Duplicate column in header: '" + name + "'");
        }
        header_.push_back(name);
    }

    for (const CanonicalField field : allCanonicalFields()) {
        const std::string& column = config_.sourceColumn(field);
        const std::string name = canonicalName(field);
        if (column.empty()) {
            warnings_.push_back("Optional field '" + name + "' is not mapped; treated as unknown");
            continue;
        }
        const auto it = std::find(header_.begin(), header_.end(), column);
        if (it == header_.end()) {
            if (isRequiredField(field)) {
                throw std::invalid_argument("Required column '" + column + "' (field '" + name +
                                            "') not found in the CSV header");
            }
            warnings_.push_back("Optional column '" + column + "' (field '" + name +
                                "') not found in the CSV header; treated as unknown");
            continue;
        }
        index_[fieldIndex(field)] = static_cast<std::size_t>(it - header_.begin());
    }
}

AdaptOutcome DatasetAdapter::adapt(const CsvRecord& record) const {
    if (!record.error.empty()) {
        return rejected("malformed_csv", record.error, 0);
    }
    if (record.fields.size() != header_.size()) {
        return rejected("wrong_field_count",
                        "Expected " + std::to_string(header_.size()) + " fields, found " +
                            std::to_string(record.fields.size()),
                        0);
    }

    std::size_t whitespaceFixes = 0;
    std::array<std::string, kCanonicalFieldCount> values;
    for (const CanonicalField field : allCanonicalFields()) {
        const auto& index = index_[fieldIndex(field)];
        if (!index) {
            continue;
        }
        const std::string& raw = record.fields[*index];
        std::string value = validation::collapseWhitespace(raw);
        if (value != raw) {
            ++whitespaceFixes;
        }
        if (config_.isMissing(value)) {
            value.clear();
        }
        if (value.empty() && isRequiredField(field)) {
            return rejected("missing_required_field",
                            std::string("Missing value for '") + canonicalName(field) +
                                "' (column '" + config_.sourceColumn(field) + "')",
                            whitespaceFixes);
        }
        values[fieldIndex(field)] = std::move(value);
    }

    const auto value = [&values](CanonicalField field) -> std::string& {
        return values[fieldIndex(field)];
    };

    CanonicalRecord canonical;
    canonical.sourceLine = record.line;
    canonical.sourceId = value(CanonicalField::SourceId).empty()
                             ? "line:" + std::to_string(record.line)
                             : value(CanonicalField::SourceId);

    const auto type = config_.classify(value(CanonicalField::ReportType));
    if (!type) {
        return rejected("invalid_report_type",
                        "Unrecognised report type '" + value(CanonicalField::ReportType) + "'",
                        whitespaceFixes);
    }
    canonical.type = *type;

    std::string error;
    if (!splitTimestamp(value(CanonicalField::Timestamp), canonical.date, canonical.timeOfDay,
                        error)) {
        return rejected("invalid_timestamp", error, whitespaceFixes);
    }

    canonical.category = value(CanonicalField::Category);
    canonical.description = value(CanonicalField::Description);
    canonical.color = validation::toLower(value(CanonicalField::Color));
    canonical.brand = value(CanonicalField::Brand);
    canonical.location = value(CanonicalField::Location);
    canonical.reporterName = value(CanonicalField::Reporter);

    AdaptOutcome outcome;
    outcome.record = std::move(canonical);
    outcome.whitespaceNormalizedFields = whitespaceFixes;
    return outcome;
}

}  // namespace findit
