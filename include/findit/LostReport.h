#pragma once

#include "findit/ItemReport.h"

namespace findit {

// A report of an item someone has lost. The report date is the date it was lost.
class LostReport final : public ItemReport {
public:
    LostReport(std::string id, const ReportInput& input);

    ReportType type() const noexcept override { return ReportType::Lost; }

    const Date& lostDate() const noexcept { return date(); }

protected:
    std::string dateLabel() const override { return "Lost on"; }
};

}  // namespace findit
