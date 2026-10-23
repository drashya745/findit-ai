// Phase 2 tests: CSV parsing/writing, configuration, dataset adapter, loader
// (duplicates, missing values, invalid rows), persistence round trip and SHA-256.

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

#include "TestFramework.h"
#include "TestHelpers.h"
#include "findit/Csv.h"
#include "findit/DatasetAdapter.h"
#include "findit/DatasetConfig.h"
#include "findit/DatasetLoader.h"
#include "findit/KeyValueConfig.h"
#include "findit/MatchEngine.h"
#include "findit/ReportCsvStore.h"
#include "findit/ReportRepository.h"
#include "findit/ReportService.h"
#include "findit/Sha256.h"

namespace fs = std::filesystem;
using namespace findit;

namespace {

std::vector<CsvRecord> readAll(const std::string& text) {
    CsvReader reader(text);
    std::vector<CsvRecord> records;
    CsvRecord record;
    while (reader.next(record)) {
        records.push_back(record);
    }
    return records;
}

KeyValueConfig parseConfig(const std::string& text) {
    std::istringstream in(text);
    return KeyValueConfig::parse(in, "test.conf");
}

const char* kMappingText =
    "column.source_id = item_id\n"
    "column.report_type = lost_or_found\n"
    "column.category = item_name\n"
    "column.description = description\n"
    "column.color = color\n"
    "column.brand = brand\n"
    "column.location = location\n"
    "column.reporter = user\n"
    "column.timestamp = timestamp\n"
    "report_type.lost = lost\n"
    "report_type.found = found\n"
    "missing_tokens = none, null, nan, n/a, na\n";

DatasetConfig testMapping() {
    return DatasetConfig::fromKeyValues(parseConfig(kMappingText));
}

const char* kHeader = "item_id,lost_or_found,item_name,description,color,brand,location,user,timestamp\n";

struct LoadFixture {
    ReportRepository repository;
    MatchEngine engine;
    ReportService service{repository, engine};
    IngestResult result;

    explicit LoadFixture(const std::string& csvText, DatasetConfig config = testMapping()) {
        CsvReader reader(csvText);
        DatasetLoader loader(std::move(config), service);
        result = loader.load(reader);
    }
};

fs::path makeTempDir() {
    std::random_device device;
    const fs::path dir = fs::temp_directory_path() / ("findit_test_" + std::to_string(device()));
    fs::create_directories(dir);
    return dir;
}

}  // namespace

// ---------------------------------------------------------------- CSV reader / writer

TEST_CASE(testCsvQuotedFields, "CSV: quoted fields, commas, escaped quotes, newlines") {
    const auto records = readAll(
        "a,b,c\n"
        "1,\"Can't find my keychain (yellow), has a sticker.\",x\n"
        "2,\"He said \"\"hi\"\"\",y\n"
        "3,\"line one\nline two\",z\n"
        "4,plain,w\n");
    CHECK(records.size() == 5);
    CHECK(records[1].fields[1] == "Can't find my keychain (yellow), has a sticker.");
    CHECK(records[2].fields[1] == "He said \"hi\"");
    CHECK(records[3].fields[1] == "line one\nline two");
    CHECK(records[3].line == 4);
    CHECK(records[4].line == 6);  // Line numbers account for the embedded newline.
    for (const auto& record : records) {
        CHECK(record.error.empty());
        CHECK(record.fields.size() == 3);
    }
}

TEST_CASE(testCsvEmptyFieldsAndLineEndings, "CSV: empty fields, CRLF, BOM, blank lines, no final newline") {
    CsvReader reader("\xEF\xBB\xBFh1,h2,h3\r\n,,\r\n\r\nx,,\"\"\r\n\nlast,row,here");
    std::vector<CsvRecord> records;
    CsvRecord record;
    while (reader.next(record)) {
        records.push_back(record);
    }
    CHECK(records.size() == 4);
    CHECK(records[0].fields[0] == "h1");  // BOM stripped.
    CHECK(records[1].fields == std::vector<std::string>({"", "", ""}));
    CHECK(records[2].fields == std::vector<std::string>({"x", "", ""}));
    CHECK(records[3].fields == std::vector<std::string>({"last", "row", "here"}));
    CHECK(reader.blankLinesSkipped() == 2);
}

