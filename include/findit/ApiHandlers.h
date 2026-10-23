#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "findit/AppContext.h"

namespace findit {

// Transport-independent HTTP request/response, so the whole API can be unit-tested without
// sockets. tools/findit_server.cpp converts cpp-httplib requests to and from these.
struct ApiRequest {
    std::string method;  // "GET", "POST", ...
    std::string path;    // e.g. "/api/reports/R000001/matches" (no query string)
    std::multimap<std::string, std::string> query;
    std::string contentType;
    std::string body;
};

struct ApiResponse {
    int status = 200;
    nlohmann::json body;
};

// JSON API over AppContext. Every rule lives in ReportService / AppContext; this class only
// routes, validates request shapes, converts domain objects to JSON and maps exceptions to
// status codes. Errors always have the form {"error": {"code": ..., "message": ...}}.
// Not thread-safe (AppContext is not); the server serializes calls.
class ApiHandlers {
public:
    static constexpr std::size_t kMaxBodyBytes = 16 * 1024;
    static constexpr std::size_t kMaxListLimit = 100;
    static constexpr std::size_t kMaxMatches = 10;
    static constexpr const char* kScoreNote =
        "Scores are relative ranking values between 0 and 1 for comparing candidates; "
        "they are not probabilities.";

    using Logger = std::function<void(const std::string&)>;

    explicit ApiHandlers(AppContext& context, Logger logger = nullptr);

    // Never throws: every failure becomes an error response.
    ApiResponse handle(const ApiRequest& request);

private:
    ApiResponse dispatch(const ApiRequest& request);
    ApiResponse health() const;
    ApiResponse stats() const;
    ApiResponse listReports(const ApiRequest& request) const;
    ApiResponse getReport(const std::string& id) const;
    ApiResponse listCategories() const;
    ApiResponse listUsers() const;
    ApiResponse createUser(const ApiRequest& request);
    ApiResponse createReport(const ApiRequest& request);
    ApiResponse findMatches(const std::string& id, const ApiRequest& request) const;
    ApiResponse confirmMatch(const ApiRequest& request);

    AppContext& context_;
    Logger logger_;
};

}  // namespace findit
