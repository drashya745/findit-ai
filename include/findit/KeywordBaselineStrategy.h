#pragma once

#include <set>
#include <string>
#include <vector>

#include "findit/ItemReport.h"
#include "findit/MatchingConfig.h"
#include "findit/SimilarityStrategy.h"

namespace findit {

// Transparent rule-based baseline. Four independent components, each in [0, 1]:
//   text     - Dice overlap of description keywords: 2|A∩B| / (|A| + |B|)
//   category - 1 same, 0.5 same configured group, else 0
//   colour   - 1 same, 0.5 same configured group, else 0
//   location - 1 same, 0.5 same configured zone, else 0
// Missing values (an empty field, "unknown", or a description with no keywords) score 0
// and are reported as missing; they are never treated as agreement.
// The total is a relative ranking score, not a probability of a match.
class KeywordBaselineStrategy final : public SimilarityStrategy {
public:
    struct Component {
        double value = 0.0;
        std::string reason;
    };

    explicit KeywordBaselineStrategy(MatchingConfig config = MatchingConfig{});

    std::vector<MatchResult> score(const LostReport& lost,
                                   const std::vector<const FoundReport*>& candidates) const override;
    std::string name() const override { return "keyword-baseline-v1"; }

    // Independent scoring functions, public so each can be tested on its own.
    static Component textScore(const std::set<std::string>& lostKeywords,
                               const std::set<std::string>& foundKeywords);
    Component categoryScore(const ItemReport& lost, const ItemReport& found) const;
    Component colourScore(const ItemReport& lost, const ItemReport& found) const;
    Component locationScore(const ItemReport& lost, const ItemReport& found) const;

    const MatchingConfig& config() const noexcept { return config_; }

private:
    MatchingConfig config_;
};

}  // namespace findit
