#include "findit/ApiHandlers.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "findit/FoundReport.h"
#include "findit/SimilarityStrategy.h"
#include "findit/Validation.h"

namespace findit {

using nlohmann::json;

namespace {

constexpr std::size_t kMaxQueryLength = 100;
constexpr std::size_t kMaxQueryWords = 10;

// A request-shape problem with a fixed status and code.
class ApiError : public std::runtime_error {
public:
    ApiError(int status, std::string code, const std::string& message)
        : std::runtime_error(message), status_(status), code_(std::move(code)) {}
    int status() const noexcept { return status_; }
    const std::string& code() const noexcept { return code_; }

private:
    int status_;
    std::string code_;
};

ApiResponse errorResponse(int status, const std::string& code, const std::string& message) {
    return {status, {{"error", {{"code", code}, {"message", message}}}}};
}

std::string lower(const std::string& text) {
    return validation::toLower(text);
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

std::string requireReportId(const std::string& id) {
    if (!validation::isValidReportId(id)) {
        throw ApiError(400, "invalid_id", "'" + id + "' is not a report ID (expected R followed by 6 digits)");
    }
    return id;
}

json reportJson(const ItemReport& report) {
    const auto* found = dynamic_cast<const FoundReport*>(&report);
    json heldAt = nullptr;
    if (found != nullptr && !found->heldAt().empty()) {
        heldAt = found->heldAt();
    }
    return {{"id", report.id()},
            {"type", lower(toString(report.type()))},
            {"status", lower(toString(report.status()))},
            {"category", report.category()},
            {"description", report.description()},
            {"color", report.color()},
            {"brand", report.brand() ? json(*report.brand()) : json(nullptr)},
            {"location", report.location()},
            {"date", report.date().toString()},
            {"reporterId", report.reporterId()},
            {"heldAt", heldAt},
            // Imported dataset rows keep their source item_id; reports made in the app have none.
            {"origin", report.sourceRef().empty() ? "app" : "dataset"},
            {"sourceRef", report.sourceRef().empty() ? json(nullptr) : json(report.sourceRef())}};
}

// ----------------------------------------------------------------- query parameters

class QueryParams {
public:
    QueryParams(const std::multimap<std::string, std::string>& query, std::set<std::string> allowed) {
        for (const auto& entry : query) {
            if (allowed.count(entry.first) == 0) {
                throw ApiError(400, "invalid_query", "Unknown query parameter '" + entry.first + "'");
            }
            if (!values_.emplace(entry.first, entry.second).second) {
                throw ApiError(400, "invalid_query", "Query parameter '" + entry.first + "' given more than once");
            }
        }
    }

    std::string text(const std::string& name, std::size_t maxLength = kMaxQueryLength) const {
        const auto it = values_.find(name);
        if (it == values_.end()) {
            return "";
        }
        if (it->second.size() > maxLength) {
            throw ApiError(400, "invalid_query", "'" + name + "' must be at most " +
                                                     std::to_string(maxLength) + " characters");
        }
        return validation::trim(it->second);
    }

    std::string choice(const std::string& name, const std::set<std::string>& options) const {
        const std::string value = lower(text(name));
        if (!value.empty() && options.count(value) == 0) {
            std::string list;
            for (const auto& option : options) {
                list += (list.empty() ? "" : ", ") + option;
            }
            throw ApiError(400, "invalid_query", "'" + name + "' must be one of: " + list);
        }
        return value;
    }

    std::size_t integer(const std::string& name, std::size_t fallback, std::size_t min, std::size_t max) const {
        const std::string value = text(name, 10);
        if (value.empty()) {
            return fallback;
        }
        if (!std::all_of(value.begin(), value.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            throw ApiError(400, "invalid_query", "'" + name + "' must be a whole number");
        }
        const unsigned long long number = std::stoull(value);
        if (number < min || number > max) {
            throw ApiError(400, "invalid_query", "'" + name + "' must be between " + std::to_string(min) +
                                                     " and " + std::to_string(max));
        }
        return static_cast<std::size_t>(number);
    }

private:
    std::map<std::string, std::string> values_;
};

// ----------------------------------------------------------------- JSON bodies

class JsonBody {
public:
    JsonBody(const ApiRequest& request, std::set<std::string> allowed) {
        if (lower(request.contentType).rfind("application/json", 0) != 0) {
            throw ApiError(415, "unsupported_media_type", "Send the request body as application/json");
        }
        if (request.body.size() > ApiHandlers::kMaxBodyBytes) {
            throw ApiError(413, "payload_too_large", "Request body must be at most " +
                                                         std::to_string(ApiHandlers::kMaxBodyBytes) + " bytes");
        }
        body_ = json::parse(request.body, nullptr, false);
        if (body_.is_discarded()) {
            throw ApiError(400, "invalid_json", "Request body is not valid JSON");
        }
        if (!body_.is_object()) {
            throw ApiError(400, "invalid_json", "Request body must be a JSON object");
        }
        for (const auto& item : body_.items()) {
            if (allowed.count(item.key()) == 0) {
                throw ApiError(400, "invalid_request", "Unknown field '" + item.key() + "'");
            }
        }
    }

    // Absent or null -> "". Any non-string value is rejected.
    std::string optionalString(const std::string& name) const {
        const auto it = body_.find(name);
        if (it == body_.end() || it->is_null()) {
            return "";
        }
        if (!it->is_string()) {
            throw ApiError(400, "invalid_request", "'" + name + "' must be a string");
        }
        return it->get<std::string>();
    }

    std::string requiredString(const std::string& name) const {
        const std::string value = optionalString(name);
        if (validation::trim(value).empty()) {
            throw ApiError(400, "invalid_request", "'" + name + "' is required");
        }
        return value;
    }

private:
    json body_;
};

}  // namespace

// ----------------------------------------------------------------- dispatch

ApiHandlers::ApiHandlers(AppContext& context, Logger logger)
    : context_(context), logger_(std::move(logger)) {}

ApiResponse ApiHandlers::handle(const ApiRequest& request) {
    try {
        return dispatch(request);
    } catch (const ApiError& e) {
        return errorResponse(e.status(), e.code(), e.what());
    } catch (const std::out_of_range& e) {
        return errorResponse(404, "not_found", e.what());
    } catch (const std::invalid_argument& e) {
        return errorResponse(400, "validation_failed", e.what());
    } catch (const std::logic_error& e) {
        return errorResponse(409, "conflict", e.what());
    } catch (const StrategyUnavailableError& e) {
        if (logger_) logger_(std::string("matcher unavailable: ") + e.what());
        return errorResponse(503, "matcher_unavailable",
                             "The matching engine is unavailable; see the server log for details");
    } catch (const std::exception& e) {
        // Typically a failed write to data/store. AppContext guarantees nothing changed.
        if (logger_) logger_(std::string("internal error: ") + e.what());
        return errorResponse(500, "internal_error",
                             "The server could not complete the request; nothing was changed");
    }
}

ApiResponse ApiHandlers::dispatch(const ApiRequest& request) {
    const std::vector<std::string> parts = splitPath(request.path);
    const std::string& method = request.method;
    const auto only = [&method](const std::string& allowed) {
        if (method != allowed) {
            throw ApiError(405, "method_not_allowed", "Use " + allowed + " for this endpoint");
        }
    };
    const auto noQuery = [&request]() { QueryParams(request.query, {}); };

    if (parts.size() < 2 || parts[0] != "api" || request.path.back() == '/') {
        throw ApiError(404, "not_found", "No such endpoint: " + request.path);
    }
    const std::string& resource = parts[1];
    if (parts.size() == 2) {
        if (resource == "health") { only("GET"); noQuery(); return health(); }
        if (resource == "stats") { only("GET"); noQuery(); return stats(); }
        if (resource == "categories") { only("GET"); noQuery(); return listCategories(); }
        if (resource == "users") {
            if (method == "GET") { noQuery(); return listUsers(); }
            only("POST"); noQuery(); return createUser(request);
        }
        if (resource == "reports") {
            if (method == "GET") return listReports(request);
            only("POST"); noQuery(); return createReport(request);
        }
        if (resource == "matches") { only("POST"); noQuery(); return confirmMatch(request); }
    }
    if (resource == "reports" && parts.size() == 3) {
        only("GET"); noQuery(); return getReport(requireReportId(parts[2]));
    }
    if (resource == "reports" && parts.size() == 4 && parts[3] == "matches") {
        only("GET"); return findMatches(requireReportId(parts[2]), request);
    }
    throw ApiError(404, "not_found", "No such endpoint: " + request.path);
}

// ----------------------------------------------------------------- read endpoints

ApiResponse ApiHandlers::health() const {
    const MatchingConfig& config = context_.config();
    // No file-system paths here: they would expose local details (e.g. the user name) to clients.
    return {200,
            {{"status", "ok"},
             {"dataMode", context_.persistent() ? "dataset" : "sample"},
             {"persistent", context_.persistent()},
             {"sampleData", !context_.persistent()},
             {"matcher",
              {{"strategy", context_.engine().strategyName()},
               {"textEngine", config.textEngine == TextEngine::Python ? "python" : "keyword"},
               {"fallbackActive", context_.usingFallback()},
               {"categoryBlocking", context_.engine().categoryBlocking()}}}}};
}

ApiResponse ApiHandlers::stats() const {
    std::size_t lost = 0, found = 0, openLost = 0, openFound = 0, matchedLost = 0, fromApp = 0;
    for (const ItemReport* report : context_.service().listReports()) {
        const bool isLost = report->type() == ReportType::Lost;
        (isLost ? lost : found)++;
        if (report->status() == ReportStatus::Open) {
            (isLost ? openLost : openFound)++;
        }
        if (isLost && report->status() == ReportStatus::Matched) {
            ++matchedLost;  // Each confirmation matches exactly one lost report.
        }
        if (report->sourceRef().empty()) {
            ++fromApp;
        }
    }
    return {200,
            {{"totalReports", lost + found},
             {"lostReports", lost},
             {"foundReports", found},
             {"openLostReports", openLost},
             {"openFoundReports", openFound},
             {"confirmedMatches", matchedLost},
             {"reportsCreatedInApp", fromApp}}};
}

ApiResponse ApiHandlers::listReports(const ApiRequest& request) const {
    const QueryParams params(request.query,
                             {"q", "type", "status", "category", "origin", "sort", "limit", "offset"});
    const std::string q = lower(params.text("q"));
    const std::string type = params.choice("type", {"lost", "found"});
    const std::string status = params.choice("status", {"open", "matched", "closed"});
    const std::string category = lower(params.text("category", validation::kShortTextMaxLength));
    const std::string origin = params.choice("origin", {"dataset", "app"});
    const std::string sort = params.choice("sort", {"recent", "id"});
    const std::size_t limit = params.integer("limit", 20, 1, kMaxListLimit);
    const std::size_t offset = params.integer("offset", 0, 0, 10'000'000);

    std::vector<std::string> words;
    {
        std::stringstream stream(q);
        std::string word;
        while (stream >> word) {
            words.push_back(word);
        }
        if (words.size() > kMaxQueryWords) {
            throw ApiError(400, "invalid_query", "'q' may contain at most " + std::to_string(kMaxQueryWords) + " words");
        }
    }

    std::vector<const ItemReport*> matches;
    for (const ItemReport* report : context_.service().listReports()) {
        if ((!type.empty() && lower(toString(report->type())) != type) ||
            (!status.empty() && lower(toString(report->status())) != status) ||
            (!category.empty() && lower(report->category()) != category) ||
            (!origin.empty() && (report->sourceRef().empty() ? "app" : "dataset") != origin)) {
            continue;
        }
        if (!words.empty()) {
            const std::string haystack =
                lower(report->id() + ' ' + report->description() + ' ' + report->category() + ' ' +
                      report->color() + ' ' + report->location() + ' ' + report->brand().value_or(""));
            if (!std::all_of(words.begin(), words.end(), [&haystack](const std::string& w) {
                    return haystack.find(w) != std::string::npos;
                })) {
                continue;
            }
        }
        matches.push_back(report);
    }
    if (sort != "id") {  // Default "recent": newest date first, then newest ID.
        std::stable_sort(matches.begin(), matches.end(), [](const ItemReport* a, const ItemReport* b) {
            if (!(a->date() == b->date())) {
                return b->date() < a->date();
            }
            return a->id() > b->id();
        });
    }
    json items = json::array();
    for (std::size_t i = offset; i < matches.size() && i < offset + limit; ++i) {
        items.push_back(reportJson(*matches[i]));
    }
    return {200, {{"items", items}, {"total", matches.size()}, {"limit", limit}, {"offset", offset}}};
}

ApiResponse ApiHandlers::getReport(const std::string& id) const {
    return {200, reportJson(context_.service().getReport(id))};
}

ApiResponse ApiHandlers::listCategories() const {
    std::map<std::string, std::pair<std::string, std::size_t>> byKey;  // lower -> (display, count)
    for (const ItemReport* report : context_.service().listReports()) {
        auto& entry = byKey[lower(report->category())];
        if (entry.first.empty()) {
            entry.first = report->category();
        }
        ++entry.second;
    }
    json items = json::array();
    for (const auto& entry : byKey) {
        items.push_back({{"name", entry.second.first}, {"count", entry.second.second}});
    }
    return {200, {{"items", items}}};
}

ApiResponse ApiHandlers::listUsers() const {
    json items = json::array();
    for (const User* user : context_.service().listUsers()) {
        items.push_back({{"id", user->id()}, {"name", user->name()}});  // Contact details omitted.
    }
    return {200, {{"items", items}}};
}

ApiResponse ApiHandlers::findMatches(const std::string& id, const ApiRequest& request) const {
    const QueryParams params(request.query, {"k"});
    const std::size_t k = params.integer("k", 3, 1, kMaxMatches);
    const ItemReport& lost = context_.service().getReport(id);
    if (lost.type() != ReportType::Lost) {
        throw ApiError(400, "not_a_lost_report", id + " is a found report; matches are searched for lost reports");
    }
    if (lost.status() != ReportStatus::Open) {
        throw ApiError(409, "not_open", id + " is already " + toString(lost.status()) + "; it no longer needs matches");
    }
    const std::vector<MatchResult> results = context_.service().suggestMatches(id, k);
    json matches = json::array();
    for (const MatchResult& result : results) {
        matches.push_back({{"rank", result.rank()},
                           {"score", result.score()},
                           {"components", result.components()},
                           {"explanations", result.explanations()},
                           {"found", reportJson(context_.service().getReport(result.foundId()))}});
    }
    json emptyReason = nullptr;
    if (results.empty()) {
        emptyReason = context_.engine().categoryBlocking() ? "no_open_found_reports_in_category"
                                                           : "no_open_found_reports";
    }
    return {200,
            {{"lost", reportJson(lost)},
             {"matcher",
              {{"strategy", context_.engine().strategyName()},
               {"fallbackActive", context_.usingFallback()},
               {"categoryBlocking", context_.engine().categoryBlocking()}}},
             {"scoreNote", kScoreNote},
             {"matches", matches},
             {"emptyReason", emptyReason}}};
}

// ----------------------------------------------------------------- write endpoints

ApiResponse ApiHandlers::createUser(const ApiRequest& request) {
    const JsonBody body(request, {"name", "contact"});
    const std::string id = context_.registerUser(body.requiredString("name"), body.optionalString("contact"));
    for (const User* user : context_.service().listUsers()) {
        if (user->id() == id) {
            return {201, {{"id", user->id()}, {"name", user->name()}, {"persisted", context_.persistent()}}};
        }
    }
    throw std::logic_error("registered user not found");  // Unreachable.
}

ApiResponse ApiHandlers::createReport(const ApiRequest& request) {
    const JsonBody body(request, {"type", "reporterId", "category", "description", "color", "brand",
                                  "location", "date", "heldAt"});
    const std::string type = lower(validation::trim(body.requiredString("type")));
    if (type != "lost" && type != "found") {
        throw ApiError(400, "invalid_request", "'type' must be 'lost' or 'found'");
    }
    const std::string heldAt = body.optionalString("heldAt");
    if (type == "lost" && !validation::trim(heldAt).empty()) {
        throw ApiError(400, "invalid_request", "'heldAt' applies only to found reports");
    }
    ReportInput input;
    input.reporterId = body.requiredString("reporterId");
    input.category = body.requiredString("category");
    input.description = body.requiredString("description");
    input.color = body.requiredString("color");
    input.brand = body.optionalString("brand");
    input.location = body.requiredString("location");
    input.date = body.requiredString("date");
    // Domain validation (lengths, date, known reporter) happens in ReportService.
    const std::string id = context_.submitReport(type == "lost" ? ReportType::Lost : ReportType::Found, input, heldAt);
    json report = reportJson(context_.service().getReport(id));
    report["persisted"] = context_.persistent();
    return {201, report};
}

ApiResponse ApiHandlers::confirmMatch(const ApiRequest& request) {
    const JsonBody body(request, {"lostId", "foundId"});
    const std::string lostId = requireReportId(validation::trim(body.requiredString("lostId")));
    const std::string foundId = requireReportId(validation::trim(body.requiredString("foundId")));
    // Validates, appends to matches.csv, and only then changes both statuses.
    context_.confirmMatch(lostId, foundId);
    return {200,
            {{"lost", reportJson(context_.service().getReport(lostId))},
             {"found", reportJson(context_.service().getReport(foundId))},
             {"confirmedOn", AppContext::todayIso()},
             {"persisted", context_.persistent()}}};
}

}  // namespace findit
