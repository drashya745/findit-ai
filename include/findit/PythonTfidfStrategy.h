#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "findit/KeywordBaselineStrategy.h"
#include "findit/MatchingConfig.h"
#include "findit/PythonTextClient.h"
#include "findit/SimilarityStrategy.h"

namespace findit {

// Text similarity from the Python TF-IDF service, combined with the C++ category, colour
// and location components using the same configurable weights as the keyword baseline:
//   score = (w.text*python_text + w.location*location + w.category*category + w.colour*colour)
//           / sum(weights)
// The Python process starts and fits TF-IDF lazily on the first query, once per strategy
// instance; later queries reuse the fitted model.
class PythonTfidfStrategy final : public SimilarityStrategy {
public:
    using CorpusProvider = std::function<std::vector<const ItemReport*>()>;

    PythonTfidfStrategy(MatchingConfig config, PythonProcessOptions options, CorpusProvider corpus);

    // Throws PythonMatcherError (a StrategyUnavailableError) if Python cannot be used.
    std::vector<MatchResult> score(const LostReport& lost,
                                   const std::vector<const FoundReport*>& candidates) const override;
    std::string name() const override { return "python-tfidf-v1"; }

    // Starts Python and fits now instead of on the first query.
    void warmUp() const;
    std::size_t fitCount() const noexcept { return fitCount_; }
    const FitSummary& lastFit() const noexcept { return lastFit_; }

private:
    // score() is logically const; the lazily started client is a cache.
    mutable std::unique_ptr<PythonTextClient> client_;
    mutable std::size_t fitCount_ = 0;
    mutable FitSummary lastFit_;

    KeywordBaselineStrategy structured_;  // Reused for category / colour / location.
    MatchingConfig config_;
    PythonProcessOptions options_;
    CorpusProvider corpus_;
};

// Uses `primary` until it throws StrategyUnavailableError, then switches to `fallback` for
// the rest of its lifetime and calls `onFallback` once with the reason.
class FallbackStrategy final : public SimilarityStrategy {
public:
    FallbackStrategy(std::unique_ptr<SimilarityStrategy> primary,
                     std::unique_ptr<SimilarityStrategy> fallback,
                     std::function<void(const std::string&)> onFallback = nullptr);

    std::vector<MatchResult> score(const LostReport& lost,
                                   const std::vector<const FoundReport*>& candidates) const override;
    std::string name() const override;

    bool usingFallback() const noexcept { return failed_; }
    const std::string& failureReason() const noexcept { return reason_; }

private:
    std::unique_ptr<SimilarityStrategy> primary_;
    std::unique_ptr<SimilarityStrategy> fallback_;
    std::function<void(const std::string&)> onFallback_;
    mutable bool failed_ = false;
    mutable std::string reason_;
};

}  // namespace findit
