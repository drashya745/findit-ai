#include "findit/KeywordBaselineStrategy.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "findit/TextKeywords.h"
#include "findit/Validation.h"

namespace findit {

namespace {

constexpr double kRelatedCredit = 0.5;

std::string normalized(const std::string& value) {
    return validation::toLower(validation::collapseWhitespace(value));
}

bool isMissing(const std::string& normalizedValue) {
    return normalizedValue.empty() || normalizedValue == "unknown" || normalizedValue == "none";
}

std::string joined(const std::vector<std::string>& items) {
    std::string text;
    for (std::size_t i = 0; i < items.size(); ++i) {
        text += (i == 0 ? "" : ", ") + items[i];
    }
    return text;
}

// Shared logic for category, colour and location: exact, same group, or different.
KeywordBaselineStrategy::Component compareValues(const std::string& label,
                                                 const std::string& lostValue,
                                                 const std::string& foundValue,
                                                 const std::map<std::string, std::string>& groups,
                                                 const std::string& groupLabel) {
    const std::string a = normalized(lostValue);
    const std::string b = normalized(foundValue);
    if (isMissing(a) || isMissing(b)) {
        const std::string side = isMissing(a) ? (isMissing(b) ? "both reports" : "lost report")
                                              : "found report";
        return {0.0, label + " missing on " + side + " (not counted as a match)"};
    }
    if (a == b) {
        return {1.0, "Same " + validation::toLower(label) + ": " + foundValue};
    }
    const auto groupA = groups.find(a);
    const auto groupB = groups.find(b);
    if (groupA != groups.end() && groupB != groups.end() && groupA->second == groupB->second) {
        return {kRelatedCredit, "Related " + validation::toLower(label) + " (" + groupLabel + " " +
                                    groupA->second + "): " + lostValue + " ~ " + foundValue};
    }
    return {0.0, "Different " + validation::toLower(label) + ": " + lostValue + " vs " + foundValue};
}

}  // namespace

KeywordBaselineStrategy::KeywordBaselineStrategy(MatchingConfig config)
    : config_(std::move(config)) {
    config_.weights.validate();
}

KeywordBaselineStrategy::Component KeywordBaselineStrategy::textScore(
    const std::set<std::string>& lostKeywords, const std::set<std::string>& foundKeywords) {
    if (lostKeywords.empty() || foundKeywords.empty()) {
        return {0.0, "Description has no usable keywords on " +
                         std::string(lostKeywords.empty() ? "the lost report" : "the found report") +
                         " (not counted as a match)"};
    }
    std::vector<std::string> shared;
    std::set_intersection(lostKeywords.begin(), lostKeywords.end(), foundKeywords.begin(),
                          foundKeywords.end(), std::back_inserter(shared));
    const double dice = 2.0 * static_cast<double>(shared.size()) /
                        static_cast<double>(lostKeywords.size() + foundKeywords.size());
    if (shared.empty()) {
        return {0.0, "No shared description keywords"};
    }
    char value[16];
    std::snprintf(value, sizeof value, "%.2f", dice);
    return {dice, "Shared keywords (" + std::to_string(shared.size()) + " of " +
                      std::to_string(lostKeywords.size()) + " lost / " +
                      std::to_string(foundKeywords.size()) + " found, overlap " + value +
                      "): " + joined(shared)};
}

KeywordBaselineStrategy::Component KeywordBaselineStrategy::categoryScore(
    const ItemReport& lost, const ItemReport& found) const {
    return compareValues("Category", lost.category(), found.category(), config_.categoryGroup,
                         "group");
}

KeywordBaselineStrategy::Component KeywordBaselineStrategy::colourScore(
    const ItemReport& lost, const ItemReport& found) const {
    return compareValues("Colour", lost.color(), found.color(), config_.colourGroup, "group");
}

KeywordBaselineStrategy::Component KeywordBaselineStrategy::locationScore(
    const ItemReport& lost, const ItemReport& found) const {
    return compareValues("Location", lost.location(), found.location(), config_.locationZone,
                         "zone");
}

std::vector<MatchResult> KeywordBaselineStrategy::score(
    const LostReport& lost, const std::vector<const FoundReport*>& candidates) const {
    const BaselineWeights& w = config_.weights;
    const std::set<std::string> lostKeywords = text::keywords(lost.description());

    std::vector<MatchResult> results;
    results.reserve(candidates.size());
    for (const FoundReport* found : candidates) {
        if (found == nullptr || found->id() == lost.id()) {
            continue;
        }
        const Component textPart = textScore(lostKeywords, text::keywords(found->description()));
        const Component categoryPart = categoryScore(lost, *found);
        const Component colourPart = colourScore(lost, *found);
        const Component locationPart = locationScore(lost, *found);

        const double weighted = w.text * textPart.value + w.location * locationPart.value +
                                w.category * categoryPart.value + w.colour * colourPart.value;
        // Clamp guards against floating-point drift just outside [0, 1].
        const double total = std::min(1.0, std::max(0.0, weighted / w.total()));

        MatchResult result(lost.id(), found->id(), total);
        result.addComponent("text", textPart.value);
        result.addComponent("location", locationPart.value);
        result.addComponent("category", categoryPart.value);
        result.addComponent("colour", colourPart.value);
        result.addExplanation(textPart.reason);
        result.addExplanation(categoryPart.reason);
        result.addExplanation(colourPart.reason);
        result.addExplanation(locationPart.reason);
        results.push_back(std::move(result));
    }
    return results;
}

}  // namespace findit
