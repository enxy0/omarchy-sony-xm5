#include "Commands.hpp"
#include "IpcServer.hpp"
#include "StateEngine.hpp"
#include "TestUtil.hpp"

#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

using namespace omarchy::sony;

namespace {

std::string reply(const std::string& line, bool allowTest = true) {
    auto r = cmd::parseLine(line, allowTest);
    return r.command ? "CMD " + r.command->line() : "ERR " + r.error;
}

std::string makeSandbox() {
    char tmpl[] = "/tmp/omasony_test_XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    return dir ? std::string(dir) : std::string();
}

int connectTo(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

std::string readAvailable(int fd) {
    char buf[4096];
    ssize_t n = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
}

} // namespace

// ---------------------------------------------------------------------------
// Grammar (backend-contract.md 3.1 steps 1-2 and 3.3)
// ---------------------------------------------------------------------------

TEST(UnknownAndEmptyCommands) {
    CHECK_EQ(reply(""), std::string("ERR empty command"));
    CHECK_EQ(reply("   \t "), std::string("ERR empty command"));
    CHECK_EQ(reply("Frobnicate x"), std::string("ERR unknown command 'frobnicate'"));
    CHECK_EQ(reply("_set_battery 50", false), std::string("ERR unknown command '_set_battery'"));
    CHECK_EQ(reply("_set_battery 50 true"), std::string("CMD _set_battery 50 true"));
}

TEST(UsageTextsMatchContractTable) {
    const std::pair<const char*, const char*> cases[] = {
        {"noise wind", "noise anc|ambient|off"},
        {"noise", "noise anc|ambient|off"},
        {"ambient-level 0", "ambient-level <n>"},
        {"ambient-level 21", "ambient-level <n>"},
        {"ambient-level x", "ambient-level <n>"},
        {"focus-voice yes", "focus-voice on|off"},
        {"eq nonexistent", "eq <preset>"},
        {"eq", "eq <preset>"},
        {"eq custom 1 2 3", "eq custom <b1> <b2> <b3> <b4> <b5> <clear_bass>"},
        {"eq custom 1 2 3 4 5 11", "eq custom <b1> <b2> <b3> <b4> <b5> <clear_bass>"},
        {"eq custom 1 2 3 4 5 6 7", "eq custom <b1> <b2> <b3> <b4> <b5> <clear_bass>"},
        {"dsee fast", "dsee on|off"},
        {"speak-to-chat", "speak-to-chat on|off"},
        {"speak-to-chat-config auto", "speak-to-chat-config <sensitivity> <timeout>"},
        {"speak-to-chat-config medium off", "speak-to-chat-config <sensitivity> <timeout>"},
        {"voice-guidance 1", "voice-guidance on|off"},
        {"voice-guidance-volume 3", "voice-guidance-volume <n>"},
        {"auto-power-off 10min", "auto-power-off <value>"},
        {"nc-amb-button nc", "nc-amb-button <value>"},
        {"button-function other", "button-function <value>"},
        {"ear-detection maybe", "ear-detect on|off"},
        {"volume 31", "volume <n>"},
        {"connection-quality other", "connection-quality sound|connection"},
        {"multipoint 2", "multipoint on|off"},
        {"source F0:1D:BC:AA:00", "source <address>"},
        {"source F0-1D-BC-AA-00-01", "source <address>"},
        {"status now", "status"},
        {"touch-function left", "touch-function left|right <value>"},
        {"touch-function up none", "touch-function left|right <value>"},
        {"touch-function right other", "touch-function left|right <value>"},
        {"touch-function right none extra", "touch-function left|right <value>"},
        {"sidetone 1", "sidetone on|off"},
        {"sidetone", "sidetone on|off"},
        {"@x status", "status"},
        {"@AC:80:0A:65:43:21 status", "status"},
        {"@ noise anc", "@<address|model> <verb> [args]"},
        {"@wf-1000xm5", "@<address|model> <verb> [args]"},
        {"@wf-1000xm5 noise wind", "noise anc|ambient|off"},
    };
    for (const auto& [line, usage] : cases) {
        std::string got = reply(line);
        std::string want = std::string("ERR usage: ") + usage;
        if (got != want) testutil::fail(std::string(line) + " -> " + got + ", expected " + want, __FILE__, __LINE__);
    }
}

TEST(ValidCommandsAreNormalized) {
    CHECK_EQ(reply("NOISE Ambient"), std::string("CMD noise ambient"));
    CHECK_EQ(reply("ambient-level 1"), std::string("CMD ambient-level 1"));
    CHECK_EQ(reply("ambient-level 20"), std::string("CMD ambient-level 20"));
    CHECK_EQ(reply("eq USER5"), std::string("CMD eq user5"));
    CHECK_EQ(reply("eq custom -10 10 0 0 0 -10"), std::string("CMD eq custom -10 10 0 0 0 -10"));
    CHECK_EQ(reply("ear-detection ON"), std::string("CMD ear-detect on"));
    CHECK_EQ(reply("voice-guidance-volume -2"), std::string("CMD voice-guidance-volume -2"));
    CHECK_EQ(reply("volume 0"), std::string("CMD volume 0"));
    CHECK_EQ(reply("speak-to-chat-config HIGH Off"), std::string("CMD speak-to-chat-config high off"));
    CHECK_EQ(reply("auto-power-off when_taken_off"), std::string("CMD auto-power-off when_taken_off"));
    CHECK_EQ(reply("source f0:1d:bc:aa:00:01"), std::string("CMD source F0:1D:BC:AA:00:01"));
    CHECK_EQ(reply("probe"), std::string("CMD probe"));
    CHECK_EQ(reply("Touch-Function LEFT Playback_Control"), std::string("CMD touch-function left playback_control"));
    CHECK_EQ(reply("sidetone OFF"), std::string("CMD sidetone off"));
}

TEST(SelectorIsKeptApartFromTheVerb) {
    auto r = cmd::parseLine("@wf-1000xm5 VOLUME 5", false);
    REQUIRE(r.command.has_value());
    CHECK_EQ(r.command->selector, std::optional<std::string>("wf-1000xm5"));
    CHECK_EQ(r.command->line(), std::string("volume 5"));
    CHECK_EQ(r.command->requestLine(), std::string("@wf-1000xm5 volume 5"));
    auto plain = cmd::parseLine("volume 5", false);
    REQUIRE(plain.command.has_value());
    CHECK(!plain.command->selector.has_value());
    CHECK_EQ(plain.command->requestLine(), std::string("volume 5"));
    CHECK_EQ(reply("@frob x"), std::string("ERR unknown command 'x'"));
    CHECK_EQ(reply("@AC:80:0A:65:43:21 _disconnect"), std::string("CMD _disconnect"));
    CHECK(cmd::validSelector("AC:80:0A:65:43:21"));
    CHECK(cmd::validSelector("wf-1000xm5"));
    CHECK(!cmd::validSelector(""));
    CHECK(!cmd::validSelector("wf 1000xm5"));
}

TEST(BareEqCustomSelectsThePreset) {
    auto bare = cmd::parseLine("eq custom", false);
    REQUIRE(bare.command.has_value());
    CHECK(bare.command->verb == cmd::Verb::Eq);
    CHECK_EQ(bare.command->line(), std::string("eq custom"));
    auto upper = cmd::parseLine("EQ CUSTOM", false);
    REQUIRE(upper.command.has_value());
    CHECK(upper.command->verb == cmd::Verb::Eq);
    auto bands = cmd::parseLine("eq custom 1 2 3 4 5 6", false);
    REQUIRE(bands.command.has_value());
    CHECK(bands.command->verb == cmd::Verb::EqCustom);
    CHECK_EQ(bands.command->numbers.size(), size_t{6});
}

// ---------------------------------------------------------------------------
// IpcServer
// ---------------------------------------------------------------------------

TEST(ServerWithoutBackendStillAnswers) {
    IpcServer server("/tmp/unused-test.sock");
    CHECK_EQ(server.handleCommandLineSync("status").value_or(""),
             std::string("{\"schema_version\":3,\"connected\":false,\"last_updated\":0,\"primary\":null,\"headsets\":[]}\n"));
    CHECK_EQ(server.handleCommandLineSync("@x status").value_or(""), std::string("ERR usage: status\n"));
    CHECK_EQ(server.handleCommandLineSync("@wf-1000xm5 noise anc").value_or(""), std::string("ERR not connected\n"));
    CHECK_EQ(server.handleCommandLineSync("noise anc").value_or(""), std::string("ERR not connected\n"));
    CHECK_EQ(server.handleCommandLineSync("noise wind").value_or(""), std::string("ERR usage: noise anc|ambient|off\n"));
    CHECK_EQ(server.handleCommandLineSync("").value_or(""), std::string("ERR empty command\n"));
}

TEST(ServerRepliesInOrderAndWaitsForAsyncResults) {
    std::string sandbox = makeSandbox();
    REQUIRE(!sandbox.empty());
    std::string path = sandbox + "/test.sock";
    {
        IpcServer server(path);
        std::deque<std::pair<std::string, IpcReply>> pending;
        IpcCallbacks cb;
        cb.execute = [&pending](const cmd::Command& c, IpcReply done) {
            if (c.verb == cmd::Verb::Status) {
                done("{\"schema_version\":2}");
                return;
            }
            pending.emplace_back(c.line(), std::move(done));
        };
        server.setCallbacks(std::move(cb));
        REQUIRE(server.start());

        struct stat st{};
        REQUIRE(::stat(path.c_str(), &st) == 0);
        CHECK_EQ(static_cast<int>(st.st_mode & 0777), 0700);

        int fd = connectTo(path);
        REQUIRE(fd >= 0);
        server.pollOnce(50);
        std::string burst = "dsee on\nstatus\nnoise anc\n";
        ::send(fd, burst.data(), burst.size(), 0);
        server.pollOnce(50);
        CHECK_EQ(readAvailable(fd), std::string(""));
        REQUIRE(pending.size() == 1);
        CHECK_EQ(pending[0].first, std::string("dsee on"));

        pending[0].second("OK");
        server.pollOnce(10);
        CHECK_EQ(readAvailable(fd), std::string("OK\n{\"schema_version\":2}\n"));
        REQUIRE(pending.size() == 2);
        CHECK_EQ(pending[1].first, std::string("noise anc"));
        pending[1].second("ERR device timeout");
        CHECK_EQ(readAvailable(fd), std::string("ERR device timeout\n"));

        std::string chunk1 = "ambient-";
        std::string chunk2 = "level 0\n";
        ::send(fd, chunk1.data(), chunk1.size(), 0);
        server.pollOnce(20);
        ::send(fd, chunk2.data(), chunk2.size(), 0);
        server.pollOnce(20);
        CHECK_EQ(readAvailable(fd), std::string("ERR usage: ambient-level <n>\n"));

        ::close(fd);
        server.pollOnce(20);
        CHECK_EQ(server.getClientCount(), size_t{0});

        int fd2 = connectTo(path);
        server.pollOnce(20);
        std::string one = "volume 5\n";
        ::send(fd2, one.data(), one.size(), 0);
        server.pollOnce(20);
        ::close(fd2);
        server.pollOnce(20);
        REQUIRE(pending.size() == 3);
        pending[2].second("OK");
        CHECK_EQ(server.getClientCount(), size_t{0});

        int fd3 = connectTo(path);
        server.pollOnce(20);
        std::string longLine(5000, 'a');
        ::send(fd3, longLine.data(), longLine.size(), 0);
        server.pollOnce(50);
        CHECK(readAvailable(fd3).find("ERR line too long") == 0);
        ::close(fd3);

        server.stop();
        CHECK(::stat(path.c_str(), &st) != 0);
    }
    std::error_code ec;
    std::filesystem::remove_all(sandbox, ec);
}

// ---------------------------------------------------------------------------
// StateEngine
// ---------------------------------------------------------------------------

TEST(StatusFileIsWrittenAtomicallyWithPrivateModes) {
    std::string sandbox = makeSandbox();
    REQUIRE(!sandbox.empty());
    {
        StateEngine engine(sandbox);
        REQUIRE(engine.initialize("{\"schema_version\":2,\"connected\":false,\"last_updated\":1}"));
        struct stat st{};
        REQUIRE(::stat(engine.getStateDirectory().c_str(), &st) == 0);
        CHECK_EQ(static_cast<int>(st.st_mode & 0777), 0700);
        REQUIRE(::stat(engine.getStateFilePath().c_str(), &st) == 0);
        CHECK_EQ(static_cast<int>(st.st_mode & 0777), 0600);
        CHECK_EQ(testutil::readFile(engine.getStatusFilePath()),
                 std::string("{\"schema_version\":2,\"connected\":false,\"last_updated\":1}\n"));
        CHECK(engine.writeAtomic("{\"a\":1}\n"));
        CHECK_EQ(testutil::readFile(engine.getStatusFilePath()), std::string("{\"a\":1}\n"));
        CHECK_EQ(engine.getStateFilePath(), std::filesystem::path(sandbox) / "sony-headphones" / "status.json");
        engine.cleanup();
        CHECK(!std::filesystem::exists(engine.getStateFilePath()));
    }
    std::error_code ec;
    std::filesystem::remove_all(sandbox, ec);
}

int main() {
    return testutil::runAll("IpcTest");
}
