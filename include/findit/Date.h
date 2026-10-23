#pragma once

#include <string>

namespace findit {

// Calendar date (no time of day). Always holds a valid date.
class Date {
public:
    Date(int year, int month, int day);

    // Parses strict ISO format YYYY-MM-DD; throws std::invalid_argument otherwise.
    static Date parse(const std::string& text);

    int year() const noexcept { return year_; }
    int month() const noexcept { return month_; }
    int day() const noexcept { return day_; }

    std::string toString() const;

    // Days since 1970-01-01; differences give the number of days between dates.
    long daysSinceEpoch() const noexcept;

    bool operator==(const Date& other) const noexcept;
    bool operator<(const Date& other) const noexcept;

private:
    int year_;
    int month_;
    int day_;
};

}  // namespace findit
