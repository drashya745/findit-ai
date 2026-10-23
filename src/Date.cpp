#include "findit/Date.h"

#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <tuple>

namespace findit {

namespace {

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (month == 2 && isLeapYear(year)) ? 29 : kDays[month - 1];
}

}  // namespace

Date::Date(int year, int month, int day) : year_(year), month_(month), day_(day) {
    if (year < 2000 || year > 2100) {
        throw std::invalid_argument("Date year must be between 2000 and 2100");
    }
    if (month < 1 || month > 12) {
        throw std::invalid_argument("Date month must be between 1 and 12");
    }
    if (day < 1 || day > daysInMonth(year, month)) {
        throw std::invalid_argument("Date day is out of range for that month");
    }
}

Date Date::parse(const std::string& text) {
    const std::string formatError = "Date must use the format YYYY-MM-DD: '" + text + "'";
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        throw std::invalid_argument(formatError);
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(text[i]))) {
            throw std::invalid_argument(formatError);
        }
    }
    return Date(std::stoi(text.substr(0, 4)), std::stoi(text.substr(5, 2)),
                std::stoi(text.substr(8, 2)));
}

std::string Date::toString() const {
    char buffer[11];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", year_, month_, day_);
    return buffer;
}

long Date::daysSinceEpoch() const noexcept {
    // Howard Hinnant's days_from_civil; years here are always >= 2000.
    const int y = year_ - (month_ <= 2 ? 1 : 0);
    const int era = y / 400;
    const int yearOfEra = y - era * 400;
    const int shiftedMonth = month_ > 2 ? month_ - 3 : month_ + 9;
    const int dayOfYear = (153 * shiftedMonth + 2) / 5 + day_ - 1;
    const int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<long>(era) * 146097 + dayOfEra - 719468;
}

bool Date::operator==(const Date& other) const noexcept {
    return std::tie(year_, month_, day_) == std::tie(other.year_, other.month_, other.day_);
}

bool Date::operator<(const Date& other) const noexcept {
    return std::tie(year_, month_, day_) < std::tie(other.year_, other.month_, other.day_);
}

}  // namespace findit
