#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "findit/FoundReport.h"
#include "findit/ItemReport.h"
#include "findit/PythonProcess.h"

namespace findit {

// One candidate's text score as returned by Python (rank_result.matches[i]).
struct TextScore {
    std::string candidateId;
    int rank = 0;
    double similarity = 0.0;
    double wordSimilarity = 0.0;
    double charSimilarity = 0.0;
    std::vector<std::string> sharedTerms;
    std::string explanation;
};

struct TextRanking {
    bool lostHasText = false;
    std::vector<TextScore> scores;  // Rank order; covers every candidate when lostHasText.
};

struct FitSummary {
    std::size_t documentsFitted = 0;
    std::size_t vocabularySize = 0;
};

// Protocol layer over PythonProcess (schema 1.0, docs/INTEGRATION.md). Builds requests with
// nlohmann::json and validates every response field before trusting it.
class PythonTextClient {
public:
    static constexpr const char* kSchemaVersion = "1.0";

    explicit PythonTextClient(PythonProcessOptions options);

    // Starts the process and checks its "ready" message (e.g. missing_dependency).
    void start();
    // Learns TF-IDF once. Only descriptions are sent: no structured fields, IDs or labels.
    FitSummary fit(const std::vector<const ItemReport*>& corpus);
    // Text similarity for every candidate.
    TextRanking rank(const ItemReport& lost, const std::vector<const FoundReport*>& candidates);
    bool isFitted() const noexcept { return fitted_; }

    // Pure request/response functions, public for unit tests. Parsers throw
    // PythonMatcherError ("protocol_error", "python_error" or "missing_dependency").
    static std::string buildFitRequest(const std::vector<const ItemReport*>& corpus,
                                       const std::string& requestId);
    static std::string buildRankRequest(const ItemReport& lost,
                                        const std::vector<const FoundReport*>& candidates,
                                        const std::string& requestId);
    static void parseReady(const std::string& line);
    static FitSummary parseFitResponse(const std::string& line, const std::string& requestId);
    static TextRanking parseRankResponse(const std::string& line, const std::string& requestId,
                                         const std::vector<std::string>& sentCandidateIds);

private:
    std::string exchange(const std::string& request);
    std::string nextRequestId();

    PythonProcess process_;
    long requestCounter_ = 0;
    bool fitted_ = false;
};

}  // namespace findit
