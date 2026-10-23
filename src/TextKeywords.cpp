#include "findit/TextKeywords.h"

#include <cctype>
#include <unordered_set>

namespace findit::text {

namespace {

std::string fixDigitZero(std::string token) {
    bool hasLetter = false;
    bool hasOtherDigit = false;
    for (const char c : token) {
        hasLetter = hasLetter || std::isalpha(static_cast<unsigned char>(c)) != 0;
        hasOtherDigit = hasOtherDigit || (std::isdigit(static_cast<unsigned char>(c)) && c != '0');
    }
    if (hasLetter && !hasOtherDigit) {
        for (char& c : token) {
            if (c == '0') {
                c = 'o';
            }
        }
    }
    return token;
}

}  // namespace

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    const auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(fixDigitZero(current));
            current.clear();
        }
    };
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}

bool isStopword(const std::string& token) {
    static const std::unordered_set<std::string> kStopwords = {
        // General English.
        "a", "an", "the", "and", "or", "but", "of", "on", "in", "at", "to", "for", "from", "with",
        "by", "near", "is", "are", "was", "were", "be", "been", "it", "its", "this", "that",
        "i", "me", "my", "mine", "we", "our", "you", "your", "has", "have", "had", "can", "cant",
        "not", "no", "any", "some", "sum", "so", "very", "just", "what", "who", "whose", "where",
        "there", "here", "up", "out", "if", "do", "does", "did", "am", "s", "t",
        // Lost-and-found boilerplate and chat filler.
        "lost", "found", "lose", "missing", "find", "finding", "picked", "pick", "left",
        "dropped", "discovered", "gone", "someone", "somebody", "somewhere", "belongs", "belong",
        "item", "thing", "last", "seen", "campus", "please", "pls", "plz", "help", "idk", "lol",
        "bro", "btw", "sure", "anyone", "owner"};
    return kStopwords.count(token) > 0;
}

std::set<std::string> keywords(const std::string& text) {
    std::set<std::string> result;
    for (const auto& token : tokenize(text)) {
        if (token.size() >= 2 && !isStopword(token)) {
            result.insert(token);
        }
    }
    return result;
}

}  // namespace findit::text
