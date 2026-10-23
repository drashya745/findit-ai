#pragma once

#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "findit/ItemReport.h"
#include "findit/ReportRepository.h"
#include "findit/User.h"

// Persistence of users and reports in the canonical CSV format (data/processed, data/store).
namespace findit::persistence {

const std::vector<std::string>& userColumns();    // user_id,name,contact
const std::vector<std::string>& reportColumns();  // report_id,report_type,status,...

// Writes to "<path>.tmp" then renames, so a crash never leaves a half-written file.
void writeFileAtomically(const std::string& path, const std::function<void(std::ostream&)>& write);

void saveUsers(const std::string& path, const std::vector<const User*>& users);
void saveReports(const std::string& path, const std::vector<const ItemReport*>& reports);
// Append one record, writing the header first if the file does not exist yet.
void appendReport(const std::string& path, const ItemReport& report);
void appendUser(const std::string& path, const User& user);

// Confirmed matches: an append-only log (lost_id,found_id,confirmed_on) in data/store/.
// Statuses are never written back into reports.csv; on start the log is replayed through
// `apply` (ReportService::confirmMatch), so every replayed row is validated again.
const std::vector<std::string>& matchColumns();
void appendConfirmedMatch(const std::string& path, const std::string& lostId,
                          const std::string& foundId, const std::string& confirmedOn);
// Throws std::runtime_error with "path:line" if a row is malformed or `apply` rejects it.
std::size_t loadConfirmedMatches(
    const std::string& path,
    const std::function<void(const std::string& lostId, const std::string& foundId)>& apply);

// Load into the repository; return the number loaded. Throw std::runtime_error with
// "path:line" on a bad header, malformed row or unknown reporter.
std::size_t loadUsers(const std::string& path, ReportRepository& repository);
std::size_t loadReports(const std::string& path, ReportRepository& repository);

}  // namespace findit::persistence
