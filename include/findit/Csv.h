#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace findit {

// One parsed CSV record.
struct CsvRecord {
    std::vector<std::string> fields;
    std::size_t line = 0;  // 1-based physical line on which the record starts.
    std::string error;     // Non-empty if the record is malformed; fields are best-effort.
};

// RFC 4180 reader: quoted fields may contain commas, newlines and doubled quotes ("").
// Accepts LF or CRLF line endings and strips a leading UTF-8 BOM. Blank lines are skipped
// and counted. Malformed records (unterminated quote, stray quote in an unquoted field,
// text after a closing quote) are returned with `error` set so callers can log them.
class CsvReader {
public:
    explicit CsvReader(std::string text);
    static CsvReader fromFile(const std::string& path);  // Throws std::runtime_error.

    // Returns false at end of input.
    bool next(CsvRecord& record);

    std::size_t blankLinesSkipped() const noexcept { return blankLines_; }

private:
    std::string text_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t blankLines_ = 0;
};

namespace csv {

// Quotes a field only when needed (comma, quote, newline, or leading/trailing space).
std::string escapeField(const std::string& field);
std::string formatRow(const std::vector<std::string>& fields);
void writeRow(std::ostream& out, const std::vector<std::string>& fields);

}  // namespace csv

}  // namespace findit
