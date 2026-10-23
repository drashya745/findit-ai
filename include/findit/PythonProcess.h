#pragma once

#include <string>
#include <sys/types.h>

#include "findit/SimilarityStrategy.h"

namespace findit {

// Any failure talking to the Python matcher. `code` is stable for tests and logs:
// spawn_failed, missing_dependency, timeout, process_exited, io_error, protocol_error,
// python_error.
class PythonMatcherError : public StrategyUnavailableError {
public:
    PythonMatcherError(std::string code, const std::string& message)
        : StrategyUnavailableError(message), code_(std::move(code)) {}
    const std::string& code() const noexcept { return code_; }

private:
    std::string code_;
};

struct PythonProcessOptions {
    std::string executable = ".venv/bin/python";  // Path, or a name looked up on PATH.
    std::string modulePath = "ml";                // Becomes PYTHONPATH (made absolute).
    std::string module = "findit_ml.service";
    std::string logPath;                          // Child's stderr; empty = inherit.
    int timeoutSeconds = 120;
};

// A child Python process speaking JSON Lines over stdin/stdout.
// Started with posix_spawnp and an argv array: no shell is involved, so no value is ever
// interpreted as shell syntax. RAII: the destructor stops the process.
class PythonProcess {
public:
    explicit PythonProcess(PythonProcessOptions options);
    ~PythonProcess();
    PythonProcess(const PythonProcess&) = delete;
    PythonProcess& operator=(const PythonProcess&) = delete;

    void start();  // Throws PythonMatcherError("spawn_failed").
    bool running() const noexcept { return pid_ > 0; }
    void writeLine(const std::string& line);
    // Waits up to the configured timeout for one complete line.
    std::string readLine();
    void stop() noexcept;

    // Last part of the child's stderr log, for error messages.
    std::string logTail(std::size_t maxBytes = 1500) const;
    const PythonProcessOptions& options() const noexcept { return options_; }

private:
    bool reap(int timeoutMs) noexcept;  // True once the child has exited and been reaped.
    std::string exitDescription();

    PythonProcessOptions options_;
    pid_t pid_ = -1;
    int toChild_ = -1;
    int fromChild_ = -1;
    int exitStatus_ = 0;
    bool exited_ = false;
    std::string buffer_;
};

}  // namespace findit
