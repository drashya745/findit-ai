// Data-directory lock: exclusivity, owner reporting, stale records, crash release, no
// inheritance by child processes, AppContext integration, and Python log path resolution.

#include <csignal>
#include <filesystem>
#include <fstream>
#include <random>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

#include "TestFramework.h"
#include "TestHelpers.h"
#include "findit/AppContext.h"
#include "findit/DataDirLock.h"

extern char** environ;

namespace fs = std::filesystem;
using namespace findit;

namespace {

fs::path tempDir() {
    std::random_device device;
    const fs::path dir = fs::temp_directory_path() / ("findit_lock_" + std::to_string(device()));
    fs::create_directories(dir);
    return dir;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string lockMessage(const std::string& storeDir) {
    try {
        DataDirLock lock(storeDir, "second");
    } catch (const DataDirLockedError& error) {
        return error.what();
    }
    return "";
}

}  // namespace

TEST_CASE(testLockExclusiveAndReleased, "DataDirLock: exclusive, names the owner, released on destruction") {
    const fs::path dir = tempDir();
    const std::string store = (dir / "store").string();
    {
        DataDirLock first(store, "test-owner");
        CHECK(fs::exists(first.path()));
        CHECK(readFile(first.path()).find("pid=" + std::to_string(::getpid())) != std::string::npos);
        const std::string message = lockMessage(store);
        CHECK(message.find("in use by another FindIt process") != std::string::npos);
        CHECK(message.find("program=test-owner") != std::string::npos);
        CHECK_THROWS_AS(DataDirLock(store, "third"), DataDirLockedError);
    }
    CHECK(fs::exists(dir / "store" / DataDirLock::kFileName));  // Never deleted...
    CHECK(readFile(dir / "store" / DataDirLock::kFileName).empty());  // ...only cleared.
    DataDirLock again(store, "after-release");  // Free again.
    CHECK(readFile(again.path()).find("program=after-release") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE(testLockStaleRecord, "DataDirLock: a leftover owner record without a live lock is not a lock") {
    const fs::path dir = tempDir();
    fs::create_directories(dir / "store");
    // As if a process crashed: the file still names it, but nobody holds the flock.
    std::ofstream(dir / "store" / DataDirLock::kFileName) << "pid=999999\nprogram=crashed\n";
    DataDirLock lock((dir / "store").string(), "recovered");
    const std::string record = readFile(lock.path());
    CHECK(record.find("program=recovered") != std::string::npos);
    CHECK(record.find("crashed") == std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE(testLockCrashAndNoInheritance, "DataDirLock: crash releases it; child processes do not inherit it") {
    const fs::path dir = tempDir();
    const std::string store = (dir / "store").string();
    int pipeFd[2];
    CHECK(::pipe(pipeFd) == 0);
    const pid_t owner = ::fork();
    if (owner == 0) {
        // Child: take the lock, start a long-lived grandchild (like the Python matcher),
        // report its pid, then die without running destructors (simulated crash).
        ::close(pipeFd[0]);
        auto* lock = new DataDirLock(store, "crashing-owner");
        (void)lock;
        pid_t sleeper = -1;
        char arg0[] = "/bin/sleep";
        char arg1[] = "30";
        char* argv[] = {arg0, arg1, nullptr};
        ::posix_spawn(&sleeper, "/bin/sleep", nullptr, nullptr, argv, environ);
        (void)!::write(pipeFd[1], &sleeper, sizeof sleeper);
        ::_exit(0);
    }
    ::close(pipeFd[1]);
    pid_t sleeper = -1;
    CHECK(::read(pipeFd[0], &sleeper, sizeof sleeper) == static_cast<ssize_t>(sizeof sleeper));
    ::close(pipeFd[0]);
    int status = 0;
    ::waitpid(owner, &status, 0);

    CHECK(sleeper > 0 && ::kill(sleeper, 0) == 0);  // The grandchild is still running...
    bool acquired = false;
    try {
        DataDirLock lock(store, "after-crash");  // ...yet the lock is free.
        acquired = true;
    } catch (const std::exception&) {
    }
    CHECK(acquired);
    if (sleeper > 0) {
        ::kill(sleeper, SIGTERM);
        ::waitpid(sleeper, &status, 0);
    }
    fs::remove_all(dir);
}

TEST_CASE(testContextUsesLock, "AppContext: the lock blocks a second instance on the same data directory") {
    const fs::path dir = tempDir();
    fs::create_directories(dir / "processed");
    for (const char* file : {"users.csv", "reports.csv"}) {
        fs::copy_file(testhelpers::fixturePath(std::string("evaluation/synthetic_v1/") + file),
                      dir / "processed" / file);
    }
    AppOptions options;
    options.dataDir = dir.string();
    options.programName = "first-instance";
    {
        AppContext first(options, MatchingConfig{});
        first.load();
        CHECK(first.holdsLock());
        AppContext second(options, MatchingConfig{});
        std::string message;
        try {
            second.load();
        } catch (const DataDirLockedError& error) {
            message = error.what();
        }
        CHECK(message.find("program=first-instance") != std::string::npos);
        CHECK(second.service().listReports().empty());  // Nothing was loaded.

        AppOptions sample = options;
        sample.sampleOnly = true;  // Sample mode saves nothing, so it needs no lock.
        AppContext sampleContext(sample, MatchingConfig{});
        CHECK(sampleContext.load().usedSampleData && !sampleContext.holdsLock());
    }
    AppContext third(options, MatchingConfig{});
    CHECK(!third.load().usedSampleData && third.holdsLock());
    fs::remove_all(dir);
}

TEST_CASE(testPythonLogPath, "AppContext: Python log path stays inside the chosen data directory") {
    MatchingConfig config;
    CHECK(AppContext::pythonLogPath(config, "data") == "data/store/python_matcher.log");
    CHECK(AppContext::pythonLogPath(config, "/tmp/custom") == "/tmp/custom/store/python_matcher.log");
    config.python.logFile = "/var/tmp/findit.log";
    CHECK(AppContext::pythonLogPath(config, "/tmp/custom") == "/var/tmp/findit.log");
    config.python.logFile = "";
    CHECK(AppContext::pythonLogPath(config, "/tmp/custom").empty());
    const MatchingConfig project = MatchingConfig::loadFile(testhelpers::fixturePath("config/matching.conf"));
    CHECK(AppContext::pythonLogPath(project, "data") == "data/store/python_matcher.log");  // Unchanged default.
}
