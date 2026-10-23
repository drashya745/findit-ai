// Phase 1 domain tests: dates, validation, users, reports, MatchResult, repository,
// MatchEngine orchestration and ReportService.

#include <cmath>
#include <memory>
#include <stdexcept>

#include "TestFramework.h"
#include "TestHelpers.h"
#include "findit/Date.h"
#include "findit/MatchEngine.h"
#include "findit/ReportRepository.h"
#include "findit/ReportService.h"
#include "findit/SampleData.h"
#include "findit/User.h"
#include "findit/Validation.h"

using namespace findit;
using testhelpers::FakeStrategy;
using testhelpers::validInput;

TEST_CASE(testDateParsing, "Date parsing and arithmetic") {
    const Date date = Date::parse("2026-09-02");
    CHECK(date.year() == 2026 && date.month() == 9 && date.day() == 2);
    CHECK(date.toString() == "2026-09-02");
    CHECK(Date::parse("2028-02-29").day() == 29);  // Leap year.
    CHECK(Date(2026, 1, 1) < Date(2026, 1, 2));
    CHECK(Date(2000, 1, 1).daysSinceEpoch() == 10957);
    CHECK(Date(2026, 3, 1).daysSinceEpoch() - Date(2026, 2, 28).daysSinceEpoch() == 1);
    CHECK(Date(2025, 1, 1).daysSinceEpoch() - Date(2024, 1, 1).daysSinceEpoch() == 366);
    CHECK_THROWS_AS(Date::parse("2026-02-29"), std::invalid_argument);
    CHECK_THROWS_AS(Date::parse("2026-13-01"), std::invalid_argument);
    CHECK_THROWS_AS(Date::parse("2026/09/02"), std::invalid_argument);
    CHECK_THROWS_AS(Date::parse("26-09-02"), std::invalid_argument);
    CHECK_THROWS_AS(Date::parse(""), std::invalid_argument);
}

TEST_CASE(testIdValidation, "ID and text validation") {
    CHECK(validation::isValidReportId("R000001"));
    CHECK(validation::isValidReportId("R123456"));
    CHECK(!validation::isValidReportId("R000000"));
    CHECK(!validation::isValidReportId("R00001"));
    CHECK(!validation::isValidReportId("r000001"));
    CHECK(!validation::isValidReportId("R00000A"));
    CHECK(!validation::isValidReportId(""));
    CHECK(validation::isValidUserId("U001"));
    CHECK(!validation::isValidUserId("U000"));
    CHECK(!validation::isValidUserId("U01"));
    CHECK(validation::trim("  hi \n") == "hi");
    CHECK(validation::collapseWhitespace("  my  pink\tbottle \n") == "my pink bottle");
}

TEST_CASE(testReportTypeParsing, "Report type parsing") {
    CHECK(parseReportType("lost") == ReportType::Lost);
    CHECK(parseReportType("  FOUND ") == ReportType::Found);
    CHECK_THROWS_AS(parseReportType("misplaced"), std::invalid_argument);
    CHECK_THROWS_AS(parseReportType(""), std::invalid_argument);
}

TEST_CASE(testUser, "User") {
    const User user("U001", "  Asha Kulkarni ", "asha@example.edu");
    CHECK(user.name() == "Asha Kulkarni");
    CHECK(user.toString() == "U001  Asha Kulkarni  <asha@example.edu>");
    CHECK(User("U002", "Dev").contact().empty());
    CHECK_THROWS_AS(User("X001", "Dev"), std::invalid_argument);
    CHECK_THROWS_AS(User("U003", "   "), std::invalid_argument);
}

TEST_CASE(testLostReport, "LostReport construction and polymorphism") {
    ReportInput input = validInput();
    input.sourceRef = "42";
    std::unique_ptr<ItemReport> report = std::make_unique<LostReport>("R000001", input);
    CHECK(report->type() == ReportType::Lost);
    CHECK(report->status() == ReportStatus::Open);
    CHECK(report->brand().has_value() && *report->brand() == "Fastrack");
    CHECK(report->sourceRef() == "42");
    CHECK(report->details().find("Lost on") != std::string::npos);
    CHECK(report->details().find("Source ref") != std::string::npos);
    CHECK(report->summary().find("R000001") == 0);
}

