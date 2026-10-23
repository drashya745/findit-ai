#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "findit/DatasetConfig.h"
#include "findit/DatasetLoader.h"

namespace findit {

// Provenance recorded alongside the counts. It has no wall-clock timestamps, so
// re-running on the same input and config produces a byte-identical summary.
struct SummaryContext {
    std::string inputPath;
    std::size_t inputBytes = 0;
    std::string inputSha256;
    std::string configPath;
    std::vector<std::pair<std::string, std::string>> outputFiles;  // label -> path
    std::size_t usersWritten = 0;
    std::size_t reportsWritten = 0;
};

void writeSummaryJson(std::ostream& out, const IngestResult& result, const DatasetConfig& config,
                      const SummaryContext& context);
void writeRejectedRowsCsv(std::ostream& out, const std::vector<RejectedRow>& rows);
// Short human-readable version for the console.
void printSummary(std::ostream& out, const IngestResult& result, const SummaryContext& context);

}  // namespace findit
