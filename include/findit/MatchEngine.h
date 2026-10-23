#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "findit/FoundReport.h"
#include "findit/LostReport.h"
#include "findit/MatchResult.h"
#include "findit/SimilarityStrategy.h"

namespace findit {

// Orchestrates matching: filters candidates, delegates scoring to the current
// strategy, then applies the threshold, sorts and assigns ranks.
class MatchEngine {
public:
    explicit MatchEngine(std::unique_ptr<SimilarityStrategy> strategy = nullptr,
                         double threshold = 0.0);

    void setStrategy(std::unique_ptr<SimilarityStrategy> strategy) noexcept;
    bool hasStrategy() const noexcept { return strategy_ != nullptr; }
    std::string strategyName() const;

    // Throws std::invalid_argument if the threshold is outside [0, 1].
    void setThreshold(double threshold);
    double threshold() const noexcept { return threshold_; }

    // When enabled (the default), only candidates of the same category are scored.
    void setCategoryBlocking(bool enabled) noexcept { categoryBlocking_ = enabled; }
    bool categoryBlocking() const noexcept { return categoryBlocking_; }

    // When > 0, only candidates dated within this many days (either side) of the lost
    // report are scored. 0 disables the filter. Throws std::invalid_argument if negative.
    void setDateWindowDays(int days);
    int dateWindowDays() const noexcept { return dateWindowDays_; }

    // Candidates that are null, not open, the lost report itself, or outside the
    // category/date filters are excluded before scoring.
    // Throws std::logic_error if no strategy is set or the lost report is not open,
    // and std::invalid_argument if topK is 0.
    std::vector<MatchResult> findMatches(const LostReport& lost,
                                         const std::vector<const FoundReport*>& candidates,
                                         std::size_t topK) const;

    // Scores one specific pair with the current strategy, ignoring the candidate filters,
    // so a user can ask "why (not) this one?". Throws std::logic_error without a strategy.
    MatchResult scorePair(const LostReport& lost, const FoundReport& found) const;
    // True if findMatches would consider this candidate (status, self, category, date).
    bool isEligible(const LostReport& lost, const FoundReport& found) const;

private:
    std::unique_ptr<SimilarityStrategy> strategy_;
    double threshold_;
    bool categoryBlocking_ = true;
    int dateWindowDays_ = 0;
};

}  // namespace findit