TEST_CASE(testFoundReport, "FoundReport details override") {
    ReportInput input = validInput();
    input.brand = "None";
    const FoundReport found("R000002", input, "Library front desk");
    const ItemReport& base = found;
    CHECK(base.type() == ReportType::Found);
    CHECK(!base.brand().has_value());
    const std::string details = base.details();  // Calls FoundReport::details virtually.
    CHECK(details.find("Found on") != std::string::npos);
    CHECK(details.find("Held at       : Library front desk") != std::string::npos);
    CHECK(details.find("(unknown)") != std::string::npos);
    const FoundReport unspecified("R000003", validInput());
    CHECK(unspecified.heldAt().empty());
    CHECK(unspecified.details().find("Not specified") != std::string::npos);
}

TEST_CASE(testReportValidation, "Report validation") {
    CHECK_THROWS_AS(LostReport("R1", validInput()), std::invalid_argument);
    CHECK_THROWS_AS(LostReport("R000000", validInput()), std::invalid_argument);

    ReportInput input = validInput();
    input.description = "too short";
    CHECK_THROWS_AS(LostReport("R000001", input), std::invalid_argument);

    input = validInput();
    input.description = std::string(501, 'x');
    CHECK_THROWS_AS(LostReport("R000001", input), std::invalid_argument);

    input = validInput();
    input.category = "  ";
    CHECK_THROWS_AS(LostReport("R000001", input), std::invalid_argument);

    input = validInput();
    input.color = "";
    CHECK_THROWS_AS(LostReport("R000001", input), std::invalid_argument);

    input = validInput();
    input.location = "";
    CHECK_THROWS_AS(LostReport("R000001", input), std::invalid_argument);

    input = validInput();
    input.reporterId = "someone";
    CHECK_THROWS_AS(LostReport("R000001", input), std::invalid_argument);

    input = validInput();
    input.date = "02-09-2026";
    CHECK_THROWS_AS(FoundReport("R000001", input), std::invalid_argument);
}

TEST_CASE(testStatusTransitions, "Status transitions") {
    LostReport report("R000001", validInput());
    report.setStatus(ReportStatus::Matched);
    CHECK(report.status() == ReportStatus::Matched);
    CHECK_THROWS_AS(report.setStatus(ReportStatus::Open), std::logic_error);
    report.setStatus(ReportStatus::Closed);
    CHECK_THROWS_AS(report.setStatus(ReportStatus::Matched), std::logic_error);
}

TEST_CASE(testMatchResult, "MatchResult") {
    MatchResult result("R000001", "R000002", 0.75);
    result.addComponent("color", 1.0);
    result.addExplanation("Colour matches");
    result.setRank(1);
    CHECK(result.components().at("color") == 1.0);
    CHECK(result.toString().find("#1  R000002  score 0.75") == 0);
    CHECK_THROWS_AS(MatchResult("R000001", "R000002", 1.5), std::invalid_argument);
    CHECK_THROWS_AS(MatchResult("R000001", "R000002", std::nan("")), std::invalid_argument);
    CHECK_THROWS_AS(MatchResult("R000001", "R000001", 0.5), std::invalid_argument);
    CHECK_THROWS_AS(result.addComponent("text", -0.1), std::invalid_argument);
    CHECK_THROWS_AS(result.setRank(0), std::invalid_argument);
}

TEST_CASE(testRepository, "ReportRepository") {
    ReportRepository repo;
    CHECK(repo.nextReportId() == "R000001");
    repo.addUser(User("U001", "Asha"));
    CHECK(repo.nextUserId() == "U002");
    CHECK_THROWS_AS(repo.addUser(User("U001", "Other")), std::invalid_argument);

    repo.addReport(std::make_unique<LostReport>("R000001", validInput()));
    repo.addReport(std::make_unique<FoundReport>("R000002", validInput()));
    repo.addReport(std::make_unique<FoundReport>("R000003", validInput()));
    CHECK(repo.reportCount() == 3);
    CHECK(repo.nextReportId() == "R000004");
    CHECK_THROWS_AS(repo.addReport(std::make_unique<LostReport>("R000001", validInput())),
                    std::invalid_argument);
    CHECK_THROWS_AS(repo.addReport(nullptr), std::invalid_argument);

    CHECK(repo.findById("R000002") != nullptr);
    CHECK(repo.findById("R999999") == nullptr);
    CHECK(repo.byType(ReportType::Found).size() == 2);
    CHECK(repo.openFoundReports().size() == 2);

    repo.updateStatus("R000002", ReportStatus::Matched);
    CHECK(repo.openFoundReports().size() == 1);
    CHECK_THROWS_AS(repo.updateStatus("R999999", ReportStatus::Closed), std::out_of_range);
}

