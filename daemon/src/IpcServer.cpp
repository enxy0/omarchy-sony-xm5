#include "IpcServer.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>

namespace omarchy::sony {

std::string IpcServer::resolveSocketPath(const std::string& overridePath) {
    if (!overridePath.empty()) {
        std::filesystem::path p(overridePath);
        if (p.extension() == ".sock") {
            return overridePath;
        }
        return (p / "sony-headphones.sock").string();
    }

    const char* xdgRuntime = std::getenv("XDG_RUNTIME_DIR");
    if (xdgRuntime != nullptr && *xdgRuntime != '\0') {
        return std::string(xdgRuntime) + "/sony-headphones.sock";
    }
    return "/tmp/run-" + std::to_string(::getuid()) + "/sony-headphones.sock";
}

IpcServer::IpcServer(std::string socketPath)
    : socketPathConfig_(std::move(socketPath)) {
    actualSocketPath_ = resolveSocketPath(socketPathConfig_);
}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    if (running_) {
        return true;
    }
    if (actualSocketPath_.empty()) {
        actualSocketPath_ = resolveSocketPath(socketPathConfig_);
    }

    std::filesystem::path sockPath(actualSocketPath_);
    if (sockPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(sockPath.parent_path(), ec);
        if (!ec) {
            ::chmod(sockPath.parent_path().c_str(), 0700);
        }
    }