TEST_CASE(testCsvMalformed, "CSV: malformed records are flagged, not dropped") {
    auto records = readAll("a,\"unterminated\n");
    CHECK(records.size() == 1 && records[0].error.find("Unterminated") != std::string::npos);
    records = readAll("a,5\" ruler,c\nnext,ok,row\n");
    CHECK(records.size() == 2);
    CHECK(records[0].error.find("Unexpected quote") != std::string::npos);
    CHECK(records[1].error.empty());  // Parsing recovers on the next line.
    records = readAll("a,\"quoted\"tail,c\n");
    CHECK(records[0].error.find("after a closing quote") != std::string::npos);
}

TEST_CASE(testCsvWriterRoundTrip, "CSV: writer escapes and round-trips") {
    const std::vector<std::string> fields = {"plain", "has,comma", "has \"quotes\"",
                                             "multi\nline", "", " padded "};
    CHECK(csv::escapeField("plain") == "plain");
    CHECK(csv::escapeField("a,b") == "\"a,b\"");
    CHECK(csv::escapeField("say \"x\"") == "\"say \"\"x\"\"\"");
    const auto records = readAll(csv::formatRow(fields) + "\n");
    CHECK(records.size() == 1 && records[0].fields == fields);
}

// ---------------------------------------------------------------- Configuration

TEST_CASE(testKeyValueConfig, "KeyValueConfig parsing") {
    const auto config = parseConfig("# comment\n\n a = 1 \nlist = x, y ,, z\nflag = yes\n");
    CHECK(config.getOr("a", "") == "1");
    CHECK(config.getInt("a", 0) == 1);
    CHECK(config.getBool("flag", false));
    CHECK(config.getList("list") == std::vector<std::string>({"x", "y", "z"}));
    CHECK(config.getOr("missing", "fallback") == "fallback");
    CHECK_THROWS_AS(parseConfig("a = 1\na = 2\n"), std::invalid_argument);
    CHECK_THROWS_AS(parseConfig("no equals sign\n"), std::invalid_argument);
    CHECK_THROWS_AS(parseConfig("x = abc\n").getDouble("x", 0), std::invalid_argument);
}

TEST_CASE(testDatasetConfig, "DatasetConfig mapping and vocabulary") {
    const DatasetConfig real =
        DatasetConfig::loadFile(testhelpers::fixturePath("config/dataset_mapping.conf"));
    CHECK(real.sourceColumn(CanonicalField::Category) == "item_name");
    CHECK(real.sourceColumn(CanonicalField::Reporter) == "user");

    const DatasetConfig config = testMapping();
    CHECK(config.isMissing(""));
    CHECK(config.isMissing("   "));
    CHECK(config.isMissing("None"));
    CHECK(config.isMissing(" N/A "));
    CHECK(!config.isMissing("Nike"));
    CHECK(config.classify(" LOST ") == ReportType::Lost);
    CHECK(config.classify("Found") == ReportType::Found);
    CHECK(!config.classify("returned").has_value());

    CHECK_THROWS_AS(DatasetConfig::fromKeyValues(parseConfig(std::string(kMappingText) + "typo = 1\n")),
                    std::invalid_argument);
    CHECK_THROWS_AS(DatasetConfig::fromKeyValues(parseConfig("column.category = item_name\n")),
                    std::invalid_argument);
    std::string overlap = kMappingText;
    overlap.replace(overlap.find("report_type.found = found"), 25, "report_type.found = lost");
    CHECK_THROWS_AS(DatasetConfig::fromKeyValues(parseConfig(overlap)), std::invalid_argument);
}

// ---------------------------------------------------------------- Adapter

