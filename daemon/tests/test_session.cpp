#include "Commands.hpp"
#include "FakeHeadset.hpp"
#include "HeadsetSession.hpp"
#include "MdrLink.hpp"
#include "TestUtil.hpp"

#include <algorithm>
#include <deque>
#include <memory>
#include <string>

using namespace omarchy::sony;
using namespace omarchy::sony::protocol;
using testutil::Json;
using Clock = std::chrono::steady_clock;

namespace {

Bytes B(std::initializer_list<uint8_t> bytes) { return Bytes(bytes); }

constexpr const char* kAddress = "AC:80:0A:12:34:56";

struct Harness {
    FakeHeadset fake;
    std::deque<Bytes> inbox;
    int forwardLimit{-1};
    int forwarded{0};
    std::vector<std::string> drops;
    int changes{0};
    Clock::time_point now{Clock::time_point{} + std::chrono::hours(1)};
    std::unique_ptr<HeadsetSession> session;

    explicit Harness(FakeHeadsetProfile profile = FakeHeadsetProfile::xm5()) : fake(std::move(profile)) {
        HeadsetSession::Hooks hooks;
        hooks.write = [this](const Bytes& bytes) {
            if (forwardLimit >= 0 && forwarded >= forwardLimit) return true;
            auto u = unpackFrame(bytes);
            if (u && u->type != PacketType::ACK) ++forwarded;
            Bytes answer = fake.receive(bytes);
            if (!answer.empty()) inbox.push_back(std::move(answer));
            return true;
        };
        hooks.stateChanged = [this] { ++changes; };
        hooks.dropLink = [this](const std::string& reason) { drops.push_back(reason); };
        session = std::make_unique<HeadsetSession>(std::move(hooks));
    }

    void pump() {
        for (int guard = 0; guard < 100000 && !inbox.empty(); ++guard) {
            Bytes bytes = std::move(inbox.front());
            inbox.pop_front();
            session->onBytes(bytes, now);
        }
    }

    void advance(int ms) {
        for (int t = 0; t < ms; t += 50) {
            now += std::chrono::milliseconds(50);
            session->tick(now);
            pump();
        }
    }

    void connect(const std::string& alias = "WH-1000XM5") {
        fake.reconnect();
        session->onTransportConnected(alias, std::string(kAddress), now);
        pump();
    }

    void push(Table table, const Bytes& payload) {
        inbox.push_back(fake.pushPayload(table, payload));
        pump();
    }

    std::string run(const std::string& line, int maxWaitMs = 4000) {
        auto parsed = cmd::parseLine(line, true);
        if (!parsed.command) return "ERR " + parsed.error;
        auto reply = std::make_shared<std::optional<std::string>>();
        session->execute(*parsed.command, [reply](std::string r) { *reply = std::move(r); }, now);
        pump();
        for (int waited = 0; !*reply && waited < maxWaitMs; waited += 50) advance(50);
        return reply->value_or("<no reply>");
    }

    Json status() const {
        auto j = testutil::parseJson(session->statusJson(1760438400));
        return j ? *j : Json{};
    }

    std::vector<std::pair<Table, Bytes>> sentSince(size_t index) const {
        return {fake.received.begin() + static_cast<long>(std::min(index, fake.received.size())), fake.received.end()};
    }
};

std::string fixtureDiff(const Json& actual, const char* name) {
    auto fixture = testutil::parseJson(testutil::readFile(std::string(SONY_FIXTURES_DIR "/") + name));
    if (!fixture) return std::string("cannot read fixture ") + name;
    return testutil::jsonDiff(actual, *fixture, {"last_updated"});
}

bool sentPayload(const Harness& h, Table table, const Bytes& payload) {
    return std::any_of(h.fake.received.begin(), h.fake.received.end(),
                       [&](const auto& r) { return r.first == table && r.second == payload; });
}

struct ManualLink {
    std::vector<Bytes> written;
    std::vector<std::pair<Table, Bytes>> dispatched;
    MdrLink link;
    Clock::time_point now{Clock::time_point{} + std::chrono::hours(1)};

    ManualLink()
        : link([this](const Bytes& f) { written.push_back(f); return true; },
               [this](Table t, const Bytes& p) { dispatched.emplace_back(t, p); }) {}

