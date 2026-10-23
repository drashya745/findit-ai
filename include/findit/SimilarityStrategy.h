#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "findit/FoundReport.h"
#include "findit/LostReport.h"
#include "findit/MatchResult.h"

namespace findit {

// Thrown by a strategy that cannot run at all (e.g. an external process is unavailable),
// as opposed to bad input. FallbackStrategy catches exactly this type.
class StrategyUnavailableError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Interface for scoring a lost report against candidate found reports.
// Implementations return one MatchResult per candidate they consider relevant;
// MatchEngine handles thresholding, ordering and ranking.
class SimilarityStrategy {
public:
    virtual ~SimilarityStrategy() = default;

    virtual std::vector<MatchResult> score(const LostReport& lost,
                                           const std::vector<const FoundReport*>& candidates) const = 0;

    virtual std::string name() const = 0;
};

}  // namespace findit
