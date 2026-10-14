#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/poll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "BluetoothManager.hpp"
#include "FakeHeadset.hpp"
#include "HeadsetSession.hpp"
#include "IpcServer.hpp"
#include "StateEngine.hpp"

namespace {

using namespace omarchy::sony;
using namespace omarchy::sony::protocol;

constexpr const char* DAEMON_VERSION = "0.2.0";

struct DaemonOptions {
    bool showHelp{false};
    bool showVersion{false};
    bool mockMode{false};
    std::string mockProfile{"xm5"};
    std::string preferredMac;
    std::string stateDir;
    std::string runtimeDir;
};

void printHelp(const char* progName) {
    std::cout << "Usage: " << progName << " [OPTIONS]\n\n"
              << "Headless background daemon managing Sony WH-1000XM5 headphones on Linux.\n\n"
              << "Options:\n"
              << "  -h, --help               Display this help message and exit\n"
              << "  -v, --version            Display version information and exit\n"
              << "  -m, --mock               Run against a simulated headset (no Bluetooth)\n"
              << "      --mock-profile <P>   Simulated headset: xm5 (default), minimal, multipoint\n"
              << "  -d, --device <MAC>       Target specific Bluetooth MAC address\n"
              << "      --state-dir <DIR>    Override directory for status.json\n"
              << "      --runtime-dir <DIR>  Override directory for IPC socket\n\n";
}

std::optional<DaemonOptions> parseCommandLine(int argc, char* argv[]) {
    DaemonOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            opts.showHelp = true;
            return opts;
        } else if (arg == "-v" || arg == "--version") {
            opts.showVersion = true;
            return opts;
        } else if (arg == "-m" || arg == "--mock") {
            opts.mockMode = true;
        } else if (arg == "--mock-profile") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --mock-profile requires a profile name\n";
                return std::nullopt;
            }
            opts.mockProfile = argv[++i];
            if (opts.mockProfile != "xm5" && opts.mockProfile != "minimal" && opts.mockProfile != "multipoint") {
                std::cerr << "Error: Unknown mock profile '" << opts.mockProfile << "'\n";
                return std::nullopt;
            }
        } else if (arg == "-d" || arg == "--device") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --device requires a MAC address argument\n";
                return std::nullopt;
            }
            opts.preferredMac = argv[++i];
        } else if (arg == "--state-dir") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --state-dir requires a directory path\n";
                return std::nullopt;
            }
            opts.stateDir = argv[++i];
        } else if (arg == "--runtime-dir") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --runtime-dir requires a directory path\n";
                return std::nullopt;
            }
            opts.runtimeDir = argv[++i];
        } else {
            std::cerr << "Error: Unrecognized option '" << arg << "'\n";
            return std::nullopt;
        }
    }
    return opts;
}

std::string resolveStateDir(const std::string& overrideDir) {
    if (!overrideDir.empty()) return overrideDir;
    const char* xdgState = std::getenv("XDG_STATE_HOME");
    if (xdgState && xdgState[0] != '\0') return xdgState;
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') return std::string(home) + "/.local/state";
    return "/tmp";
}

std::string resolveRuntimeDir(const std::string& overrideDir) {
    if (!overrideDir.empty()) return overrideDir;
    const char* xdgRun = std::getenv("XDG_RUNTIME_DIR");
    if (xdgRun && xdgRun[0] != '\0') return xdgRun;
    return "/tmp/run-" + std::to_string(::getuid());
}

std::shared_ptr<FakeHeadset> makeFakeHeadset(const std::string& profile) {
    if (profile == "minimal") return std::make_shared<FakeHeadset>(FakeHeadsetProfile::minimal());
    auto headset = std::make_shared<FakeHeadset>();
    if (profile == "multipoint") {
        headset->multipoint = true;
        headset->devices = {{"F0:1D:BC:AA:00:01", "laptop", 1}, {"5C:E9:1E:BB:00:02", "Pixel 8", 2},
                            {"3C:22:FB:CC:00:03", "", 0}};
    }
    return headset;
}

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