    UnpackedFrame lastWritten() const { return *unpackFrame(written.back()); }
    void deliver(const Bytes& bytes) { link.onBytes(bytes, now); }
    void ack(uint8_t seq) { deliver(packFrame(PacketType::ACK, seq, {})); }
};

LinkRequest request(Bytes payload, std::shared_ptr<std::vector<LinkResult>> results, bool priority = false) {
    LinkRequest r;
    r.payload = std::move(payload);
    r.priority = priority;
    r.done = [results](LinkResult res) { results->push_back(res); };
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Link layer: one request in flight, ACK and seq rules
// ---------------------------------------------------------------------------

TEST(LinkSeqFlipsOnlyOnMatchingAck) {
    ManualLink m;
    auto results = std::make_shared<std::vector<LinkResult>>();
    m.link.submit(request(B({0x22, 0x00}), results), m.now);
    m.link.submit(request(B({0x12, 0x02}), results), m.now);
    REQUIRE(m.written.size() == 1);
    CHECK_EQ(m.lastWritten().seq, uint8_t{0});
    m.ack(0);
    CHECK_EQ(m.written.size(), size_t{1});
    CHECK_EQ(m.link.sequence(), uint8_t{0});
    m.ack(1);
    CHECK_EQ(m.link.sequence(), uint8_t{1});
    REQUIRE(m.written.size() == 2);
    CHECK_EQ(m.lastWritten().seq, uint8_t{1});
    CHECK_EQ(m.lastWritten().payload, B({0x12, 0x02}));
    m.ack(0);
    CHECK_EQ(m.link.sequence(), uint8_t{0});
    CHECK_EQ(results->size(), size_t{2});
    m.ack(1);
    CHECK_EQ(m.link.sequence(), uint8_t{0});
}

TEST(LinkRetriesWithSameSeqThenFails) {
    ManualLink m;
    auto results = std::make_shared<std::vector<LinkResult>>();
    LinkRequest r = request(B({0xE8, 0x01, 0x01}), results);
    r.ackTimeoutMs = 900;
    r.maxAttempts = 2;
    m.link.submit(std::move(r), m.now);
    m.now += std::chrono::milliseconds(899);
    m.link.tick(m.now);
    CHECK_EQ(m.written.size(), size_t{1});
    m.now += std::chrono::milliseconds(1);
    m.link.tick(m.now);
    REQUIRE(m.written.size() == 2);
    CHECK(m.written[0] == m.written[1]);
    m.now += std::chrono::milliseconds(900);
    m.link.tick(m.now);
    REQUIRE(results->size() == 1);
    CHECK(results->at(0) == LinkResult::NoAck);
    CHECK_EQ(m.link.sequence(), uint8_t{0});
}

TEST(LinkAcceptsReplyBeforeAck) {
    ManualLink m;
    auto results = std::make_shared<std::vector<LinkResult>>();
    LinkRequest r = request(B({0x22, 0x00}), results);
    r.reply = replyFor(Table::T1, r.payload);
    m.link.submit(std::move(r), m.now);
    m.deliver(packFrame(PacketType::DATA_MDR, 0, B({0x23, 0x00, 50, 0x00})));
    CHECK(results->empty());
    REQUIRE(m.dispatched.size() == 1);
    auto hostAck = unpackFrame(m.written.back());
    REQUIRE(hostAck.has_value());
    CHECK(hostAck->type == PacketType::ACK);
    CHECK_EQ(hostAck->seq, uint8_t{1});
    m.ack(1);
    REQUIRE(results->size() == 1);
    CHECK(results->at(0) == LinkResult::Ok);
}

TEST(LinkWaitsForReplyAfterAck) {
    ManualLink m;
    auto results = std::make_shared<std::vector<LinkResult>>();
    LinkRequest r = request(B({0x56, 0x00}), results);
    r.reply = replyFor(Table::T1, r.payload);
    r.replyTimeoutMs = 700;
    m.link.submit(std::move(r), m.now);
    m.link.submit(request(B({0x12, 0x02}), results), m.now);
    m.ack(1);
    CHECK_EQ(m.written.size(), size_t{1});
    m.deliver(packFrame(PacketType::DATA_MDR_NO2, 0, B({0x57, 0x00, 0x10, 0x00})));
    CHECK(results->empty());
    m.now += std::chrono::milliseconds(700);
    m.link.tick(m.now);
    REQUIRE(results->size() == 1);
    CHECK(results->at(0) == LinkResult::NoReply);
    CHECK_EQ(m.lastWritten().payload, B({0x12, 0x02}));
}

TEST(LinkAcksEveryDeviceDataPacket) {
    ManualLink m;
    m.deliver(packFrame(PacketType::DATA_MDR, 0, B({0x25, 0x00, 10, 0x00})));
    m.deliver(packFrame(PacketType::DATA_MDR_NO2, 1, B({0xF5, 0x00, 0x00})));
    REQUIRE(m.written.size() == 2);
    CHECK_EQ(m.written[0], serializeACK(0));
    CHECK_EQ(m.written[1], serializeACK(1));
    REQUIRE(m.dispatched.size() == 2);
    CHECK(m.dispatched[1].first == Table::T2);
}

TEST(LinkPriorityRequestsGoFirst) {
    ManualLink m;
    auto results = std::make_shared<std::vector<LinkResult>>();
    m.link.submit(request(B({0x01, 0x01}), results), m.now);
    m.link.submit(request(B({0x02, 0x02}), results), m.now);
    m.link.submit(request(B({0x03, 0x03}), results, true), m.now);
    m.ack(1);
    CHECK_EQ(m.lastWritten().payload, B({0x03, 0x03}));
}

TEST(LinkKeepsLast64DataFrames) {
    ManualLink m;
    for (int i = 0; i < 70; ++i) {
        m.deliver(packFrame(PacketType::DATA_MDR, static_cast<uint8_t>(i & 1), B({0x25, 0x00, static_cast<uint8_t>(i), 0x00})));
    }
    auto frames = m.link.recentFrames();
    CHECK_EQ(frames.size(), size_t{64});
    CHECK_EQ(frames.front().payload[2], uint8_t{6});
    CHECK(std::none_of(frames.begin(), frames.end(), [](const FrameRecord& f) { return f.type == PacketType::ACK; }));
}

TEST(LinkResetCancelsAndRestartsSeq) {
    ManualLink m;
    auto results = std::make_shared<std::vector<LinkResult>>();
    m.link.submit(request(B({0x22, 0x00}), results), m.now);
    m.ack(1);
    m.link.submit(request(B({0x12, 0x02}), results), m.now);
    m.link.reset();
    REQUIRE(results->size() == 2);
    CHECK(results->at(1) == LinkResult::Cancelled);
    CHECK_EQ(m.link.sequence(), uint8_t{0});
    CHECK(m.link.idle());
}

// ---------------------------------------------------------------------------
// Init sequence
// ---------------------------------------------------------------------------

TEST(InitSequenceFollowsProtocolOrder) {
    Harness h;
    h.connect();
    h.advance(200);
    CHECK(h.session->initComplete());
    const std::vector<std::pair<Table, Bytes>> expected = {
        {Table::T1, B({0x00, 0x00})}, {Table::T1, B({0x02, 0x00})}, {Table::T1, B({0x04, 0x02})},
        {Table::T1, B({0x04, 0x01})}, {Table::T1, B({0x04, 0x03})}, {Table::T1, B({0x06, 0x00})},
        {Table::T2, B({0x06, 0x00})},
        {Table::T1, B({0xD0, 0xD1, 0x01})}, {Table::T1, B({0xE0, 0x01})}, {Table::T1, B({0xF0, 0x03})},
        {Table::T1, B({0x50, 0x00, 0x01})},
        {Table::T1, B({0x22, 0x00})}, {Table::T1, B({0x94, 0x00, 0x00})}, {Table::T1, B({0xD6, 0xD1})},
        {Table::T1, B({0x12, 0x02})}, {Table::T1, B({0xA6, 0x20})}, {Table::T1, B({0x66, 0x17})},
        {Table::T2, B({0x32, 0x02})}, {Table::T2, B({0x36, 0x02})}, {Table::T2, B({0x36, 0x01})},
        {Table::T1, B({0xF6, 0x0C})}, {Table::T1, B({0xFA, 0x0C})}, {Table::T1, B({0x52, 0x00})},
        {Table::T1, B({0x56, 0x00})}, {Table::T1, B({0xE6, 0x00})}, {Table::T1, B({0xE2, 0x01})},
        {Table::T1, B({0xE6, 0x01})}, {Table::T1, B({0xF6, 0x03})}, {Table::T1, B({0x66, 0x30})},
        {Table::T1, B({0x26, 0x05})}, {Table::T1, B({0xF6, 0x01})}, {Table::T2, B({0xF2, 0x00})},
        {Table::T2, B({0x46, 0x01})}, {Table::T2, B({0x46, 0x20})}, {Table::T1, B({0xC4, 0x01, 0x00})},
    };
    CHECK_EQ(h.fake.received.size(), expected.size());
    for (size_t i = 0; i < std::min(expected.size(), h.fake.received.size()); ++i) {
        if (h.fake.received[i] != expected[i]) {
            testutil::fail("step " + std::to_string(i) + ": got " + hexBytes(h.fake.received[i].second) +
                           ", expected " + hexBytes(expected[i].second), __FILE__, __LINE__);
            break;
        }
    }
}

TEST(InitAlternatesSeqAndAcksDevicePackets) {
    Harness h;
    h.connect();
    h.advance(200);
    REQUIRE(!h.fake.receivedSeqs.empty());
    for (size_t i = 0; i < h.fake.receivedSeqs.size(); ++i) {
        CHECK_EQ(h.fake.receivedSeqs[i], static_cast<uint8_t>(i & 1));
    }
    REQUIRE(!h.fake.hostAckSeqs.empty());
    for (size_t i = 0; i < h.fake.hostAckSeqs.size(); ++i) {
        CHECK_EQ(h.fake.hostAckSeqs[i], static_cast<uint8_t>(1 - (i & 1)));
    }
}

TEST(DroppedFirstPacketIsRetriedWithSameSeq) {
    auto profile = FakeHeadsetProfile::xm5();
    profile.dropFirstDataPackets = 1;
    Harness h(profile);
    h.connect();
    CHECK(!h.session->state().connected);
    CHECK(h.fake.received.empty());
    h.advance(1100);
    CHECK_EQ(h.fake.droppedPackets, 1);
    REQUIRE(!h.fake.received.empty());
    CHECK_EQ(h.fake.received[0].second, B({0x00, 0x00}));
    CHECK_EQ(h.fake.receivedSeqs[0], uint8_t{0});
    h.advance(200);
    CHECK(h.session->initComplete());
    CHECK(h.drops.empty());
}

TEST(UnansweredHandshakeDropsTheLink) {
    Harness h;
    h.forwardLimit = 0;
    h.connect();
    h.advance(9900);
    CHECK(h.drops.empty());
    h.advance(200);
    CHECK_EQ(h.drops.size(), size_t{1});
    CHECK(!h.session->state().connected);
}

TEST(ReplyBeforeAckDeviceStillInitializes) {
    auto profile = FakeHeadsetProfile::xm5();
    profile.replyBeforeAck = true;
    Harness h(profile);
    h.connect();
    h.advance(200);
    CHECK(h.session->initComplete());
    CHECK_EQ(h.status()["battery_level"].number, 80.0);
}

TEST(VariantsAreChosenFromTheAdvertisedList) {
    FakeHeadsetProfile profile;
    profile.table1 = {0x28, 0x6D, 0x24, 0xE3, 0xFE, 0xD3};
    profile.table2 = {0x30, 0x44};
    profile.multipointSlot = 0xD3;
    Harness h(profile);
    h.connect();
    h.advance(200);
    CHECK(sentPayload(h, Table::T1, B({0x22, 0x08})));
    CHECK(sentPayload(h, Table::T1, B({0x66, 0x19})));
    CHECK(sentPayload(h, Table::T1, B({0x26, 0x04})));
    CHECK(sentPayload(h, Table::T1, B({0xE6, 0x02})));
    CHECK(sentPayload(h, Table::T1, B({0xF0, 0x0E})));
    CHECK(sentPayload(h, Table::T1, B({0xD0, 0xD3, 0x01})));
    CHECK(sentPayload(h, Table::T2, B({0x36, 0x00})));
    CHECK(sentPayload(h, Table::T2, B({0x46, 0x02})));
    CHECK(!sentPayload(h, Table::T1, B({0x66, 0x17})));
    Json probe = *testutil::parseJson(h.session->probeJson());
    CHECK_EQ(probe["nc_asm_type"].string, std::string("0x19"));

    size_t mark = h.fake.received.size();
    CHECK_EQ(h.run("noise ambient"), std::string("OK"));
    auto sent = h.sentSince(mark);
    REQUIRE(!sent.empty());
    CHECK_EQ(sent.back().second.size(), size_t{9});
    CHECK_EQ(sent.back().second[1], uint8_t{0x19});
    CHECK_EQ(h.run("multipoint on"), std::string("OK"));
    CHECK(sentPayload(h, Table::T1, B({0xD8, 0xD3, 0x00, 0x00})));
    CHECK_EQ(h.run("auto-power-off when_taken_off"), std::string("ERR unsupported auto_power_off when_taken_off"));
    CHECK_EQ(h.run("auto-power-off 30min"), std::string("OK"));
    CHECK(sentPayload(h, Table::T1, B({0x28, 0x04, 0x01, 0x01})));
}

TEST(FallbackWhenSupportListIsMissing) {
    auto profile = FakeHeadsetProfile::xm5();
    profile.answerSupportFunction = false;
    Harness h(profile);
    h.fake.battery = 15;
    h.fake.ncTotalEffect = 0x00;
    h.fake.ncLevel = 5;
    h.fake.speakToChat = false;
    h.fake.s2cSensitivity = 0x02;
    h.fake.s2cTimeout = 0x03;
    h.fake.autoPowerOff = 0x11;
    h.fake.ncAmbButton = 0x03;
    h.fake.volume = 7;
    h.connect();
    h.advance(2000);
    CHECK(h.session->initComplete());
    CHECK(!sentPayload(h, Table::T2, B({0x06, 0x00})));
    CHECK(!sentPayload(h, Table::T1, B({0x50, 0x00, 0x01})));
    CHECK_EQ(h.status()["supported_source"].string, std::string("fallback"));
    CHECK_EQ(h.run("eq custom 0 0 0 0 0 0"), std::string("OK"));
    CHECK_EQ(h.run("eq off"), std::string("OK"));
    h.advance(200);
    CHECK_EQ(fixtureDiff(h.status(), "status_v2_fallback.json"), std::string(""));
    CHECK_EQ(h.run("dsee on"), std::string("ERR unsupported dsee"));
}

// ---------------------------------------------------------------------------
// status.json against the contract fixtures
// ---------------------------------------------------------------------------

TEST(InitializingStatusMatchesFixture) {
    Harness h;
    h.forwardLimit = 1;
    h.connect();
    CHECK(h.session->state().connected);
    CHECK_EQ(fixtureDiff(h.status(), "status_v2_initializing.json"), std::string(""));
    CHECK_EQ(h.run("noise anc"), std::string("ERR not ready"));
}

TEST(FullStatusMatchesFixture) {
    Harness h;
    h.fake.ncMode = 0x01;
    h.fake.ncAmbientMode = 0x01;
    h.connect();
    h.advance(200);
    CHECK_EQ(fixtureDiff(h.status(), "status_v2_full.json"), std::string(""));
}

TEST(MinimalStatusMatchesFixture) {
    auto profile = FakeHeadsetProfile::minimal();
    profile.firmware.clear();
    Harness h(profile);
    h.fake.battery = 45;
    h.fake.charging = 0x01;
    h.fake.ncLevel = 20;
    h.connect();
    h.advance(1500);
    CHECK(h.session->initComplete());
    CHECK_EQ(fixtureDiff(h.status(), "status_v2_minimal.json"), std::string(""));
}

TEST(MultipointStatusMatchesFixture) {
    FakeHeadsetProfile profile;
    profile.table1 = {0x12, 0x20, 0x25, 0x50, 0x69, 0x6B, 0x90, 0xA1, 0xD1, 0xE1, 0xE2, 0xF1, 0xFC};
    profile.table2 = {0x32, 0x31, 0x41};
    Harness h(profile);
    h.fake.battery = 62;
    h.fake.ncLevel = 10;
    h.fake.dsee = false;
    h.fake.connectionQuality = 0x01;
    h.fake.codec = 0x02;
    h.fake.volume = 18;
    h.fake.speakToChat = false;
    h.fake.s2cSensitivity = 0x01;
    h.fake.s2cTimeout = 0x00;
    h.fake.voiceGuidance = false;
    h.fake.autoPowerOff = 0x01;
    h.fake.ncAmbButton = 0x02;
    h.fake.earDetection = false;
    h.fake.worn = 0x01;
    h.fake.multipoint = true;
    h.fake.playbackSlot = 2;
    h.fake.devices = {{"F0:1D:BC:AA:00:01", "laptop", 1}, {"5C:E9:1E:BB:00:02", "Pixel 8", 2},
                      {"3C:22:FB:CC:00:03", "", 0}};
    h.connect("My XM5");
    h.advance(200);
    CHECK_EQ(h.run("eq custom 0 0 0 0 0 0"), std::string("OK"));
    CHECK_EQ(h.run("eq bright"), std::string("OK"));
    h.advance(100);
    CHECK_EQ(fixtureDiff(h.status(), "status_v2_multipoint.json"), std::string(""));
}

TEST(StatusIsWrittenOnlyWhenSomethingChanges) {
    Harness h;
    h.connect();
    h.advance(200);
    int before = h.changes;
    h.push(Table::T1, B({0x25, 0x00, 80, 0x00}));
    CHECK_EQ(h.changes, before);
    h.push(Table::T1, B({0x25, 0x00, 79, 0x00}));
    CHECK_EQ(h.changes, before + 1);
    CHECK_EQ(h.status()["battery_level"].number, 79.0);
}

TEST(DisconnectWritesThreeKeyDocument) {
    Harness h;
    h.connect();
    h.advance(200);
    h.session->onTransportDisconnected(h.now);
    Json j = h.status();
    CHECK_EQ(j.keyOrder.size(), size_t{3});
    CHECK(!j["connected"].boolean);
    CHECK_EQ(h.run("noise anc"), std::string("ERR not connected"));
    CHECK_EQ(h.run("probe"), std::string("ERR not connected"));
}

// ---------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------

TEST(NoiseVerbsKeepStoredAmbientValues) {
    Harness h;
    h.fake.ncMode = 0x01;
    h.fake.ncAmbientMode = 0x01;
    h.fake.ncLevel = 14;
    h.connect();
    h.advance(200);
    CHECK_EQ(h.run("noise anc"), std::string("OK"));
    CHECK_EQ(h.fake.received.back().second, B({0x68, 0x17, 0x01, 0x01, 0x00, 0x01, 0x0E}));
    CHECK_EQ(h.status()["noise_mode"].string, std::string("anc"));
    CHECK_EQ(h.run("noise off"), std::string("OK"));
    CHECK_EQ(h.fake.received.back().second, B({0x68, 0x17, 0x01, 0x00, 0x00, 0x01, 0x0E}));
    CHECK_EQ(h.run("ambient-level 3"), std::string("OK"));
    CHECK_EQ(h.fake.received.back().second, B({0x68, 0x17, 0x01, 0x01, 0x01, 0x01, 0x03}));
    CHECK_EQ(h.run("focus-voice off"), std::string("OK"));
    CHECK_EQ(h.fake.received.back().second, B({0x68, 0x17, 0x01, 0x01, 0x01, 0x00, 0x03}));
    Json j = h.status();
    CHECK_EQ(j["noise_mode"].string, std::string("ambient"));
    CHECK_EQ(j["ambient_sound_level"].number, 3.0);
    CHECK(!j["voice_passthrough"].boolean);
    CHECK_EQ(h.run("noise wind"), std::string("ERR usage: noise anc|ambient|off"));
}

TEST(SetterVerbsSendProtocolBytes) {
    Harness h;
    h.connect();
    h.advance(200);
    struct Case {
        const char* line;
        Table table;
        Bytes payload;
    };
    const Case cases[] = {
        {"eq bright", Table::T1, B({0x58, 0x00, 0x10, 0x00})},
        {"eq custom", Table::T1, B({0x58, 0x00, 0xA0, 0x00})},
        {"eq custom 1 2 3 4 5 -6", Table::T1, B({0x58, 0x00, 0xA0, 0x06, 4, 11, 12, 13, 14, 15})},
        {"dsee off", Table::T1, B({0xE8, 0x01, 0x00})},
        {"speak-to-chat off", Table::T1, B({0xF8, 0x0C, 0x01, 0x01})},
        {"speak-to-chat-config low long", Table::T1, B({0xFC, 0x0C, 0x02, 0x02})},
        {"voice-guidance off", Table::T2, B({0x48, 0x01, 0x01})},
        {"voice-guidance-volume -1", Table::T2, B({0x48, 0x20, 0xFF, 0x00})},
        {"auto-power-off 60min", Table::T1, B({0x28, 0x05, 0x02, 0x02})},
        {"nc-amb-button ambient_off", Table::T1, B({0x68, 0x30, 0x04})},
        {"button-function google_assistant", Table::T1, B({0xF8, 0x03, 0x01, 0x31})},
        {"ear-detect off", Table::T1, B({0xF8, 0x01, 0x01})},
        {"volume 25", Table::T1, B({0xA8, 0x20, 25})},
        {"connection-quality connection", Table::T1, B({0xE8, 0x00, 0x01})},
    };
    for (const auto& c : cases) {
        size_t mark = h.fake.received.size();
        std::string reply = h.run(c.line);
        if (reply != "OK") testutil::fail(std::string(c.line) + " -> " + reply, __FILE__, __LINE__);
        auto sent = h.sentSince(mark);
        bool found = std::any_of(sent.begin(), sent.end(),
                                 [&](const auto& r) { return r.first == c.table && r.second == c.payload; });
        if (!found) testutil::fail(std::string(c.line) + " did not send " + hexBytes(c.payload), __FILE__, __LINE__);
    }
    h.advance(200);
    Json j = h.status();
    CHECK_EQ(j["eq_preset"].string, std::string("custom"));
    CHECK_EQ(j["clear_bass"].number, -6.0);
    CHECK(!j["dsee_extreme"].boolean);
    CHECK_EQ(j["speak_to_chat_sensitivity"].string, std::string("low"));
    CHECK_EQ(j["speak_to_chat_timeout"].string, std::string("long"));
    CHECK(!j["voice_guidance"].boolean);
    CHECK_EQ(j["voice_guidance_volume"].number, -1.0);
    CHECK_EQ(j["auto_power_off"].string, std::string("60min"));
    CHECK_EQ(j["nc_amb_button"].string, std::string("ambient_off"));
    CHECK_EQ(j["button_function"].string, std::string("google_assistant"));
    CHECK(!j["ear_detection"].boolean);
    CHECK_EQ(j["volume"].number, 25.0);
    CHECK_EQ(j["connection_quality"].string, std::string("connection"));
}

TEST(EqSetIsFollowedByValueRead) {
    Harness h;
    h.connect();
    h.advance(200);
    size_t mark = h.fake.received.size();
    CHECK_EQ(h.run("eq vocal"), std::string("OK"));
    h.advance(100);
    auto sent = h.sentSince(mark);
    REQUIRE(sent.size() >= 2);
    CHECK_EQ(sent[0].second, B({0x58, 0x00, 0x14, 0x00}));
    CHECK_EQ(sent[1].second, B({0x56, 0x00}));
}

TEST(MultipointConfirmsTheAlert) {
    Harness h;
    h.connect();
    h.advance(200);
    CHECK_EQ(h.run("multipoint on"), std::string("OK"));
    h.advance(100);
    REQUIRE(h.fake.alertsAnswered.size() == 1);
    CHECK_EQ(h.fake.alertsAnswered[0], B({0x98, 0x00, 0x07, 0x01}));
    CHECK(h.status()["multipoint"].boolean);
}

TEST(UnrequestedAlertIsNotConfirmed) {
    Harness h;
    h.connect();
    h.advance(200);
    h.push(Table::T1, B({0x99, 0x00, 0x07, 0x01}));
    h.advance(100);
    CHECK(h.fake.alertsAnswered.empty());
}

TEST(SourceSwitchChecksConnectedDevices) {
    Harness h;
    h.fake.devices = {{"F0:1D:BC:AA:00:01", "laptop", 1}, {"5C:E9:1E:BB:00:02", "Pixel 8", 2},
                      {"3C:22:FB:CC:00:03", "", 0}};
    h.connect();
    h.advance(200);
    CHECK_EQ(h.run("source 3c:22:fb:cc:00:03"), std::string("ERR unsupported source_switch 3C:22:FB:CC:00:03"));
    CHECK_EQ(h.run("source 11:22:33:44:55:66"), std::string("ERR unsupported source_switch 11:22:33:44:55:66"));
    size_t mark = h.fake.received.size();
    CHECK_EQ(h.run("source 5c:e9:1e:bb:00:02"), std::string("OK"));
    h.advance(100);
    auto sent = h.sentSince(mark);
    REQUIRE(sent.size() >= 2);
    CHECK(sent[0].first == Table::T2);
    CHECK_EQ(sent[0].second, req::setSourceSwitch("5C:E9:1E:BB:00:02"));
    CHECK_EQ(sent[1].second, B({0x36, 0x02}));
    Json devices = h.status()["devices"];
    REQUIRE(devices.array.size() == 3);
    CHECK(!devices.array[0]["active"].boolean);
    CHECK(devices.array[1]["active"].boolean);
}

TEST(GatingFollowsContractOrder) {
    auto profile = FakeHeadsetProfile::minimal();
    Harness h(profile);
    CHECK_EQ(h.run("dsee on"), std::string("ERR not connected"));
    CHECK_EQ(h.run("dsee maybe"), std::string("ERR usage: dsee on|off"));
    CHECK_EQ(h.run("frobnicate"), std::string("ERR unknown command 'frobnicate'"));
    h.connect();
    h.advance(1500);
    CHECK_EQ(h.run("dsee on"), std::string("ERR unsupported dsee"));
    CHECK_EQ(h.run("eq bright"), std::string("ERR unsupported eq_preset"));
    CHECK_EQ(h.run("source F0:1D:BC:AA:00:01"), std::string("ERR unsupported source_switch"));
    CHECK_EQ(h.run("noise anc"), std::string("OK"));
}

TEST(ListChecksUseHeadsetOptions) {
    Harness h;
    h.connect();
    h.advance(200);
    CHECK_EQ(h.run("eq user1"), std::string("ERR unsupported eq_preset user1"));
    CHECK_EQ(h.run("button-function voice_recognition"),
             std::string("ERR unsupported button_function voice_recognition"));
    CHECK_EQ(h.run("button-function none"), std::string("ERR unsupported button_function none"));
}

TEST(DeviceTimeoutAfterRetry) {
    Harness h;
    h.connect();
    h.advance(200);
    h.forwardLimit = h.forwarded;
    auto parsed = cmd::parseLine("dsee on", false);
    auto reply = std::make_shared<std::optional<std::string>>();
    size_t writes = static_cast<size_t>(h.forwarded);
    h.session->execute(*parsed.command, [reply](std::string r) { *reply = std::move(r); }, h.now);
    h.advance(850);
    CHECK(!reply->has_value());
    h.advance(900);
    CHECK(!reply->has_value());
    h.advance(100);
    CHECK_EQ(reply->value_or(""), std::string("ERR device timeout"));
    CHECK_EQ(static_cast<size_t>(h.forwarded), writes);
}

TEST(LateEqValueTurnsEqCustomOn) {
    FakeHeadsetProfile profile;
    profile.table1 = {0x20, 0x50};
    profile.table2 = {};
    Harness h(profile);
    h.forwardLimit = 7;
    h.connect();
    h.advance(3500);
    h.forwardLimit = -1;
    REQUIRE(h.status()["supported"].has("eq_custom"));
    CHECK(!h.status()["supported"]["eq_custom"].boolean);
    h.push(Table::T1, B({0x59, 0x00, 0xA0, 0x06, 10, 10, 10, 10, 10, 12}));
    CHECK(h.status()["supported"]["eq_custom"].boolean);
    CHECK_EQ(h.status()["eq_custom_bands"].array[4].number, 2.0);
}

TEST(PeriodicSyncRereadsBatteryVolumeAndWorn) {
    Harness h;
    h.connect();
    h.advance(200);
    size_t mark = h.fake.received.size();
    h.advance(29000);
    CHECK(h.sentSince(mark).empty());
    h.advance(1500);
    auto sent = h.sentSince(mark);
    REQUIRE(sent.size() == 3);
    CHECK_EQ(sent[0].second, B({0x22, 0x00}));
    CHECK_EQ(sent[1].second, B({0xA6, 0x20}));
    CHECK(sent[2].first == Table::T2);
    CHECK_EQ(sent[2].second, B({0xF2, 0x00}));
}

TEST(WearLogLineRereadsWornStatus) {
    Harness h;
    h.connect();
    h.advance(200);
    h.fake.worn = 0x04;
    size_t mark = h.fake.received.size();
    Bytes line = B({0xC9, 0x01, 0x00});
    std::string text = "unitRemove";
    line.insert(line.end(), text.begin(), text.end());
    h.push(Table::T1, line);
    h.advance(100);
    auto sent = h.sentSince(mark);
    REQUIRE(sent.size() == 1);
    CHECK_EQ(sent[0].second, B({0xF2, 0x00}));
    CHECK(!h.status()["worn"].boolean);
    CHECK(!h.status()["worn"].isNull());
}

TEST(ProbeReportsDeviceData) {
    Harness h;
    h.connect();
    h.advance(200);
    std::string reply = h.run("probe");
    auto j = testutil::parseJson(reply);
    REQUIRE(j.has_value());
    CHECK_EQ((*j)["address"].string, std::string(kAddress));
    CHECK_EQ((*j)["protocol"]["version"].number, 4096.0);
    CHECK((*j)["protocol"]["table2"].boolean);
    CHECK_EQ((*j)["unique_id"].string, std::string("FAKE-XM5-0001"));
    CHECK_EQ((*j)["color"].string, std::string("0x01"));
    CHECK_EQ((*j)["support_table1"].array.size(), size_t{15});
    CHECK_EQ((*j)["support_table2"].array.size(), size_t{4});
    CHECK_EQ((*j)["general_settings"].array[0]["subject"].string, std::string("MULTIPOINT_SETTING"));
    CHECK_EQ((*j)["eq_capability"]["bands"].number, 6.0);
    CHECK_EQ((*j)["eq_capability"]["presets"].array.size(), size_t{10});
    CHECK_EQ((*j)["assignable"]["type"].string, std::string("0x03"));
    CHECK_EQ((*j)["assignable"]["keys"].array[0]["key"].string, std::string("0x04"));
    CHECK_EQ((*j)["assignable"]["value"].array[0].string, std::string("0x00"));
    CHECK_EQ((*j)["recent_frames"].array.size(), size_t{64});
    CHECK(reply.find('\n') == std::string::npos);
}

TEST(ReconnectStartsAFreshSession) {
    Harness h;
    h.connect();
    h.advance(200);
    h.session->onTransportDisconnected(h.now);
    h.fake.received.clear();
    h.fake.receivedSeqs.clear();
    h.connect();
    h.advance(200);
    REQUIRE(!h.fake.receivedSeqs.empty());
    CHECK_EQ(h.fake.receivedSeqs[0], uint8_t{0});
    CHECK_EQ(h.fake.received[0].second, B({0x00, 0x00}));
    CHECK(h.session->initComplete());
}

int main() {
    return testutil::runAll("SessionTest");
}
