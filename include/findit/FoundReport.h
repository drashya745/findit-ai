#pragma once

#include "findit/ItemReport.h"

namespace findit {

// A report of an item someone has found. Records where the item is being held.
class FoundReport final : public ItemReport {
public:
    // heldAt is optional; empty means not specified.
    FoundReport(std::string id, const ReportInput& input, std::string heldAt = "");

    ReportType type() const noexcept override { return ReportType::Found; }
    std::string details() const override;

    const Date& foundDate() const noexcept { return date(); }
    const std::string& heldAt() const noexcept { return heldAt_; }  // Empty if not specified.

protected:
    std::string dateLabel() const override { return "Found on"; }

private:
    std::string heldAt_;
};

}  // namespace findit
