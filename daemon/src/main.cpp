#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
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
#include "SessionManager.hpp"
#include "StateEngine.hpp"

namespace {

using namespace omarchy::sony;
using namespace omarchy::sony::protocol;
using Clock = std::chrono::steady_clock;

constexpr const char* DAEMON_VERSION = "0.3.0";
constexpr auto kBluezPollInterval = std::chrono::seconds(2);
constexpr uint32_t kMaxLinkAttempts = 6;
constexpr uint32_t kSlowRetryMs = 60000;
constexpr const char* kMockAdapter = "F0:1D:BC:AA:00:01";

struct DaemonOptions {
    bool showHelp{false};
    bool showVersion{false};
    bool mockMode{false};
    std::string mockProfile{"xm5"};
    std::vector<std::string> devices;
    std::string stateDir;
    std::string runtimeDir;
};

void printHelp(const char* progName) {
    std::cout << "Usage: " << progName << " [OPTIONS]\n\n"
              << "Headless background daemon managing Sony WH-1000XM5 and WF-1000XM5 headsets on Linux.\n\n"
              << "Options:\n"
              << "  -h, --help               Display this help message and exit\n"
              << "  -v, --version            Display version information and exit\n"
              << "  -m, --mock               Run against simulated headsets (no Bluetooth)\n"
              << "      --mock-profile <P>   Simulated headsets: xm5 (default), minimal, multipoint, wf, both\n"
              << "  -d, --device <MAC>       Manage only this Bluetooth address (repeat for more)\n"
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
            const std::string_view p = opts.mockProfile;
            if (p != "xm5" && p != "minimal" && p != "multipoint" && p != "wf" && p != "both") {
                std::cerr << "Error: Unknown mock profile '" << opts.mockProfile << "'\n";
                return std::nullopt;
            }
        } else if (arg == "-d" || arg == "--device") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --device requires a MAC address argument\n";
                return std::nullopt;
            }
            auto mac = cmd::normalizeAddress(argv[++i]);
            if (!mac) {
                std::cerr << "Error: Invalid MAC address '" << argv[i] << "'\n";
                return std::nullopt;
            }
            opts.devices.push_back(*mac);
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

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

struct FakeDevice {
    BluetoothDeviceInfo info;
    std::shared_ptr<FakeHeadset> headset;
};

BluetoothDeviceInfo fakeInfo(const std::string& mac, const std::string& name) {
    BluetoothDeviceInfo info;
    info.macAddress = mac;
    info.name = name;
    info.alias = name;
    info.paired = true;
    info.connected = true;
    info.uuids.emplace_back(MDR_UUID_XM5_LOWER);
    return info;
}

std::vector<FakeDevice> makeFakeDevices(const std::string& profile) {
    const std::string wh = "AC:80:0A:12:34:56";
    const std::string wf = "AC:80:0A:65:43:21";
    if (profile == "minimal") {
        return {{fakeInfo(wh, "WH-1000XM5"), std::make_shared<FakeHeadset>(FakeHeadsetProfile::minimal())}};
    }
    if (profile == "wf" || profile == "both") {
        auto earbuds = std::make_shared<FakeHeadset>(FakeHeadsetProfile::wf1000xm5Capture());
        earbuds->useFixtureValues();
        if (profile == "wf") return {{fakeInfo(wf, "WF-1000XM5"), earbuds}};
        earbuds->devices = {{kMockAdapter, "laptop", 1}};
        earbuds->assignable = {0x35, 0x20};
        auto headphones = std::make_shared<FakeHeadset>(FakeHeadsetProfile::wh1000xm5Capture());
        headphones->useFixtureValues();
        headphones->devices = {{kMockAdapter, "laptop", 1}, {"5C:E9:1E:BB:00:02", "phone", 2}};
        headphones->playbackSlot = 2;
        return {{fakeInfo(wh, "WH-1000XM5"), headphones}, {fakeInfo(wf, "WF-1000XM5"), earbuds}};
    }
    auto headset = std::make_shared<FakeHeadset>();
    if (profile == "multipoint") {
        headset->multipoint = true;
        headset->devices = {{kMockAdapter, "laptop", 1}, {"5C:E9:1E:BB:00:02", "Pixel 8", 2},
                            {"3C:22:FB:CC:00:03", "", 0}};
    }
    return {{fakeInfo(wh, "WH-1000XM5"), headset}};
}

// One managed headset: its RFCOMM link and, in mock mode, its simulated headset.
struct Slot {
    BluetoothDeviceInfo info;
    std::unique_ptr<BluetoothManager> link;
    std::shared_ptr<FakeHeadset> fake;
    FakeHeadsetTransport* fakeTransport{nullptr};
    HeadsetSession* session{nullptr};
};

