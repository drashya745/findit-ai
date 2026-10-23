// ApiHandlers: routing, request validation, JSON shapes, status codes and safe persistence,
// exercised without sockets against an AppContext loaded with the synthetic evaluation set.

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>

#include "TestFramework.h"
#include "TestHelpers.h"
#include "findit/ApiHandlers.h"

namespace fs = std::filesystem;
using namespace findit;
using nlohmann::json;

namespace {

struct ApiFixture {
    fs::path dir;
    std::unique_ptr<AppContext> context;
    std::unique_ptr<ApiHandlers> api;
    std::vector<std::string> log;

    ApiFixture() {
        std::random_device device;
        dir = fs::temp_directory_path() / ("findit_api_" + std::to_string(device()));
        fs::create_directories(dir / "processed");
        for (const char* file : {"users.csv", "reports.csv"}) {
            fs::copy_file(testhelpers::fixturePath(std::string("evaluation/synthetic_v1/") + file),
                          dir / "processed" / file);
        }
        start();
    }
    ~ApiFixture() {
        api.reset();
        context.reset();
        fs::remove_all(dir);
    }
    void start() {  // (Re)start: a fresh AppContext on the same files.
        api.reset();
        context.reset();
        AppOptions options;
        options.dataDir = dir.string();
        options.programName = "api-test";
        context = std::make_unique<AppContext>(options, MatchingConfig{});  // Keyword engine.
        context->load();
        api = std::make_unique<ApiHandlers>(*context, [this](const std::string& line) { log.push_back(line); });
    }
    ApiResponse get(const std::string& path, std::multimap<std::string, std::string> query = {}) {
        return api->handle({"GET", path, std::move(query), "", ""});
    }
    ApiResponse post(const std::string& path, const std::string& body,
                     const std::string& contentType = "application/json") {
        return api->handle({"POST", path, {}, contentType, body});
    }
};

std::string code(const ApiResponse& response) {
    return response.body.value("error", json::object()).value("code", "");
}

std::string validReport(const std::string& type, const std::string& extra = "") {
    return R"({"type":")" + type + R"(","reporterId":"U001","category":"Wallet",)"
           R"("description":"Black leather wallet with a student card","color":"black",)"
           R"("location":"Library","date":"2026-03-15")" + extra + "}";
}

}  // namespace

TEST_CASE(testApiRouting, "API: routing, 404, 405 and error shape") {
    ApiFixture f;
    CHECK(f.get("/api/health").status == 200);
    CHECK(f.get("/api/nope").status == 404 && code(f.get("/api/nope")) == "not_found");
    CHECK(f.get("/api/health/").status == 404);
    CHECK(f.get("/etc/passwd").status == 404);
    CHECK(f.get("/api/reports/R900001/extra").status == 404);
    CHECK(f.post("/api/health", "{}").status == 405);
    CHECK(code(f.api->handle({"DELETE", "/api/reports/R900001", {}, "", ""})) == "method_not_allowed");
    const ApiResponse error = f.get("/api/reports/../../etc/passwd");
    CHECK(error.status == 400 || error.status == 404);
    CHECK(error.body["error"].contains("message"));
    CHECK(f.get("/api/health", {{"x", "1"}}).status == 400);  // No query parameters allowed.
}

TEST_CASE(testApiHealthAndStats, "API: health and stats") {
    ApiFixture f;
    const json health = f.get("/api/health").body;
    CHECK(health["status"] == "ok" && health["persistent"] == true && health["sampleData"] == false);
    CHECK(health["dataMode"] == "dataset" && !health.contains("dataSource"));
    CHECK(health.dump().find(f.dir.string()) == std::string::npos);  // No local paths exposed.
    CHECK(health["matcher"]["strategy"] == "keyword-baseline-v1");
    CHECK(health["matcher"]["textEngine"] == "keyword" && health["matcher"]["fallbackActive"] == false);
    const json stats = f.get("/api/stats").body;
    CHECK(stats["totalReports"] == 55 && stats["lostReports"] == 23 && stats["foundReports"] == 32);
    CHECK(stats["confirmedMatches"] == 0 && stats["reportsCreatedInApp"] == 0);
}