TEST_CASE(testAdapterHeader, "Adapter: required vs optional columns") {
    const std::vector<std::string> noCategory = {"item_id", "lost_or_found", "description", "color",
                                                 "location", "timestamp"};
    CHECK_THROWS_AS(DatasetAdapter(testMapping(), noCategory), std::invalid_argument);

    // Brand, user and item_id absent: still usable, with warnings.
    const std::vector<std::string> minimal = {"lost_or_found", "item_name", "description", "color",
                                              "location", "timestamp"};
    const DatasetAdapter adapter(testMapping(), minimal);
    CHECK(adapter.warnings().size() == 3);
    CHECK(!adapter.hasField(CanonicalField::Brand));
    CsvRecord record;
    record.line = 7;
    record.fields = {"found", "Umbrella", "Green folding umbrella on bench", "Green", "Bus Stop",
                     "2025-09-01"};
    const AdaptOutcome outcome = adapter.adapt(record);
    CHECK(outcome.record.has_value());
    CHECK(outcome.record->sourceId == "line:7");
    CHECK(outcome.record->brand.empty());
    CHECK(outcome.record->color == "green");
    CHECK(outcome.record->timeOfDay.empty());

    const std::vector<std::string> duplicated = {"item_name", "item_name"};
    CHECK_THROWS_AS(DatasetAdapter(testMapping(), duplicated), std::invalid_argument);
}

TEST_CASE(testAdapterRows, "Adapter: normalization and row rejection") {
    const auto header = readAll(kHeader)[0].fields;
    const DatasetAdapter adapter(testMapping(), header);
    const auto adapt = [&adapter](const std::string& line) {
        return adapter.adapt(readAll(line + "\n")[0]);
    };

    const AdaptOutcome ok =
        adapt("9,  LOST ,Power  Bank,\"My  silver power bank\",Silver,None,Corridor,Ananya,2025-08-19 17:34:15");
    CHECK(ok.record.has_value());
    CHECK(ok.record->type == ReportType::Lost);
    CHECK(ok.record->category == "Power Bank");
    CHECK(ok.record->description == "My silver power bank");
    CHECK(ok.record->color == "silver");
    CHECK(ok.record->brand.empty());  // "None" means unknown.
    CHECK(ok.record->date == "2025-08-19");
    CHECK(ok.record->timeOfDay == "17:34:15");
    CHECK(ok.whitespaceNormalizedFields == 3);

    CHECK(adapt("1,lost,Pen,,blue,HP,Gym,Amit,2025-09-01").reject.code == "missing_required_field");
    CHECK(adapt("1,lost,Pen,Blue pen lost,NA,HP,Gym,Amit,2025-09-01").reject.code ==
          "missing_required_field");
    CHECK(adapt("1,returned,Pen,Blue pen here,blue,HP,Gym,Amit,2025-09-01").reject.code ==
          "invalid_report_type");
    CHECK(adapt("1,lost,Pen,Blue pen here,blue,HP,Gym,Amit,2025-13-01").reject.code ==
          "invalid_timestamp");
    CHECK(adapt("1,lost,Pen,Blue pen here,blue,HP,Gym,Amit,yesterday").reject.code ==
          "invalid_timestamp");
    CHECK(adapt("1,lost,Pen,Blue pen here,blue,HP,Gym,Amit,2025-09-01 25:00").reject.code ==
          "invalid_timestamp");
    CHECK(adapt("1,lost,Pen,Blue pen here,blue").reject.code == "wrong_field_count");
    CHECK(adapt("1,lost,Pen,\"Blue pen\" here,blue,HP,Gym,Amit,2025-09-01").reject.code ==
          "malformed_csv");
}

// ---------------------------------------------------------------- Loader

