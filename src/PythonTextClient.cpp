#include "findit/PythonTextClient.h"

#include <cmath>
#include <set>

#include <nlohmann/json.hpp>

namespace findit {

using nlohmann::json;

namespace {

PythonMatcherError protocolError(const std::string& message) {
    return PythonMatcherError("protocol_error", "Invalid response from Python matcher: " + message);
}

// Only the free-text description goes to Python. Category, colour and location are scored
// in C++ (KeywordBaselineStrategy components); sending them too would count them twice.
json textFields(const ItemReport& report) {
    return {{"description", report.description()}};
}

std::string dump(const json& value) {
    // ASCII output with invalid UTF-8 replaced, so one request is always one line.
    return value.dump(-1, ' ', true, json::error_handler_t::replace);
}

// Parses one line and checks the fields every message shares.
json parseMessage(const std::string& line, const std::string& expectedType,
                  const std::string* expectedRequestId) {
    json message;
    try {
        message = json::parse(line);
    } catch (const json::parse_error& e) {
        throw protocolError(std::string("not JSON (") + e.what() + "): " + line.substr(0, 200));
    }
    if (!message.is_object()) {
        throw protocolError("expected a JSON object");
    }
    if (message.value("schema_version", json()) != PythonTextClient::kSchemaVersion) {
        throw protocolError("schema_version must be \"1.0\"");
    }
    const std::string status = message.value("status", "");
    if (status == "error" || message.value("type", "") == "error") {
        const json error = message.value("error", json::object());
        const std::string code = error.is_object() ? error.value("code", "unknown") : "unknown";
        const std::string text = error.is_object() ? error.value("message", "") : "";
        throw PythonMatcherError(code == "missing_dependency" ? code : "python_error",
                                 "Python matcher reported " + code + ": " + text);
    }
    if (status != "ok") {
        throw protocolError("status must be \"ok\" or \"error\"");
    }
    if (message.value("type", "") != expectedType) {
        throw protocolError("expected type \"" + expectedType + "\"");
    }
    if (expectedRequestId != nullptr &&
        message.value("request_id", json()) != json(*expectedRequestId)) {
        throw protocolError("request_id does not match request " + *expectedRequestId);
    }
    return message;
}

double unitScore(const json& match, const char* field, std::size_t index) {
    const auto it = match.find(field);
    if (it == match.end() || !it->is_number()) {
        throw protocolError("matches[" + std::to_string(index) + "]." + field + " must be a number");
    }
    const double value = it->get<double>();
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw protocolError("matches[" + std::to_string(index) + "]." + field +
                            " must be between 0 and 1");
    }
    return value;
}

}  // namespace

PythonTextClient::PythonTextClient(PythonProcessOptions options) : process_(std::move(options)) {}

std::string PythonTextClient::nextRequestId() {
    return std::to_string(++requestCounter_);
}

std::string PythonTextClient::exchange(const std::string& request) {
    process_.writeLine(request);
    return process_.readLine();
}

void PythonTextClient::start() {
    process_.start();
    try {
        parseReady(process_.readLine());
    } catch (const PythonMatcherError&) {
        process_.stop();
        throw;
    }
}

FitSummary PythonTextClient::fit(const std::vector<const ItemReport*>& corpus) {
    const std::string id = nextRequestId();
    const FitSummary summary = parseFitResponse(exchange(buildFitRequest(corpus, id)), id);
    fitted_ = true;
    return summary;
}

TextRanking PythonTextClient::rank(const ItemReport& lost,
                                   const std::vector<const FoundReport*>& candidates) {
    std::vector<std::string> ids;
    ids.reserve(candidates.size());
    for (const FoundReport* candidate : candidates) {
        ids.push_back(candidate->id());
    }
    const std::string id = nextRequestId();
    return parseRankResponse(exchange(buildRankRequest(lost, candidates, id)), id, ids);
}

std::string PythonTextClient::buildFitRequest(const std::vector<const ItemReport*>& corpus,
                                              const std::string& requestId) {
    json documents = json::array();
    for (const ItemReport* report : corpus) {
        if (report != nullptr) {
            documents.push_back(textFields(*report));
        }
    }
    return dump({{"schema_version", kSchemaVersion},
                 {"type", "fit"},
                 {"request_id", requestId},
                 {"documents", documents}});
}

