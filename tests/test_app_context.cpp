// AppContext tests: shared startup (config, load, sample fallback) and the persisted
// operations' "validate -> write -> apply" ordering, including failed writes and restarts.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>

#include "TestFramework.h"
#include "TestHelpers.h"
#include "findit/AppContext.h"

namespace fs = std::filesystem;
using namespace findit;

namespace {

// A data directory whose processed/ holds the synthetic evaluation set (23 lost, 32 found).
fs::path makeDataDir() {
    std::random_device device;
    const fs::path dir = fs::temp_directory_path() / ("findit_ctx_" + std::to_string(device()));
    fs::create_directories(dir / "processed");
    for (const char* file : {"users.csv", "reports.csv"}) {
        fs::copy_file(testhelpers::fixturePath(std::string("evaluation/synthetic_v1/") + file),
                      dir / "processed" / file);
    }
    return dir;
}

AppOptions optionsFor(const fs::path& dir) {
    AppOptions options;
    options.dataDir = dir.string();
    return options;
}

std::size_t lineCount(const fs::path& path) {
    std::ifstream in(path);
    return static_cast<std::size_t>(std::count(std::istreambuf_iterator<char>(in),
                                               std::istreambuf_iterator<char>(), '\n'));
}

ReportInput lostInput() {
    ReportInput input = testhelpers::makeInput("Wallet", "Black leather wallet with a bus pass",
                                               "black", "Library");
    input.reporterId = "U001";
    return input;
}

}  // namespace

TEST_CASE(testContextConfigHelpers, "AppContext: config file and text engine helpers") {
    std::string note;
    const MatchingConfig defaults = AppContext::loadMatchingConfig("/nonexistent/matching.conf", note);
    CHECK(defaults.textEngine == TextEngine::Keyword);
    CHECK(note.find("not found; using built-in defaults (keyword matcher)") != std::string::npos);
    const MatchingConfig project =
        AppContext::loadMatchingConfig(testhelpers::fixturePath("config/matching.conf"), note);
    CHECK(note.empty() && project.textEngine == TextEngine::Python);

    MatchingConfig config;
    CHECK(AppContext::applyTextEngine("", config) && config.textEngine == TextEngine::Keyword);
    CHECK(AppContext::applyTextEngine("python", config) && config.textEngine == TextEngine::Python);
    CHECK(AppContext::applyTextEngine("keyword", config) && config.textEngine == TextEngine::Keyword);
    CHECK(!AppContext::applyTextEngine("neural", config));
    CHECK(AppContext::todayIso().size() == 10);
}

TEST_CASE(testContextLoadAndSample, "AppContext: loads processed data, else sample data") {
    const fs::path dir = makeDataDir();
    AppContext context(optionsFor(dir), MatchingConfig{});
    const LoadSummary summary = context.load();
    CHECK(!summary.usedSampleData);
    CHECK(summary.users == 4 && summary.reports == 55 && summary.storeReports == 0 && summary.matches == 0);
    CHECK(context.persistent());
    CHECK(context.paths().matches == (dir / "store" / "matches.csv").string());
    CHECK(fs::is_directory(dir / "store"));
    CHECK(context.dataSource() == dir.string() + "/processed + " + dir.string() + "/store");

    AppOptions sampleOptions = optionsFor(dir);
    sampleOptions.sampleOnly = true;
    AppContext sample(sampleOptions, MatchingConfig{});
    const LoadSummary seeded = sample.load();
    CHECK(seeded.usedSampleData && seeded.reports == 6 && !sample.persistent());
    sample.registerUser("Not Saved", "");  // Sample mode: in memory only.
    CHECK(!fs::exists(dir / "store" / "users.csv"));

    AppContext empty(optionsFor(dir / "does-not-exist"), MatchingConfig{});
    CHECK(empty.load().usedSampleData);
    fs::remove_all(dir);
}

TEST_CASE(testContextPersistAndRestart, "AppContext: user, report and confirmation survive a restart") {
    const fs::path dir = makeDataDir();
    std::string userId;
    std::string lostId;
    {
        AppContext context(optionsFor(dir), MatchingConfig{});
        context.load();
        userId = context.registerUser("Context Tester", "tester@example.edu");
        ReportInput input = lostInput();
        input.reporterId = userId;
        lostId = context.submitReport(ReportType::Lost, input, "");
        CHECK(lostId == "R950033");  // Next ID after the highest loaded (R950032).
        context.confirmMatch("R900001", "R950001");
        CHECK(lineCount(dir / "store" / "matches.csv") == 2);
        CHECK_THROWS_AS(context.confirmMatch("R900001", "R950001"), std::logic_error);
        CHECK(lineCount(dir / "store" / "matches.csv") == 2);  // Rejected repeat wrote nothing.
    }
    AppContext restarted(optionsFor(dir), MatchingConfig{});
    const LoadSummary summary = restarted.load();
    CHECK(summary.users == 5 && summary.storeReports == 1 && summary.matches == 1);
    CHECK(restarted.service().getReport(lostId).reporterId() == userId);
    CHECK(restarted.service().getReport("R900001").status() == ReportStatus::Matched);
    CHECK(restarted.service().getReport("R950001").status() == ReportStatus::Matched);
    fs::remove_all(dir);
}

