#include "StateEngine.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace omarchy::sony {

std::filesystem::path StateEngine::resolveStateFilePath(const std::filesystem::path& customStatePath) {
    if (!customStatePath.empty()) {
        if (customStatePath.filename() == "status.json") {
            return customStatePath;
        }
        if (customStatePath.filename() == "sony-headphones") {
            return customStatePath / "status.json";
        }
        return customStatePath / "sony-headphones" / "status.json";
    }

    const char* xdgState = std::getenv("XDG_STATE_HOME");
    if (xdgState && *xdgState != '\0') {
        std::filesystem::path p(xdgState);
        if (p.filename() == "sony-headphones") {
            return p / "status.json";
        }
        return p / "sony-headphones" / "status.json";
    }

    const char* home = std::getenv("HOME");
    if (home && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "state" / "sony-headphones" / "status.json";
    }

    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir && *(pw->pw_dir) != '\0') {
        return std::filesystem::path(pw->pw_dir) / ".local" / "state" / "sony-headphones" / "status.json";
    }

    return std::filesystem::path("/tmp/sony-headphones/status.json");
}

bool StateEngine::ensureStateDirectory(const std::filesystem::path& dirPath) {
    std::error_code ec;
    std::filesystem::create_directories(dirPath, ec);
    if (ec) {
        std::cerr << "[StateEngine] Failed to create directories " << dirPath << ": " << ec.message() << std::endl;
        return false;
    }
    if (::chmod(dirPath.c_str(), S_IRWXU) != 0) {
        std::cerr << "[StateEngine] Failed to chmod 0700 on " << dirPath << ": " << std::strerror(errno) << std::endl;
        return false;
    }
    return true;
}

StateEngine::StateEngine(const std::filesystem::path& customStatePath)
    : stateFilePath_(resolveStateFilePath(customStatePath)),
      stateDir_(stateFilePath_.parent_path()) {}

StateEngine::~StateEngine() {
    cleanup();
}

bool StateEngine::initialize(const std::string& jsonContent) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cleanedUp_ = false;
    }
    if (!ensureStateDirectory(stateDir_)) {
        return false;
    }
    return writeAtomic(jsonContent);
}

void StateEngine::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cleanedUp_) {
        return;
    }
    cleanedUp_ = true;

    if (!stateFilePath_.empty()) {
        std::error_code ec;
        std::filesystem::remove(stateFilePath_, ec);
        std::string tmpPath = stateFilePath_.string() + ".tmp." + std::to_string(::getpid());
        std::filesystem::remove(tmpPath, ec);
    }
}

bool StateEngine::writeAtomic(const std::string& jsonContent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stateFilePath_.empty()) {
        return false;
    }
    if (!ensureStateDirectory(stateDir_)) {
        return false;
    }

    std::string tmpPath = stateFilePath_.string() + ".tmp." + std::to_string(::getpid());
    int fd = ::open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        std::cerr << "[StateEngine] Failed to open tmp file " << tmpPath << ": " << std::strerror(errno) << std::endl;
        return false;
    }

    if (::fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        std::cerr << "[StateEngine] Failed to fchmod 0600 on " << tmpPath << ": " << std::strerror(errno) << std::endl;
        ::close(fd);
        ::unlink(tmpPath.c_str());
        return false;
    }

    std::string content = jsonContent;
    if (content.empty() || content.back() != '\n') {
        content.push_back('\n');
    }

    const char* buf = content.data();
    size_t remaining = content.size();
    while (remaining > 0) {
        ssize_t written = ::write(fd, buf, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[StateEngine] Write error: " << std::strerror(errno) << std::endl;
            ::close(fd);
            ::unlink(tmpPath.c_str());
            return false;
        }
        buf += written;
        remaining -= static_cast<size_t>(written);
    }

    if (::fsync(fd) != 0) {
        std::cerr << "[StateEngine] fsync failed: " << std::strerror(errno) << std::endl;
        ::close(fd);
        ::unlink(tmpPath.c_str());
        return false;
    }

    if (::close(fd) != 0) {
        ::unlink(tmpPath.c_str());
        return false;
    }

    if (::rename(tmpPath.c_str(), stateFilePath_.c_str()) != 0) {
        std::cerr << "[StateEngine] rename failed (" << tmpPath << " -> " << stateFilePath_ << "): "
                  << std::strerror(errno) << std::endl;
        ::unlink(tmpPath.c_str());
        return false;
    }

    return true;
}

} // namespace omarchy::sony
