#pragma once

#include "findit/ReportService.h"

namespace findit {

// Seeds a handful of fictional users and reports for development and demos.
// Not derived from the real dataset.
void seedSampleData(ReportService& service);

}  // namespace findit