    ::unlink(actualSocketPath_.c_str());

    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) {
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (actualSocketPath_.length() >= sizeof(addr.sun_path)) {
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    std::strncpy(addr.sun_path, actualSocketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listenFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (::chmod(actualSocketPath_.c_str(), 0700) < 0) {
        ::unlink(actualSocketPath_.c_str());
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (::listen(listenFd_, SOMAXCONN) < 0) {
        ::unlink(actualSocketPath_.c_str());
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    running_ = true;
    return true;
}

void IpcServer::stop() {
    if (!running_ && listenFd_ < 0 && clients_.empty()) {
        return;
    }
    for (auto& [fd, session] : clients_) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    clients_.clear();

    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (!actualSocketPath_.empty()) {
        ::unlink(actualSocketPath_.c_str());
    }
    running_ = false;
}

void IpcServer::acceptClients() {
    while (running_ && listenFd_ >= 0) {
        struct sockaddr_un clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = ::accept4(listenFd_, reinterpret_cast<struct sockaddr*>(&clientAddr),
                                 &addrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        ClientSession session;
        session.fd = clientFd;
        session.id = nextClientId_++;
        session.connectedAt = std::chrono::steady_clock::now();
        clients_[clientFd] = std::move(session);
    }
}

void IpcServer::handleClientRead(int clientFd) {
    char buf[2048];
    while (true) {
        auto it = clients_.find(clientFd);
        if (it == clients_.end()) {
            return;
        }
        ClientSession& session = it->second;

        ssize_t n = ::recv(clientFd, buf, sizeof(buf), 0);
        if (n > 0) {
            session.inBuffer.append(buf, static_cast<size_t>(n));
            size_t firstLine = session.inBuffer.find('\n');
            size_t lineLength = firstLine == std::string::npos ? session.inBuffer.size() : firstLine;
            if (lineLength > maxLineLength_ || session.inBuffer.size() > maxBuffered_) {
                queueResponse(session, "ERR line too long\n");
                char drainBuf[8192];
                while (::recv(clientFd, drainBuf, sizeof(drainBuf), MSG_DONTWAIT) > 0) {}
                closeClient(clientFd);
                return;
            }
            drainClient(clientFd);
        } else if (n == 0) {
            closeClient(clientFd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            closeClient(clientFd);
            return;
        }
    }
}

void IpcServer::drainClient(int clientFd) {
    if (drainingFd_ == clientFd) {
        return;
    }
    const int saved = drainingFd_;
    drainingFd_ = clientFd;
    while (true) {
        auto it = clients_.find(clientFd);
        if (it == clients_.end()) break;
        ClientSession& session = it->second;
        if (session.busy) break;
        size_t pos = session.inBuffer.find('\n');
        if (pos == std::string::npos) break;

        std::string line = session.inBuffer.substr(0, pos);
        session.inBuffer.erase(0, pos + 1);
        session.busy = true;
        const uint64_t id = session.id;
        handleCommandLine(line, [this, clientFd, id](std::string reply) { completeClient(clientFd, id, reply); });
    }
    drainingFd_ = saved;
}

void IpcServer::completeClient(int clientFd, uint64_t id, const std::string& reply) {
    auto it = clients_.find(clientFd);
    if (it == clients_.end() || it->second.id != id) {
        return;
    }
    it->second.busy = false;
    if (!queueResponse(it->second, reply)) {
        closeClient(clientFd);
        return;
    }
    if (drainingFd_ != clientFd) {
        drainClient(clientFd);
    }
}

bool IpcServer::flushClientOutBuffer(ClientSession& session) {
    while (!session.outBuffer.empty()) {
        ssize_t sent = ::send(session.fd, session.outBuffer.data(), session.outBuffer.size(), MSG_NOSIGNAL);
        if (sent > 0) {
            session.outBuffer.erase(0, static_cast<size_t>(sent));
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        } else {
            return true;
        }
    }
    return true;
}

void IpcServer::handleClientWrite(int clientFd) {
    auto it = clients_.find(clientFd);
    if (it != clients_.end()) {
        if (!flushClientOutBuffer(it->second)) {
            closeClient(clientFd);
        }
    }
}

bool IpcServer::queueResponse(ClientSession& session, const std::string& response) {
    session.outBuffer.append(response);
    return flushClientOutBuffer(session);
}

void IpcServer::closeClient(int clientFd) {
    auto it = clients_.find(clientFd);
    if (it != clients_.end()) {
        ::close(it->first);
        clients_.erase(it);
    }
}

void IpcServer::appendPollFds(std::vector<struct pollfd>& pfds) const {
    if (listenFd_ >= 0) {
        pfds.push_back({listenFd_, POLLIN, 0});
    }
    for (const auto& [fd, session] : clients_) {
        short events = POLLIN;
        if (!session.outBuffer.empty()) {
            events |= POLLOUT;
        }
        pfds.push_back({fd, events, 0});
    }
}

void IpcServer::handleSocketEvent(int fd, short revents) {
    if (fd == listenFd_) {
        if (revents & (POLLIN | POLLERR)) {
            acceptClients();
        }
        return;
    }

    if (clients_.find(fd) == clients_.end()) {
        return;
    }
    if (revents & POLLOUT) {
        handleClientWrite(fd);
    }
    if ((revents & POLLIN) && clients_.find(fd) != clients_.end()) {
        handleClientRead(fd);
    }
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) && clients_.find(fd) != clients_.end()) {
        closeClient(fd);
    }
}

void IpcServer::handlePollEvents(std::span<const struct pollfd> pfds) {
    for (const auto& pfd : pfds) {
        if (pfd.revents != 0) {
            handleSocketEvent(pfd.fd, pfd.revents);
        }
    }
}

int IpcServer::pollOnce(int timeoutMs) {
    std::vector<pollfd> pfds;
    appendPollFds(pfds);
    if (pfds.empty()) {
        return 0;
    }
    int ret = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeoutMs);
    if (ret > 0) {
        handlePollEvents(pfds);
    }
    return ret;
}

void IpcServer::handleCommandLine(const std::string& line, IpcReply done) {
    auto parsed = cmd::parseLine(line, true);
    if (!parsed.command) {
        done("ERR " + parsed.error + "\n");
        return;
    }
    if (!callbacks_.execute) {
        if (parsed.command->verb == cmd::Verb::Status) {
            done("{\"schema_version\":3,\"connected\":false,\"last_updated\":0,\"primary\":null,\"headsets\":[]}\n");
        } else {
            done("ERR not connected\n");
        }
        return;
    }
    callbacks_.execute(*parsed.command, [done = std::move(done)](std::string reply) {
        if (reply.empty() || reply.back() != '\n') {
            reply.push_back('\n');
        }
        done(reply);
    });
}

std::optional<std::string> IpcServer::handleCommandLineSync(const std::string& line) {
    auto result = std::make_shared<std::optional<std::string>>();
    handleCommandLine(line, [result](std::string reply) { *result = std::move(reply); });
    return *result;
}

} // namespace omarchy::sony
