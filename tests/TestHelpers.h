#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "findit/FoundReport.h"
#include "findit/ItemReport.h"
#include "findit/LostReport.h"
#include "findit/MatchResult.h"
#include "findit/SimilarityStrategy.h"

namespace testhelpers {

inline findit::ReportInput validInput(const std::string& category = "Wallet") {
    findit::ReportInput input;
    input.reporterId = "U001";
    input.category = category;
    input.description = "Black leather wallet with a library card.";
    input.color = "black";
    input.brand = "Fastrack";
    input.location = "Library";
    input.date = "2026-09-02";
    return input;
}

inline findit::ReportInput makeInput(const std::string& category, const std::string& description,
                                     const std::string& color, const std::string& location,
                                     const std::string& date = "2026-09-02") {
    findit::ReportInput input;
    input.reporterId = "U001";
    input.category = category;
    input.description = description;
    input.color = color;
    input.location = location;
    input.date = date;
    return input;
}

// Test double: returns a scripted score per found report ID (default 0.5).
class FakeStrategy final : public findit::SimilarityStrategy {
public:
    FakeStrategy(std::map<std::string, double> scores, std::size_t* seenCount)
        : scores_(std::move(scores)), seenCount_(seenCount) {}

    std::vector<findit::MatchResult> score(
        const findit::LostReport& lost,
        const std::vector<const findit::FoundReport*>& candidates) const override {
        if (seenCount_ != nullptr) {
            *seenCount_ = candidates.size();
        }
        std::vector<findit::MatchResult> results;
        for (const findit::FoundReport* found : candidates) {
            const auto it = scores_.find(found->id());
            findit::MatchResult result(lost.id(), found->id(),
                                       it == scores_.end() ? 0.5 : it->second);
            result.addComponent("fake", result.score());
            result.addExplanation("scripted by FakeStrategy");
            results.push_back(std::move(result));
        }
        return results;
    }

    std::string name() const override { return "fake"; }

private:
    std::map<std::string, double> scores_;
    std::size_t* seenCount_;
};

inline std::string fixturePath(const std::string& relative) {
    return std::string(FINDIT_SOURCE_DIR) + "/" + relative;
}

}  // namespace testhelpers