TEST_CASE(testEngineRequiresStrategy, "MatchEngine requires strategy and valid settings") {
    const LostReport lost("R000001", validInput());
    const FoundReport found("R000002", validInput());
    const MatchEngine noStrategy;
    CHECK(!noStrategy.hasStrategy());
    CHECK_THROWS_AS(noStrategy.findMatches(lost, {&found}, 3), std::logic_error);

    MatchEngine engine(std::make_unique<FakeStrategy>(std::map<std::string, double>{}, nullptr));
    CHECK(engine.strategyName() == "fake");
    CHECK_THROWS_AS(engine.findMatches(lost, {&found}, 0), std::invalid_argument);
    CHECK_THROWS_AS(MatchEngine(nullptr, 1.2), std::invalid_argument);
    CHECK_THROWS_AS(engine.setDateWindowDays(-1), std::invalid_argument);
}

TEST_CASE(testEngineFiltersSortsRanks, "MatchEngine filters, sorts and ranks") {
    const LostReport lost("R000001", validInput("Wallet"));
    const FoundReport a("R000002", validInput("Wallet"));
    const FoundReport b("R000003", validInput("wallet"));  // Case-insensitive category.
    const FoundReport c("R000004", validInput("Wallet"));
    const FoundReport otherCategory("R000005", validInput("Umbrella"));
    FoundReport matched("R000006", validInput("Wallet"));
    matched.setStatus(ReportStatus::Matched);
    const FoundReport sameId("R000001", validInput("Wallet"));  // Same ID as the lost report.

    std::size_t seen = 0;
    MatchEngine engine(std::make_unique<FakeStrategy>(
                           std::map<std::string, double>{{"R000002", 0.4}, {"R000003", 0.9},
                                                         {"R000004", 0.4}, {"R000005", 1.0}},
                           &seen),
                       0.3);
    const std::vector<const FoundReport*> candidates = {&a,       &b,      &c, &otherCategory,
                                                        &matched, &sameId, nullptr};

    std::vector<MatchResult> results = engine.findMatches(lost, candidates, 5);
    CHECK(seen == 3);  // Other category, non-open, same-record and null are excluded.
    CHECK(results.size() == 3);
    CHECK(results[0].foundId() == "R000003" && results[0].rank() == 1);
    CHECK(results[1].foundId() == "R000002" && results[1].rank() == 2);  // Tie broken by ID.
    CHECK(results[2].foundId() == "R000004" && results[2].rank() == 3);

    CHECK(engine.findMatches(lost, candidates, 1).size() == 1);

    engine.setThreshold(0.5);
    CHECK(engine.findMatches(lost, candidates, 5).size() == 1);

    engine.setCategoryBlocking(false);
    results = engine.findMatches(lost, candidates, 5);
    CHECK(seen == 4);
    CHECK(results.size() == 2 && results[0].foundId() == "R000005");
}

TEST_CASE(testEngineDateWindow, "MatchEngine date window filter") {
    const LostReport lost("R000001", testhelpers::makeInput("Wallet", "Black wallet with card",
                                                            "black", "Library", "2026-09-10"));
    const FoundReport near("R000002", testhelpers::makeInput("Wallet", "Black wallet found here",
                                                             "black", "Library", "2026-09-13"));
    const FoundReport before("R000003", testhelpers::makeInput("Wallet", "Black wallet found here",
                                                               "black", "Library", "2026-09-07"));
    const FoundReport far("R000004", testhelpers::makeInput("Wallet", "Black wallet found here",
                                                            "black", "Library", "2026-10-30"));
    std::size_t seen = 0;
    MatchEngine engine(std::make_unique<FakeStrategy>(std::map<std::string, double>{}, &seen));
    engine.findMatches(lost, {&near, &before, &far}, 5);
    CHECK(seen == 3);  // Disabled by default.
    engine.setDateWindowDays(3);
    const auto results = engine.findMatches(lost, {&near, &before, &far}, 5);
    CHECK(seen == 2);
    CHECK(results.size() == 2);
}

