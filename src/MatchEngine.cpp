#include "findit/MatchEngine.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

MatchEngine::MatchEngine(std::unique_ptr<SimilarityStrategy> strategy, double threshold)
    : strategy_(std::move(strategy)), threshold_(0.0) {
    setThreshold(threshold);
}

void MatchEngine::setStrategy(std::unique_ptr<SimilarityStrategy> strategy) noexcept {
    strategy_ = std::move(strategy);
}

std::string MatchEngine::strategyName() const {
    return strategy_ ? strategy_->name() : "(none)";
}

void MatchEngine::setThreshold(double threshold) {
    if (!(threshold >= 0.0 && threshold <= 1.0)) {
        throw std::invalid_argument("Threshold must be between 0 and 1");
    }
    threshold_ = threshold;
}

void MatchEngine::setDateWindowDays(int days) {
    if (days < 0) {
        throw std::invalid_argument("Date window must be 0 (disabled) or a positive number of days");
    }
    dateWindowDays_ = days;
}

bool MatchEngine::isEligible(const LostReport& lost, const FoundReport& found) const {
    if (found.status() != ReportStatus::Open || found.id() == lost.id()) {
        return false;
    }
    if (categoryBlocking_ &&
        validation::toLower(found.category()) != validation::toLower(lost.category())) {
        return false;
    }
    if (dateWindowDays_ > 0) {
        const long gap = found.date().daysSinceEpoch() - lost.date().daysSinceEpoch();
        if (gap > dateWindowDays_ || -gap > dateWindowDays_) {
            return false;
        }
    }
    return true;
}

MatchResult MatchEngine::scorePair(const LostReport& lost, const FoundReport& found) const {
    if (!strategy_) {
        throw std::logic_error("No similarity strategy configured");
    }
    std::vector<MatchResult> results = strategy_->score(lost, {&found});
    if (results.empty()) {
        throw std::invalid_argument("A report cannot be compared with itself");
    }
    results.front().setRank(1);
    return results.front();
}

std::vector<MatchResult> MatchEngine::findMatches(const LostReport& lost,
                                                  const std::vector<const FoundReport*>& candidates,
                                                  std::size_t topK) const {
    if (!strategy_) {
        throw std::logic_error("No similarity strategy configured");
    }
    if (topK == 0) {
        throw std::invalid_argument("topK must be at least 1");
    }
    if (lost.status() != ReportStatus::Open) {
        throw std::logic_error("Lost report " + lost.id() + " is not open");
    }

    std::vector<const FoundReport*> eligible;
    std::set<std::string> eligibleIds;
    for (const FoundReport* candidate : candidates) {
        if (candidate == nullptr || !isEligible(lost, *candidate)) {
            continue;
        }
        eligible.push_back(candidate);
        eligibleIds.insert(candidate->id());
    }
    if (eligible.empty()) {
        return {};
    }

    std::vector<MatchResult> scored = strategy_->score(lost, eligible);

    // Keep only results for this lost report and eligible candidates, at or above threshold.
    std::vector<MatchResult> results;
    for (auto& result : scored) {
        if (result.lostId() == lost.id() && eligibleIds.count(result.foundId()) > 0 &&
            result.score() >= threshold_) {
            results.push_back(std::move(result));
        }
    }

    std::sort(results.begin(), results.end(), [](const MatchResult& a, const MatchResult& b) {
        if (a.score() != b.score()) {
            return a.score() > b.score();
        }
        return a.foundId() < b.foundId();
    });
    if (results.size() > topK) {
        results.erase(results.begin() + static_cast<std::ptrdiff_t>(topK), results.end());
    }
    for (std::size_t i = 0; i < results.size(); ++i) {
        results[i].setRank(static_cast<int>(i + 1));
    }
    return results;
}

}  // namespace findit