TEST_CASE(testContextInvalidInputChangesNothing, "AppContext: invalid input writes nothing") {
    const fs::path dir = makeDataDir();
    AppContext context(optionsFor(dir), MatchingConfig{});
    context.load();
    const std::size_t before = context.service().listReports().size();
    ReportInput input = lostInput();
    input.description = "short";
    CHECK_THROWS_AS(context.submitReport(ReportType::Lost, input, ""), std::invalid_argument);
    input = lostInput();
    input.reporterId = "U999";
    CHECK_THROWS_AS(context.submitReport(ReportType::Found, input, "Desk"), std::invalid_argument);
    CHECK_THROWS_AS(context.registerUser(" ", ""), std::invalid_argument);
    CHECK_THROWS_AS(context.confirmMatch("R950001", "R900001"), std::invalid_argument);
    CHECK_THROWS_AS(context.confirmMatch("R900001", "R999999"), std::out_of_range);
    CHECK(context.service().listReports().size() == before);
    CHECK(!fs::exists(dir / "store" / "reports.csv"));
    CHECK(!fs::exists(dir / "store" / "users.csv"));
    CHECK(!fs::exists(dir / "store" / "matches.csv"));
    fs::remove_all(dir);
}

TEST_CASE(testContextFailedWriteChangesNothing, "AppContext: a failed write leaves memory unchanged") {
    const fs::path dir = makeDataDir();
    AppContext context(optionsFor(dir), MatchingConfig{});
    context.load();
    // A directory where a CSV file should be makes every append fail.
    fs::create_directories(dir / "store" / "matches.csv");
    fs::create_directories(dir / "store" / "reports.csv");
    fs::create_directories(dir / "store" / "users.csv");

    CHECK_THROWS_AS(context.confirmMatch("R900001", "R950001"), std::runtime_error);
    CHECK(context.service().getReport("R900001").status() == ReportStatus::Open);
    CHECK(context.service().getReport("R950001").status() == ReportStatus::Open);

    const std::size_t reportsBefore = context.service().listReports().size();
    CHECK_THROWS_AS(context.submitReport(ReportType::Lost, lostInput(), ""), std::runtime_error);
    CHECK(context.service().listReports().size() == reportsBefore);

    const std::size_t usersBefore = context.service().listUsers().size();
    CHECK_THROWS_AS(context.registerUser("Write Fails", ""), std::runtime_error);
    CHECK(context.service().listUsers().size() == usersBefore);

    // Once writing works again, the same operations succeed with the IDs they would have had.
    for (const char* name : {"matches.csv", "reports.csv", "users.csv"}) {
        fs::remove(dir / "store" / name);
    }
    CHECK(context.submitReport(ReportType::Lost, lostInput(), "") == "R950033");
    context.confirmMatch("R900001", "R950001");
    CHECK(context.service().getReport("R900001").status() == ReportStatus::Matched);
    fs::remove_all(dir);
}

TEST_CASE(testServiceTwoStepPrimitives, "ReportService: create/add and validateConfirmation") {
    ReportRepository repo;
    MatchEngine engine;
    ReportService service(repo, engine);
    const User user = service.createUser("Two Step", "");
    CHECK(service.listUsers().empty());  // Created, not stored.
    service.addUser(user);
    CHECK(service.listUsers().size() == 1);

    ReportInput input = testhelpers::validInput();
    input.reporterId = user.id();
    auto lost = service.createReport(ReportType::Lost, input);
    auto found = service.createReport(ReportType::Found, input, "Desk");
    CHECK(lost->id() == "R000001" && found->id() == "R000001");  // Same next ID: nothing stored.
    CHECK(lost->type() == ReportType::Lost && found->type() == ReportType::Found);
    CHECK(service.listReports().empty());
    CHECK(service.addReport(std::move(lost)) == "R000001");
    CHECK(service.addReport(service.createReport(ReportType::Found, input, "Desk")) == "R000002");
    CHECK_THROWS_AS(service.addReport(nullptr), std::invalid_argument);

    service.validateConfirmation("R000001", "R000002");  // Valid: no throw, no change.
    CHECK(service.getReport("R000001").status() == ReportStatus::Open);
    CHECK_THROWS_AS(service.validateConfirmation("R000002", "R000001"), std::invalid_argument);
    service.confirmMatch("R000001", "R000002");
    CHECK_THROWS_AS(service.validateConfirmation("R000001", "R000002"), std::logic_error);
}