int main(int argc, char* argv[]) {
    ::umask(0077);

    auto optsOpt = parseCommandLine(argc, argv);
    if (!optsOpt) return 1;
    const auto& opts = *optsOpt;

    if (opts.showHelp) {
        printHelp(argv[0]);
        return 0;
    }
    if (opts.showVersion) {
        std::cout << "sony-headphones-daemon " << DAEMON_VERSION << "\n";
        return 0;
    }

    const std::string stateDir = resolveStateDir(opts.stateDir);
    const std::string runtimeDir = resolveRuntimeDir(opts.runtimeDir);

    ::signal(SIGPIPE, SIG_IGN);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    if (::sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        std::cerr << "Fatal: Failed to mask signals: " << std::strerror(errno) << "\n";
        return 1;
    }
    int sigFd = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigFd < 0) {
        std::cerr << "Fatal: Failed to create signalfd: " << std::strerror(errno) << "\n";
        return 1;
    }

    StateEngine stateEngine(stateDir);
    std::unique_ptr<BluetoothManager> btManager;
    std::shared_ptr<FakeHeadset> fakeHeadset;
    FakeHeadsetTransport* fakeTransport = nullptr;

    HeadsetSession::Hooks hooks;
    hooks.stateChanged = [&stateEngine](const std::string& statusJson) { stateEngine.writeAtomic(statusJson); };
    hooks.write = [&btManager](const Bytes& bytes) {
        return btManager && btManager->getState() == ConnectionState::CONNECTED && btManager->sendPacket(bytes);
    };
    hooks.dropLink = [&btManager](const std::string& reason) {
        if (btManager) btManager->reconnect(reason);
    };
    if (!opts.mockMode) {
        hooks.log = [](const std::string& line) {
            std::fprintf(stderr, "[MDR] %s\n", line.c_str());
            std::fflush(stderr);
        };
    }
    HeadsetSession session(std::move(hooks));

    if (!stateEngine.initialize(session.statusJson(unixNow()))) {
        std::cerr << "Fatal: Failed to initialize status file under " << stateDir << "\n";
        ::close(sigFd);
        return 1;
    }

    BluetoothConfig btConfig;
    btConfig.preferredMac = opts.preferredMac;
    btConfig.autoReconnect = true;
    btConfig.mockMode = opts.mockMode;
    if (opts.mockMode) {
        fakeHeadset = makeFakeHeadset(opts.mockProfile);
        auto transport = std::make_unique<FakeHeadsetTransport>(fakeHeadset);
        fakeTransport = transport.get();
        btManager = std::make_unique<BluetoothManager>(btConfig, std::move(transport),
                                                       std::make_unique<MockDeviceDiscovery>(),
                                                       std::make_unique<MockSdpResolver>());
    } else {
        btManager = BluetoothManager::createLinux(btConfig);
    }
    if (!btManager) {
        std::cerr << "Fatal: Failed to create BluetoothManager\n";
        stateEngine.cleanup();
        ::close(sigFd);
        return 1;
    }

    BluetoothCallbacks callbacks;
    callbacks.onConnected = [&]() {
        const auto& dev = btManager->getCurrentDevice();
        std::string alias = !dev.alias.empty() ? dev.alias : "WH-1000XM5";
        std::optional<std::string> address;
        if (!dev.macAddress.empty()) address = dev.macAddress;
        std::fprintf(stderr, "[DAEMON] RFCOMM link open (%s); starting the MDR init sequence\n", alias.c_str());
        std::fflush(stderr);
        session.onTransportConnected(alias, address, std::chrono::steady_clock::now());
    };
    callbacks.onDisconnected = [&](const std::string& reason) {
        std::fprintf(stderr, "[DAEMON] Headset disconnected: %s\n", reason.c_str());
        std::fflush(stderr);
        session.onTransportDisconnected(std::chrono::steady_clock::now());
    };
    callbacks.onDataReceived = [&](const uint8_t* data, size_t length) {
        session.onBytes(std::span<const uint8_t>(data, length), std::chrono::steady_clock::now());
    };
    btManager->setCallbacks(std::move(callbacks));

    auto dropTransport = [&]() {
        btManager->disconnect();
        session.onTransportDisconnected(std::chrono::steady_clock::now());
    };

    IpcServer ipcServer(runtimeDir + "/sony-headphones.sock");
    IpcCallbacks ipcCb;
    ipcCb.execute = [&](const cmd::Command& command, IpcReply done) {
        switch (command.verb) {
        case cmd::Verb::TestSetBattery:
            if (!fakeTransport) break;
            fakeTransport->inject(fakeHeadset->pushBattery(command.numbers[0], command.numbers[1] != 0));
            done("OK");
            return;
        case cmd::Verb::TestDisconnect:
            if (!fakeTransport) break;
            dropTransport();
            done("OK");
            return;
        case cmd::Verb::TestReconnect:
            if (!fakeTransport) break;
            btManager->start();
            done("OK");
            return;
        default:
            session.execute(command, std::move(done), std::chrono::steady_clock::now());
            return;
        }
        done("ERR unknown command '" + command.name + "'");
    };
    ipcServer.setCallbacks(std::move(ipcCb));

    if (!ipcServer.start()) {
        std::cerr << "Fatal: Failed to start IPC server at " << ipcServer.getSocketPath() << "\n";
        btManager->stop();
        stateEngine.cleanup();
        ::close(sigFd);
        return 1;
    }

    btManager->start();

    const auto startedAt = std::chrono::steady_clock::now();
    bool readyAnnounced = false;
    auto announceReady = [&]() {
        if (readyAnnounced) return;
        if (opts.mockMode && !session.initComplete() &&
            std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(2)) {
            return;
        }
        readyAnnounced = true;
        std::cout << (opts.mockMode ? "[MOCK_DAEMON]" : "[DAEMON]") << " Ready PID=" << ::getpid() << std::endl;
    };
    announceReady();

    bool running = true;
    while (running) {
        std::vector<struct pollfd> pfds;
        pfds.push_back({sigFd, POLLIN, 0});

        int btIndex = -1;
        int btFd = btManager->getPollFd();
        short btEvents = btManager->getPollEvents();
        if (btFd >= 0 && btEvents != 0) {
            btIndex = static_cast<int>(pfds.size());
            pfds.push_back({btFd, btEvents, 0});
        }
        size_t ipcStart = pfds.size();
        ipcServer.appendPollFds(pfds);

        int timeoutMs = 100;
        if (auto deadline = session.nextDeadline()) {
            auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
                *deadline - std::chrono::steady_clock::now()).count();
            timeoutMs = static_cast<int>(std::clamp<int64_t>(wait + 1, 0, 100));
        }

        int pollRc = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeoutMs);
        if (pollRc < 0) {
            if (errno == EINTR) continue;
            std::cerr << "Event loop poll error: " << std::strerror(errno) << "\n";
            break;
        }

        if (pfds[0].revents & POLLIN) {
            struct signalfd_siginfo fdsi{};
            if (::read(sigFd, &fdsi, sizeof(fdsi)) == sizeof(fdsi)) {
                if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
                    running = false;
                    break;
                }
                if (fdsi.ssi_signo == SIGUSR1) {
                    dropTransport();
                } else if (fdsi.ssi_signo == SIGUSR2) {
                    btManager->start();
                }
            }
        }

        if (btIndex >= 0 && pfds[btIndex].revents != 0) {
            btManager->handleSocketEvent(pfds[btIndex].revents);
        }
        for (size_t i = ipcStart; i < pfds.size(); ++i) {
            if (pfds[i].revents != 0) {
                ipcServer.handleSocketEvent(pfds[i].fd, pfds[i].revents);
            }
        }

        session.tick(std::chrono::steady_clock::now());
        btManager->tick();
        announceReady();
    }

    std::cout << (opts.mockMode ? "[MOCK_DAEMON]" : "[DAEMON]") << " Shutdown cleanly\n";
    std::cout.flush();

    btManager->stop();
    session.onTransportDisconnected(std::chrono::steady_clock::now());
    ipcServer.stop();
    stateEngine.cleanup();
    ::close(sigFd);
    return 0;
}
