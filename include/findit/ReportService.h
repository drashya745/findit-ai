#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "findit/ItemReport.h"
#include "findit/MatchEngine.h"
#include "findit/MatchResult.h"
#include "findit/ReportRepository.h"
#include "findit/User.h"

namespace findit {

// Use-case facade for the CLI. Assigns IDs, enforces cross-object rules (e.g. the
// reporter must exist) and coordinates the repository and match engine.
// Does not own either; both must outlive the service.
class ReportService {
public:
    ReportService(ReportRepository& repository, MatchEngine& engine) noexcept;

    // Each returns the newly assigned ID. Throw std::invalid_argument on bad input.
    std::string registerUser(const std::string& name, const std::string& contact = "");
    std::string submitLost(const ReportInput& input);
    std::string submitFound(const ReportInput& input, const std::string& heldAt = "");

    // Two-step versions of the above for callers that must persist before changing state
    // (AppContext): create* validates fully and assigns the next ID without storing anything;
    // add* stores the object. Nothing may be added to the repository in between.
    User createUser(const std::string& name, const std::string& contact = "") const;
    void addUser(User user);
    // heldAt is ignored for lost reports.
    std::unique_ptr<ItemReport> createReport(ReportType type, const ReportInput& input,
                                             const std::string& heldAt = "") const;
    std::string addReport(std::unique_ptr<ItemReport> report);

    // Throws std::out_of_range if the report does not exist.
    const ItemReport& getReport(const std::string& id) const;

    std::vector<const ItemReport*> listReports() const;
    std::vector<const ItemReport*> listReports(ReportType type) const;
    std::vector<const User*> listUsers() const;

    // Ranked candidates for an open lost report. Requires a strategy on the engine.
    std::vector<MatchResult> suggestMatches(const std::string& lostId, std::size_t topK) const;

    // Scores one lost/found pair (even if filters would skip it) and reports whether
    // suggestMatches would have considered it. Throws std::invalid_argument on wrong types.
    MatchResult explainMatch(const std::string& lostId, const std::string& foundId,
                             bool& wouldBeConsidered) const;

    // Marks both reports MATCHED. Both must exist (std::out_of_range), have the right types
    // (std::invalid_argument) and be OPEN (std::logic_error, e.g. a repeated confirmation);
    // otherwise nothing changes. Category/colour need not agree: a person decides.
    void confirmMatch(const std::string& lostId, const std::string& foundId);
    // Performs every check of confirmMatch and throws the same errors, but changes nothing.
    void validateConfirmation(const std::string& lostId, const std::string& foundId) const;

private:
    void requireKnownReporter(const ReportInput& input) const;

    ReportRepository& repository_;
    MatchEngine& engine_;
};

}  // namespace findit
