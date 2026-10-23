#include "findit/ReportService.h"

#include <memory>
#include <stdexcept>

#include "findit/FoundReport.h"
#include "findit/LostReport.h"
#include "findit/Validation.h"

namespace findit {

ReportService::ReportService(ReportRepository& repository, MatchEngine& engine) noexcept
    : repository_(repository), engine_(engine) {}

std::string ReportService::registerUser(const std::string& name, const std::string& contact) {
    User user = createUser(name, contact);
    std::string id = user.id();
    addUser(std::move(user));
    return id;
}

std::string ReportService::submitLost(const ReportInput& input) {
    return addReport(createReport(ReportType::Lost, input));
}

std::string ReportService::submitFound(const ReportInput& input, const std::string& heldAt) {
    return addReport(createReport(ReportType::Found, input, heldAt));
}

User ReportService::createUser(const std::string& name, const std::string& contact) const {
    return User(repository_.nextUserId(), name, contact);
}

void ReportService::addUser(User user) {
    repository_.addUser(std::move(user));
}

std::unique_ptr<ItemReport> ReportService::createReport(ReportType type, const ReportInput& input,
                                                        const std::string& heldAt) const {
    requireKnownReporter(input);
    if (type == ReportType::Lost) {
        return std::make_unique<LostReport>(repository_.nextReportId(), input);
    }
    return std::make_unique<FoundReport>(repository_.nextReportId(), input, heldAt);
}

std::string ReportService::addReport(std::unique_ptr<ItemReport> report) {
    if (!report) {
        throw std::invalid_argument("Cannot add a null report");
    }
    std::string id = report->id();
    repository_.addReport(std::move(report));
    return id;
}

const ItemReport& ReportService::getReport(const std::string& id) const {
    const ItemReport* report = repository_.findById(validation::trim(id));
    if (report == nullptr) {
        throw std::out_of_range("Report not found: " + validation::trim(id));
    }
    return *report;
}

std::vector<const ItemReport*> ReportService::listReports() const {
    return repository_.all();
}

std::vector<const ItemReport*> ReportService::listReports(ReportType type) const {
    return repository_.byType(type);
}

std::vector<const User*> ReportService::listUsers() const {
    return repository_.users();
}

std::vector<MatchResult> ReportService::suggestMatches(const std::string& lostId,
                                                       std::size_t topK) const {
    const auto* lost = dynamic_cast<const LostReport*>(&getReport(lostId));
    if (lost == nullptr) {
        throw std::invalid_argument(validation::trim(lostId) + " is not a lost report");
    }
    return engine_.findMatches(*lost, repository_.openFoundReports(), topK);
}

MatchResult ReportService::explainMatch(const std::string& lostId, const std::string& foundId,
                                        bool& wouldBeConsidered) const {
    const auto* lost = dynamic_cast<const LostReport*>(&getReport(lostId));
    const auto* found = dynamic_cast<const FoundReport*>(&getReport(foundId));
    if (lost == nullptr) {
        throw std::invalid_argument(validation::trim(lostId) + " is not a lost report");
    }
    if (found == nullptr) {
        throw std::invalid_argument(validation::trim(foundId) + " is not a found report");
    }
    wouldBeConsidered = lost->status() == ReportStatus::Open && engine_.isEligible(*lost, *found);
    return engine_.scorePair(*lost, *found);
}

void ReportService::validateConfirmation(const std::string& lostId,
                                         const std::string& foundId) const {
    const ItemReport& lost = getReport(lostId);
    const ItemReport& found = getReport(foundId);
    if (lost.type() != ReportType::Lost) {
        throw std::invalid_argument(lost.id() + " is not a lost report");
    }
    if (found.type() != ReportType::Found) {
        throw std::invalid_argument(found.id() + " is not a found report");
    }
    for (const ItemReport* report : {&lost, &found}) {
        if (report->status() != ReportStatus::Open) {
            throw std::logic_error(report->id() + " is already " + toString(report->status()) +
                                   "; only OPEN reports can be confirmed as a match");
        }
    }
}

void ReportService::confirmMatch(const std::string& lostId, const std::string& foundId) {
    // Check both before changing either, so a failure leaves no half-confirmed match.
    validateConfirmation(lostId, foundId);
    const std::string lost = getReport(lostId).id();
    const std::string found = getReport(foundId).id();
    repository_.updateStatus(lost, ReportStatus::Matched);
    repository_.updateStatus(found, ReportStatus::Matched);
}

void ReportService::requireKnownReporter(const ReportInput& input) const {
    const std::string reporterId = validation::trim(input.reporterId);
    if (repository_.findUser(reporterId) == nullptr) {
        throw std::invalid_argument("Unknown reporter ID: '" + reporterId + "'");
    }
}

}  // namespace findit