void logLine(const char* tag, const std::string& id, const std::string& line) {
    std::fprintf(stderr, "[%s %s] %s\n", tag, id.c_str(), line.c_str());
    std::fflush(stderr);
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
    SessionManager::Hooks managerHooks;
    managerHooks.write = [&stateEngine](const std::string& statusJson) { stateEngine.writeAtomic(statusJson); };
    SessionManager manager(std::move(managerHooks));

    if (!stateEngine.initialize(manager.statusJson(unixNow()))) {
        std::cerr << "Fatal: Failed to initialize status file under " << stateDir << "\n";
        ::close(sigFd);
        return 1;
    }
    manager.publish();

    std::unique_ptr<IDeviceDiscovery> discovery;
    std::map<std::string, std::shared_ptr<FakeHeadset>> fakes;
    if (opts.mockMode) {
        auto mock = std::make_unique<MockDeviceDiscovery>();
        mock->adapterAddresses = {kMockAdapter};
        for (auto& fake : makeFakeDevices(opts.mockProfile)) {
            mock->devices.push_back(fake.info);
            fakes[fake.info.macAddress] = fake.headset;
        }
        discovery = std::move(mock);
    } else {
        discovery = createBluezDiscovery();
    }

    std::vector<BluetoothDeviceInfo> latestDevices;
    std::map<std::string, std::unique_ptr<Slot>> slots;

    auto attach = [&](const BluetoothDeviceInfo& info, const std::string& id) {
        auto slot = std::make_unique<Slot>();
        Slot* s = slot.get();
        s->info = info;

        BluetoothConfig config;
        config.preferredMac = id;
        config.autoReconnect = true;
        if (opts.mockMode) {
            config.mockMode = true;
            s->fake = fakes[info.macAddress];
            auto transport = std::make_unique<FakeHeadsetTransport>(s->fake);
            s->fakeTransport = transport.get();
            auto fakeDiscovery = std::make_unique<MockDeviceDiscovery>();
            fakeDiscovery->devices = {info};
            s->link = std::make_unique<BluetoothManager>(config, std::move(transport), std::move(fakeDiscovery),
                                                         std::make_unique<MockSdpResolver>());
        } else {
            config.maxReconnectAttempts = kMaxLinkAttempts;
            config.slowRetryMs = kSlowRetryMs;
            auto lookup = std::make_unique<LookupDiscovery>([&latestDevices](const std::string& mac) {
                std::optional<BluetoothDeviceInfo> found;
                for (const auto& dev : latestDevices) {
                    if (cmd::normalizeAddress(dev.macAddress) == cmd::normalizeAddress(mac)) found = dev;
                }
                return found;
            });
            s->link = BluetoothManager::createLinuxLink(config, std::move(lookup));
        }

        HeadsetSession::Hooks hooks;
        hooks.write = [s](const Bytes& bytes) {
            return s->link->getState() == ConnectionState::CONNECTED && s->link->sendPacket(bytes);
        };
        hooks.dropLink = [s](const std::string& reason) { s->link->reconnect(reason); };
        hooks.linkHealthy = [s] { s->link->markHealthy(); };
        if (!opts.mockMode) hooks.log = [id](const std::string& line) { logLine("MDR", id, line); };
        s->session = &manager.add(id, std::move(hooks));

        BluetoothCallbacks callbacks;
        callbacks.onConnected = [s, id]() {
            logLine("DAEMON", id, "RFCOMM link open (" + s->info.alias + "); starting the MDR init sequence");
            s->session->onTransportConnected(s->info.alias, s->info.name, id, Clock::now());
        };
        callbacks.onDisconnected = [s, id](const std::string& reason) {
            logLine("DAEMON", id, "link closed: " + reason);
            s->session->onTransportDisconnected(Clock::now());
        };
        callbacks.onDataReceived = [s](const uint8_t* data, size_t length) {
            s->session->onBytes(std::span<const uint8_t>(data, length), Clock::now());
        };
        s->link->setCallbacks(std::move(callbacks));
        logLine("DAEMON", id, "BlueZ reports " + (info.alias.empty() ? info.name : info.alias) + " connected");
        slots[id] = std::move(slot);
        s->link->start();
    };

    auto detach = [&](const std::string& id, const std::string& reason) {
        auto it = slots.find(id);
        if (it == slots.end()) return;
        std::unique_ptr<Slot> slot = std::move(it->second);
        slots.erase(it);
        logLine("DAEMON", id, reason);
        slot->link->stop();
        manager.remove(id, Clock::now());
    };

    auto pollBluez = [&]() {
        BluezSnapshot snap = discovery->snapshot();
        if (!snap.ok) return;
        latestDevices = snap.devices;
        manager.setAdapterAddresses(snap.adapterAddresses);
        std::vector<std::string> keep;
        for (const auto& dev : selectHeadsets(snap.devices, opts.devices)) {
            auto id = cmd::normalizeAddress(dev.macAddress);
            if (!id) continue;
            keep.push_back(*id);
            auto it = slots.find(*id);
            if (it == slots.end()) attach(dev, *id);
            else it->second->info = dev;
        }
        std::vector<std::string> gone;
        for (const auto& [id, slot] : slots) {
            if (std::find(keep.begin(), keep.end(), id) == keep.end()) gone.push_back(id);
        }
        for (const auto& id : gone) detach(id, "BlueZ reports the headset disconnected; closing its session");
    };

    auto dropLink = [&](Slot& slot) {
        slot.link->disconnect();
        slot.session->onTransportDisconnected(Clock::now());
    };

    IpcServer ipcServer(runtimeDir + "/sony-headphones.sock");
    IpcCallbacks ipcCb;
    ipcCb.execute = [&](const cmd::Command& command, IpcReply done) {
        const bool testVerb = command.verb == cmd::Verb::TestSetBattery || command.verb == cmd::Verb::TestDisconnect ||
                              command.verb == cmd::Verb::TestReconnect;
        if (!testVerb) {
            manager.execute(command, std::move(done), Clock::now());
            return;
        }
        if (!opts.mockMode) {
            done("ERR unknown command '" + command.name + "'");
            return;
        }
        if (command.verb == cmd::Verb::TestReconnect && !command.selector) {
            for (auto& [id, slot] : slots) slot->link->start();
            done("OK");
            return;
        }
        auto target = manager.resolve(command.selector, command.verb != cmd::Verb::TestReconnect);
        auto it = target.session ? slots.find(target.id) : slots.end();
        if (it == slots.end()) {
            done(target.error.empty() ? "ERR not connected" : target.error);
            return;
        }
        Slot& slot = *it->second;
        if (command.verb == cmd::Verb::TestSetBattery) {
            slot.fakeTransport->inject(slot.fake->pushBattery(command.numbers[0], command.numbers[1] != 0));
        } else if (command.verb == cmd::Verb::TestDisconnect) {
            dropLink(slot);
        } else {
            slot.link->start();
        }
        done("OK");
    };
    ipcServer.setCallbacks(std::move(ipcCb));

    if (!ipcServer.start()) {
        std::cerr << "Fatal: Failed to start IPC server at " << ipcServer.getSocketPath() << "\n";
        stateEngine.cleanup();
        ::close(sigFd);
        return 1;
    }

    pollBluez();
    auto nextBluezPoll = Clock::now() + kBluezPollInterval;

    const auto startedAt = Clock::now();
    bool readyAnnounced = false;
    auto announceReady = [&]() {
        if (readyAnnounced) return;
        if (opts.mockMode && !manager.allInitComplete() && Clock::now() - startedAt < std::chrono::seconds(2)) {
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

        std::vector<std::pair<size_t, std::string>> linkFds;
        for (const auto& [id, slot] : slots) {
            int fd = slot->link->getPollFd();
            short events = slot->link->getPollEvents();
            if (fd >= 0 && events != 0) {
                linkFds.emplace_back(pfds.size(), id);
                pfds.push_back({fd, events, 0});
            }
        }
        size_t ipcStart = pfds.size();
        ipcServer.appendPollFds(pfds);

        int timeoutMs = 100;
        if (auto deadline = manager.nextDeadline()) {
            auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now()).count();
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
                    for (auto& [id, slot] : slots) dropLink(*slot);
                } else if (fdsi.ssi_signo == SIGUSR2) {
                    for (auto& [id, slot] : slots) slot->link->start();
                }
            }
        }

        for (const auto& [index, id] : linkFds) {
            if (pfds[index].revents == 0) continue;
            auto it = slots.find(id);
            if (it != slots.end()) it->second->link->handleSocketEvent(pfds[index].revents);
        }
        for (size_t i = ipcStart; i < pfds.size(); ++i) {
            if (pfds[i].revents != 0) {
                ipcServer.handleSocketEvent(pfds[i].fd, pfds[i].revents);
            }
        }

        manager.tick(Clock::now());
        for (auto& [id, slot] : slots) slot->link->tick();
        if (Clock::now() >= nextBluezPoll) {
            nextBluezPoll = Clock::now() + kBluezPollInterval;
            pollBluez();
        }
        announceReady();
    }

    std::cout << (opts.mockMode ? "[MOCK_DAEMON]" : "[DAEMON]") << " Shutdown cleanly\n";
    std::cout.flush();

    std::vector<std::string> ids;
    for (const auto& [id, slot] : slots) ids.push_back(id);
    for (const auto& id : ids) detach(id, "daemon stopping; closing the session");
    ipcServer.stop();
    stateEngine.cleanup();
    ::close(sigFd);
    return 0;
}
