// End-to-end tests: C++ ReportService -> MatchEngine -> PythonTfidfStrategy -> real Python
// service (ml/findit_ml/service.py) -> validated JSON -> combined ranking.
// Failure modes use small stub services in a temporary module directory.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>

#include "../TestFramework.h"
#include "../TestHelpers.h"
#include "findit/KeywordBaselineStrategy.h"
#include "findit/MatchEngine.h"
#include "findit/PythonTfidfStrategy.h"
#include "findit/ReportRepository.h"
#include "findit/ReportService.h"

namespace fs = std::filesystem;
using namespace findit;
using testhelpers::makeInput;

namespace {

fs::path tempDir() {
    std::random_device device;
    const fs::path dir = fs::temp_directory_path() / ("findit_it_" + std::to_string(device()));
    fs::create_directories(dir);
    return dir;
}

PythonProcessOptions realOptions(const fs::path& logDir) {
    PythonProcessOptions options;
    options.executable = FINDIT_PYTHON;
    options.modulePath = std::string(FINDIT_SOURCE_DIR) + "/ml";
    options.logPath = (logDir / "python.log").string();
    options.timeoutSeconds = 60;
    return options;
}

// A module directory whose findit_ml/service.py is the given stub.
fs::path stubModule(const fs::path& dir, const std::string& serviceSource) {
    fs::create_directories(dir / "findit_ml");
    std::ofstream(dir / "findit_ml" / "__init__.py") << "";
    std::ofstream(dir / "findit_ml" / "service.py") << serviceSource;
    return dir;
}

const char* kReady =
    "import sys, json\n"
    "print(json.dumps({'schema_version': '1.0', 'type': 'ready', 'status': 'ok'}), flush=True)\n";

struct Scenario {
    ReportRepository repository;
    MatchEngine engine;
    ReportService service{repository, engine};
    PythonTfidfStrategy* python = nullptr;  // Owned by engine.

    explicit Scenario(const PythonProcessOptions& options) {
        auto strategy = std::make_unique<PythonTfidfStrategy>(
            MatchingConfig::loadFile(testhelpers::fixturePath("config/matching.conf")), options,
            [this]() { return repository.all(); });
        python = strategy.get();
        engine.setStrategy(std::move(strategy));
        engine.setCategoryBlocking(false);

        const std::string user = service.registerUser("Integration Tester");
        const auto add = [&](bool lost, const std::string& category, const std::string& text,
                             const std::string& colour, const std::string& location) {
            ReportInput input = makeInput(category, text, colour, location);
            input.reporterId = user;
            return lost ? service.submitLost(input) : service.submitFound(input);
        };
        add(true, "Wallet", "Black leather wallet with student ID card and metro pass", "black", "Library");
        add(false, "Wallet", "Found a black leather wallet with a student ID card", "black", "Library");
        add(false, "Wallet", "Brown wallet with no cards", "brown", "Library");
        add(false, "Umbrella", "Green folding umbrella", "green", "Bus Stop");
        add(false, "Water Bottle", "Blue steel bottle with a dent", "blue", "Gym");
        add(true, "Wallet", "lost it, pls help", "black", "Library");  // R000006: no keywords.
    }
};

}  // namespace

