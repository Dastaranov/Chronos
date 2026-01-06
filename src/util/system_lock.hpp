#pragma once

#include <string>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace chrono_util {

class SystemLock {
public:
    explicit SystemLock(const std::string& lock_file_path) : lock_file_path_(lock_file_path), fd_(-1) {
        fd_ = open(lock_file_path_.c_str(), O_RDWR | O_CREAT, 0666);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open lock file: " + lock_file_path_ + " (" + std::strerror(errno) + ")");
        }

        if (flock(fd_, LOCK_EX | LOCK_NB) < 0) {
            close(fd_);
            throw std::runtime_error("Another instance is already running (locked " + lock_file_path_ + ")");
        }
    }

    ~SystemLock() {
        if (fd_ >= 0) {
            flock(fd_, LOCK_UN);
            close(fd_);
            // We don't unlink (delete) the file because another process might have opened it 
            // and be waiting on the lock, or we might race with a new process creation.
            // Leaving the file is standard practice for lockfiles.
        }
    }

    // Prevent copying
    SystemLock(const SystemLock&) = delete;
    SystemLock& operator=(const SystemLock&) = delete;

private:
    std::string lock_file_path_;
    int fd_;
};

} // namespace chrono_util
