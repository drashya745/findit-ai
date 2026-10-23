// Phase 3 tests: keyword extraction, component scorers, weighted baseline score,
// missing-field handling, ties, configuration and end-to-end ranking.

#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "TestFramework.h"
#include "TestHelpers.h"
#include "findit/DatasetLoader.h"
#include "findit/KeywordBaselineStrategy.h"
#include "findit/MatchEngine.h"
#include "findit/MatchingConfig.h"
#include "findit/ReportRepository.h"
#include "findit/ReportService.h"
#include "findit/SampleData.h"
#include "findit/TextKeywords.h"

using namespace findit;
using testhelpers::makeInput;

namespace {

MatchingConfig projectConfig() {
    return MatchingConfig::loadFile(testhelpers::fixturePath("config/matching.conf"));
}

MatchingConfig parseMatching(const std::string& text) {
    std::istringstream in(text);
    return MatchingConfig::fromKeyValues(KeyValueConfig::parse(in, "test.conf"));
}

bool hasReason(const MatchResult& result, const std::string& fragment) {
    for (const auto& reason : result.explanations()) {
        if (reason.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

MatchResult scoreOne(const KeywordBaselineStrategy& strategy, const LostReport& lost,
                     const FoundReport& found) {
    const auto results = strategy.score(lost, {&found});
    if (results.size() != 1) {
        throw std::logic_error("expected exactly one result");
    }
    return results[0];
}

}  // namespace

TEST_CASE(testKeywords, "Keywords: tokenization, stopwords, digit-zero fix") {
    CHECK(text::keywords("L0st notebook (brown) bro pls help find.") ==
          std::set<std::string>({"notebook", "brown"}));
    CHECK(text::keywords("Can't find my keychain (yellow), has a sticker.") ==
          std::set<std::string>({"keychain", "yellow", "sticker"}));
    CHECK(text::keywords("Picked up yellow keychain, belongs to someone.") ==
          std::set<std::string>({"yellow", "keychain"}));
    CHECK(text::tokenize("Classroom A102") == std::vector<std::string>({"classroom", "a102"}));
    CHECK(text::tokenize("2026") == std::vector<std::string>({"2026"}));
    CHECK(text::keywords("Found it near the campus, pls help").empty());
}

TEST_CASE(testTextScore, "Text score: Dice keyword overlap") {
    using S = KeywordBaselineStrategy;
    CHECK_NEAR(S::textScore({"a", "b"}, {"a", "b"}).value, 1.0);
    CHECK_NEAR(S::textScore({"a", "b", "c"}, {"b", "c", "d", "e"}).value, 4.0 / 7.0);
    const auto disjoint = S::textScore({"a"}, {"b"});
    CHECK_NEAR(disjoint.value, 0.0);
    CHECK(disjoint.reason == "No shared description keywords");
    const auto empty = S::textScore({"a"}, {});
    CHECK_NEAR(empty.value, 0.0);
    CHECK(empty.reason.find("no usable keywords on the found report") != std::string::npos);
    CHECK(S::textScore({"keychain", "sticker", "yellow"}, {"keychain", "yellow"}).reason ==
          "Shared keywords (2 of 3 lost / 2 found, overlap 0.80): keychain, yellow");
}

TEST_CASE(testFieldScores, "Category, colour and location scores") {
    const KeywordBaselineStrategy strategy(projectConfig());
    const LostReport lost("R000001", makeInput("Water Bottle", "Blue steel bottle", "blue", "Gym"));
    const FoundReport same("R000002", makeInput("water bottle", "Blue steel bottle", "Blue", "gym"));
    const FoundReport related("R000003", makeInput("Flask", "Blue steel bottle", "blue", "Sports Complex"));
    const FoundReport different("R000004", makeInput("Wallet", "Blue steel bottle", "green", "Library"));
    const FoundReport unknownColour("R000005", makeInput("Flask", "Blue steel bottle", "unknown", "Gym"));

    CHECK_NEAR(strategy.categoryScore(lost, same).value, 1.0);
    CHECK_NEAR(strategy.colourScore(lost, same).value, 1.0);
    CHECK_NEAR(strategy.locationScore(lost, same).value, 1.0);

    CHECK_NEAR(strategy.categoryScore(lost, related).value, 0.5);
    CHECK(strategy.categoryScore(lost, related).reason ==
          "Related category (group BOTTLES): Water Bottle ~ Flask");
    CHECK_NEAR(strategy.locationScore(lost, related).value, 0.5);
    CHECK(strategy.locationScore(lost, related).reason ==
          "Related location (zone SPORTS): Gym ~ Sports Complex");

    CHECK_NEAR(strategy.categoryScore(lost, different).value, 0.0);
    CHECK_NEAR(strategy.colourScore(lost, different).value, 0.0);
    CHECK(strategy.colourScore(lost, different).reason == "Different colour: blue vs green");
    CHECK_NEAR(strategy.locationScore(lost, different).value, 0.0);

    const auto missing = strategy.colourScore(lost, unknownColour);
    CHECK_NEAR(missing.value, 0.0);
    CHECK(missing.reason == "Colour missing on found report (not counted as a match)");
}

TEST_CASE(testExactMatch, "Baseline: exact match scores 1") {
    const KeywordBaselineStrategy strategy(projectConfig());
    const ReportInput input =
        makeInput("Water Bottle", "Blue steel water bottle with dent", "blue", "Gym");
    const LostReport lost("R000001", input);
    const FoundReport found("R000002", input);
    const MatchResult result = scoreOne(strategy, lost, found);
    CHECK_NEAR(result.score(), 1.0);
    for (const auto& component : result.components()) {
        CHECK_NEAR(component.second, 1.0);
    }
    CHECK(hasReason(result, "Same category: Water Bottle"));
    CHECK(hasReason(result, "Same location: Gym"));
}

TEST_CASE(testPartialMatch, "Baseline: partial match uses the weighted formula") {
    const KeywordBaselineStrategy strategy(projectConfig());
    const LostReport lost("R000001", makeInput("Water Bottle",
                                               "Blue steel water bottle with dent near cap",
                                               "blue", "Gym"));
    const FoundReport found("R000002",
                            makeInput("Flask", "Blue bottle found near gym", "blue", "Sports Complex"));
    const MatchResult result = scoreOne(strategy, lost, found);
    // Keywords {blue,steel,water,bottle,dent,cap} vs {blue,bottle,gym}: Dice = 4/9.
    const double text = 4.0 / 9.0;
    CHECK_NEAR(result.components().at("text"), text);
    CHECK_NEAR(result.components().at("category"), 0.5);
    CHECK_NEAR(result.components().at("colour"), 1.0);
    CHECK_NEAR(result.components().at("location"), 0.5);
    CHECK_NEAR(result.score(), 0.60 * text + 0.20 * 0.5 + 0.10 * 0.5 + 0.10 * 1.0);
    CHECK(hasReason(result, "Shared keywords (2 of 6 lost / 3 found, overlap 0.44): blue, bottle"));
}

TEST_CASE(testUnrelated, "Baseline: unrelated items score 0") {
    const KeywordBaselineStrategy strategy(projectConfig());
    const LostReport lost("R000001",
                          makeInput("Wallet", "Black leather wallet with card", "black", "Library"));
    const FoundReport found("R000002", makeInput("Umbrella", "Green folding umbrella left on bench",
                                                 "green", "Bus Stop"));
    const MatchResult result = scoreOne(strategy, lost, found);
    CHECK_NEAR(result.score(), 0.0);
    CHECK(hasReason(result, "No shared description keywords"));
    CHECK(hasReason(result, "Different category: Wallet vs Umbrella"));
    CHECK(hasReason(result, "Different location: Library vs Bus Stop"));
}

TEST_CASE(testMissingFields, "Baseline: missing fields are never counted as agreement") {
    const KeywordBaselineStrategy strategy(projectConfig());
    const LostReport lost("R000001",
                          makeInput("Wallet", "Black leather wallet with card", "black", "Library"));
    const FoundReport vague("R000002",
                            makeInput("Wallet", "Found it near the campus, pls help", "unknown", "Library"));
    const MatchResult result = scoreOne(strategy, lost, vague);
    CHECK_NEAR(result.components().at("text"), 0.0);
    CHECK_NEAR(result.components().at("colour"), 0.0);
    CHECK_NEAR(result.score(), 0.20 * 1.0 + 0.10 * 1.0);  // Only location and category count.
    CHECK(hasReason(result, "Description has no usable keywords on the found report"));
    CHECK(hasReason(result, "Colour missing on found report"));
}

TEST_CASE(testTies, "Baseline: ties are broken deterministically by found ID") {
    MatchEngine engine(std::make_unique<KeywordBaselineStrategy>(projectConfig()));
    const LostReport lost("R000001",
                          makeInput("Wallet", "Black leather wallet with card", "black", "Library"));
    const ReportInput twin = makeInput("Wallet", "Black wallet found", "black", "Library");
    const FoundReport later("R000009", twin);
    const FoundReport earlier("R000004", twin);
    const FoundReport weaker("R000002", makeInput("Wallet", "Brown purse", "brown", "Gym"));

    const auto results = engine.findMatches(lost, {&later, &weaker, &earlier}, 3);
    CHECK(results.size() == 3);
    CHECK(results[0].score() == results[1].score());
    CHECK(results[0].foundId() == "R000004" && results[0].rank() == 1);
    CHECK(results[1].foundId() == "R000009" && results[1].rank() == 2);
    CHECK(results[2].foundId() == "R000002" && results[2].score() < results[1].score());
    // Same input, same output.
    const auto again = engine.findMatches(lost, {&earlier, &later, &weaker}, 3);
    for (std::size_t i = 0; i < results.size(); ++i) {
        CHECK(again[i].foundId() == results[i].foundId());
        CHECK(again[i].score() == results[i].score());
    }
}

TEST_CASE(testWeights, "Baseline: configurable and validated weights") {
    MatchingConfig textOnly;
    textOnly.weights = {1.0, 0.0, 0.0, 0.0};
    const LostReport lost("R000001", makeInput("Wallet", "Black leather wallet", "black", "Library"));
    const FoundReport found("R000002", makeInput("Umbrella", "Black wallet", "green", "Gym"));
    // {black,leather,wallet} vs {black,wallet}: Dice = 0.8; other components ignored.
    CHECK_NEAR(scoreOne(KeywordBaselineStrategy(textOnly), lost, found).score(), 0.8);

    MatchingConfig unnormalized;
    unnormalized.weights = {3.0, 1.0, 0.0, 0.0};  // Sum 4: score still in [0, 1].
    CHECK_NEAR(scoreOne(KeywordBaselineStrategy(unnormalized), lost, found).score(), 0.6);

    MatchingConfig negative;
    negative.weights = {0.6, -0.2, 0.1, 0.1};
    CHECK_THROWS_AS(KeywordBaselineStrategy(negative), std::invalid_argument);
    MatchingConfig zero;
    zero.weights = {0.0, 0.0, 0.0, 0.0};
    CHECK_THROWS_AS(KeywordBaselineStrategy(zero), std::invalid_argument);
}

TEST_CASE(testMatchingConfig, "MatchingConfig file parsing") {
    const MatchingConfig config = projectConfig();
    CHECK_NEAR(config.weights.text, 0.60);
    CHECK_NEAR(config.weights.location, 0.20);
    CHECK_NEAR(config.weights.category, 0.10);
    CHECK_NEAR(config.weights.colour, 0.10);
    CHECK(config.categoryBlocking);
    CHECK(config.dateWindowDays == 0);
    CHECK(config.topK == 3);
    CHECK(config.locationZone.at("sports complex") == "SPORTS");
    CHECK(config.colourGroup.at("silver") == "GREYSCALE");

    CHECK_THROWS_AS(parseMatching("weight.txt = 1\n"), std::invalid_argument);
    CHECK_THROWS_AS(parseMatching("zone.A = Gym\nzone.B = gym\n"), std::invalid_argument);
    CHECK_THROWS_AS(parseMatching("threshold = 2\n"), std::invalid_argument);
    CHECK_THROWS_AS(parseMatching("top_k = 0\n"), std::invalid_argument);
    CHECK_THROWS_AS(parseMatching("weight.text = -1\n"), std::invalid_argument);
}

TEST_CASE(testServiceRanking, "End to end: ranked suggestions through ReportService") {
    ReportRepository repo;
    MatchEngine engine(std::make_unique<KeywordBaselineStrategy>(projectConfig()));
    ReportService service(repo, engine);
    seedSampleData(service);

    // R000003: blue Milton water bottle lost at the Gym. With blocking, only R000006 is a candidate.
    auto results = service.suggestMatches("R000003", 5);
    CHECK(results.size() == 1 && results[0].foundId() == "R000006");

    engine.setCategoryBlocking(false);
    results = service.suggestMatches("R000003", 5);
    CHECK(results.size() == 3);
    CHECK(results[0].foundId() == "R000006");
    for (std::size_t i = 1; i < results.size(); ++i) {
        CHECK(results[i - 1].score() >= results[i].score());
    }
    CHECK(service.suggestMatches("R000003", 2).size() == 2);
}

TEST_CASE(testExplainMatch, "ReportService: explain one specific pair, even when filtered out") {
    ReportRepository repo;
    MatchEngine engine(std::make_unique<KeywordBaselineStrategy>(projectConfig()));
    ReportService service(repo, engine);
    seedSampleData(service);

    bool considered = false;
    // R000003 (lost water bottle) vs R000006 (found water bottle): eligible.
    const MatchResult same = service.explainMatch("R000003", "R000006", considered);
    CHECK(considered);
    CHECK(same.rank() == 1 && same.foundId() == "R000006");
    CHECK(hasReason(same, "Same category: Water Bottle"));

    // R000003 vs R000004 (umbrella): blocked by category, but still explained.
    const MatchResult blocked = service.explainMatch("R000003", "R000004", considered);
    CHECK(!considered);
    CHECK(hasReason(blocked, "Different category: Water Bottle vs Umbrella"));
    engine.setCategoryBlocking(false);
    service.explainMatch("R000003", "R000004", considered);
    CHECK(considered);

    CHECK_THROWS_AS(service.explainMatch("R000004", "R000003", considered), std::invalid_argument);
    CHECK_THROWS_AS(service.explainMatch("R000003", "R999999", considered), std::out_of_range);
}

TEST_CASE(testRealisticRanking, "End to end: realistic dataset records") {
    ReportRepository repo;
    MatchEngine engine(std::make_unique<KeywordBaselineStrategy>(projectConfig()));
    ReportService service(repo, engine);
    std::ifstream in(testhelpers::fixturePath("tests/fixtures/realistic_sample.csv"));
    std::ostringstream text;
    text << in.rdbuf();
    CsvReader reader(text.str());
    DatasetLoader loader(
        DatasetConfig::loadFile(testhelpers::fixturePath("config/dataset_mapping.conf")), service);
    loader.load(reader);

    // Lost keychain (yellow, Garden) vs found keychain (yellow, Garden):
    // keywords {keychain,sticker,yellow} vs {keychain,yellow} -> 0.8; all other components 1.
    const auto results = service.suggestMatches("R000001", 5);
    CHECK(results.size() == 1);
    CHECK(results[0].foundId() == "R000002");
    CHECK_NEAR(results[0].score(), 0.60 * 0.8 + 0.20 + 0.10 + 0.10);
}
