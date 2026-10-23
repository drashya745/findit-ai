#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "findit/Csv.h"
#include "findit/DatasetConfig.h"
#include "findit/ReportService.h"

namespace findit {

struct RejectedRow {
    std::size_t line = 0;
    std::string sourceId;  // Empty if it could not be determined.
    std::string code;
    std::string detail;
    std::vector<std::string> rawFields;
};

// Counts computed while loading. Everything here is measured, never estimated.
struct IngestStats {
    std::size_t recordsRead = 0;  // Data records after the header (blank lines excluded).
    std::size_t blankLinesSkipped = 0;
    std::size_t accepted = 0;
    std::size_t acceptedLost = 0;
    std::size_t acceptedFound = 0;
    std::size_t rejected = 0;
    std::map<std::string, std::size_t> rejectedByCode;

    // Per source column, over records with the right field count, in header order.
    std::vector<std::pair<std::string, std::size_t>> missingBySourceColumn;
    // Raw spellings counted as missing, e.g. "None" -> 1652; "(empty)" for blank fields.
    std::map<std::string, std::size_t> missingTokensSeen;

    std::size_t exactDuplicateRows = 0;  // Rejected.
    std::size_t duplicateSourceIds = 0;  // Rejected.
    std::size_t contentDuplicates = 0;   // Kept: same content except the source ID.
    std::size_t whitespaceNormalizedFields = 0;

    std::size_t usersCreated = 0;
    bool placeholderReporterUsed = false;
    std::string minDate;
    std::string maxDate;
    std::size_t distinctTimesOfDay = 0;
    std::map<std::string, std::size_t> distinctValues;  // Canonical field -> distinct count.
    std::vector<std::string> warnings;
};

struct IngestResult {
    IngestStats stats;
    std::vector<RejectedRow> rejectedRows;
};

// Reads a source CSV, adapts and validates every row, and submits accepted rows through
// ReportService as LostReport / FoundReport. Rejected rows are recorded, not dropped silently.
class DatasetLoader {
public:
    static constexpr const char* kPlaceholderReporter = "Unknown reporter (imported)";

    DatasetLoader(DatasetConfig config, ReportService& service);

    // Throws std::runtime_error if the input has no header or a required column is missing.
    IngestResult load(CsvReader& reader);

private:
    std::string reporterIdFor(const std::string& name, IngestStats& stats);

    DatasetConfig config_;
    ReportService& service_;
    std::map<std::string, std::string> userIdByName_;  // Lowercased name -> user ID.
};

}  // namespace findit
