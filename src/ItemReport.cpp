#include "findit/ItemReport.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

namespace {

std::string validatedReportId(std::string id) {
    id = validation::trim(id);
    if (!validation::isValidReportId(id)) {
        throw std::invalid_argument("Invalid report ID '" + id +
                                    "' (expected R followed by 6 digits, e.g. R000001)");
    }
    return id;
}

std::string validatedReporterId(const std::string& id) {
    std::string trimmed = validation::trim(id);
    if (!validation::isValidUserId(trimmed)) {
        throw std::invalid_argument("Invalid reporter ID '" + trimmed +
                                    "' (expected U followed by 3 digits, e.g. U001)");
    }
    return trimmed;
}

std::optional<std::string> optionalBrand(const std::string& brand) {
    std::string trimmed = validation::trim(brand);
    const std::string lower = validation::toLower(trimmed);
    if (lower.empty() || lower == "none" || lower == "unknown") {
        return std::nullopt;
    }
    if (trimmed.size() > validation::kShortTextMaxLength) {
        throw std::invalid_argument("Brand must be at most " +
                                    std::to_string(validation::kShortTextMaxLength) + " characters");
    }
    return trimmed;
}

std::string validatedSourceRef(const std::string& sourceRef) {
    std::string trimmed = validation::trim(sourceRef);
    if (trimmed.size() > validation::kShortTextMaxLength) {
        throw std::invalid_argument("Source reference must be at most " +
                                    std::to_string(validation::kShortTextMaxLength) + " characters");
    }
    return trimmed;
}

}  // namespace

ItemReport::ItemReport(std::string id, const ReportInput& input)
    : id_(validatedReportId(std::move(id))),
      reporterId_(validatedReporterId(input.reporterId)),
      category_(validation::requireText(input.category, "Category", 2,
                                        validation::kShortTextMaxLength)),
      description_(validation::requireText(input.description, "Description",
                                           validation::kDescriptionMinLength,
                                           validation::kDescriptionMaxLength)),
      color_(validation::requireText(input.color, "Colour", 3, validation::kShortTextMaxLength)),
      brand_(optionalBrand(input.brand)),
      location_(validation::requireText(input.location, "Location", 2,
                                        validation::kShortTextMaxLength)),
      date_(Date::parse(validation::trim(input.date))),
      sourceRef_(validatedSourceRef(input.sourceRef)) {}

std::string ItemReport::summary() const {
    std::ostringstream out;
    out << std::left << std::setw(9) << id_ << std::setw(7) << toString(type()) << std::setw(9)
        << toString(status_) << std::setw(16) << category_ << std::setw(9) << color_
        << std::setw(18) << location_ << date_.toString();
    return out.str();
}

std::string ItemReport::details() const {
    std::ostringstream out;
    out << std::left;
    out << std::setw(14) << "Report ID" << ": " << id_ << '\n';
    out << std::setw(14) << "Type" << ": " << toString(type()) << '\n';
    out << std::setw(14) << "Status" << ": " << toString(status_) << '\n';
    out << std::setw(14) << "Reporter" << ": " << reporterId_ << '\n';
    out << std::setw(14) << "Category" << ": " << category_ << '\n';
    out << std::setw(14) << "Colour" << ": " << color_ << '\n';
    out << std::setw(14) << "Brand" << ": " << brand_.value_or("(unknown)") << '\n';
    out << std::setw(14) << "Location" << ": " << location_ << '\n';
    out << std::setw(14) << dateLabel() << ": " << date_.toString() << '\n';
    out << std::setw(14) << "Description" << ": " << description_ << '\n';
    if (!sourceRef_.empty()) {
        out << std::setw(14) << "Source ref" << ": " << sourceRef_ << '\n';
    }
    return out.str();
}

void ItemReport::setStatus(ReportStatus next) {
    if (!canTransition(status_, next)) {
        throw std::logic_error("Cannot change status of " + id_ + " from " + toString(status_) +
                               " to " + toString(next));
    }
    status_ = next;
}

}  // namespace findit
