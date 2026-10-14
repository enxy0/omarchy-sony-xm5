#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace omarchy::sony {

class StateEngine {
public:
    explicit StateEngine(const std::filesystem::path& customStatePath = "");
    ~StateEngine();

    StateEngine(const StateEngine&) = delete;
    StateEngine& operator=(const StateEngine&) = delete;
    StateEngine(StateEngine&&) = delete;
    StateEngine& operator=(StateEngine&&) = delete;

    static std::filesystem::path resolveStateFilePath(const std::filesystem::path& customStatePath = "");
    static bool ensureStateDirectory(const std::filesystem::path& dirPath);

    bool initialize(const std::string& jsonContent);
    void cleanup();

    bool writeAtomic(const std::string& jsonContent);

    [[nodiscard]] const std::filesystem::path& getStateFilePath() const noexcept { return stateFilePath_; }
    [[nodiscard]] std::string getStatusFilePath() const { return stateFilePath_.string(); }
    [[nodiscard]] const std::filesystem::path& getStateDirectory() const noexcept { return stateDir_; }

private:
    std::mutex mutex_;
    std::filesystem::path stateFilePath_;
    std::filesystem::path stateDir_;
    bool cleanedUp_{false};
};

} // namespace omarchy::sony
