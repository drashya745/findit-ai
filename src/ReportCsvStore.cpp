#include "findit/ReportCsvStore.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

#include "findit/Csv.h"
#include "findit/FoundReport.h"
#include "findit/LostReport.h"

namespace findit::persistence {

namespace {

std::vector<std::string> reportRow(const ItemReport& report) {
    const auto* found = dynamic_cast<const FoundReport*>(&report);
    return {report.id(),
            toString(report.type()),
            toString(report.status()),
            report.category(),
            report.description(),
            report.color(),
            report.brand().value_or(""),
            report.location(),
            report.reporterId(),
            report.date().toString(),
            found != nullptr ? found->heldAt() : "",
            report.sourceRef()};
}

ReportStatus parseStatus(const std::string& text) {
    if (text == "OPEN") return ReportStatus::Open;
    if (text == "MATCHED") return ReportStatus::Matched;
    if (text == "CLOSED") return ReportStatus::Closed;
    throw std::invalid_argument("Unknown status '" + text + "'");
}

// Opens a canonical CSV, checks its header, and calls onRow for each data record.
template <typename OnRow>
std::size_t readCanonicalCsv(const std::string& path, const std::vector<std::string>& columns,
                             OnRow onRow) {
    CsvReader reader = CsvReader::fromFile(path);
    CsvRecord record;
    if (!reader.next(record) || record.fields != columns) {
        throw std::runtime_error(path + ": unexpected header (expected " + csv::formatRow(columns) +
                                 ")");
    }
    std::size_t count = 0;
    while (reader.next(record)) {
        const std::string where = path + ":" + std::to_string(record.line) + ": ";
        if (!record.error.empty()) {
            throw std::runtime_error(where + record.error);
        }
        if (record.fields.size() != columns.size()) {
            throw std::runtime_error(where + "expected " + std::to_string(columns.size()) +
                                     " fields, found " + std::to_string(record.fields.size()));
        }
        try {
            onRow(record.fields);
        } catch (const std::exception& e) {
            throw std::runtime_error(where + e.what());
        }
        ++count;
    }
    return count;
}

}  // namespace

const std::vector<std::string>& userColumns() {
    static const std::vector<std::string> columns = {"user_id", "name", "contact"};
    return columns;
}

const std::vector<std::string>& reportColumns() {
    static const std::vector<std::string> columns = {
        "report_id", "report_type", "status",      "category", "description", "color",
        "brand",     "location",    "reporter_id", "date",     "held_at",     "source_ref"};
    return columns;
}

void writeFileAtomically(const std::string& path,
                         const std::function<void(std::ostream&)>& write) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Cannot write file: " + temporary);
        }
        write(out);
        out.flush();
        if (!out) {
            throw std::runtime_error("Error while writing file: " + temporary);
        }
    }
    std::filesystem::rename(temporary, path);
}

void saveUsers(const std::string& path, const std::vector<const User*>& users) {
    writeFileAtomically(path, [&users](std::ostream& out) {
        csv::writeRow(out, userColumns());
        for (const User* user : users) {
            csv::writeRow(out, {user->id(), user->name(), user->contact()});
        }
    });
}

void saveReports(const std::string& path, const std::vector<const ItemReport*>& reports) {
    writeFileAtomically(path, [&reports](std::ostream& out) {
        csv::writeRow(out, reportColumns());
        for (const ItemReport* report : reports) {
            csv::writeRow(out, reportRow(*report));
        }
    });
}

namespace {

void appendRow(const std::string& path, const std::vector<std::string>& columns,
               const std::vector<std::string>& row) {
    const bool isNew = !std::filesystem::exists(path);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        throw std::runtime_error("Cannot append to file: " + path);
    }
    if (isNew) {
        csv::writeRow(out, columns);
    }
    csv::writeRow(out, row);
    if (!out) {
        throw std::runtime_error("Error while appending to file: " + path);
    }
}

}  // namespace

void appendReport(const std::string& path, const ItemReport& report) {
    appendRow(path, reportColumns(), reportRow(report));
}

void appendUser(const std::string& path, const User& user) {
    appendRow(path, userColumns(), {user.id(), user.name(), user.contact()});
}

const std::vector<std::string>& matchColumns() {
    static const std::vector<std::string> columns = {"lost_id", "found_id", "confirmed_on"};
    return columns;
}

void appendConfirmedMatch(const std::string& path, const std::string& lostId,
                          const std::string& foundId, const std::string& confirmedOn) {
    (void)Date::parse(confirmedOn);  // Validate before writing.
    appendRow(path, matchColumns(), {lostId, foundId, confirmedOn});
}

std::size_t loadConfirmedMatches(
    const std::string& path,
    const std::function<void(const std::string& lostId, const std::string& foundId)>& apply) {
    return readCanonicalCsv(path, matchColumns(), [&apply](const std::vector<std::string>& f) {
        (void)Date::parse(f[2]);
        apply(f[0], f[1]);
    });
}

std::size_t loadUsers(const std::string& path, ReportRepository& repository) {
    return readCanonicalCsv(path, userColumns(), [&repository](const std::vector<std::string>& f) {
        repository.addUser(User(f[0], f[1], f[2]));
    });
}

std::size_t loadReports(const std::string& path, ReportRepository& repository) {
    return readCanonicalCsv(path, reportColumns(), [&repository](const std::vector<std::string>& f) {
        ReportInput input;
        input.category = f[3];
        input.description = f[4];
        input.color = f[5];
        input.brand = f[6];
        input.location = f[7];
        input.reporterId = f[8];
        input.date = f[9];
        input.sourceRef = f[11];
        if (repository.findUser(input.reporterId) == nullptr) {
            throw std::invalid_argument("Unknown reporter ID '" + input.reporterId + "'");
        }
        const ReportStatus status = parseStatus(f[2]);
        std::unique_ptr<ItemReport> report;
        if (f[1] == "LOST") {
            report = std::make_unique<LostReport>(f[0], input);
        } else if (f[1] == "FOUND") {
            report = std::make_unique<FoundReport>(f[0], input, f[10]);
        } else {
            throw std::invalid_argument("Unknown report type '" + f[1] + "'");
        }
        if (status != ReportStatus::Open) {
            report->setStatus(status);
        }
        repository.addReport(std::move(report));
    });
}

}  // namespace findit::persistence
