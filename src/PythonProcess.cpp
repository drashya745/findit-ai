#include "findit/PythonProcess.h"

#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace findit {

namespace {

void closeFd(int& fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void setCloseOnExec(int fd) {
    ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
}

// Writing to a pipe whose reader has exited must fail with EPIPE, not kill this process.
void disableSigpipe(int fd) {
#ifdef F_SETNOSIGPIPE
    ::fcntl(fd, F_SETNOSIGPIPE, 1);
#else
    (void)fd;
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

PythonProcess::PythonProcess(PythonProcessOptions options) : options_(std::move(options)) {}

PythonProcess::~PythonProcess() {
    stop();
}

void PythonProcess::start() {
    if (running()) {
        return;
    }
    if (!options_.logPath.empty()) {
        const auto parent = std::filesystem::path(options_.logPath).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
    }

    int toChild[2];
    int fromChild[2];
    if (::pipe(toChild) != 0) {
        throw PythonMatcherError("spawn_failed", std::string("pipe() failed: ") + std::strerror(errno));
    }
    if (::pipe(fromChild) != 0) {
        const int error = errno;
        ::close(toChild[0]);
        ::close(toChild[1]);
        throw PythonMatcherError("spawn_failed", std::string("pipe() failed: ") + std::strerror(error));
    }
    setCloseOnExec(toChild[1]);
    setCloseOnExec(fromChild[0]);
    disableSigpipe(toChild[1]);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, toChild[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fromChild[1], STDOUT_FILENO);
    if (!options_.logPath.empty()) {
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, options_.logPath.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    posix_spawn_file_actions_addclose(&actions, toChild[0]);
    posix_spawn_file_actions_addclose(&actions, fromChild[1]);

    // Environment: inherit, but set exactly the variables the service needs.
    std::vector<std::string> environment;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string variable(*entry);
        if (!startsWith(variable, "PYTHONPATH=") && !startsWith(variable, "PYTHONUNBUFFERED=") &&
            !startsWith(variable, "PYTHONIOENCODING=")) {
            environment.push_back(variable);
        }
    }
    environment.push_back("PYTHONPATH=" + std::filesystem::absolute(options_.modulePath).string());
    environment.push_back("PYTHONUNBUFFERED=1");
    environment.push_back("PYTHONIOENCODING=utf-8");

    // argv is passed as separate strings: values are never parsed by a shell.
    std::vector<std::string> arguments = {options_.executable, "-m", options_.module};
    std::vector<char*> argv;
    for (auto& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    std::vector<char*> envp;
    for (auto& variable : environment) {
        envp.push_back(variable.data());
    }
    envp.push_back(nullptr);

    pid_t pid = -1;
    const int result = ::posix_spawnp(&pid, options_.executable.c_str(), &actions, nullptr,
                                      argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    ::close(toChild[0]);
    ::close(fromChild[1]);
    if (result != 0) {
        ::close(toChild[1]);
        ::close(fromChild[0]);
        throw PythonMatcherError(
            "spawn_failed", "Cannot start Python interpreter '" + options_.executable +
                                "': " + std::strerror(result) +
                                ". Create the venv with: python3 -m venv .venv && "
                                ".venv/bin/pip install -r ml/requirements.txt, or set "
                                "python.executable in config/matching.conf");
    }
    pid_ = pid;
    toChild_ = toChild[1];
    fromChild_ = fromChild[0];
    exited_ = false;
    buffer_.clear();
}

void PythonProcess::writeLine(const std::string& line) {
    if (!running() || toChild_ < 0) {
        throw PythonMatcherError("io_error", "Python matcher is not running");
    }
    const std::string data = line + "\n";
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(toChild_, data.data() + offset, data.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int error = errno;
            throw PythonMatcherError("io_error", std::string("Cannot send request to Python matcher: ") +
                                                     std::strerror(error) + exitDescription());
        }
        offset += static_cast<std::size_t>(written);
    }
}

std::string PythonProcess::readLine() {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(options_.timeoutSeconds);
    while (true) {
        const auto newline = buffer_.find('\n');
        if (newline != std::string::npos) {
            std::string line = buffer_.substr(0, newline);
            buffer_.erase(0, newline + 1);
            return line;
        }
        if (fromChild_ < 0) {
            throw PythonMatcherError("io_error", "Python matcher is not running");
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) {
            throw PythonMatcherError("timeout", "No response from Python matcher within " +
                                                    std::to_string(options_.timeoutSeconds) +
                                                    " s (python.timeout_seconds)");
        }
        pollfd descriptor{fromChild_, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, static_cast<int>(remaining));
        if (ready < 0 && errno != EINTR) {
            throw PythonMatcherError("io_error", std::string("poll() failed: ") + std::strerror(errno));
        }
        if (ready <= 0) {
            continue;
        }
        char chunk[65536];
        const ssize_t count = ::read(fromChild_, chunk, sizeof chunk);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            throw PythonMatcherError("io_error", std::string("Cannot read from Python matcher: ") +
                                                     std::strerror(errno));
        }
        if (count == 0) {
            throw PythonMatcherError("process_exited",
                                     "Python matcher exited unexpectedly" + exitDescription());
        }
        buffer_.append(chunk, static_cast<std::size_t>(count));
    }
}

bool PythonProcess::reap(int timeoutMs) noexcept {
    if (pid_ <= 0) {
        return exited_;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        int status = 0;
        const pid_t result = ::waitpid(pid_, &status, WNOHANG);
        if (result == pid_ || (result < 0 && errno == ECHILD)) {
            exitStatus_ = status;
            exited_ = true;
            pid_ = -1;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::string PythonProcess::exitDescription() {
    std::string description;
    if (reap(500)) {
        if (WIFEXITED(exitStatus_)) {
            description = " (exit code " + std::to_string(WEXITSTATUS(exitStatus_)) + ")";
        } else if (WIFSIGNALED(exitStatus_)) {
            description = " (killed by signal " + std::to_string(WTERMSIG(exitStatus_)) + ")";
        }
    }
    const std::string tail = logTail();
    if (!tail.empty()) {
        description += "\nPython stderr (" + options_.logPath + "):\n" + tail;
    }
    return description;
}

void PythonProcess::stop() noexcept {
    closeFd(toChild_);  // EOF on stdin asks the service to exit cleanly.
    if (pid_ > 0 && !reap(2000)) {
        ::kill(pid_, SIGTERM);
        if (!reap(1000)) {
            ::kill(pid_, SIGKILL);
            reap(1000);
        }
    }
    closeFd(fromChild_);
    buffer_.clear();
}

std::string PythonProcess::logTail(std::size_t maxBytes) const {
    if (options_.logPath.empty()) {
        return "";
    }
    std::ifstream in(options_.logPath, std::ios::binary | std::ios::ate);
    if (!in) {
        return "";
    }
    const std::streamoff size = in.tellg();
    const std::streamoff start = size > static_cast<std::streamoff>(maxBytes)
                                     ? size - static_cast<std::streamoff>(maxBytes)
                                     : 0;
    in.seekg(start);
    std::string tail(static_cast<std::size_t>(size - start), '\0');
    in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
    while (!tail.empty() && (tail.back() == '\n' || tail.back() == '\r')) {
        tail.pop_back();
    }
    return tail;
}

}  // namespace findit
