#pragma once

#include <string>

namespace findit {

// Lowercase hex SHA-256 digest; used to fingerprint the source dataset in summaries.
// Matches `shasum -a 256` output.
std::string sha256Hex(const std::string& data);

}  // namespace findit