TEST_CASE(testApiListReports, "API: list, search, filter, paginate and validate queries") {
    ApiFixture f;
    json all = f.get("/api/reports").body;
    CHECK(all["total"] == 55 && all["items"].size() == 20 && all["limit"] == 20 && all["offset"] == 0);
    CHECK(all["items"][0]["date"] == "2026-03-14");  // Default sort: most recent first.
    const json first = all["items"][0];
    for (const char* field : {"id", "type", "status", "category", "description", "color", "brand",
                              "location", "date", "reporterId", "heldAt", "origin", "sourceRef"}) {
        CHECK(first.contains(field));
    }
    CHECK(first["origin"] == "dataset" && first["sourceRef"].get<std::string>().rfind("SYNTHETIC:", 0) == 0);

    CHECK(f.get("/api/reports", {{"type", "lost"}}).body["total"] == 23);
    CHECK(f.get("/api/reports", {{"type", "FOUND"}, {"category", "wallet"}}).body["total"] == 3);  // R950001-3.
    CHECK(f.get("/api/reports", {{"status", "matched"}}).body["total"] == 0);
    const json search = f.get("/api/reports", {{"q", "black leather WALLET"}}).body;
    CHECK(search["total"] == 2);  // R900001 and R950001: every word must appear.
    CHECK(f.get("/api/reports", {{"q", "R950012"}}).body["items"][0]["id"] == "R950012");
    CHECK(f.get("/api/reports", {{"q", "zzz-no-such-thing"}}).body["items"].empty());

    const json page = f.get("/api/reports", {{"sort", "id"}, {"limit", "5"}, {"offset", "50"}}).body;
    CHECK(page["items"].size() == 5 && page["items"][0]["id"] == "R950028");
    CHECK(f.get("/api/reports", {{"offset", "1000"}}).body["items"].empty());

    for (const auto& bad : std::vector<std::pair<std::string, std::string>>{
             {"limit", "0"}, {"limit", "101"}, {"limit", "-1"}, {"limit", "abc"}, {"offset", "1e3"},
             {"type", "stolen"}, {"status", "lost"}, {"sort", "random"}, {"bogus", "1"},
             {"q", std::string(101, 'a')}, {"q", "a b c d e f g h i j k"}}) {
        const ApiResponse response = f.get("/api/reports", {{bad.first, bad.second}});
        CHECK(response.status == 400 && code(response) == "invalid_query");
    }
    CHECK(f.get("/api/reports", {{"limit", "1"}, {"limit", "2"}}).status == 400);  // Duplicate.
}

TEST_CASE(testApiGetReportCategoriesUsers, "API: single report, categories and users") {
    ApiFixture f;
    const json report = f.get("/api/reports/R950001").body;
    CHECK(report["type"] == "found" && report["status"] == "open" && report["brand"].is_null());
    CHECK(f.get("/api/reports/R999999").status == 404);
    CHECK(code(f.get("/api/reports/abc")) == "invalid_id");
    CHECK(code(f.get("/api/reports/r900001")) == "invalid_id");

    const json categories = f.get("/api/categories").body["items"];
    CHECK(categories.size() == 25);  // Distinct categories in synthetic_v1/reports.csv.
    CHECK(categories[0]["name"] == "Bag" && categories[0]["count"] == 1);

    const json users = f.get("/api/users").body["items"];
    CHECK(users.size() == 4 && users[0] == json({{"id", "U001"}, {"name", "Synthetic Reporter A"}}));
    CHECK(!users[0].contains("contact"));  // Contact details are not exposed.
}

