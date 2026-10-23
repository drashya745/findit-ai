#include "findit/Types.h"

#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

std::string toString(ReportType type) {
    switch (type) {
        case ReportType::Lost: return "LOST";
        case ReportType::Found: return "FOUND";
    }
    return "UNKNOWN";
}

std::string toString(ReportStatus status) {
    switch (status) {
        case ReportStatus::Open: return "OPEN";
        case ReportStatus::Matched: return "MATCHED";
        case ReportStatus::Closed: return "CLOSED";
    }
    return "UNKNOWN";
}

ReportType parseReportType(const std::string& text) {
    const std::string value = validation::toLower(validation::trim(text));
    if (value == "lost") {
        return ReportType::Lost;
    }
    if (value == "found") {
        return ReportType::Found;
    }
    throw std::invalid_argument("Report type must be 'lost' or 'found': '" + text + "'");
}

bool canTransition(ReportStatus from, ReportStatus to) noexcept {
    switch (from) {
        case ReportStatus::Open: return to == ReportStatus::Matched || to == ReportStatus::Closed;
        case ReportStatus::Matched: return to == ReportStatus::Closed;
        case ReportStatus::Closed: return false;
    }
    return false;
}

}  // namespace findit
