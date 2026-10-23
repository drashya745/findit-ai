// findit_eval: runs the integrated matching pipeline (MatchEngine + strategy) over every lost
// report in a canonical dataset and records (a) every scored candidate with its components
// and (b) runtime. It computes no quality metrics; it never sees labels.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "findit/AppContext.h"
#include "findit/Csv.h"
#include "findit/KeywordBaselineStrategy.h"
#include "findit/MatchEngine.h"
#include "findit/MatchingConfig.h"
#include "findit/PythonTfidfStrategy.h"
#include "findit/ReportCsvStore.h"
#include "findit/ReportRepository.h"

namespace fs = std::filesystem;
using namespace findit;
using Clock = std::chrono::steady_clock;

namespace {

struct Options {
    std::string reports;
    std::string users;
    std::string datasetName = "dataset";
    std::string rankingsOut;  // Optional.
    std::string runtimeOut;   // Optional; appended to.
    std::string matchingConfig = "config/matching.conf";
    std::vector<std::string> strategies = {"keyword", "python"};
    std::vector<bool> blocking = {true, false};
    std::size_t lostSample = 0;  // 0 = all lost reports.
    unsigned seed = 42;
};

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> items;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

void usage() {
    std::cerr << "Usage: findit_eval --reports CSV --users CSV [--dataset-name NAME]\n"
                 "                   [--rankings-out CSV] [--runtime-out CSV]\n"
                 "                   [--strategies keyword,python] [--blocking on,off]\n"
                 "                   [--lost-sample N --seed S] [--matching-config PATH]\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) {
            return false;
        }
        const std::string value = argv[++i];
        if (arg == "--reports") o.reports = value;
        else if (arg == "--users") o.users = value;
        else if (arg == "--dataset-name") o.datasetName = value;
        else if (arg == "--rankings-out") o.rankingsOut = value;
        else if (arg == "--runtime-out") o.runtimeOut = value;
        else if (arg == "--matching-config") o.matchingConfig = value;
        else if (arg == "--strategies") o.strategies = splitList(value);
        else if (arg == "--lost-sample") o.lostSample = static_cast<std::size_t>(std::stoul(value));
        else if (arg == "--seed") o.seed = static_cast<unsigned>(std::stoul(value));
        else if (arg == "--blocking") {
            o.blocking.clear();
            for (const auto& item : splitList(value)) {
                if (item != "on" && item != "off") {
                    return false;
                }
                o.blocking.push_back(item == "on");
            }
        } else {
            return false;
        }
    }
    return !o.reports.empty() && !o.users.empty() && !o.strategies.empty() && !o.blocking.empty();
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(rank, 1) - 1];  // Nearest-rank method.
}

