#include "findit/LostReport.h"

namespace findit {

LostReport::LostReport(std::string id, const ReportInput& input)
    : ItemReport(std::move(id), input) {}

}  // namespace findit