TEST_CASE(testApiCreateUser, "API: register a user with validation") {
    ApiFixture f;
    const ApiResponse created = f.post("/api/users", R"({"name":"Api Student","contact":"api@example.edu"})");
    CHECK(created.status == 201 && created.body["id"] == "U005" && created.body["persisted"] == true);
    CHECK(fs::exists(f.dir / "store" / "users.csv"));
    CHECK(code(f.post("/api/users", R"({"contact":"x"})")) == "invalid_request");
    CHECK(code(f.post("/api/users", R"({"name":42})")) == "invalid_request");
    CHECK(code(f.post("/api/users", R"({"name":"Ok Name","admin":true})")) == "invalid_request");
    CHECK(code(f.post("/api/users", R"({"name":"X"})")) == "validation_failed");  // Domain: >= 2 chars.
    CHECK(code(f.post("/api/users", "not json")) == "invalid_json");
    CHECK(code(f.post("/api/users", "[1,2]")) == "invalid_json");
    CHECK(f.post("/api/users", R"({"name":"Ok Name"})", "text/plain").status == 415);
    CHECK(f.post("/api/users", R"({"name":")" + std::string(ApiHandlers::kMaxBodyBytes, 'a') + R"("})").status == 413);
    CHECK(f.get("/api/users").body["items"].size() == 5);  // Only the valid one was added.
}

TEST_CASE(testApiCreateReport, "API: create lost and found reports with validation") {
    ApiFixture f;
    const ApiResponse lost = f.post("/api/reports", validReport("lost"));
    CHECK(lost.status == 201 && lost.body["id"] == "R950033");
    CHECK(lost.body["type"] == "lost" && lost.body["origin"] == "app" && lost.body["persisted"] == true);
    CHECK(lost.body["brand"].is_null() && lost.body["heldAt"].is_null());
    const ApiResponse found = f.post("/api/reports", validReport("found", R"(,"brand":"Fastrack","heldAt":"Front desk")"));
    CHECK(found.status == 201 && found.body["heldAt"] == "Front desk" && found.body["brand"] == "Fastrack");

    CHECK(code(f.post("/api/reports", validReport("lost", R"(,"heldAt":"Desk")"))) == "invalid_request");
    CHECK(code(f.post("/api/reports", validReport("stolen"))) == "invalid_request");
    CHECK(code(f.post("/api/reports", validReport("lost", R"(,"id":"R000001")"))) == "invalid_request");
    CHECK(code(f.post("/api/reports", validReport("lost", R"(,"brand":7)"))) == "invalid_request");
    CHECK(code(f.post("/api/reports", R"({"type":"lost"})")) == "invalid_request");
    std::string body = validReport("lost");
    body.replace(body.find("Black leather wallet with a student card"), 40, "short");
    CHECK(code(f.post("/api/reports", body)) == "validation_failed");
    body = validReport("lost");
    body.replace(body.find("2026-03-15"), 10, "2026-02-30");
    CHECK(code(f.post("/api/reports", body)) == "validation_failed");
    body = validReport("lost");
    body.replace(body.find("U001"), 4, "U999");
    const ApiResponse unknownUser = f.post("/api/reports", body);
    CHECK(code(unknownUser) == "validation_failed");
    CHECK(unknownUser.body["error"]["message"].get<std::string>().find("Unknown reporter") != std::string::npos);

    CHECK(f.get("/api/stats").body["totalReports"] == 57);  // Only the two valid reports.
}

TEST_CASE(testApiMatches, "API: top matches with scores, labels and errors") {
    ApiFixture f;
    const ApiResponse response = f.get("/api/reports/R900001/matches");
    CHECK(response.status == 200);
    const json& body = response.body;
    CHECK(body["lost"]["id"] == "R900001");
    CHECK(body["scoreNote"].get<std::string>().find("not probabilities") != std::string::npos);
    CHECK(body["matches"].size() == 3 && body["emptyReason"].is_null());
    CHECK(body["matches"][0]["rank"] == 1 && body["matches"][0]["found"]["id"] == "R950001");
    CHECK(body["matches"][0]["score"].get<double>() >= body["matches"][1]["score"].get<double>());
    CHECK(body["matches"][0]["components"].contains("text"));
    CHECK(body["matches"][0]["explanations"].size() == 4);

    CHECK(f.get("/api/reports/R900001/matches", {{"k", "1"}}).body["matches"].size() == 1);
    CHECK(code(f.get("/api/reports/R900001/matches", {{"k", "0"}})) == "invalid_query");
    CHECK(code(f.get("/api/reports/R900001/matches", {{"k", "11"}})) == "invalid_query");
    CHECK(code(f.get("/api/reports/R950001/matches")) == "not_a_lost_report");
    CHECK(f.get("/api/reports/R999999/matches").status == 404);

    const json none = f.get("/api/reports/R900020/matches").body;  // Hoodie: nothing in category.
    CHECK(none["matches"].empty() && none["emptyReason"] == "no_open_found_reports_in_category");
}

