#include "findit/PythonTfidfStrategy.h"

#include <algorithm>
#include <map>

namespace findit {

PythonTfidfStrategy::PythonTfidfStrategy(MatchingConfig config, PythonProcessOptions options,
                                         CorpusProvider corpus)
    : structured_(config),
      config_(std::move(config)),
      options_(std::move(options)),
      corpus_(std::move(corpus)) {
    if (!corpus_) {
        throw std::invalid_argument("PythonTfidfStrategy needs a corpus provider");
    }
}

void PythonTfidfStrategy::warmUp() const {
    try {
        if (!client_) {
            auto client = std::make_unique<PythonTextClient>(options_);
            client->start();
            client_ = std::move(client);
        }
        if (!client_->isFitted()) {
            lastFit_ = client_->fit(corpus_());
            ++fitCount_;
        }
    } catch (const PythonMatcherError&) {
        client_.reset();  // Stops the process; a later call starts afresh.
        throw;
    }
}

std::vector<MatchResult> PythonTfidfStrategy::score(
    const LostReport& lost, const std::vector<const FoundReport*>& candidates) const {
    std::vector<const FoundReport*> eligible;
    for (const FoundReport* candidate : candidates) {
        if (candidate != nullptr && candidate->id() != lost.id()) {
            eligible.push_back(candidate);
        }
    }
    if (eligible.empty()) {
        return {};
    }

    warmUp();
    TextRanking ranking;
    try {
        ranking = client_->rank(lost, eligible);
    } catch (const PythonMatcherError&) {
        client_.reset();
        throw;
    }
    std::map<std::string, const TextScore*> textById;
    for (const TextScore& text : ranking.scores) {
        textById[text.candidateId] = &text;
    }

    const BaselineWeights& w = config_.weights;
    std::vector<MatchResult> results;
    results.reserve(eligible.size());
    for (const FoundReport* found : eligible) {
        double text = 0.0;
        std::string textReason =
            "Text (TF-IDF): lost report has no usable description (not counted as a match)";
        if (ranking.lostHasText) {
            const TextScore& score = *textById.at(found->id());  // Presence checked by the client.
            text = score.similarity;
            textReason = "Text (TF-IDF): " + score.explanation;
        }
        const auto category = structured_.categoryScore(lost, *found);
        const auto colour = structured_.colourScore(lost, *found);
        const auto location = structured_.locationScore(lost, *found);

        const double weighted = w.text * text + w.location * location.value +
                                w.category * category.value + w.colour * colour.value;
        const double total = std::min(1.0, std::max(0.0, weighted / w.total()));

        MatchResult result(lost.id(), found->id(), total);
        result.addComponent("text", text);
        result.addComponent("location", location.value);
        result.addComponent("category", category.value);
        result.addComponent("colour", colour.value);
        result.addExplanation(textReason);
        result.addExplanation(category.reason);
        result.addExplanation(colour.reason);
        result.addExplanation(location.reason);
        results.push_back(std::move(result));
    }
    return results;
}

FallbackStrategy::FallbackStrategy(std::unique_ptr<SimilarityStrategy> primary,
                                   std::unique_ptr<SimilarityStrategy> fallback,
                                   std::function<void(const std::string&)> onFallback)
    : primary_(std::move(primary)), fallback_(std::move(fallback)), onFallback_(std::move(onFallback)) {
    if (!primary_ || !fallback_) {
        throw std::invalid_argument("FallbackStrategy needs both strategies");
    }
}

std::vector<MatchResult> FallbackStrategy::score(
    const LostReport& lost, const std::vector<const FoundReport*>& candidates) const {
    if (!failed_) {
        try {
            return primary_->score(lost, candidates);
        } catch (const StrategyUnavailableError& error) {
            failed_ = true;
            reason_ = error.what();
            if (onFallback_) {
                onFallback_(reason_);
            }
        }
    }
    return fallback_->score(lost, candidates);
}

std::string FallbackStrategy::name() const {
    return failed_ ? fallback_->name() + " (fallback: " + primary_->name() + " unavailable)"
                   : primary_->name();
}

}  // namespace findit
