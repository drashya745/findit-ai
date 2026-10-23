#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "findit/FoundReport.h"
#include "findit/ItemReport.h"
#include "findit/User.h"

namespace findit {

// In-memory store that owns all users and reports. Callers receive non-owning
// const pointers; only the repository can change a report's status.
// (Phase 1 has no file persistence.)
class ReportRepository {
public:
    ReportRepository() = default;
    ReportRepository(const ReportRepository&) = delete;
    ReportRepository& operator=(const ReportRepository&) = delete;

    // Throw std::invalid_argument on a null report or a duplicate ID.
    void addUser(User user);
    void addReport(std::unique_ptr<ItemReport> report);

    // Return nullptr when not found.
    const User* findUser(const std::string& id) const;
    const ItemReport* findById(const std::string& id) const;

    std::vector<const User*> users() const;
    std::vector<const ItemReport*> all() const;
    std::vector<const ItemReport*> byType(ReportType type) const;
    std::vector<const FoundReport*> openFoundReports() const;

    // Throws std::out_of_range if the ID is unknown, std::logic_error on an invalid transition.
    void updateStatus(const std::string& id, ReportStatus status);

    std::string nextReportId() const;
    std::string nextUserId() const;
    std::size_t reportCount() const noexcept { return reports_.size(); }

private:
    std::vector<std::unique_ptr<ItemReport>> reports_;  // Insertion order.
    std::map<std::string, std::size_t> reportIndex_;    // ID -> position in reports_.
    std::map<std::string, User> users_;
    int nextReportNumber_ = 1;
    int nextUserNumber_ = 1;
};

}  // namespace findit
