#pragma once

#include "Commands.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/poll.h>

namespace omarchy::sony {

struct ClientSession {
    int fd{-1};
    uint64_t id{0};
    bool busy{false};
    std::string inBuffer;
    std::string outBuffer;
    std::chrono::steady_clock::time_point connectedAt;
};

using IpcReply = std::function<void(std::string reply)>;

struct IpcCallbacks {
    std::function<void(const cmd::Command& command, IpcReply done)> execute;
};

class IpcServer {
public:
    explicit IpcServer(std::string socketPath = "");
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    [[nodiscard]] static std::string resolveSocketPath(const std::string& overridePath = "");

    bool start();
    void stop();
    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    void setCallbacks(IpcCallbacks callbacks) { callbacks_ = std::move(callbacks); }

    [[nodiscard]] const std::string& getSocketPath() const noexcept { return actualSocketPath_; }
    [[nodiscard]] int getListenFd() const noexcept { return listenFd_; }
    [[nodiscard]] size_t getClientCount() const noexcept { return clients_.size(); }

    void handleCommandLine(const std::string& line, IpcReply done);
    [[nodiscard]] std::optional<std::string> handleCommandLineSync(const std::string& line);

    void appendPollFds(std::vector<struct pollfd>& pfds) const;
    void handleSocketEvent(int fd, short revents);
    void handlePollEvents(std::span<const struct pollfd> pfds);
    int pollOnce(int timeoutMs = 0);

private:
    void acceptClients();
    void handleClientRead(int clientFd);
    void handleClientWrite(int clientFd);
    void drainClient(int clientFd);
    void completeClient(int clientFd, uint64_t id, const std::string& reply);
    void closeClient(int clientFd);
    bool queueResponse(ClientSession& session, const std::string& response);
    bool flushClientOutBuffer(ClientSession& session);

    std::string socketPathConfig_;
    std::string actualSocketPath_;
    int listenFd_{-1};
    bool running_{false};
    size_t maxLineLength_{4096};
    size_t maxBuffered_{65536};
    uint64_t nextClientId_{1};
    int drainingFd_{-1};

    IpcCallbacks callbacks_;
    std::unordered_map<int, ClientSession> clients_;
};

} // namespace omarchy::sony
