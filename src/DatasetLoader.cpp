#include "findit/DatasetLoader.h"

#include <array>
#include <set>
#include <stdexcept>

#include "findit/DatasetAdapter.h"
#include "findit/Validation.h"

namespace findit {

namespace {

std::string joinKey(const std::vector<std::string>& parts) {
    std::string key;
    for (const auto& part : parts) {
        key += part;
        key += '\x1f';
    }
    return key;
}

std::string contentKey(const CanonicalRecord& r) {
    return joinKey({toString(r.type), r.category, r.description, r.color, r.brand, r.location,
                    r.reporterName, r.date, r.timeOfDay});
}

}  // namespace

DatasetLoader::DatasetLoader(DatasetConfig config, ReportService& service)
    : config_(std::move(config)), service_(service) {}

std::string DatasetLoader::reporterIdFor(const std::string& name, IngestStats& stats) {
    const std::string displayName = name.empty() ? kPlaceholderReporter : name;
    const std::string key = validation::toLower(displayName);
    const auto it = userIdByName_.find(key);
    if (it != userIdByName_.end()) {
        return it->second;
    }
    const std::string id = service_.registerUser(displayName);
    userIdByName_.emplace(key, id);
    ++stats.usersCreated;
    if (name.empty()) {
        stats.placeholderReporterUsed = true;
    }
    return id;
}

IngestResult DatasetLoader::load(CsvReader& reader) {
    IngestResult result;
    IngestStats& stats = result.stats;

    CsvRecord headerRecord;
    if (!reader.next(headerRecord)) {
        throw std::runtime_error("Dataset is empty: no header row");
    }
    if (!headerRecord.error.empty()) {
        throw std::runtime_error("Malformed header row: " + headerRecord.error);
    }
    const DatasetAdapter adapter = [&]() {
        try {
            return DatasetAdapter(config_, headerRecord.fields);
        } catch (const std::invalid_argument& e) {
            throw std::runtime_error(e.what());
        }
    }();
    stats.warnings = adapter.warnings();
    const std::vector<std::string>& header = adapter.header();

    std::vector<std::size_t> missingCounts(header.size(), 0);
    std::map<std::string, std::size_t> firstLineByRawRow;
    std::map<std::string, std::size_t> firstLineBySourceId;
    std::set<std::string> contentKeys;
    std::array<std::set<std::string>, kCanonicalFieldCount> distinct;
    std::set<std::string> timesOfDay;

    const auto reject = [&result](const CsvRecord& record, const std::string& sourceId,
                                  const std::string& code, const std::string& detail) {
        result.rejectedRows.push_back({record.line, sourceId, code, detail, record.fields});
        ++result.stats.rejectedByCode[code];
    };

    CsvRecord record;
    while (reader.next(record)) {
        ++stats.recordsRead;

        if (record.error.empty() && record.fields.size() == header.size()) {
            for (std::size_t i = 0; i < header.size(); ++i) {
                if (adapter.isMissingValue(record.fields[i])) {
                    ++missingCounts[i];
                    const std::string token = validation::trim(record.fields[i]);
                    ++stats.missingTokensSeen[token.empty() ? "(empty)" : token];
                }
            }
        }

        if (record.error.empty()) {
            const auto inserted = firstLineByRawRow.emplace(joinKey(record.fields), record.line);
            if (!inserted.second) {
                ++stats.exactDuplicateRows;
                reject(record, "", "duplicate_row",
                       "Exact duplicate of line " + std::to_string(inserted.first->second));
                continue;
            }
        }

        AdaptOutcome outcome = adapter.adapt(record);
        stats.whitespaceNormalizedFields += outcome.whitespaceNormalizedFields;
        if (!outcome.record) {
            reject(record, "", outcome.reject.code, outcome.reject.detail);
            continue;
        }
        const CanonicalRecord& canonical = *outcome.record;

        const auto previous = firstLineBySourceId.find(canonical.sourceId);
        if (previous != firstLineBySourceId.end()) {
            ++stats.duplicateSourceIds;
            reject(record, canonical.sourceId, "duplicate_source_id",
                   "Source ID '" + canonical.sourceId + "' already used on line " +
                       std::to_string(previous->second));
            continue;
        }

        try {
            ReportInput input;
            input.reporterId = reporterIdFor(canonical.reporterName, stats);
            input.category = canonical.category;
            input.description = canonical.description;
            input.color = canonical.color;
            input.brand = canonical.brand;
            input.location = canonical.location;
            input.date = canonical.date;
            input.sourceRef = canonical.sourceId;
            if (canonical.type == ReportType::Lost) {
                service_.submitLost(input);
                ++stats.acceptedLost;
            } else {
                service_.submitFound(input);
                ++stats.acceptedFound;
            }
        } catch (const std::exception& e) {
            reject(record, canonical.sourceId, "validation_failed", e.what());
            continue;
        }

        ++stats.accepted;
        firstLineBySourceId.emplace(canonical.sourceId, canonical.sourceLine);
        if (!contentKeys.insert(contentKey(canonical)).second) {
            ++stats.contentDuplicates;
        }
        distinct[fieldIndex(CanonicalField::Category)].insert(canonical.category);
        distinct[fieldIndex(CanonicalField::Color)].insert(canonical.color);
        if (!canonical.brand.empty()) {
            distinct[fieldIndex(CanonicalField::Brand)].insert(canonical.brand);
        }
        distinct[fieldIndex(CanonicalField::Location)].insert(canonical.location);
        if (!canonical.reporterName.empty()) {
            distinct[fieldIndex(CanonicalField::Reporter)].insert(canonical.reporterName);
        }
        distinct[fieldIndex(CanonicalField::Description)].insert(canonical.description);
        if (stats.minDate.empty() || canonical.date < stats.minDate) {
            stats.minDate = canonical.date;
        }
        if (stats.maxDate.empty() || canonical.date > stats.maxDate) {
            stats.maxDate = canonical.date;
        }
        if (!canonical.timeOfDay.empty()) {
            timesOfDay.insert(canonical.timeOfDay);
        }
    }

    stats.blankLinesSkipped = reader.blankLinesSkipped();
    stats.rejected = result.rejectedRows.size();
    stats.distinctTimesOfDay = timesOfDay.size();
    for (std::size_t i = 0; i < header.size(); ++i) {
        stats.missingBySourceColumn.emplace_back(header[i], missingCounts[i]);
    }
    for (const CanonicalField field :
         {CanonicalField::Category, CanonicalField::Description, CanonicalField::Color,
          CanonicalField::Brand, CanonicalField::Location, CanonicalField::Reporter}) {
        stats.distinctValues[canonicalName(field)] = distinct[fieldIndex(field)].size();
    }
    return result;
}

}  // namespace findit
