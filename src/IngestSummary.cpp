#include "findit/IngestSummary.h"

#include <cstdio>
#include <iomanip>

#include "findit/Csv.h"

namespace findit {

namespace {

// Small streaming JSON writer with indentation and automatic commas.
class JsonWriter {
public:
    explicit JsonWriter(std::ostream& out) : out_(out) {}

    void beginObject() { open('{'); }
    void endObject() { close('}'); }
    void beginArray() { open('['); }
    void endArray() { close(']'); }

    void key(const std::string& name) {
        separate();
        out_ << quote(name) << ": ";
        afterKey_ = true;
    }
    void string(const std::string& value) { scalar(quote(value)); }
    void number(unsigned long long value) { scalar(std::to_string(value)); }
    void boolean(bool value) { scalar(value ? "true" : "false"); }

    void field(const std::string& name, const std::string& value) { key(name); string(value); }
    void field(const std::string& name, unsigned long long value) { key(name); number(value); }

private:
    static std::string quote(const std::string& text) {
        std::string result = "\"";
        for (const char c : text) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buffer[8];
                        std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                        result += buffer;
                    } else {
                        result += c;
                    }
            }
        }
        return result + "\"";
    }

    void separate() {
        if (afterKey_) {
            afterKey_ = false;
            return;
        }
        if (!hasItems_.empty()) {
            out_ << (hasItems_.back() ? ",\n" : "\n") << std::string(2 * hasItems_.size(), ' ');
            hasItems_.back() = true;
        }
    }
    void scalar(const std::string& text) {
        separate();
        out_ << text;
    }
    void open(char bracket) {
        separate();
        out_ << bracket;
        hasItems_.push_back(false);
    }
    void close(char bracket) {
        const bool hadItems = hasItems_.back();
        hasItems_.pop_back();
        if (hadItems) {
            out_ << '\n' << std::string(2 * hasItems_.size(), ' ');
        }
        out_ << bracket;
    }

    std::ostream& out_;
    std::vector<bool> hasItems_;
    bool afterKey_ = false;
};

void stringArray(JsonWriter& json, const std::string& name, const std::vector<std::string>& items) {
    json.key(name);
    json.beginArray();
    for (const auto& item : items) {
        json.string(item);
    }
    json.endArray();
}

}  // namespace

void writeSummaryJson(std::ostream& out, const IngestResult& result, const DatasetConfig& config,
                      const SummaryContext& context) {
    const IngestStats& s = result.stats;
    JsonWriter json(out);
    json.beginObject();
    json.field("summary_version", 1ULL);
    json.field("generated_by", "findit_ingest");

    json.key("input");
    json.beginObject();
    json.field("path", context.inputPath);
    json.field("bytes", context.inputBytes);
    json.field("sha256", context.inputSha256);
    json.endObject();

    json.key("config");
    json.beginObject();
    json.field("path", context.configPath);
    json.key("column_mapping");
    json.beginObject();
    for (const CanonicalField field : allCanonicalFields()) {
        json.field(canonicalName(field), config.sourceColumn(field));
    }
    json.endObject();
    stringArray(json, "lost_values", config.lostValues());
    stringArray(json, "found_values", config.foundValues());
    stringArray(json, "missing_tokens", config.missingTokens());
    json.endObject();

    json.key("rows");
    json.beginObject();
    json.field("records_read", s.recordsRead);
    json.field("blank_lines_skipped", s.blankLinesSkipped);
    json.field("accepted", s.accepted);
    json.field("accepted_lost", s.acceptedLost);
    json.field("accepted_found", s.acceptedFound);
    json.field("rejected", s.rejected);
    json.key("rejected_by_reason");
    json.beginObject();
    for (const auto& entry : s.rejectedByCode) {
        json.field(entry.first, entry.second);
    }
    json.endObject();
    json.endObject();

    json.key("missing_values_by_source_column");
    json.beginObject();
    for (const auto& entry : s.missingBySourceColumn) {
        json.field(entry.first, entry.second);
    }
    json.endObject();
    json.key("missing_value_spellings");
    json.beginObject();
    for (const auto& entry : s.missingTokensSeen) {
        json.field(entry.first, entry.second);
    }
    json.endObject();

    json.key("duplicates");
    json.beginObject();
    json.field("exact_duplicate_rows_rejected", s.exactDuplicateRows);
    json.field("duplicate_source_ids_rejected", s.duplicateSourceIds);
    json.field("same_content_different_source_id_kept", s.contentDuplicates);
    json.endObject();

    json.key("normalization");
    json.beginObject();
    json.field("fields_with_whitespace_normalized", s.whitespaceNormalizedFields);
    json.field("users_created", s.usersCreated);
    json.key("placeholder_reporter_used");
    json.boolean(s.placeholderReporterUsed);
    json.endObject();

    json.key("accepted_profile");
    json.beginObject();
    json.field("min_date", s.minDate);
    json.field("max_date", s.maxDate);
    json.field("distinct_times_of_day", s.distinctTimesOfDay);
    json.key("distinct_values");
    json.beginObject();
    for (const auto& entry : s.distinctValues) {
        json.field(entry.first, entry.second);
    }
    json.endObject();
    json.endObject();

    stringArray(json, "warnings", s.warnings);

    json.key("outputs");
    json.beginObject();
    for (const auto& entry : context.outputFiles) {
        json.field(entry.first, entry.second);
    }
    json.field("users_written", context.usersWritten);
    json.field("reports_written", context.reportsWritten);
    json.endObject();

    json.endObject();
    out << '\n';
}

void writeRejectedRowsCsv(std::ostream& out, const std::vector<RejectedRow>& rows) {
    csv::writeRow(out, {"source_line", "source_id", "reason_code", "reason_detail", "raw_record"});
    for (const RejectedRow& row : rows) {
        csv::writeRow(out, {std::to_string(row.line), row.sourceId, row.code, row.detail,
                            csv::formatRow(row.rawFields)});
    }
}

void printSummary(std::ostream& out, const IngestResult& result, const SummaryContext& context) {
    const IngestStats& s = result.stats;
    out << "Input      : " << context.inputPath << " (" << context.inputBytes << " bytes)\n"
        << "SHA-256    : " << context.inputSha256 << '\n'
        << "Records    : " << s.recordsRead << " read, " << s.accepted << " accepted ("
        << s.acceptedLost << " lost / " << s.acceptedFound << " found), " << s.rejected
        << " rejected\n";
    for (const auto& entry : s.rejectedByCode) {
        out << "  rejected " << std::left << std::setw(24) << entry.first << entry.second << '\n';
    }
    out << "Missing    :";
    bool any = false;
    for (const auto& entry : s.missingBySourceColumn) {
        if (entry.second > 0) {
            out << ' ' << entry.first << '=' << entry.second;
            any = true;
        }
    }
    out << (any ? "\n" : " none\n");
    out << "Duplicates : " << s.exactDuplicateRows << " exact rows, " << s.duplicateSourceIds
        << " source IDs (both rejected); " << s.contentDuplicates
        << " same-content rows kept\n"
        << "Users      : " << s.usersCreated << " created\n"
        << "Dates      : " << s.minDate << " to " << s.maxDate << ", " << s.distinctTimesOfDay
        << " distinct time(s) of day\n";
    for (const auto& warning : s.warnings) {
        out << "Warning    : " << warning << '\n';
    }
    for (const auto& entry : context.outputFiles) {
        out << "Wrote      : " << entry.second << '\n';
    }
}

}  // namespace findit