TEST_CASE(testServiceSubmitAndLookup, "ReportService submit and lookup") {
    ReportRepository repo;
    MatchEngine engine;
    ReportService service(repo, engine);
    const std::string user = service.registerUser("Asha");
    CHECK(user == "U001");

    ReportInput input = validInput();
    input.reporterId = user;
    CHECK(service.submitLost(input) == "R000001");
    CHECK(service.submitFound(input, "Desk") == "R000002");
    CHECK(service.getReport(" R000002 ").type() == ReportType::Found);
    CHECK(service.listReports().size() == 2);
    CHECK(service.listReports(ReportType::Lost).size() == 1);
    CHECK_THROWS_AS(service.getReport("R000099"), std::out_of_range);

    input.reporterId = "U999";
    CHECK_THROWS_AS(service.submitLost(input), std::invalid_argument);
    input.reporterId = user;
    input.description = "short";
    CHECK_THROWS_AS(service.submitLost(input), std::invalid_argument);
    CHECK(repo.reportCount() == 2);  // Failed submissions add nothing.
}

TEST_CASE(testServiceConfirmMatch, "ReportService confirm match") {
    ReportRepository repo;
    MatchEngine engine(std::make_unique<FakeStrategy>(std::map<std::string, double>{}, nullptr));
    ReportService service(repo, engine);
    service.registerUser("Asha");
    const std::string lost = service.submitLost(validInput());
    const std::string found = service.submitFound(validInput());
    const std::string otherFound = service.submitFound(validInput());

    CHECK(service.suggestMatches(lost, 5).size() == 2);
    CHECK_THROWS_AS(service.suggestMatches(found, 5), std::invalid_argument);
    CHECK_THROWS_AS(service.confirmMatch(found, lost), std::invalid_argument);

    service.confirmMatch(lost, found);
    CHECK(service.getReport(lost).status() == ReportStatus::Matched);
    CHECK(service.getReport(found).status() == ReportStatus::Matched);
    CHECK(service.getReport(otherFound).status() == ReportStatus::Open);

    CHECK_THROWS_AS(service.confirmMatch(lost, otherFound), std::logic_error);
    CHECK(service.getReport(otherFound).status() == ReportStatus::Open);
    CHECK_THROWS_AS(service.suggestMatches(lost, 5), std::logic_error);
}

TEST_CASE(testServiceConfirmRules, "ReportService confirm: repeated, matched found, unknown IDs") {
    ReportRepository repo;
    MatchEngine engine;
    ReportService service(repo, engine);
    service.registerUser("Asha");
    const std::string lostA = service.submitLost(validInput());
    const std::string lostB = service.submitLost(validInput());
    const std::string found = service.submitFound(validInput());
    service.confirmMatch(lostA, found);

    std::string message;
    try {
        service.confirmMatch(lostA, found);  // Exact repeat.
    } catch (const std::logic_error& e) {
        message = e.what();
    }
    CHECK(message == lostA + " is already MATCHED; only OPEN reports can be confirmed as a match");

    // A found item already handed to someone cannot be matched again; lostB stays OPEN.
    CHECK_THROWS_AS(service.confirmMatch(lostB, found), std::logic_error);
    CHECK(service.getReport(lostB).status() == ReportStatus::Open);

    CHECK_THROWS_AS(service.confirmMatch("R999999", found), std::out_of_range);
    CHECK_THROWS_AS(service.confirmMatch(lostB, "R999999"), std::out_of_range);
    CHECK_THROWS_AS(service.confirmMatch(lostB, lostA), std::invalid_argument);  // Two lost reports.
    CHECK(service.getReport(lostB).status() == ReportStatus::Open);
}

TEST_CASE(testSampleData, "Sample data") {
    ReportRepository repo;
    MatchEngine engine;
    ReportService service(repo, engine);
    seedSampleData(service);
    CHECK(service.listUsers().size() == 4);
    CHECK(service.listReports().size() == 6);
    CHECK(service.listReports(ReportType::Lost).size() == 3);
    CHECK(service.listReports(ReportType::Found).size() == 3);
}
