#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "findit/DataDirLock.h"
#include "findit/ItemReport.h"
#include "findit/MatchEngine.h"
#include "findit/MatchingConfig.h"
#include "findit/ReportRepository.h"
#include "findit/ReportService.h"
#include "findit/Types.h"

namespace findit {

// Command-line style options shared by every front end (CLI now, HTTP server later).
struct AppOptions {
    bool sampleOnly = false;
    std::string dataDir = "data";
    std::string matchingConfig = "config/matching.conf";
    std::string textEngine;  // Empty = use the config file; else "python" or "keyword".
    std::string programName = "findit";  // Recorded in the data-directory lock file.
};

class FallbackStrategy;

struct LoadSummary {
    std::size_t users = 0;
    std::size_t reports = 0;       // Including those created in the app.
    std::size_t storeReports = 0;  // Created in the app (data/store/reports.csv).
    std::size_t matches = 0;       // Confirmations replayed from data/store/matches.csv.
    bool usedSampleData = false;
};

// Where user-created data is persisted. All empty in sample mode (nothing is saved).
struct StorePaths {
    std::string users;
    std::string reports;
    std::string matches;
};

// Owns one application instance: config, repository, match engine and service, plus the
// load-and-persist steps every front end needs. Each persisted operation follows
// "validate -> append to data/store -> apply in memory", so a failed write changes nothing.
// Not thread-safe; a multi-threaded front end must serialize calls.
class AppContext {
public:
    using FallbackNotifier = std::function<void(const std::string& reason)>;

    // Loads the matching config, or the built-in defaults if the file does not exist
    // (`note` then explains). Throws std::exception if the file exists but is invalid.
    static MatchingConfig loadMatchingConfig(const std::string& path, std::string& note);
    // Applies a --text-engine value ("", "python", "keyword"). Returns false if invalid.
    static bool applyTextEngine(const std::string& value, MatchingConfig& config);
    // Where the Python matcher's stderr goes: an absolute python.log_file is used as given; a
    // relative one is placed inside <dataDir>/store/, so a custom --data-dir never writes into
    // the project's default data/ folder. Empty stays empty (inherit stderr).
    static std::string pythonLogPath(const MatchingConfig& config, const std::string& dataDir);

    // Builds the matcher (Python TF-IDF with keyword fallback, or keyword only) without
    // loading data. `onFallback` is called once if Python becomes unavailable.
    AppContext(AppOptions options, MatchingConfig config, FallbackNotifier onFallback = nullptr);
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    // Loads data/processed + data/store and replays confirmed matches; seeds fictional sample
    // data instead when --sample is set or no processed dataset exists. Throws on bad files.
    // In persistent mode it first takes the data-directory lock (DataDirLockedError if another
    // findit / findit_server uses the same directory) and holds it until destruction.
    LoadSummary load();

    // Persisted operations. Return the new ID. Throw the domain's exceptions on invalid input
    // (nothing changes) and std::runtime_error if writing fails (nothing changes either).
    std::string registerUser(const std::string& name, const std::string& contact);
    std::string submitReport(ReportType type, const ReportInput& input, const std::string& heldAt);
    // Both reports MATCHED only after the confirmation has been appended to matches.csv.
    void confirmMatch(const std::string& lostId, const std::string& foundId);

    bool persistent() const noexcept { return !paths_.reports.empty(); }
    const StorePaths& paths() const noexcept { return paths_; }
    const std::string& dataSource() const noexcept { return dataSource_; }
    const AppOptions& options() const noexcept { return options_; }
    const MatchingConfig& config() const noexcept { return config_; }
    ReportService& service() noexcept { return service_; }
    const ReportService& service() const noexcept { return service_; }
    MatchEngine& engine() noexcept { return engine_; }
    const MatchEngine& engine() const noexcept { return engine_; }
    // True once Python failed and the keyword baseline took over for this session.
    bool usingFallback() const noexcept;
    bool holdsLock() const noexcept { return lock_ != nullptr; }

    // Today's local date as YYYY-MM-DD (recorded with each confirmation).
    static std::string todayIso();

private:
    // Declared first so it is destroyed last: the lock outlives everything that uses the store.
    std::unique_ptr<DataDirLock> lock_;
    AppOptions options_;
    MatchingConfig config_;
    // Declaration order matters: the engine's strategy reads the repository, and the service
    // refers to both, so they are constructed in this order and destroyed in reverse.
    ReportRepository repository_;
    const FallbackStrategy* fallback_ = nullptr;  // Owned by engine_; null if no fallback.
    MatchEngine engine_;
    ReportService service_;
    StorePaths paths_;
    std::string dataSource_;
};

}  // namespace findit
