// Phase 5 unit tests that need no Python: request building, strict response validation,
// spawn failures, fallback behaviour and config parsing.

#include <memory>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "TestFramework.h"
#include "TestHelpers.h"
#include "findit/KeywordBaselineStrategy.h"
#include "findit/MatchingConfig.h"
#include "findit/PythonTextClient.h"
#include "findit/PythonTfidfStrategy.h"

using namespace findit;
using nlohmann::json;
using testhelpers::makeInput;

namespace {

std::string codeOf(const std::function<void()>& action) {
    try {
        action();
    } catch (const PythonMatcherError& error) {
        return error.code();
    }
    return "(no error)";
}

std::string messageOf(const std::function<void()>& action) {
    try {
        action();
    } catch (const std::exception& error) {
        return error.what();
    }
    return "";
}

json match(const std::string& id, int rank, double similarity) {
    return {{"candidate_id", id}, {"rank", rank}, {"similarity", similarity},
            {"word_similarity", similarity}, {"char_similarity", similarity},
            {"shared_terms", json::array({"wallet"})}, {"explanation", "Exact shared terms: wallet"}};
}

json rankResult(json matches, bool lostHasText = true, const std::string& requestId = "7") {
    return {{"schema_version", "1.0"}, {"type", "rank_result"}, {"request_id", requestId},
            {"status", "ok"}, {"lost_has_text", lostHasText}, {"matches", std::move(matches)}};
}

const std::vector<std::string> kSent = {"R000002", "R000003"};

TextRanking parse(const json& message) {
    return PythonTextClient::parseRankResponse(message.dump(), "7", kSent);
}

class AlwaysUnavailable final : public SimilarityStrategy {
public:
    std::vector<MatchResult> score(const LostReport&, const std::vector<const FoundReport*>&) const override {
        ++calls;
        throw StrategyUnavailableError("simulated outage");
    }
    std::string name() const override { return "always-unavailable"; }
    mutable int calls = 0;
};

}  // namespace

TEST_CASE(testBuildRequests, "Python protocol: requests carry the description only, IDs only where needed") {
    ReportInput input = makeInput("Wallet", "Black \"leather\" wallet, caf\xC3\xA9 receipt", "black", "Library");
    input.sourceRef = "SECRET-SOURCE";
    const LostReport lost("R000001", input);
    const FoundReport found("R000002", makeInput("Wallet", "Found black wallet", "black", "Gym"));

    const json fit = json::parse(PythonTextClient::buildFitRequest({&lost, &found, nullptr}, "1"));
    CHECK(fit["schema_version"] == "1.0" && fit["type"] == "fit" && fit["request_id"] == "1");
    CHECK(fit["documents"].size() == 2);
    // Description only: category, colour and location are scored in C++, never twice.
    CHECK(fit["documents"][0].size() == 1);
    CHECK(!fit["documents"][0].contains("report_id"));
    CHECK(fit["documents"][0]["description"] == "Black \"leather\" wallet, caf\xC3\xA9 receipt");
    for (const char* field : {"category", "color", "brand", "location"}) {
        CHECK(!fit["documents"][1].contains(field));
    }

    const std::string rankLine = PythonTextClient::buildRankRequest(lost, {&found}, "2");
    CHECK(rankLine.find('\n') == std::string::npos);  // One request per line.
    CHECK(rankLine.find("SECRET-SOURCE") == std::string::npos);
    const json rank = json::parse(rankLine);
    CHECK(rank["top_k"] == 1);
    CHECK(rank["lost"]["report_id"] == "R000001");
    CHECK(rank["candidates"][0]["report_id"] == "R000002");
    CHECK(rank["lost"].size() == 2 && rank["candidates"][0].size() == 2);  // report_id + description.
    CHECK(!rank["lost"].contains("category") && !rank["candidates"][0].contains("color"));
}

TEST_CASE(testParseValidResponses, "Python protocol: valid ready/fit/rank responses") {
    PythonTextClient::parseReady(R"({"schema_version":"1.0","type":"ready","status":"ok"})");
    const FitSummary fit = PythonTextClient::parseFitResponse(
        R"({"schema_version":"1.0","type":"fit_result","request_id":"3","status":"ok","documents_fitted":30000,"vocabulary_size":1665})",
        "3");
    CHECK(fit.documentsFitted == 30000 && fit.vocabularySize == 1665);

    const TextRanking ranking = parse(rankResult({match("R000003", 1, 0.9), match("R000002", 2, 0.1)}));
    CHECK(ranking.lostHasText);
    CHECK(ranking.scores.size() == 2);
    CHECK(ranking.scores[0].candidateId == "R000003" && ranking.scores[0].similarity == 0.9);
    CHECK(ranking.scores[0].sharedTerms == std::vector<std::string>({"wallet"}));

    const TextRanking noText = parse(rankResult(json::array(), false));
    CHECK(!noText.lostHasText && noText.scores.empty());
}