TEST_CASE(testLoaderInvalidAndDuplicates, "Loader: invalid rows, duplicates and missing values") {
    const std::string csvText =
        std::string(kHeader) +
        "1,lost,Wallet,\"Black wallet, with \"\"ID\"\" card\",black,Fastrack,Library,Asha,2025-09-01 10:00:00\n"
        "2,found,Wallet,Found a black wallet near desk,black,None,Library,asha,2025-09-02 10:00:00\n"
        "2,found,Wallet,Found a black wallet near desk,black,None,Library,asha,2025-09-02 10:00:00\n"
        "2,found,Umbrella,Green umbrella on a bench,green,,Bus Stop,Dev,2025-09-03 10:00:00\n"
        "3,found,Wallet,Found a black wallet near desk,black,None,Library,asha,2025-09-02 10:00:00\n"
        "4,lost,Pen,,blue,HP,Gym,Dev,2025-09-04 10:00:00\n"
        "5,returned,Pen,Blue pen near the gym,blue,HP,Gym,Dev,2025-09-04 10:00:00\n"
        "6,lost,Pen,too short,blue,HP,Gym,Dev,2025-09-04 10:00:00\n"
        "7,lost,Pen,\"unterminated,blue,HP,Gym,Dev,2025-09-04 10:00:00\n";
    // The unterminated quote swallows the rest of the file into one malformed record.
    LoadFixture fx(csvText);
    const IngestStats& s = fx.result.stats;
    CHECK(s.recordsRead == 9);
    CHECK(s.accepted == 3);  // Lines 2, 3 and 6.
    CHECK(s.acceptedLost == 1);
    CHECK(s.acceptedFound == 2);
    CHECK(s.rejected == 6);
    CHECK(s.rejected + s.accepted == s.recordsRead);
    CHECK(s.exactDuplicateRows == 1);
    CHECK(s.duplicateSourceIds == 1);
    CHECK(s.contentDuplicates == 1);  // Line 6 = line 3 except item_id; kept.
    CHECK(s.rejectedByCode.at("duplicate_row") == 1);
    CHECK(s.rejectedByCode.at("duplicate_source_id") == 1);
    CHECK(s.rejectedByCode.at("missing_required_field") == 1);
    CHECK(s.rejectedByCode.at("invalid_report_type") == 1);
    CHECK(s.rejectedByCode.at("validation_failed") == 1);
    CHECK(s.rejectedByCode.at("malformed_csv") == 1);
    CHECK(fx.result.rejectedRows[0].line == 4);
    CHECK(fx.result.rejectedRows[0].detail == "Exact duplicate of line 3");

    // Missing counts are over well-formed rows: brand None x3 + empty x1; description empty x1.
    std::map<std::string, std::size_t> missing(s.missingBySourceColumn.begin(),
                                               s.missingBySourceColumn.end());
    CHECK(missing.at("brand") == 4);
    CHECK(missing.at("description") == 1);
    CHECK(missing.at("item_id") == 0);
    CHECK(s.missingTokensSeen.at("None") == 3);
    CHECK(s.missingTokensSeen.at("(empty)") == 2);

    // Reporter names are matched case-insensitively: Asha/asha -> one user.
    CHECK(s.usersCreated == 2);
    CHECK(fx.service.listUsers().size() == 2);

    const ItemReport& first = fx.service.getReport("R000001");
    CHECK(first.description() == "Black wallet, with \"ID\" card");
    CHECK(first.sourceRef() == "1");
    CHECK(!fx.service.getReport("R000002").brand().has_value());
}

TEST_CASE(testLoaderPlaceholderReporter, "Loader: missing reporter uses a placeholder user") {
    LoadFixture fx(std::string(kHeader) +
                   "1,lost,Cap,Red cap with a white logo,red,Puma,Gym,,2025-09-01\n"
                   "2,found,Cap,Red cap found in the gym,red,Puma,Gym,N/A,2025-09-02\n");
    CHECK(fx.result.stats.accepted == 2);
    CHECK(fx.result.stats.placeholderReporterUsed);
    CHECK(fx.service.listUsers().size() == 1);
    CHECK(fx.service.listUsers()[0]->name() == DatasetLoader::kPlaceholderReporter);
}

