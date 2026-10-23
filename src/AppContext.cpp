#include "findit/AppContext.h"

#include <ctime>
#include <filesystem>
#include <memory>

#include "findit/KeywordBaselineStrategy.h"
#include "findit/PythonTfidfStrategy.h"
#include "findit/ReportCsvStore.h"
#include "findit/SampleData.h"

namespace fs = std::filesystem;

namespace findit {

namespace {

// Python text scoring with a keyword fallback, or the keyword baseline alone.
// `fallbackOut` receives the FallbackStrategy (owned by the returned pointer), if any.
std::unique_ptr<SimilarityStrategy> makeStrategy(const MatchingConfig& config,
                                                 const std::string& dataDir,
                                                 const ReportRepository& repository,
                                                 AppContext::FallbackNotifier onFallback,
                                                 const FallbackStrategy*& fallbackOut) {
    auto keyword = std::make_unique<KeywordBaselineStrategy>(config);
    if (config.textEngine == TextEngine::Keyword) {
        return keyword;
    }
    PythonProcessOptions options;
    options.executable = config.python.executable;
    options.modulePath = config.python.modulePath;
    options.logPath = AppContext::pythonLogPath(config, dataDir);
    options.timeoutSeconds = config.python.timeoutSeconds;
    auto python = std::make_unique<PythonTfidfStrategy>(
        config, options, [&repository]() { return repository.all(); });
    if (!config.python.fallbackToKeyword) {
        return python;
    }
    auto fallback = std::make_unique<FallbackStrategy>(std::move(python), std::move(keyword),
                                                       std::move(onFallback));
    fallbackOut = fallback.get();
    return fallback;
}

}  // namespace

MatchingConfig AppContext::loadMatchingConfig(const std::string& path, std::string& note) {
    note.clear();
    if (fs::exists(path)) {
        return MatchingConfig::loadFile(path);
    }
    note = "Note: " + path + " not found; using built-in defaults (keyword matcher).";
    return MatchingConfig{};
}

bool AppContext::applyTextEngine(const std::string& value, MatchingConfig& config) {
    if (value == "python") {
        config.textEngine = TextEngine::Python;
    } else if (value == "keyword") {
        config.textEngine = TextEngine::Keyword;
    } else if (!value.empty()) {
        return false;
    }
    return true;
}

std::string AppContext::pythonLogPath(const MatchingConfig& config, const std::string& dataDir) {
    const fs::path configured(config.python.logFile);
    if (configured.empty() || configured.is_absolute()) {
        return configured.string();
    }
    return (fs::path(dataDir) / "store" / configured).string();
}

bool AppContext::usingFallback() const noexcept {
    return fallback_ != nullptr && fallback_->usingFallback();
}

AppContext::AppContext(AppOptions options, MatchingConfig config, FallbackNotifier onFallback)
    : options_(std::move(options)),
      config_(std::move(config)),
      engine_(makeStrategy(config_, options_.dataDir, repository_, std::move(onFallback), fallback_),
              config_.threshold),
      service_(repository_, engine_) {
    engine_.setCategoryBlocking(config_.categoryBlocking);
    engine_.setDateWindowDays(config_.dateWindowDays);
}

LoadSummary AppContext::load() {
    LoadSummary summary;
    const fs::path dir(options_.dataDir);
    const fs::path users = dir / "processed" / "users.csv";
    const fs::path reports = dir / "processed" / "reports.csv";
    if (options_.sampleOnly || !fs::exists(users) || !fs::exists(reports)) {
        seedSampleData(service_);
        dataSource_ = "built-in fictional sample data";
        summary.usedSampleData = true;
        summary.users = service_.listUsers().size();
        summary.reports = service_.listReports().size();
        return summary;
    }

    const fs::path storeUsers = dir / "store" / "users.csv";
    const fs::path storeReports = dir / "store" / "reports.csv";
    const fs::path storeMatches = dir / "store" / "matches.csv";
    // Lock before reading anything from the store, so we never load a half-written state.
    lock_ = std::make_unique<DataDirLock>((dir / "store").string(), options_.programName);
    summary.users = persistence::loadUsers(users.string(), repository_);
    if (fs::exists(storeUsers)) {
        summary.users += persistence::loadUsers(storeUsers.string(), repository_);
    }
    summary.reports = persistence::loadReports(reports.string(), repository_);
    if (fs::exists(storeReports)) {
        summary.storeReports = persistence::loadReports(storeReports.string(), repository_);
        summary.reports += summary.storeReports;
    }
    if (fs::exists(storeMatches)) {
        summary.matches = persistence::loadConfirmedMatches(
            storeMatches.string(), [this](const std::string& lostId, const std::string& foundId) {
                service_.confirmMatch(lostId, foundId);
            });
    }
    fs::create_directories(storeReports.parent_path());
    paths_ = {storeUsers.string(), storeReports.string(), storeMatches.string()};
    dataSource_ = options_.dataDir + "/processed + " + options_.dataDir + "/store";
    return summary;
}

std::string AppContext::registerUser(const std::string& name, const std::string& contact) {
    User user = service_.createUser(name, contact);  // Validates; nothing stored yet.
    if (persistent()) {
        persistence::appendUser(paths_.users, user);
    }
    std::string id = user.id();
    service_.addUser(std::move(user));
    return id;
}

std::string AppContext::submitReport(ReportType type, const ReportInput& input,
                                     const std::string& heldAt) {
    std::unique_ptr<ItemReport> report = service_.createReport(type, input, heldAt);
    if (persistent()) {
        persistence::appendReport(paths_.reports, *report);
    }
    return service_.addReport(std::move(report));
}

void AppContext::confirmMatch(const std::string& lostId, const std::string& foundId) {
    service_.validateConfirmation(lostId, foundId);
    if (persistent()) {
        persistence::appendConfirmedMatch(paths_.matches, service_.getReport(lostId).id(),
                                          service_.getReport(foundId).id(), todayIso());
    }
    service_.confirmMatch(lostId, foundId);
}

std::string AppContext::todayIso() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[11];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d", &local);
    return buffer;
}

}  // namespace findit