TEST_CASE(testParseInvalidResponses, "Python protocol: invalid responses are rejected with clear errors") {
    const auto bad = [](const json& message) { return codeOf([&] { parse(message); }); };
    CHECK(codeOf([] { PythonTextClient::parseRankResponse("not json", "7", kSent); }) == "protocol_error");
    CHECK(codeOf([] { PythonTextClient::parseRankResponse("[1,2]", "7", kSent); }) == "protocol_error");

    json wrongVersion = rankResult({match("R000003", 1, 0.9), match("R000002", 2, 0.1)});
    wrongVersion["schema_version"] = "2.0";
    CHECK(bad(wrongVersion) == "protocol_error");
    CHECK(bad(rankResult({match("R000003", 1, 0.9), match("R000002", 2, 0.1)}, true, "8")) ==
          "protocol_error");  // Answer to a different request.
    CHECK(bad(rankResult({match("R000003", 1, 0.9)})) == "protocol_error");  // Missing R000002.
    CHECK(messageOf([] { parse(rankResult({match("R000003", 1, 0.9)})); })
              .find("missing candidate IDs: R000002") != std::string::npos);
    CHECK(bad(rankResult({match("R000003", 1, 0.9), match("R999999", 2, 0.1)})) == "protocol_error");
    CHECK(bad(rankResult({match("R000003", 1, 0.9), match("R000003", 2, 0.1)})) == "protocol_error");
    CHECK(bad(rankResult({match("R000003", 1, 1.5), match("R000002", 2, 0.1)})) == "protocol_error");
    CHECK(bad(rankResult({match("R000003", 2, 0.9), match("R000002", 1, 0.1)})) == "protocol_error");
    CHECK(bad(rankResult({match("R000002", 1, 0.1), match("R000003", 2, 0.9)})) == "protocol_error");
    CHECK(bad(rankResult({match("R000003", 1, 0.9), match("R000002", 2, 0.1)}, false)) ==
          "protocol_error");

    json noId = rankResult({match("R000003", 1, 0.9), match("R000002", 2, 0.1)});
    noId["matches"][1].erase("candidate_id");
    CHECK(bad(noId) == "protocol_error");
    json stringScore = rankResult({match("R000003", 1, 0.9), match("R000002", 2, 0.1)});
    stringScore["matches"][0]["similarity"] = "0.9";
    CHECK(bad(stringScore) == "protocol_error");

    const json pythonError = {{"schema_version", "1.0"}, {"type", "error"}, {"request_id", "7"},
                              {"status", "error"},
                              {"error", {{"code", "not_fitted"}, {"message", "send fit first"}}}};
    CHECK(bad(pythonError) == "python_error");
    CHECK(messageOf([&] { parse(pythonError); }).find("not_fitted: send fit first") != std::string::npos);

    const std::string missingDependency =
        R"({"schema_version":"1.0","type":"ready","status":"error","error":{"code":"missing_dependency","message":"No module named 'sklearn'"}})";
    CHECK(codeOf([&] { PythonTextClient::parseReady(missingDependency); }) == "missing_dependency");
}

TEST_CASE(testSpawnFailure, "Python process: missing interpreter gives a clear error") {
    PythonProcessOptions options;
    options.executable = "/nonexistent/findit/python";
    PythonTextClient client(options);
    CHECK(codeOf([&] { client.start(); }) == "spawn_failed");
    CHECK(messageOf([&] { client.start(); }).find("/nonexistent/findit/python") != std::string::npos);
}

TEST_CASE(testFallbackStrategy, "FallbackStrategy switches once and keeps working") {
    auto primary = std::make_unique<AlwaysUnavailable>();
    AlwaysUnavailable* primaryPtr = primary.get();
    int notifications = 0;
    std::string reason;
    const FallbackStrategy strategy(std::move(primary), std::make_unique<KeywordBaselineStrategy>(),
                                    [&](const std::string& why) {
                                        ++notifications;
                                        reason = why;
                                    });
    CHECK(strategy.name() == "always-unavailable");
    const LostReport lost("R000001", makeInput("Wallet", "Black leather wallet", "black", "Library"));
    const FoundReport found("R000002", makeInput("Wallet", "Black wallet", "black", "Library"));
    CHECK(strategy.score(lost, {&found}).size() == 1);
    CHECK(strategy.score(lost, {&found}).size() == 1);
    CHECK(primaryPtr->calls == 1);  // Not retried on every query.
    CHECK(notifications == 1 && reason == "simulated outage");
    CHECK(strategy.usingFallback());
    CHECK(strategy.name() == "keyword-baseline-v1 (fallback: always-unavailable unavailable)");
}

TEST_CASE(testPythonConfig, "MatchingConfig: text engine and python settings") {
    std::istringstream in(
        "text_engine = python\npython.executable = /opt/py/bin/python3\npython.module_path = ml\n"
        "python.timeout_seconds = 30\npython.fallback = false\n");
    const MatchingConfig config = MatchingConfig::fromKeyValues(KeyValueConfig::parse(in, "t.conf"));
    CHECK(config.textEngine == TextEngine::Python);
    CHECK(config.python.executable == "/opt/py/bin/python3");
    CHECK(config.python.timeoutSeconds == 30);
    CHECK(!config.python.fallbackToKeyword);
    CHECK(MatchingConfig{}.textEngine == TextEngine::Keyword);

    std::istringstream badEngine("text_engine = neural\n");
    CHECK_THROWS_AS(MatchingConfig::fromKeyValues(KeyValueConfig::parse(badEngine, "t.conf")),
                    std::invalid_argument);
    std::istringstream badTimeout("python.timeout_seconds = 0\n");
    CHECK_THROWS_AS(MatchingConfig::fromKeyValues(KeyValueConfig::parse(badTimeout, "t.conf")),
                    std::invalid_argument);
}