std::string PythonTextClient::buildRankRequest(const ItemReport& lost,
                                               const std::vector<const FoundReport*>& candidates,
                                               const std::string& requestId) {
    json lostJson = textFields(lost);
    lostJson["report_id"] = lost.id();
    json candidateJson = json::array();
    for (const FoundReport* candidate : candidates) {
        json entry = textFields(*candidate);
        entry["report_id"] = candidate->id();
        candidateJson.push_back(std::move(entry));
    }
    // Ask for every candidate: C++ combines text with the structured scores before cutting to top K.
    const std::size_t topK = candidates.empty() ? 1 : candidates.size();
    return dump({{"schema_version", kSchemaVersion},
                 {"type", "rank"},
                 {"request_id", requestId},
                 {"top_k", topK},
                 {"lost", lostJson},
                 {"candidates", candidateJson}});
}

void PythonTextClient::parseReady(const std::string& line) {
    parseMessage(line, "ready", nullptr);
}

FitSummary PythonTextClient::parseFitResponse(const std::string& line, const std::string& requestId) {
    const json message = parseMessage(line, "fit_result", &requestId);
    const auto count = message.find("documents_fitted");
    const auto vocabulary = message.find("vocabulary_size");
    if (count == message.end() || !count->is_number_unsigned() || vocabulary == message.end() ||
        !vocabulary->is_number_unsigned()) {
        throw protocolError("fit_result needs non-negative integers documents_fitted and vocabulary_size");
    }
    return {count->get<std::size_t>(), vocabulary->get<std::size_t>()};
}

TextRanking PythonTextClient::parseRankResponse(const std::string& line, const std::string& requestId,
                                                const std::vector<std::string>& sentCandidateIds) {
    const json message = parseMessage(line, "rank_result", &requestId);
    const auto lostHasText = message.find("lost_has_text");
    const auto matches = message.find("matches");
    if (lostHasText == message.end() || !lostHasText->is_boolean()) {
        throw protocolError("lost_has_text must be true or false");
    }
    if (matches == message.end() || !matches->is_array()) {
        throw protocolError("matches must be an array");
    }

    const std::set<std::string> sent(sentCandidateIds.begin(), sentCandidateIds.end());
    std::set<std::string> returned;
    TextRanking ranking;
    ranking.lostHasText = lostHasText->get<bool>();
    for (std::size_t i = 0; i < matches->size(); ++i) {
        const json& match = (*matches)[i];
        const std::string where = "matches[" + std::to_string(i) + "]";
        if (!match.is_object()) {
            throw protocolError(where + " must be an object");
        }
        TextScore score;
        const auto id = match.find("candidate_id");
        if (id == match.end() || !id->is_string() || id->get<std::string>().empty()) {
            throw protocolError(where + ".candidate_id must be a non-empty string");
        }
        score.candidateId = id->get<std::string>();
        if (sent.count(score.candidateId) == 0) {
            throw protocolError("unknown candidate_id '" + score.candidateId + "' (not in the request)");
        }
        if (!returned.insert(score.candidateId).second) {
            throw protocolError("candidate_id '" + score.candidateId + "' returned twice");
        }
        const auto rank = match.find("rank");
        if (rank == match.end() || !rank->is_number_integer() ||
            rank->get<long long>() != static_cast<long long>(i + 1)) {
            throw protocolError(where + ".rank must be " + std::to_string(i + 1));
        }
        score.rank = static_cast<int>(i + 1);
        score.similarity = unitScore(match, "similarity", i);
        score.wordSimilarity = unitScore(match, "word_similarity", i);
        score.charSimilarity = unitScore(match, "char_similarity", i);
        if (i > 0 && score.similarity > ranking.scores.back().similarity) {
            throw protocolError("matches are not sorted by similarity");
        }
        const auto terms = match.find("shared_terms");
        if (terms == match.end() || !terms->is_array()) {
            throw protocolError(where + ".shared_terms must be an array");
        }
        for (const json& term : *terms) {
            if (!term.is_string()) {
                throw protocolError(where + ".shared_terms must contain strings");
            }
            score.sharedTerms.push_back(term.get<std::string>());
        }
        const auto explanation = match.find("explanation");
        if (explanation == match.end() || !explanation->is_string()) {
            throw protocolError(where + ".explanation must be a string");
        }
        score.explanation = explanation->get<std::string>();
        ranking.scores.push_back(std::move(score));
    }

    if (!ranking.lostHasText && !ranking.scores.empty()) {
        throw protocolError("matches must be empty when lost_has_text is false");
    }
    if (ranking.lostHasText && returned.size() != sent.size()) {
        std::string missing;
        int listed = 0;
        for (const auto& candidateId : sentCandidateIds) {
            if (returned.count(candidateId) == 0 && listed++ < 5) {
                missing += (missing.empty() ? "" : ", ") + candidateId;
            }
        }
        throw protocolError("missing candidate IDs: " + missing +
                            (sent.size() - returned.size() > 5 ? ", ..." : ""));
    }
    return ranking;
}

}  // namespace findit
