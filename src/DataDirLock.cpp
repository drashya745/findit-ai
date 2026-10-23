#include "findit/DataDirLock.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <sys/file.h>
#include <unistd.h>

namespace findit {

namespace {

std::string utcNow() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

// Reads the informational owner record (best effort; empty if unreadable).
std::string readOwner(int fd) {
    char buffer[512];
    const ssize_t count = ::pread(fd, buffer, sizeof buffer - 1, 0);
    if (count <= 0) {
        return "";
    }
    buffer[count] = '\0';
    std::string text(buffer);
    for (char& c : text) {
        if (c == '\n') {
            c = ' ';
        }
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

}  // namespace

DataDirLock::DataDirLock(const std::string& storeDir, const std::string& programName)
    : path_((std::filesystem::path(storeDir) / kFileName).string()) {
    std::filesystem::create_directories(storeDir);
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("Cannot open lock file " + path_ + ": " + std::strerror(errno));
    }
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        const std::string owner = readOwner(fd_);
        ::close(fd_);
        fd_ = -1;
        if (error == EWOULDBLOCK) {
            throw DataDirLockedError(
                "The data directory is in use by another FindIt process (" +
                (owner.empty() ? std::string("owner unknown") : owner) + "; lock file " + path_ +
                "). Stop that process first; run only one of findit / findit_server per data "
                "directory. The lock is released automatically when that process exits.");
        }
        throw std::runtime_error("Cannot lock " + path_ + ": " + std::strerror(error));
    }
    // We own the lock: replace any leftover owner record from an earlier, exited process.
    const std::string record = "pid=" + std::to_string(::getpid()) + "\nprogram=" + programName +
                               "\nsince=" + utcNow() + "\n";
    if (::ftruncate(fd_, 0) != 0 ||
        ::pwrite(fd_, record.data(), record.size(), 0) != static_cast<ssize_t>(record.size())) {
        // The lock itself is held; only the informational record failed. Keep going.
    }
}

DataDirLock::~DataDirLock() {
    if (fd_ >= 0) {
        (void)::ftruncate(fd_, 0);  // Clear the owner record; the file itself stays.
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}

}  // namespace findit
