// FindIt AI command-line interface.
// Loads the processed dataset (or fictional sample data), lets users report lost items and
// register found items, and suggests ranked, explained matches.

#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "findit/AppContext.h"
#include "findit/Validation.h"

using findit::AppContext;
using findit::ItemReport;
using findit::MatchResult;
using findit::ReportInput;
using findit::ReportService;
using findit::ReportType;
using findit::validation::trim;

namespace {

constexpr std::size_t kPageSize = 20;

// CLI-only presentation state; data, paths and persistence live in AppContext.
struct AppState {
    std::size_t topK = 3;
    bool pythonNoticeShown = false;
};

// Returns false on end of input so the menu can exit cleanly.
bool readLine(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
    return static_cast<bool>(std::getline(std::cin, out));
}

// Pages through long lists so 30,000 reports are never dumped at once.
bool printReports(const std::vector<const ItemReport*>& reports) {
    if (reports.empty()) {
        std::cout << "(no reports)\n";
        return true;
    }
    std::cout << "ID       TYPE   STATUS   CATEGORY        COLOUR   LOCATION          DATE\n";
    for (std::size_t i = 0; i < reports.size(); ++i) {
        std::cout << reports[i]->summary() << '\n';  // Virtual dispatch through the base pointer.
        const std::size_t shown = i + 1;
        if (shown % kPageSize == 0 && shown < reports.size()) {
            std::string answer;
            if (!readLine("-- " + std::to_string(shown) + " of " + std::to_string(reports.size()) +
                              " shown. Enter for more, q to stop: ",
                          answer)) {
                return false;
            }
            if (trim(answer) == "q") {
                break;
            }
        }
    }
    return true;
}

bool listReports(const ReportService& service) {
    std::string choice;
    if (!readLine("Show [a]ll, [l]ost, [f]ound, or [n]ew (created in this app)? ", choice)) {
        return false;
    }
    choice = findit::validation::toLower(trim(choice));
    std::vector<const ItemReport*> reports;
    if (choice == "l") {
        reports = service.listReports(ReportType::Lost);
    } else if (choice == "f") {
        reports = service.listReports(ReportType::Found);
    } else if (choice == "n") {
        for (const ItemReport* report : service.listReports()) {
            if (report->sourceRef().empty()) {  // Imported reports keep their dataset item_id.
                reports.push_back(report);
            }
        }
    } else if (choice == "a" || choice.empty()) {
        reports = service.listReports();
    } else {
        std::cout << "Unknown choice '" << choice << "'.\n";
        return true;
    }
    return printReports(reports);
}

void printUsers(const ReportService& service) {
    const auto users = service.listUsers();
    for (std::size_t i = 0; i < users.size() && i < 50; ++i) {
        std::cout << "  " << users[i]->toString() << '\n';
    }
    if (users.size() > 50) {
        std::cout << "  ... and " << users.size() - 50 << " more\n";
    }
}

bool registerUser(AppContext& context) {
    std::string name;
    std::string contact;
    if (!readLine("Full name: ", name) || !readLine("Contact (email/phone, Enter to skip): ", contact)) {
        return false;
    }
    try {
        const std::string id = context.registerUser(name, contact);
        std::cout << "Registered " << id << ". Use this ID when reporting items.\n";
    } catch (const std::exception& error) {
        std::cout << "Error: " << error.what() << "\nUser not registered.\n";
    }
    return true;
}

// Returns false if input ended mid-way.
bool createReport(AppContext& context, ReportType type) {
    const ReportService& service = context.service();
    std::cout << "\nRegistered users (register a new one with option 7):\n";
    printUsers(service);

    ReportInput input;
    std::string heldAt;
    if (!readLine("Your user ID (e.g. U001): ", input.reporterId) ||
        !readLine("Category (e.g. Wallet): ", input.category) ||
        !readLine("Description (at least 10 characters): ", input.description) ||
        !readLine("Colour: ", input.color) ||
        !readLine("Brand (Enter if unknown): ", input.brand) ||
        !readLine("Location: ", input.location) ||
        !readLine(type == ReportType::Lost ? "Date lost (YYYY-MM-DD): " : "Date found (YYYY-MM-DD): ",
                  input.date)) {
        return false;
    }
    if (type == ReportType::Found && !readLine("Where is it held now? (Enter to skip): ", heldAt)) {
        return false;
    }

    try {
        const std::string id = context.submitReport(type, input, heldAt);
        if (context.persistent()) {
            std::cout << "Saved " << id << " to " << context.paths().reports << ".\n";
        } else {
            std::cout << "Created " << id << " (sample mode: not saved).\n";
        }
        if (type == ReportType::Lost) {
            std::cout << "Tip: choose option 5 with " << id << " to look for matches.\n";
        }
    } catch (const std::exception& error) {
        std::cout << "Error: " << error.what() << "\nReport not created; please try again.\n";
    }
    return true;
}

bool viewReport(const ReportService& service) {
    std::string id;
    if (!readLine("Report ID (e.g. R000001): ", id)) {
        return false;
    }
    try {
        std::cout << '\n' << service.getReport(id).details();
    } catch (const std::exception& error) {
        std::cout << "Error: " << error.what() << '\n';
    }
    return true;
}

void printMatchSettings(const findit::MatchEngine& engine, const findit::MatchingConfig& config) {
    const auto& w = config.weights;
    std::cout << std::fixed << std::setprecision(2) << "Matcher: " << engine.strategyName()
              << " | weights text " << w.text << ", location " << w.location << ", category "
              << w.category << ", colour " << w.colour << " | category blocking "
              << (engine.categoryBlocking() ? "ON" : "OFF") << '\n';
    std::cout.unsetf(std::ios::fixed);
}

void printResult(const ReportService& service, const MatchResult& result) {
    const ItemReport& found = service.getReport(result.foundId());
    std::cout << result.toString() << "\n    found: " << found.summary() << "\n    \""
              << found.description() << "\"\n\n";
}

void showPythonNotice(const findit::MatchingConfig& config, const ReportService& service,
                      AppState& state) {
    if (config.textEngine == findit::TextEngine::Python && !state.pythonNoticeShown) {
        std::cout << "(First match request: starting the Python matcher and fitting TF-IDF on "
                  << service.listReports().size() << " reports, once; this takes a few seconds.)\n";
        state.pythonNoticeShown = true;
    }
}

bool findMatches(const ReportService& service, const findit::MatchEngine& engine,
                 const findit::MatchingConfig& config, AppState& state) {
    std::string id;
    if (!readLine("Lost report ID (e.g. R000001): ", id)) {
        return false;
    }
    try {
        const ItemReport& lost = service.getReport(id);
        if (lost.type() != ReportType::Lost) {
            std::cout << "Error: " << lost.id() << " is a found report; enter a lost report ID.\n";
            return true;
        }
        if (lost.status() != findit::ReportStatus::Open) {
            std::cout << lost.id() << " is already " << findit::toString(lost.status())
                      << "; it no longer needs suggestions.\n";
            return true;
        }
        showPythonNotice(config, service, state);
        const auto results = service.suggestMatches(id, state.topK);
        std::cout << '\n';
        printMatchSettings(engine, config);
        std::cout << "Lost: " << lost.summary() << "\n      \"" << lost.description() << "\"\n\n";
        if (results.empty()) {
            std::cout << "No matching found reports yet.";
            if (engine.categoryBlocking()) {
                std::cout << " Only '" << lost.category()
                          << "' items are compared; turn category blocking off in Settings (9)"
                             " to search all categories.";
            }
            std::cout << "\n";
            return true;
        }
        std::cout << "Top " << results.size() << " candidate(s):\n\n";
        for (const auto& result : results) {
            printResult(service, result);
        }
        std::cout << "Scores rank candidates against each other; they are not probabilities.\n"
                     "Use option 6 to see why any specific found report does or doesn't match.\n";
    } catch (const std::exception& error) {
        std::cout << "Error: " << error.what() << '\n';
    }
    return true;
}

bool explainMatch(const ReportService& service, const findit::MatchingConfig& config,
                  AppState& state) {
    std::string lostId;
    std::string foundId;
    if (!readLine("Lost report ID: ", lostId) || !readLine("Found report ID: ", foundId)) {
        return false;
    }
    try {
        showPythonNotice(config, service, state);
        bool considered = false;
        const MatchResult result = service.explainMatch(lostId, foundId, considered);
        std::cout << "\nLost:  " << service.getReport(lostId).summary() << "\n       \""
                  << service.getReport(lostId).description() << "\"\n";
        printResult(service, result);
        if (!considered) {
            std::cout << "Note: this pair is NOT included in option 5's suggestions (a report is not "
                         "OPEN, or category blocking excludes it).\n";
        }
    } catch (const std::exception& error) {
        std::cout << "Error: " << error.what() << '\n';
    }
    return true;
}

// Shows the pair's score and reasons, asks for confirmation, then marks both MATCHED and
// appends the decision to data/store/matches.csv.
bool confirmMatch(AppContext& context, AppState& state) {
    const ReportService& service = context.service();
    const findit::MatchingConfig& config = context.config();
    std::string lostId;
    std::string foundId;
    if (!readLine("Lost report ID: ", lostId) || !readLine("Found report ID: ", foundId)) {
        return false;
    }
    lostId = trim(lostId);
    foundId = trim(foundId);
    try {
        const ItemReport& lost = service.getReport(lostId);
        const ItemReport& found = service.getReport(foundId);
        if (lost.type() != ReportType::Lost || found.type() != ReportType::Found) {
            std::cout << "Error: enter a LOST report ID first and a FOUND report ID second.\n";
            return true;
        }
        for (const ItemReport* report : {&lost, &found}) {
            if (report->status() != findit::ReportStatus::Open) {
                std::cout << "Error: " << report->id() << " is already "
                          << findit::toString(report->status()) << "; it cannot be confirmed again.\n";
                return true;
            }
        }
        showPythonNotice(config, service, state);
        bool considered = false;
        const MatchResult result = service.explainMatch(lostId, foundId, considered);
        std::cout << "\nLost:  " << lost.summary() << "\n       \"" << lost.description() << "\"\n";
        printResult(service, result);
        std::string answer;
        if (!readLine("Confirm that these describe the same item? [y/N]: ", answer)) {
            return false;
        }
        if (findit::validation::toLower(trim(answer)) != "y") {
            std::cout << "Not confirmed; nothing changed.\n";
            return true;
        }
        context.confirmMatch(lostId, foundId);  // Saved first; statuses change only if that worked.
        if (context.persistent()) {
            std::cout << "Confirmed: " << lost.id() << " and " << found.id()
                      << " are now MATCHED (saved to " << context.paths().matches << ").\n";
        } else {
            std::cout << "Confirmed: " << lost.id() << " and " << found.id()
                      << " are now MATCHED (sample mode: not saved).\n";
        }
    } catch (const std::exception& error) {
        std::cout << "Error: " << error.what() << '\n';
    }
    return true;
}

bool settings(AppContext& context) {
    findit::MatchEngine& engine = context.engine();
    const findit::MatchingConfig& config = context.config();
    const ReportService& service = context.service();
    std::cout << "\nData source : " << context.dataSource() << "\n"
              << "Saving to   : "
              << (context.persistent() ? context.paths().reports
                                       : std::string("(sample mode: nothing is saved)"))
              << "\nReports     : " << service.listReports().size() << " ("
              << service.listReports(ReportType::Lost).size() << " lost, "
              << service.listReports(ReportType::Found).size() << " found)\n";
    printMatchSettings(engine, config);
    std::string answer;
    if (!readLine("Toggle category blocking? [y/N]: ", answer)) {
        return false;
    }
    if (findit::validation::toLower(trim(answer)) == "y") {
        engine.setCategoryBlocking(!engine.categoryBlocking());
        std::cout << "Category blocking is now " << (engine.categoryBlocking() ? "ON" : "OFF") << ".\n";
    }
    return true;
}

void printMenu() {
    std::cout << "\n=== FindIt AI - Campus Lost & Found ===\n"
                 "1. Report a lost item\n"
                 "2. Register a found item\n"
                 "3. List reports\n"
                 "4. View a report\n"
                 "5. Find matches for a lost report (top 3)\n"
                 "6. Explain a specific lost/found match\n"
                 "7. Register a new user\n"
                 "8. List users\n"
                 "9. Settings and status\n"
                 "10. Confirm a match (mark lost and found as MATCHED)\n"
                 "0. Exit\n";
}

bool parseArgs(int argc, char** argv, findit::AppOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sample") {
            options.sampleOnly = true;
        } else if (arg == "--data-dir" && i + 1 < argc) {
            options.dataDir = argv[++i];
        } else if (arg == "--matching-config" && i + 1 < argc) {
            options.matchingConfig = argv[++i];
        } else if (arg == "--text-engine" && i + 1 < argc) {
            options.textEngine = argv[++i];
        } else {
            std::cerr << "Usage: findit [--sample] [--data-dir DIR] [--matching-config PATH]"
                         " [--text-engine python|keyword]\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    findit::AppOptions options;
    if (!parseArgs(argc, argv, options)) {
        return 2;
    }

    findit::MatchingConfig matchingConfig;
    try {
        std::string note;
        matchingConfig = AppContext::loadMatchingConfig(options.matchingConfig, note);
        if (!note.empty()) {
            std::cout << note << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Invalid matching config: " << error.what() << '\n';
        return 1;
    }
    if (!AppContext::applyTextEngine(options.textEngine, matchingConfig)) {
        std::cerr << "--text-engine must be python or keyword\n";
        return 2;
    }

    AppContext context(options, matchingConfig, [](const std::string& reason) {
        std::cout << "\n[!] Python matcher unavailable: " << reason
                  << "\n    Using the keyword baseline for the rest of this session.\n";
    });
    ReportService& service = context.service();
    findit::MatchEngine& engine = context.engine();
    const findit::MatchingConfig& config = context.config();
    AppState state;
    state.topK = config.topK;

    try {
        const findit::LoadSummary loaded = context.load();
        if (loaded.usedSampleData) {
            if (!options.sampleOnly) {
                std::cout << "No processed dataset in " << options.dataDir
                          << "/processed (run ./build/findit_ingest first). Using sample data.\n";
            }
        } else {
            std::cout << "Loaded " << loaded.users << " users, " << loaded.reports << " reports ("
                      << loaded.storeReports << " created in this app) and " << loaded.matches
                      << " confirmed match(es) from " << options.dataDir << "/\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Failed to load data: " << error.what() << '\n';
        return 1;
    }

    std::string choice;
    while (true) {
        printMenu();
        if (!readLine("Choose an option: ", choice)) {
            break;
        }
        choice = trim(choice);
        bool keepGoing = true;
        if (choice == "1") {
            keepGoing = createReport(context, ReportType::Lost);
        } else if (choice == "2") {
            keepGoing = createReport(context, ReportType::Found);
        } else if (choice == "3") {
            keepGoing = listReports(service);
        } else if (choice == "4") {
            keepGoing = viewReport(service);
        } else if (choice == "5") {
            keepGoing = findMatches(service, engine, config, state);
        } else if (choice == "6") {
            keepGoing = explainMatch(service, config, state);
        } else if (choice == "7") {
            keepGoing = registerUser(context);
        } else if (choice == "8") {
            printUsers(service);
        } else if (choice == "9") {
            keepGoing = settings(context);
        } else if (choice == "10") {
            keepGoing = confirmMatch(context, state);
        } else if (choice == "0") {
            break;
        } else {
            std::cout << "Unknown option '" << choice << "'. Enter a number from 0 to 10.\n";
        }
        if (!keepGoing) {
            break;
        }
    }
    std::cout << "\nGoodbye.\n";
    return 0;
}