TEST_CASE(testLoaderMissingHeaderColumn, "Loader: missing required column fails fast") {
    CHECK_THROWS_AS(LoadFixture("item_id,lost_or_found,description\n1,lost,abc\n"),
                    std::runtime_error);
    CHECK_THROWS_AS(LoadFixture(""), std::runtime_error);
}

TEST_CASE(testLoaderRealisticSample, "Loader: realistic records from the dataset") {
    const DatasetConfig config =
        DatasetConfig::loadFile(testhelpers::fixturePath("config/dataset_mapping.conf"));
    std::ifstream in(testhelpers::fixturePath("tests/fixtures/realistic_sample.csv"));
    std::ostringstream text;
    text << in.rdbuf();
    LoadFixture fx(text.str(), config);
    const IngestStats& s = fx.result.stats;
    CHECK(s.recordsRead == 12);
    CHECK(s.accepted == 12);
    CHECK(s.acceptedLost == 6 && s.acceptedFound == 6);
    CHECK(s.rejected == 0);
    CHECK(s.usersCreated == 9);  // Ananya and Manoj appear twice.
    CHECK(s.distinctTimesOfDay == 1);
    CHECK(s.minDate == "2025-08-16" && s.maxDate == "2025-11-24");

    const ItemReport& first = fx.service.getReport("R000001");
    CHECK(first.type() == ReportType::Lost);
    CHECK(first.category() == "Keychain");
    CHECK(first.description() == "Can't find my keychain (yellow), has a sticker.");
    CHECK(first.sourceRef() == "1");
    const ItemReport& fifth = fx.service.getReport("R000005");
    CHECK(fifth.description() == "L0st notebook (brown) bro pls help find.");  // Text preserved.
}

// ---------------------------------------------------------------- Persistence

TEST_CASE(testPersistenceRoundTrip, "Persistence: save, load, append") {
    const fs::path dir = makeTempDir();
    const std::string usersPath = (dir / "users.csv").string();
    const std::string reportsPath = (dir / "reports.csv").string();

    ReportRepository original;
    MatchEngine engine;
    ReportService service(original, engine);
    service.registerUser("Asha, \"A\" K", "asha@example.edu");
    ReportInput input = testhelpers::validInput();
    input.description = "Wallet with \"ID\", cards\nand notes";  // Quotes, comma, newline.
    input.sourceRef = "77";
    const std::string lostId = service.submitLost(input);
    input.brand = "";
    const std::string foundId = service.submitFound(input, "Front desk");
    service.confirmMatch(lostId, foundId);

    persistence::saveUsers(usersPath, service.listUsers());
    persistence::saveReports(reportsPath, service.listReports());
    CHECK(!fs::exists(reportsPath + ".tmp"));

    ReportRepository loaded;
    CHECK(persistence::loadUsers(usersPath, loaded) == 1);
    CHECK(persistence::loadReports(reportsPath, loaded) == 2);
    CHECK(loaded.findUser("U001")->name() == "Asha, \"A\" K");
    const ItemReport* lost = loaded.findById(lostId);
    const auto* found = dynamic_cast<const FoundReport*>(loaded.findById(foundId));
    CHECK(lost != nullptr && found != nullptr);
    CHECK(lost->description() == "Wallet with \"ID\", cards\nand notes");
    CHECK(lost->status() == ReportStatus::Matched);
    CHECK(lost->sourceRef() == "77");
    CHECK(*lost->brand() == "Fastrack");
    CHECK(!found->brand().has_value());
    CHECK(found->heldAt() == "Front desk");
    CHECK(loaded.nextReportId() == "R000003");

    const std::string storePath = (dir / "store.csv").string();
    persistence::appendReport(storePath, *lost);
    persistence::appendReport(storePath, *found);
    ReportRepository fromStore;
    persistence::loadUsers(usersPath, fromStore);
    CHECK(persistence::loadReports(storePath, fromStore) == 2);

    const std::string storeUsers = (dir / "store_users.csv").string();
    persistence::appendUser(storeUsers, User("U002", "New Student", "new@example.edu"));
    persistence::appendUser(storeUsers, User("U003", "Another, \"Quoted\" Student"));
    CHECK(persistence::loadUsers(storeUsers, fromStore) == 2);
    CHECK(fromStore.findUser("U003")->name() == "Another, \"Quoted\" Student");
    CHECK(fromStore.nextUserId() == "U004");
    CHECK_THROWS_AS(persistence::loadUsers(storeUsers, fromStore), std::runtime_error);  // Duplicates.

    std::ofstream(dir / "bad.csv") << "wrong,header\n";
    CHECK_THROWS_AS(persistence::loadReports((dir / "bad.csv").string(), fromStore),
                    std::runtime_error);
    ReportRepository noUsers;
    CHECK_THROWS_AS(persistence::loadReports(reportsPath, noUsers), std::runtime_error);
    fs::remove_all(dir);
}