TEST_CASE(testEndToEndRanking, "End to end: one lost report, several found candidates, ranked response") {
    const fs::path dir = tempDir();
    Scenario s(realOptions(dir));

    const auto results = s.service.suggestMatches("R000001", 3);
    CHECK(results.size() == 3);
    CHECK(results[0].foundId() == "R000002");  // Same item, colour, place, wording.
    CHECK(results[1].foundId() == "R000003");  // Same category and place, different colour.
    for (std::size_t i = 0; i < results.size(); ++i) {
        CHECK(results[i].rank() == static_cast<int>(i + 1));
        if (i > 0) {
            CHECK(results[i - 1].score() >= results[i].score());
        }
    }

    const MatchResult& top = results[0];
    const double text = top.components().at("text");
    CHECK(text > 0.5 && text <= 1.0);
    // Combined exactly as configured: 0.60 text + 0.20 location + 0.10 category + 0.10 colour.
    CHECK_NEAR(top.score(), 0.60 * text + 0.20 * top.components().at("location") +
                                0.10 * top.components().at("category") +
                                0.10 * top.components().at("colour"));
    CHECK(top.explanations()[0].rfind("Text (TF-IDF): Exact shared terms:", 0) == 0);
    CHECK(top.explanations()[0].find("leather") != std::string::npos);
    CHECK(top.explanations()[1] == "Same category: Wallet");

    // Fitted once, on the whole corpus, and reused by later queries.
    CHECK(s.python->fitCount() == 1);
    CHECK(s.python->lastFit().documentsFitted == 5);  // R000006 has no usable text.
    const auto again = s.service.suggestMatches("R000001", 3);
    CHECK(s.python->fitCount() == 1);
    for (std::size_t i = 0; i < results.size(); ++i) {
        CHECK(again[i].foundId() == results[i].foundId() && again[i].score() == results[i].score());
    }

    s.engine.setCategoryBlocking(true);
    CHECK(s.service.suggestMatches("R000001", 5).size() == 2);  // Wallets only.
    fs::remove_all(dir);
}

TEST_CASE(testEndToEndNoText, "End to end: lost report without usable text") {
    const fs::path dir = tempDir();
    Scenario s(realOptions(dir));
    const auto results = s.service.suggestMatches("R000006", 5);
    CHECK(!results.empty());
    for (const auto& result : results) {
        CHECK(result.components().at("text") == 0.0);
        CHECK(result.explanations()[0].find("no usable description") != std::string::npos);
    }
    CHECK(results[0].foundId() == "R000002");  // Category, colour and location still count.
    fs::remove_all(dir);
}

TEST_CASE(testComponentsAreIndependent, "End to end: text score ignores category, colour and location") {
    const fs::path dir = tempDir();
    ReportRepository repository;
    MatchEngine engine;
    ReportService service(repository, engine);
    auto strategy = std::make_unique<PythonTfidfStrategy>(
        MatchingConfig::loadFile(testhelpers::fixturePath("config/matching.conf")), realOptions(dir),
        [&repository]() { return repository.all(); });
    engine.setStrategy(std::move(strategy));
    engine.setCategoryBlocking(false);
    const std::string user = service.registerUser("Independence Tester");
    const auto add = [&](bool lost, const std::string& category, const std::string& text,
                         const std::string& colour, const std::string& location) {
        ReportInput input = makeInput(category, text, colour, location);
        input.reporterId = user;
        return lost ? service.submitLost(input) : service.submitFound(input);
    };
    const std::string text = "Leather wallet with student card and metro pass";
    const std::string lost = add(true, "Wallet", text, "black", "Library");
    const std::string agrees = add(false, "Wallet", text, "black", "Library");
    const std::string disagrees = add(false, "Umbrella", text, "green", "Bus Stop");
    // Shares category, colour and location words with the lost report but no description words.
    const std::string fieldsOnly = add(false, "Wallet", "Found near the reading room shelves", "black", "Library");

    const auto results = service.suggestMatches(lost, 3);
    const auto byId = [&](const std::string& id) -> const MatchResult& {
        for (const auto& r : results) {
            if (r.foundId() == id) return r;
        }
        throw std::logic_error("missing result " + id);
    };
    const MatchResult& a = byId(agrees);
    const MatchResult& d = byId(disagrees);
    const MatchResult& f = byId(fieldsOnly);
    // Same description -> same text score, whatever the structured fields say.
    CHECK(a.components().at("text") == d.components().at("text"));
    CHECK_NEAR(a.components().at("text"), 1.0);
    CHECK(a.components().at("category") == 1.0 && d.components().at("category") == 0.0);
    CHECK(d.components().at("colour") == 0.0 && d.components().at("location") == 0.0);
    // Totals differ by exactly the structured weights: 0.20 + 0.10 + 0.10.
    CHECK_NEAR(a.score() - d.score(), 0.40);
    // Category/colour/location words do not leak into text: no shared description terms.
    CHECK(f.explanations()[0].find("Exact shared terms") == std::string::npos);
    CHECK(f.components().at("text") < 0.1);
    fs::remove_all(dir);
}

