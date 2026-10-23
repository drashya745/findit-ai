#pragma once

#include <string>

namespace findit {

enum class ReportType { Lost, Found };

// Lifecycle: Open -> Matched -> Closed, or Open -> Closed. No transitions backwards.
enum class ReportStatus { Open, Matched, Closed };

std::string toString(ReportType type);
std::string toString(ReportStatus status);

// Accepts "lost" / "found" in any case; throws std::invalid_argument otherwise.
ReportType parseReportType(const std::string& text);

bool canTransition(ReportStatus from, ReportStatus to) noexcept;

}  // namespace findit
