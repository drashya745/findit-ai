#pragma once

#include <map>
#include <string>
#include <vector>

namespace findit {

// One candidate pairing of a lost report with a found report, with its score,
// per-component scores and human-readable reasons. All scores are in [0, 1].
class MatchResult {
public:
    // Throws std::invalid_argument for malformed IDs or a score outside [0, 1].
    MatchResult(std::string lostId, std::string foundId, double score);

    void addComponent(const std::string& name, double value);
    void addExplanation(std::string reason);
    void setRank(int rank);

    const std::string& lostId() const noexcept { return lostId_; }
    const std::string& foundId() const noexcept { return foundId_; }
    double score() const noexcept { return score_; }
    int rank() const noexcept { return rank_; }  // 0 until ranked by MatchEngine.
    const std::map<std::string, double>& components() const noexcept { return components_; }
    const std::vector<std::string>& explanations() const noexcept { return explanations_; }

    std::string toString() const;

private:
    std::string lostId_;
    std::string foundId_;
    double score_;
    int rank_ = 0;
    std::map<std::string, double> components_;
    std::vector<std::string> explanations_;
};

}  // namespace findit
