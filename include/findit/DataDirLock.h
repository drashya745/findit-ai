#pragma once

#include <stdexcept>
#include <string>

namespace findit {

// Thrown when another live process holds the lock. The message names the owner.
class DataDirLockedError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Exclusive, process-wide lock on a FindIt data directory's store, so the CLI and the HTTP
// server never load and append to the same data/store/ at the same time.
//
// Design:
// - The lock is a kernel advisory lock (flock, LOCK_EX | LOCK_NB) on `<store>/.findit.lock`.
//   The kernel releases it automatically when the owning process exits for any reason,
//   including a crash, so a leftover file is never "stale" in a way that blocks anyone.
// - The file's contents (pid, program, start time) are informational, used for messages only.
//   Ownership is decided solely by flock, never by the file existing or by the pid in it.
// - The file is never deleted (unlinking a lock file lets two processes lock different inodes).
//   On release the contents are cleared and the flock is dropped.
// - The descriptor is opened with O_CLOEXEC so child processes (the Python matcher) never
//   inherit, and therefore never prolong, the lock.
// Local file systems only: flock is not reliable on network shares.
class DataDirLock {
public:
    // Throws DataDirLockedError if another process holds the lock, std::runtime_error if the
    // lock file cannot be created or opened.
    DataDirLock(const std::string& storeDir, const std::string& programName);
    ~DataDirLock();
    DataDirLock(const DataDirLock&) = delete;
    DataDirLock& operator=(const DataDirLock&) = delete;

    const std::string& path() const noexcept { return path_; }

    static constexpr const char* kFileName = ".findit.lock";

private:
    std::string path_;
    int fd_ = -1;
};

}  // namespace findit