TEST_CASE(testApiConfirmMatch, "API: confirm only after persisting; conflicts and errors") {
    ApiFixture f;
    const ApiResponse ok = f.post("/api/matches", R"({"lostId":"R900001","foundId":"R950001"})");
    CHECK(ok.status == 200 && ok.body["persisted"] == true);
    CHECK(ok.body["lost"]["status"] == "matched" && ok.body["found"]["status"] == "matched");
    CHECK(ok.body["confirmedOn"].get<std::string>().size() == 10);

    const ApiResponse repeat = f.post("/api/matches", R"({"lostId":"R900001","foundId":"R950001"})");
    CHECK(repeat.status == 409 && code(repeat) == "conflict");
    CHECK(f.post("/api/matches", R"({"lostId":"R900002","foundId":"R950001"})").status == 409);
    CHECK(code(f.get("/api/reports/R900001/matches")) == "not_open");
    CHECK(f.post("/api/matches", R"({"lostId":"R900002","foundId":"R999999"})").status == 404);
    CHECK(code(f.post("/api/matches", R"({"lostId":"R950003","foundId":"R900002"})")) == "validation_failed");
    CHECK(code(f.post("/api/matches", R"({"lostId":"nope","foundId":"R950003"})")) == "invalid_id");
    CHECK(code(f.post("/api/matches", R"({"lostId":"R900002"})")) == "invalid_request");
    CHECK(f.get("/api/stats").body["confirmedMatches"] == 1);

    // Restart on the same files: the confirmation and statuses are restored.
    f.start();
    CHECK(f.get("/api/reports/R900001").body["status"] == "matched");
    CHECK(f.get("/api/stats").body["confirmedMatches"] == 1);
}

TEST_CASE(testApiFailedWrite, "API: a failed write returns 500 and changes nothing") {
    ApiFixture f;
    fs::create_directories(f.dir / "store" / "matches.csv");  // Appending will fail.
    fs::create_directories(f.dir / "store" / "reports.csv");
    const ApiResponse confirm = f.post("/api/matches", R"({"lostId":"R900001","foundId":"R950001"})");
    CHECK(confirm.status == 500 && code(confirm) == "internal_error");
    CHECK(confirm.body["error"]["message"].get<std::string>().find(f.dir.string()) == std::string::npos);
    CHECK(f.get("/api/reports/R900001").body["status"] == "open");
    CHECK(f.post("/api/reports", validReport("lost")).status == 500);
    CHECK(f.get("/api/stats").body["totalReports"] == 55);
    CHECK(f.log.size() == 2 && f.log[0].find("internal error") != std::string::npos);  // Details logged.
}

TEST_CASE(testApiSampleMode, "API: sample mode works but reports persisted = false") {
    std::random_device device;
    const fs::path dir = fs::temp_directory_path() / ("findit_api_sample_" + std::to_string(device()));
    AppOptions options;
    options.dataDir = dir.string();
    options.sampleOnly = true;
    AppContext context(options, MatchingConfig{});
    context.load();
    ApiHandlers api(context);
    CHECK(api.handle({"GET", "/api/health", {}, "", ""}).body["sampleData"] == true);
    const ApiResponse created = api.handle({"POST", "/api/users", {}, "application/json", R"({"name":"Temp User"})"});
    CHECK(created.status == 201 && created.body["persisted"] == false);
    CHECK(!fs::exists(dir));  // Nothing written anywhere.
}
