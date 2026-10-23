#include "findit/Csv.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace findit {

CsvReader::CsvReader(std::string text) : text_(std::move(text)) {
    if (text_.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        pos_ = 3;
    }
}

CsvReader CsvReader::fromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open CSV file: " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return CsvReader(buffer.str());
}

bool CsvReader::next(CsvRecord& record) {
    record = CsvRecord{};

    while (pos_ < text_.size()) {
        if (text_[pos_] == '\n') {
            pos_ += 1;
        } else if (text_.compare(pos_, 2, "\r\n") == 0) {
            pos_ += 2;
        } else {
            break;
        }
        ++line_;
        ++blankLines_;
    }
    if (pos_ >= text_.size()) {
        return false;
    }

    record.line = line_;
    std::string field;
    bool inQuotes = false;
    bool fieldWasQuoted = false;
    const auto fail = [&record, this](const std::string& message) {
        if (record.error.empty()) {
            record.error = message + " (line " + std::to_string(line_) + ")";
        }
    };

    while (pos_ < text_.size()) {
        const char c = text_[pos_++];
        if (inQuotes) {
            if (c == '"') {
                if (pos_ < text_.size() && text_[pos_] == '"') {
                    field += '"';
                    ++pos_;
                } else {
                    inQuotes = false;
                }
            } else {
                if (c == '\n') {
                    ++line_;
                }
                field += c;
            }
            continue;
        }
        if (c == ',') {
            record.fields.push_back(std::move(field));
            field.clear();
            fieldWasQuoted = false;
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (c == '\r' && pos_ < text_.size() && text_[pos_] == '\n') {
                ++pos_;
            }
            ++line_;
            record.fields.push_back(std::move(field));
            return true;
        }
        if (c == '"') {
            if (field.empty() && !fieldWasQuoted) {
                inQuotes = true;
                fieldWasQuoted = true;
                continue;
            }
            fail("Unexpected quote inside a field");
        } else if (fieldWasQuoted) {
            fail("Unexpected text after a closing quote");
        }
        field += c;
    }

    if (inQuotes) {
        fail("Unterminated quoted field");
    }
    record.fields.push_back(std::move(field));
    return true;
}

namespace csv {

std::string escapeField(const std::string& field) {
    const bool needsQuotes = field.find_first_of(",\"\r\n") != std::string::npos ||
                             (!field.empty() && (field.front() == ' ' || field.back() == ' '));
    if (!needsQuotes) {
        return field;
    }
    std::string quoted = "\"";
    for (const char c : field) {
        quoted += c;
        if (c == '"') {
            quoted += '"';
        }
    }
    quoted += '"';
    return quoted;
}

std::string formatRow(const std::vector<std::string>& fields) {
    std::string row;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            row += ',';
        }
        row += escapeField(fields[i]);
    }
    return row;
}

void writeRow(std::ostream& out, const std::vector<std::string>& fields) {
    out << formatRow(fields) << '\n';
}

}  // namespace csv

}  // namespace findit