TEST_CASE(testMissingDependency, "Failure: missing Python dependency is reported and falls back") {
    const fs::path dir = tempDir();
    // Real service code, but "import sklearn" fails.
    fs::create_directories(dir / "mod" / "sklearn");
    fs::create_directory_symlink(fs::path(FINDIT_SOURCE_DIR) / "ml" / "findit_ml", dir / "mod" / "findit_ml");
    std::ofstream(dir / "mod" / "sklearn" / "__init__.py")
        << "raise ImportError(\"No module named 'sklearn' (simulated by test)\")\n";
    PythonProcessOptions options = realOptions(dir);
    options.modulePath = (dir / "mod").string();

    Scenario direct(options);
    std::string code;
    std::string message;
    try {
        direct.service.suggestMatches("R000001", 3);
    } catch (const PythonMatcherError& error) {
        code = error.code();
        message = error.what();
    }
    CHECK(code == "missing_dependency");
    CHECK(message.find("pip install -r ml/requirements.txt") != std::string::npos);

    Scenario fallback(options);
    std::string reason;
    fallback.engine.setStrategy(std::make_unique<FallbackStrategy>(
        std::make_unique<PythonTfidfStrategy>(MatchingConfig{}, options,
                                              [&fallback]() { return fallback.repository.all(); }),
        std::make_unique<KeywordBaselineStrategy>(),
        [&reason](const std::string& why) { reason = why; }));
    const auto results = fallback.service.suggestMatches("R000001", 3);
    CHECK(results.size() == 3 && results[0].foundId() == "R000002");
    CHECK(reason.find("missing_dependency") != std::string::npos);
    CHECK(fallback.engine.strategyName().find("fallback") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE(testInvalidJsonFromPython, "Failure: invalid JSON from Python is rejected") {
    const fs::path dir = tempDir();
    PythonProcessOptions options = realOptions(dir);
    options.modulePath = stubModule(dir / "mod", std::string(kReady) +
                                                     "for line in sys.stdin:\n"
                                                     "    print('this is not json', flush=True)\n")
                             .string();
    Scenario s(options);
    std::string code;
    try {
        s.service.suggestMatches("R000001", 3);
    } catch (const PythonMatcherError& error) {
        code = error.code();
    }
    CHECK(code == "protocol_error");
    fs::remove_all(dir);
}

TEST_CASE(testPythonCrash, "Failure: Python crash is reported with its stderr") {
    const fs::path dir = tempDir();
    PythonProcessOptions options = realOptions(dir);
    options.modulePath = stubModule(dir / "mod", std::string(kReady) +
                                                     "sys.stdin.readline()\n"
                                                     "print('boom: simulated crash', file=sys.stderr)\n"
                                                     "sys.exit(5)\n")
                             .string();
    Scenario s(options);
    std::string code;
    std::string message;
    try {
        s.service.suggestMatches("R000001", 3);
    } catch (const PythonMatcherError& error) {
        code = error.code();
        message = error.what();
    }
    CHECK(code == "process_exited");
    CHECK(message.find("exit code 5") != std::string::npos);
    CHECK(message.find("boom: simulated crash") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE(testPythonTimeout, "Failure: unresponsive Python times out") {
    const fs::path dir = tempDir();
    PythonProcessOptions options = realOptions(dir);
    options.timeoutSeconds = 1;
    options.modulePath =
        stubModule(dir / "mod", std::string(kReady) + "import time\ntime.sleep(30)\n").string();
    Scenario s(options);
    std::string code;
    try {
        s.service.suggestMatches("R000001", 3);
    } catch (const PythonMatcherError& error) {
        code = error.code();
    }
    CHECK(code == "timeout");
    fs::remove_all(dir);
}
