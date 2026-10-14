#include "Commands.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

namespace cmd = omarchy::sony::cmd;

bool is_negative_number(const std::string& str) {
    return str.size() >= 2 && str[0] == '-' && std::isdigit(static_cast<unsigned char>(str[1])) != 0;
}

void print_usage(std::ostream& os) {
    os << "Usage: sony-ctl [-s <socket>] <verb> [args...]\n\n"
       << "Read verbs:\n"
       << "  status | probe\n\n"
       << "Setter verbs:\n";
    for (auto verb : {cmd::Verb::Noise, cmd::Verb::AmbientLevel, cmd::Verb::FocusVoice, cmd::Verb::Eq,
                      cmd::Verb::EqCustom, cmd::Verb::Dsee, cmd::Verb::SpeakToChat, cmd::Verb::SpeakToChatConfig,
                      cmd::Verb::VoiceGuidance, cmd::Verb::VoiceGuidanceVolume, cmd::Verb::AutoPowerOff,
                      cmd::Verb::NcAmbButton, cmd::Verb::ButtonFunction, cmd::Verb::EarDetect, cmd::Verb::Volume,
                      cmd::Verb::ConnectionQuality, cmd::Verb::Multipoint, cmd::Verb::Source,
                      cmd::Verb::TouchPanel, cmd::Verb::QuickAccess}) {
        os << "  " << cmd::usage(verb) << "\n";
    }
    os << "\nOptions:\n"
       << "  -s, --socket <path>  Override socket path\n"
       << "  -h, --help           Show help\n"
       << "  -v, --version        Show version\n";
}

std::string get_default_socket_path() {
    const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime != nullptr && *xdg_runtime != '\0') {
        return std::string(xdg_runtime) + "/sony-headphones.sock";
    }
    return "/tmp/run-" + std::to_string(::getuid()) + "/sony-headphones.sock";
}

int send_command(const std::string& socket_path, const std::string& line, bool expects_json) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        std::cerr << "Error: Cannot create socket (" << std::strerror(errno) << ")\n";
        return 1;
    }

    struct timeval tv{};
    tv.tv_sec = 2;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        std::cerr << "Error: Socket path too long: " << socket_path << "\n";
        ::close(fd);
        return 1;
    }
    std::memcpy(addr.sun_path, socket_path.data(), socket_path.size());

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Error: Cannot connect to daemon socket (" << std::strerror(errno) << ")\n";
        ::close(fd);
        return 2;
    }

    std::string payload = line + "\n";
    size_t total_sent = 0;
    while (total_sent < payload.size()) {
        ssize_t sent = ::send(fd, payload.data() + total_sent, payload.size() - total_sent, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) {
            std::cerr << "Error: Socket communication failed ("
                      << (sent < 0 ? std::strerror(errno) : "connection closed") << ")\n";
            ::close(fd);
            return 1;
        }
        total_sent += static_cast<size_t>(sent);
    }

    std::string response;
    char buf[4096];
    while (response.find('\n') == std::string::npos) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            response.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            std::cerr << "Error: Socket communication failed (" << std::strerror(errno) << ")\n";
            ::close(fd);
            return 1;
        }
    }
    ::close(fd);

    size_t newline_pos = response.find('\n');
    if (newline_pos != std::string::npos) response.resize(newline_pos);
    while (!response.empty() && (response.back() == '\r' || response.back() == ' ' || response.back() == '\t')) {
        response.pop_back();
    }

    if (response.empty()) {
        std::cerr << "Error: Empty response from daemon\n";
        return 1;
    }
    if (response.rfind("ERR", 0) == 0) {
        std::cerr << "Error from daemon: " << response << "\n";
        return 1;
    }
    if (expects_json ? response.front() == '{' : response == "OK") {
        std::cout << response << "\n";
        return 0;
    }
    std::cerr << "Error: Unexpected response from daemon: " << response << "\n";
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string socket_path;
    bool show_help = false;
    bool show_version = false;
    std::vector<std::string> remaining;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (!remaining.empty()) {
            remaining.push_back(arg);
        } else if (arg == "-h" || arg == "--help") {
            show_help = true;
        } else if (arg == "-v" || arg == "--version") {
            show_version = true;
        } else if (arg == "-s" || arg == "--socket") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a socket path argument\n";
                return 1;
            }
            socket_path = argv[++i];
        } else if (arg.rfind("--socket=", 0) == 0) {
            socket_path = arg.substr(9);
        } else if (arg == "--") {
            for (++i; i < argc; ++i) remaining.emplace_back(argv[i]);
            break;
        } else if (!arg.empty() && arg[0] == '-' && !is_negative_number(arg)) {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            print_usage(std::cerr);
            return 1;
        } else {
            remaining.push_back(arg);
        }
    }

    if (show_help) {
        print_usage(std::cout);
        return 0;
    }
    if (show_version) {
        std::cout << "sony-ctl 0.2.0\n";
        return 0;
    }
    if (remaining.empty()) {
        print_usage(std::cerr);
        return 1;
    }
    if (socket_path.empty()) {
        socket_path = get_default_socket_path();
    }

    auto parsed = cmd::parse(remaining, false);
    if (!parsed.command) {
        std::cerr << "Error: " << parsed.error << "\n";
        return 1;
    }

    const bool expects_json = parsed.command->verb == cmd::Verb::Status || parsed.command->verb == cmd::Verb::Probe;
    return send_command(socket_path, parsed.command->line(), expects_json);
}