TEST_CASE(testConfirmedMatchLog, "Persistence: confirmed matches survive a restart") {
    const fs::path dir = makeTempDir();
    const std::string usersPath = (dir / "users.csv").string();
    const std::string reportsPath = (dir / "reports.csv").string();
    const std::string matchesPath = (dir / "matches.csv").string();

    std::string lostId;
    std::string foundId;
    std::string otherFound;
    {
        ReportRepository repo;
        MatchEngine engine;
        ReportService service(repo, engine);
        service.registerUser("Asha");
        lostId = service.submitLost(testhelpers::validInput());
        foundId = service.submitFound(testhelpers::validInput());
        otherFound = service.submitFound(testhelpers::validInput());
        persistence::saveUsers(usersPath, service.listUsers());
        persistence::saveReports(reportsPath, service.listReports());  // All OPEN.
        service.confirmMatch(lostId, foundId);
        persistence::appendConfirmedMatch(matchesPath, lostId, foundId, "2026-10-23");
        CHECK_THROWS_AS(persistence::appendConfirmedMatch(matchesPath, lostId, foundId, "23/10/2026"),
                        std::invalid_argument);
    }

    // "Restart": fresh objects, same files. reports.csv itself was never rewritten.
    ReportRepository repo;
    MatchEngine engine;
    ReportService service(repo, engine);
    persistence::loadUsers(usersPath, repo);
    persistence::loadReports(reportsPath, repo);
    CHECK(service.getReport(lostId).status() == ReportStatus::Open);
    const std::size_t replayed = persistence::loadConfirmedMatches(
        matchesPath, [&service](const std::string& l, const std::string& f) { service.confirmMatch(l, f); });
    CHECK(replayed == 1);
    CHECK(service.getReport(lostId).status() == ReportStatus::Matched);
    CHECK(service.getReport(foundId).status() == ReportStatus::Matched);
    CHECK(service.getReport(otherFound).status() == ReportStatus::Open);

    // A log that confirms the same pair twice is rejected with its line number.
    persistence::appendConfirmedMatch(matchesPath, lostId, foundId, "2026-10-23");
    ReportRepository repo2;
    MatchEngine engine2;
    ReportService service2(repo2, engine2);
    persistence::loadUsers(usersPath, repo2);
    persistence::loadReports(reportsPath, repo2);
    std::string message;
    try {
        persistence::loadConfirmedMatches(
            matchesPath, [&service2](const std::string& l, const std::string& f) { service2.confirmMatch(l, f); });
    } catch (const std::runtime_error& e) {
        message = e.what();
    }
    CHECK(message.find("matches.csv:3:") != std::string::npos);
    CHECK(message.find("already MATCHED") != std::string::npos);

    std::ofstream(dir / "bad_matches.csv") << "lost,found\nR000001,R000002\n";
    CHECK_THROWS_AS(persistence::loadConfirmedMatches((dir / "bad_matches.csv").string(),
                                                      [](const std::string&, const std::string&) {}),
                    std::runtime_error);
    fs::remove_all(dir);
}

TEST_CASE(testSha256, "SHA-256 known vectors") {
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
