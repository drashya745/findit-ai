#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "findit/Csv.h"
#include "findit/DatasetConfig.h"
#include "findit/Types.h"

namespace findit {

// A source row after mapping and normalization. Empty optional fields mean "unknown".
struct CanonicalRecord {
    std::size_t sourceLine = 0;
    std::string sourceId;  // From the source, or "line:<n>" when the dataset has no ID column.
    ReportType type = ReportType::Lost;
    std::string category;
    std::string description;
    std::string color;         // Lowercased.
    std::string brand;         // Empty = unknown.
    std::string location;
    std::string reporterName;  // Empty = unknown.
    std::string date;          // YYYY-MM-DD
    std::string timeOfDay;     // HH:MM[:SS] if the source had one, else empty.
};

struct RejectReason {
    std::string code;    // Stable category, e.g. "missing_required_field".
    std::string detail;  // Human-readable explanation.
};

struct AdaptOutcome {
    std::optional<CanonicalRecord> record;  // Set on success.
    RejectReason reject;                    // Set on failure.
    std::size_t whitespaceNormalizedFields = 0;
};

// Maps source CSV rows to CanonicalRecord using a DatasetConfig and the file's header.
// Normalization: trim and collapse whitespace in every field, map configured missing
// tokens to empty, lowercase colour. Description wording, case and typos are preserved.
class DatasetAdapter {
public:
    // Throws std::invalid_argument if a required column is absent or the header has duplicates.
    DatasetAdapter(DatasetConfig config, const std::vector<std::string>& header);

    AdaptOutcome adapt(const CsvRecord& record) const;

    bool isMissingValue(const std::string& raw) const { return config_.isMissing(raw); }
    bool hasField(CanonicalField field) const { return index_[fieldIndex(field)].has_value(); }
    const std::vector<std::string>& header() const noexcept { return header_; }
    // E.g. optional columns that are mapped but absent from the file.
    const std::vector<std::string>& warnings() const noexcept { return warnings_; }

private:
    DatasetConfig config_;
    std::vector<std::string> header_;
    std::array<std::optional<std::size_t>, kCanonicalFieldCount> index_;
    std::vector<std::string> warnings_;
};

}  // namespace findit
