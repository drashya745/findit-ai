#include "findit/ReportRepository.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace findit {

namespace {

constexpr int kMaxReportNumber = 999999;
constexpr int kMaxUserNumber = 999;

// IDs are already validated by User / ItemReport, so the numeric part is safe to parse.
int numberPart(const std::string& id) {
    return std::stoi(id.substr(1));
}

std::string formatId(char prefix, int number, int width) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%c%0*d", prefix, width, number);
    return buffer;
}

}  // namespace

void ReportRepository::addUser(User user) {
    const std::string id = user.id();
    if (users_.count(id) > 0) {
        throw std::invalid_argument("Duplicate user ID: " + id);
    }
    users_.emplace(id, std::move(user));
    nextUserNumber_ = std::max(nextUserNumber_, numberPart(id) + 1);
}

void ReportRepository::addReport(std::unique_ptr<ItemReport> report) {
    if (!report) {
        throw std::invalid_argument("Cannot add a null report");
    }
    const std::string id = report->id();
    if (reportIndex_.count(id) > 0) {
        throw std::invalid_argument("Duplicate report ID: " + id);
    }
    reportIndex_[id] = reports_.size();
    reports_.push_back(std::move(report));
    nextReportNumber_ = std::max(nextReportNumber_, numberPart(id) + 1);
}

const User* ReportRepository::findUser(const std::string& id) const {
    const auto it = users_.find(id);
    return it == users_.end() ? nullptr : &it->second;
}

const ItemReport* ReportRepository::findById(const std::string& id) const {
    const auto it = reportIndex_.find(id);
    return it == reportIndex_.end() ? nullptr : reports_[it->second].get();
}

std::vector<const User*> ReportRepository::users() const {
    std::vector<const User*> result;
    result.reserve(users_.size());
    for (const auto& entry : users_) {
        result.push_back(&entry.second);
    }
    return result;
}

std::vector<const ItemReport*> ReportRepository::all() const {
    std::vector<const ItemReport*> result;
    result.reserve(reports_.size());
    for (const auto& report : reports_) {
        result.push_back(report.get());
    }
    return result;
}

std::vector<const ItemReport*> ReportRepository::byType(ReportType type) const {
    std::vector<const ItemReport*> result;
    for (const auto& report : reports_) {
        if (report->type() == type) {
            result.push_back(report.get());
        }
    }
    return result;
}

std::vector<const FoundReport*> ReportRepository::openFoundReports() const {
    std::vector<const FoundReport*> result;
    for (const auto& report : reports_) {
        const auto* found = dynamic_cast<const FoundReport*>(report.get());
        if (found != nullptr && found->status() == ReportStatus::Open) {
            result.push_back(found);
        }
    }
    return result;
}

void ReportRepository::updateStatus(const std::string& id, ReportStatus status) {
    const auto it = reportIndex_.find(id);
    if (it == reportIndex_.end()) {
        throw std::out_of_range("Report not found: " + id);
    }
    reports_[it->second]->setStatus(status);
}

std::string ReportRepository::nextReportId() const {
    if (nextReportNumber_ > kMaxReportNumber) {
        throw std::overflow_error("Report ID space exhausted");
    }
    return formatId('R', nextReportNumber_, 6);
}

std::string ReportRepository::nextUserId() const {
    if (nextUserNumber_ > kMaxUserNumber) {
        throw std::overflow_error("User ID space exhausted");
    }
    return formatId('U', nextUserNumber_, 3);
}

}  // namespace findit
