#include "findit/MatchResult.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

namespace {

// Written as a negated range check so NaN is rejected too.
double validatedScore(double value, const std::string& what) {
    if (!(value >= 0.0 && value <= 1.0)) {
        throw std::invalid_argument(what + " must be between 0 and 1");
    }
    return value;
}

std::string validatedReportId(std::string id) {
    if (!validation::isValidReportId(id)) {
        throw std::invalid_argument("Invalid report ID in match result: '" + id + "'");
    }
    return id;
}

}  // namespace

MatchResult::MatchResult(std::string lostId, std::string foundId, double score)
    : lostId_(validatedReportId(std::move(lostId))),
      foundId_(validatedReportId(std::move(foundId))),
      score_(validatedScore(score, "Match score")) {
    if (lostId_ == foundId_) {
        throw std::invalid_argument("A report cannot be matched with itself");
    }
}

void MatchResult::addComponent(const std::string& name, double value) {
    if (validation::trim(name).empty()) {
        throw std::invalid_argument("Component name is required");
    }
    components_[name] = validatedScore(value, "Component score '" + name + "'");
}

void MatchResult::addExplanation(std::string reason) {
    explanations_.push_back(std::move(reason));
}

void MatchResult::setRank(int rank) {
    if (rank < 1) {
        throw std::invalid_argument("Rank must be 1 or greater");
    }
    rank_ = rank;
}

std::string MatchResult::toString() const {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << '#' << rank_ << "  " << foundId_ << "  score " << score_;
    if (!components_.empty()) {
        out << "  [";
        bool first = true;
        for (const auto& [name, value] : components_) {
            out << (first ? "" : ", ") << name << '=' << value;
            first = false;
        }
        out << ']';
    }
    for (const auto& reason : explanations_) {
        out << "\n    - " << reason;
    }
    return out.str();
}

}  // namespace findit
