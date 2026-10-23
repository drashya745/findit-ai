#pragma once

#include <set>
#include <string>
#include <vector>

// Keyword extraction for the baseline matcher. Deliberately simple and transparent:
// no stemming, no synonyms, no spelling correction beyond the digit-zero fix below.
namespace findit::text {

// Lowercases and splits on any non-alphanumeric character. In tokens that contain
// letters and whose only digit is '0', '0' becomes 'o' ("l0st" -> "lost"); tokens like
// "a102" are left alone.
std::vector<std::string> tokenize(const std::string& text);

// Generic English words and lost-and-found report boilerplate ("lost", "found", "my",
// "pls", "someone", ...) that carry no information about which item it is.
bool isStopword(const std::string& token);

// Distinct tokens of length >= 2 that are not stopwords.
std::set<std::string> keywords(const std::string& text);

}  // namespace findit::text