std::string fixed(double value, int digits) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(digits) << value;
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        usage();
        return 2;
    }
    try {
        const MatchingConfig config = MatchingConfig::loadFile(options.matchingConfig);
        ReportRepository repository;
        persistence::loadUsers(options.users, repository);
        persistence::loadReports(options.reports, repository);

        std::vector<const LostReport*> lostReports;
        for (const ItemReport* report : repository.byType(ReportType::Lost)) {
            const auto* lost = dynamic_cast<const LostReport*>(report);
            if (lost != nullptr && lost->status() == ReportStatus::Open) {
                lostReports.push_back(lost);
            }
        }
        if (options.lostSample > 0 && options.lostSample < lostReports.size()) {
            std::mt19937 generator(options.seed);
            std::shuffle(lostReports.begin(), lostReports.end(), generator);
            lostReports.resize(options.lostSample);
            std::sort(lostReports.begin(), lostReports.end(),
                      [](const LostReport* a, const LostReport* b) { return a->id() < b->id(); });
        }
        const std::vector<const FoundReport*> found = repository.openFoundReports();
        const std::size_t topK = std::max<std::size_t>(found.size(), 1);  // Keep every scored pair.

        std::ofstream rankings;
        if (!options.rankingsOut.empty()) {
            rankings.open(options.rankingsOut, std::ios::binary | std::ios::trunc);
            csv::writeRow(rankings, {"dataset", "strategy", "category_blocking", "lost_id", "rank",
                                     "found_id", "score", "text", "location", "category", "colour"});
        }
        std::ofstream runtime;
        if (!options.runtimeOut.empty()) {
            const bool isNew = !fs::exists(options.runtimeOut);
            runtime.open(options.runtimeOut, std::ios::binary | std::ios::app);
            if (isNew) {
                csv::writeRow(runtime, {"dataset", "strategy", "category_blocking", "queries",
                                        "found_pool", "mean_candidates", "fit_ms", "total_query_ms",
                                        "mean_query_ms", "p50_query_ms", "p95_query_ms",
                                        "max_query_ms"});
            }
        }

        for (const std::string& strategyName : options.strategies) {
            for (const bool blocking : options.blocking) {
                std::unique_ptr<SimilarityStrategy> strategy;
                PythonTfidfStrategy* python = nullptr;
                if (strategyName == "keyword") {
                    strategy = std::make_unique<KeywordBaselineStrategy>(config);
                } else if (strategyName == "python") {
                    PythonProcessOptions processOptions;
                    processOptions.executable = config.python.executable;
                    processOptions.modulePath = config.python.modulePath;
                    // Same rule as the app; findit_eval always used the default data/ folder.
                    processOptions.logPath = AppContext::pythonLogPath(config, "data");
                    processOptions.timeoutSeconds = config.python.timeoutSeconds;
                    auto pythonStrategy = std::make_unique<PythonTfidfStrategy>(
                        config, processOptions, [&repository]() { return repository.all(); });
                    python = pythonStrategy.get();
                    strategy = std::move(pythonStrategy);  // No fallback: evaluation must fail loudly.
                } else {
                    throw std::invalid_argument("Unknown strategy '" + strategyName + "'");
                }
                const std::string label = strategy->name();
                MatchEngine engine(std::move(strategy), 0.0);
                engine.setCategoryBlocking(blocking);
                engine.setDateWindowDays(config.dateWindowDays);

                double fitMs = 0.0;
                if (python != nullptr) {
                    const auto start = Clock::now();
                    python->warmUp();  // Start process + one fit, measured separately from queries.
                    fitMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                }

                std::vector<double> queryMs;
                std::size_t candidateTotal = 0;
                for (const LostReport* lost : lostReports) {
                    const auto start = Clock::now();
                    const std::vector<MatchResult> results = engine.findMatches(*lost, found, topK);
                    queryMs.push_back(
                        std::chrono::duration<double, std::milli>(Clock::now() - start).count());
                    candidateTotal += results.size();
                    if (rankings.is_open()) {
                        for (const MatchResult& r : results) {
                            const auto& c = r.components();
                            csv::writeRow(rankings,
                                          {options.datasetName, label, blocking ? "on" : "off",
                                           lost->id(), std::to_string(r.rank()), r.foundId(),
                                           fixed(r.score(), 10), fixed(c.at("text"), 10),
                                           fixed(c.at("location"), 10), fixed(c.at("category"), 10),
                                           fixed(c.at("colour"), 10)});
                        }
                    }
                }

                double total = 0.0;
                for (const double ms : queryMs) {
                    total += ms;
                }
                const double queries = static_cast<double>(std::max<std::size_t>(queryMs.size(), 1));
                if (runtime.is_open()) {
                    csv::writeRow(runtime,
                                  {options.datasetName, label, blocking ? "on" : "off",
                                   std::to_string(queryMs.size()), std::to_string(found.size()),
                                   fixed(static_cast<double>(candidateTotal) / queries, 1),
                                   fixed(fitMs, 1), fixed(total, 1), fixed(total / queries, 3),
                                   fixed(percentile(queryMs, 0.50), 3),
                                   fixed(percentile(queryMs, 0.95), 3),
                                   fixed(percentile(queryMs, 1.0), 3)});
                }
                std::cout << options.datasetName << " | " << label << " | blocking "
                          << (blocking ? "on " : "off") << " | " << queryMs.size()
                          << " queries | fit " << fixed(fitMs, 0) << " ms | mean query "
                          << fixed(total / queries, 2) << " ms\n";
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "findit_eval: " << error.what() << '\n';
        return 1;
    }
}
