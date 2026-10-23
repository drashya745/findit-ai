#pragma once

#include <optional>
#include <string>

#include "findit/Date.h"
#include "findit/Types.h"

namespace findit {

// Raw, unvalidated input for a new report. The report ID is assigned separately.
struct ReportInput {
    std::string reporterId;
    std::string category;
    std::string description;
    std::string color;
    std::string brand;  // Empty, "none" or "unknown" means the brand is unknown.
    std::string location;
    std::string date;   // YYYY-MM-DD
    std::string sourceRef;  // Optional: ID of the source record, e.g. the dataset's item_id.
};

// Abstract base for lost and found reports. Holds and validates the shared fields.
// Reports are owned through std::unique_ptr<ItemReport>, so copying is disabled to
// prevent slicing.
class ItemReport {
public:
    virtual ~ItemReport() = default;
    ItemReport(const ItemReport&) = delete;
    ItemReport& operator=(const ItemReport&) = delete;

    virtual ReportType type() const noexcept = 0;

    // One-line row for tables.
    virtual std::string summary() const;
    // Multi-line view; derived classes may append their own fields.
    virtual std::string details() const;

    const std::string& id() const noexcept { return id_; }
    const std::string& reporterId() const noexcept { return reporterId_; }
    const std::string& category() const noexcept { return category_; }
    const std::string& description() const noexcept { return description_; }
    const std::string& color() const noexcept { return color_; }
    const std::optional<std::string>& brand() const noexcept { return brand_; }
    const std::string& location() const noexcept { return location_; }
    const Date& date() const noexcept { return date_; }
    ReportStatus status() const noexcept { return status_; }
    const std::string& sourceRef() const noexcept { return sourceRef_; }  // Empty if none.

    // Throws std::logic_error if the lifecycle does not allow this transition.
    void setStatus(ReportStatus next);

protected:
    // Throws std::invalid_argument if any field fails validation.
    ItemReport(std::string id, const ReportInput& input);

    // Label for the date field, e.g. "Lost on"; used by details().
    virtual std::string dateLabel() const = 0;

private:
    std::string id_;
    std::string reporterId_;
    std::string category_;
    std::string description_;
    std::string color_;
    std::optional<std::string> brand_;
    std::string location_;
    Date date_;
    std::string sourceRef_;
    ReportStatus status_ = ReportStatus::Open;
};

}  // namespace findit
