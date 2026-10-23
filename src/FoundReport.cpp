#include "findit/FoundReport.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

namespace {

std::string validatedHeldAt(const std::string& heldAt) {
    std::string trimmed = validation::trim(heldAt);
    if (trimmed.size() > validation::kShortTextMaxLength) {
        throw std::invalid_argument("Held-at location must be at most " +
                                    std::to_string(validation::kShortTextMaxLength) +
                                    " characters");
    }
    return trimmed;
}

}  // namespace

FoundReport::FoundReport(std::string id, const ReportInput& input, std::string heldAt)
    : ItemReport(std::move(id), input), heldAt_(validatedHeldAt(heldAt)) {}

std::string FoundReport::details() const {
    std::ostringstream out;
    out << ItemReport::details() << std::left << std::setw(14) << "Held at" << ": "
        << (heldAt_.empty() ? "Not specified" : heldAt_) << '\n';
    return out.str();
}

}  // namespace findit
