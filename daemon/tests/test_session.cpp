#include "Commands.hpp"
#include "FakeHeadset.hpp"
#include "HeadsetSession.hpp"
#include "MdrLink.hpp"
#include "SessionManager.hpp"
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

Bytes withText(Bytes prefix, const std::string& text, Bytes suffix = {}) {
    prefix.push_back(static_cast<uint8_t>(text.size()));
    prefix.insert(prefix.end(), text.begin(), text.end());
    prefix.insert(prefix.end(), suffix.begin(), suffix.end());
    return prefix;
}

constexpr const char* kAddress = "AC:80:0A:12:34:56";
constexpr const char* kWfAddress = "AC:80:0A:65:43:21";
constexpr const char* kAdapter = "F0:1D:BC:AA:00:01";

// One simulated headset wired to its own session.
struct Device {
    explicit Device(FakeHeadsetProfile profile) : fake(std::move(profile)) {}
    FakeHeadset fake;
    std::string id;
    std::deque<Bytes> inbox;
    int forwardLimit{-1};
    int forwarded{0};
    std::vector<std::string> drops;
    HeadsetSession* session{nullptr};
};

struct Harness {
    int64_t unixClock{1760438400};
    int writes{0};
    std::string lastWritten;
    SessionManager manager;
    std::vector<std::unique_ptr<Device>> devices;
    Device& first;
    FakeHeadset& fake;
    HeadsetSession* session;
    Clock::time_point now{Clock::time_point{} + std::chrono::hours(1)};

    explicit Harness(FakeHeadsetProfile profile = FakeHeadsetProfile::xm5(), const std::string& id = kAddress)
        : manager(managerHooks()), first(add(std::move(profile), id)), fake(first.fake), session(first.session) {}

    SessionManager::Hooks managerHooks() {
        SessionManager::Hooks hooks;
        hooks.write = [this](const std::string& json) {
            ++writes;
            lastWritten = json;
        };
        hooks.unixNow = [this] { return unixClock; };
        return hooks;
    }

    Device& add(FakeHeadsetProfile profile, const std::string& id) {
        devices.push_back(std::make_unique<Device>(std::move(profile)));
        Device* d = devices.back().get();
        d->id = id;
        HeadsetSession::Hooks hooks;
        hooks.write = [d](const Bytes& bytes) {
            if (d->forwardLimit >= 0 && d->forwarded >= d->forwardLimit) return true;
            auto u = unpackFrame(bytes);
            if (u && u->type != PacketType::ACK) ++d->forwarded;
            Bytes answer = d->fake.receive(bytes);
            if (!answer.empty()) d->inbox.push_back(std::move(answer));
            return true;
        };
        hooks.dropLink = [d](const std::string& reason) { d->drops.push_back(reason); };
        d->session = &manager.add(id, std::move(hooks));
        return *d;
    }

    void pump() {
        for (int guard = 0; guard < 100000; ++guard) {
            bool any = false;
            for (auto& d : devices) {
                if (d->inbox.empty() || !manager.find(d->id)) continue;
                Bytes bytes = std::move(d->inbox.front());
                d->inbox.pop_front();
                d->session->onBytes(bytes, now);
                any = true;
            }
            if (!any) return;
        }
    }

    void advance(int ms) {
        for (int t = 0; t < ms; t += 50) {
            now += std::chrono::milliseconds(50);
            manager.tick(now);
            pump();
        }
    }

    void connect(Device& d, const std::string& alias) {
        d.fake.reconnect();
        d.session->onTransportConnected(alias, alias, d.id, now);
        pump();
    }

    void connect(const std::string& alias = "WH-1000XM5") { connect(first, alias); }

    void push(Device& d, Table table, const Bytes& payload) {
        d.inbox.push_back(d.fake.pushPayload(table, payload));
        pump();
    }

    void push(Table table, const Bytes& payload) { push(first, table, payload); }

    std::string run(const std::string& line, int maxWaitMs = 4000) {
        auto parsed = cmd::parseLine(line, true);
        if (!parsed.command) return "ERR " + parsed.error;
        auto reply = std::make_shared<std::optional<std::string>>();
        manager.execute(*parsed.command, [reply](std::string r) { *reply = std::move(r); }, now);
        pump();
        for (int waited = 0; !*reply && waited < maxWaitMs; waited += 50) advance(50);
        return reply->value_or("<no reply>");
    }

    Json doc() const {
        auto j = testutil::parseJson(manager.statusJson(1760438400));
        return j ? *j : Json{};
    }

    // The first entry of `headsets`.
    Json status() const {
        Json d = doc();
        if (d["headsets"].array.empty()) return Json{};
        return d["headsets"].array[0];
    }

    Json entry(const std::string& id) const {
        for (const auto& h : doc()["headsets"].array) {
            if (h["id"].string == id) return h;
        }
        return Json{};
    }

    std::vector<std::pair<Table, Bytes>> sentSince(size_t index) const { return sentSince(first, index); }

    static std::vector<std::pair<Table, Bytes>> sentSince(const Device& d, size_t index) {
        const auto& r = d.fake.received;
        return {r.begin() + static_cast<long>(std::min(index, r.size())), r.end()};
    }
};

const std::vector<std::string> kVolatile = {"last_updated", "connected_since"};

std::string fixtureDiff(const Json& document, const char* name) {
    auto fixture = testutil::parseJson(testutil::readFile(std::string(SONY_FIXTURES_DIR "/") + name));
    if (!fixture) return std::string("cannot read fixture ") + name;
    return testutil::jsonDiff(document, *fixture, kVolatile);
}

// Compares one schema 3 entry with a schema 2 fixture: every v2 key that v3
// keeps must have the same value; the keys new in v3 must be null (or false in
// `supported`) for a headset without earbud features.
std::string v2FixtureDiff(const Json& entry, const char* name) {
    auto fixture = testutil::parseJson(testutil::readFile(std::string(SONY_FIXTURES_DIR "/") + name));
    if (!fixture) return std::string("cannot read fixture ") + name;
    const std::vector<std::string> dropped = {"schema_version", "connected", "last_updated", "charging",
                                              "ambient_level", "eq_bands", "dsee"};
    for (const auto& key : fixture->keyOrder) {
        if (std::find(dropped.begin(), dropped.end(), key) != dropped.end()) continue;
        if (!entry.has(key)) return key + ": missing in the v3 entry";
        if (key == "supported" && !(*fixture)[key].isNull()) {
            for (const auto& sk : (*fixture)[key].keyOrder) {
                auto d = testutil::jsonDiff(entry[key][sk], (*fixture)[key][sk], {}, "supported." + sk);
                if (!d.empty()) return d;
            }
            for (const char* sk : {"battery_lr", "battery_case", "touch_function", "sidetone"}) {
                if (entry[key][sk].boolean) return std::string("supported.") + sk + " is true";
            }
            continue;
        }
        auto d = testutil::jsonDiff(entry[key], (*fixture)[key], {}, key);
        if (!d.empty()) return d;
    }
    for (const char* key : {"battery_left", "battery_left_charging", "battery_right", "battery_right_charging",
                            "battery_case", "battery_case_charging", "touch_function_left", "touch_function_right",
                            "touch_function_left_options", "touch_function_right_options", "worn_left", "worn_right",
                            "sidetone"}) {
        if (!entry.has(key)) return std::string(key) + ": missing";
        if (!entry[key].isNull()) return std::string(key) + ": not null";
    }
    return "";
}

bool sentPayload(const Device& d, Table table, const Bytes& payload) {
    return std::any_of(d.fake.received.begin(), d.fake.received.end(),
                       [&](const auto& r) { return r.first == table && r.second == payload; });
}

bool sentPayload(const Harness& h, Table table, const Bytes& payload) { return sentPayload(h.first, table, payload); }

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

FakeHeadsetProfile wfProfile() { return FakeHeadsetProfile::wf1000xm5Capture(); }

// A WF-1000XM5 with the fw 6.1.0 capture values, ready.
struct WfHarness : Harness {
    WfHarness() : Harness(wfProfile(), kWfAddress) {
        fake.useFixtureValues();
        connect("WF-1000XM5");
        advance(300);
    }
};

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
        {Table::T1, B({0xD0, 0xD1, 0x01})}, {Table::T1, B({0xD0, 0xD2, 0x01})}, {Table::T1, B({0xE0, 0x01})},
        {Table::T1, B({0xF0, 0x03})}, {Table::T1, B({0x50, 0x00, 0x01})}, {Table::T1, B({0xF0, 0x0D})},
        {Table::T1, B({0x22, 0x00})}, {Table::T1, B({0x94, 0x00, 0x00})}, {Table::T1, B({0xD6, 0xD1})},
        {Table::T1, B({0xD6, 0xD2})},
        {Table::T1, B({0x12, 0x02})}, {Table::T1, B({0xA6, 0x20})}, {Table::T1, B({0x66, 0x17})},
        {Table::T2, B({0x32, 0x02})}, {Table::T2, B({0x36, 0x02})}, {Table::T2, B({0x36, 0x01})},
        {Table::T1, B({0xF6, 0x0C})}, {Table::T1, B({0xFA, 0x0C})}, {Table::T1, B({0x52, 0x00})},
        {Table::T1, B({0x56, 0x00})}, {Table::T1, B({0xE6, 0x00})}, {Table::T1, B({0xE2, 0x01})},
        {Table::T1, B({0xE6, 0x01})}, {Table::T1, B({0xF6, 0x03})}, {Table::T1, B({0x66, 0x30})},
        {Table::T1, B({0x26, 0x05})}, {Table::T1, B({0xF6, 0x01})}, {Table::T2, B({0xF2, 0x00})},
        {Table::T2, B({0x46, 0x01})}, {Table::T2, B({0x46, 0x20})}, {Table::T1, B({0xF6, 0x0D})},
        {Table::T1, B({0xC4, 0x01, 0x00})},
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

TEST(WfInitSequenceMatchesCaptureOrder) {
    WfHarness h;
    CHECK(h.session->initComplete());
    const std::vector<std::pair<Table, Bytes>> expected = {
        {Table::T1, B({0x00, 0x00})}, {Table::T1, B({0x02, 0x00})}, {Table::T1, B({0x04, 0x02})},
        {Table::T1, B({0x04, 0x01})}, {Table::T1, B({0x04, 0x03})}, {Table::T1, B({0x06, 0x00})},
        {Table::T2, B({0x06, 0x00})},
        {Table::T1, B({0xD0, 0xD1, 0x01})}, {Table::T1, B({0xD0, 0xD2, 0x01})}, {Table::T1, B({0xE0, 0x01})},
        {Table::T1, B({0xF0, 0x03})}, {Table::T1, B({0x50, 0x00, 0x01})}, {Table::T1, B({0xF0, 0x0D})},
        {Table::T1, B({0x22, 0x09})}, {Table::T1, B({0x22, 0x0A})},
        {Table::T1, B({0x94, 0x00, 0x00})}, {Table::T1, B({0xD6, 0xD1})}, {Table::T1, B({0xD6, 0xD2})},
        {Table::T1, B({0x12, 0x02})}, {Table::T1, B({0xA6, 0x20})}, {Table::T1, B({0x66, 0x17})},
        {Table::T2, B({0x32, 0x02})}, {Table::T2, B({0x36, 0x02})}, {Table::T2, B({0x36, 0x01})},
        {Table::T1, B({0xF6, 0x0C})}, {Table::T1, B({0xFA, 0x0C})}, {Table::T1, B({0x52, 0x00})},
        {Table::T1, B({0x56, 0x00})}, {Table::T1, B({0xE2, 0x01})}, {Table::T1, B({0xE6, 0x01})},
        {Table::T1, B({0xF6, 0x03})}, {Table::T1, B({0x26, 0x05})}, {Table::T1, B({0xF6, 0x01})},
        {Table::T2, B({0xF2, 0x00})}, {Table::T2, B({0x46, 0x01})}, {Table::T2, B({0x46, 0x20})},
        {Table::T1, B({0xE0, 0x05})}, {Table::T1, B({0xE2, 0x05})},
        {Table::T1, B({0xC4, 0x01, 0x00})},
    };
    CHECK_EQ(h.fake.received.size(), expected.size());
    for (size_t i = 0; i < std::min(expected.size(), h.fake.received.size()); ++i) {
        if (h.fake.received[i] != expected[i]) {
            testutil::fail("step " + std::to_string(i) + ": got " + hexBytes(h.fake.received[i].second) +
                           ", expected " + hexBytes(expected[i].second), __FILE__, __LINE__);
            break;
        }
    }
    CHECK(!sentPayload(h, Table::T1, B({0x22, 0x00})));
    CHECK(!sentPayload(h, Table::T1, B({0xF6, 0x0D})));
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
    CHECK(h.first.drops.empty());
}

TEST(UnansweredHandshakeDropsTheLink) {
    Harness h;
    h.first.forwardLimit = 0;
    h.connect();
    h.advance(9900);
    CHECK(h.first.drops.empty());
    h.advance(200);
    CHECK_EQ(h.first.drops.size(), size_t{1});
    CHECK(!h.session->state().connected);
    CHECK(h.doc()["headsets"].array.empty());
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
    profile.table1 = {0x28, 0x6D, 0x24, 0xE3, 0xFE, 0xD3, 0x21, 0x22};
    profile.table2 = {0x30, 0x44};
    profile.multipointSlot = 0xD3;
    Harness h(profile);
    h.connect();
    h.advance(200);
    CHECK(sentPayload(h, Table::T1, B({0x22, 0x08})));
    CHECK(sentPayload(h, Table::T1, B({0x22, 0x01})));
    CHECK(sentPayload(h, Table::T1, B({0x22, 0x02})));
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
    CHECK_EQ(h.status()["battery_left"].number, 70.0);
    CHECK_EQ(h.status()["battery_case"].number, 53.0);

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
    CHECK_EQ(v2FixtureDiff(h.status(), "status_v2_fallback.json"), std::string(""));
    CHECK_EQ(h.run("dsee on"), std::string("ERR unsupported dsee"));
}

TEST(WfFallbackUsesTheEarbudList) {
    auto profile = wfProfile();
    profile.answerSupportFunction = false;
    Harness h(profile, kWfAddress);
    h.fake.useFixtureValues();
    h.connect("WF-1000XM5");
    h.advance(2000);
    CHECK(h.session->initComplete());
    Json s = h.status();
    CHECK_EQ(s["supported_source"].string, std::string("fallback"));
    Json sup = s["supported"];
    std::vector<std::string> on;
    for (const auto& k : sup.keyOrder) if (sup[k].boolean) on.push_back(k);
    const std::vector<std::string> expected = {"battery_lr", "battery_case", "noise_control", "eq_preset", "eq_custom",
                                               "speak_to_chat", "voice_guidance", "voice_guidance_volume",
                                               "auto_power_off", "ear_detection", "volume"};
    CHECK(on == expected);
    CHECK(sentPayload(h, Table::T1, B({0x22, 0x09})));
    CHECK(sentPayload(h, Table::T1, B({0x22, 0x0A})));
    CHECK(!sentPayload(h, Table::T1, B({0x22, 0x00})));
    CHECK(!sentPayload(h, Table::T1, B({0x66, 0x30})));
    CHECK_EQ(s["battery_left"].number, 70.0);
    CHECK_EQ(s["battery_case"].number, 53.0);
    CHECK_EQ(s["voice_guidance_volume"].number, 0.0);
    CHECK_EQ(s["kind"].string, std::string("earbuds"));
}

TEST(OtherModelFallbackIsEmpty) {
    auto profile = FakeHeadsetProfile::minimal();
    profile.answerSupportFunction = false;
    profile.modelName = "WH-1000XM6";
    Harness h(profile);
    h.connect("Some headset");
    h.advance(2000);
    Json s = h.status();
    CHECK_EQ(s["model"].string, std::string("other"));
    CHECK_EQ(s["kind"].string, std::string("headphones"));
    for (const auto& k : s["supported"].keyOrder) CHECK(!s["supported"][k].boolean);
    CHECK(!sentPayload(h, Table::T1, B({0x22, 0x00})));
}

// ---------------------------------------------------------------------------
// status.json against the contract fixtures
// ---------------------------------------------------------------------------

TEST(DisconnectedDocumentMatchesFixture) {
    Harness h;
    CHECK_EQ(fixtureDiff(h.doc(), "status_v3_disconnected.json"), std::string(""));
    CHECK_EQ(h.doc().keyOrder.size(), size_t{5});
}

TEST(InitializingStatusMatchesFixture) {
    Harness h(wfProfile(), kWfAddress);
    h.first.forwardLimit = 1;
    h.connect("WF-1000XM5");
    CHECK(h.session->state().connected);
    CHECK_EQ(fixtureDiff(h.doc(), "status_v3_initializing.json"), std::string(""));
    CHECK_EQ(h.run("noise anc"), std::string("ERR not ready"));
}

TEST(InitializingKeepsV2Fields) {
    Harness h;
    h.first.forwardLimit = 1;
    h.connect();
    CHECK_EQ(v2FixtureDiff(h.status(), "status_v2_initializing.json"), std::string(""));
}

TEST(WhCaptureMatchesFixture) {
    Harness h(FakeHeadsetProfile::wh1000xm5Capture());
    h.fake.useFixtureValues();
    h.manager.setAdapterAddresses({kAdapter});
    h.connect();
    h.advance(300);
    CHECK_EQ(fixtureDiff(h.doc(), "status_v3_wh.json"), std::string(""));
}

TEST(WfCaptureMatchesFixture) {
    WfHarness h;
    h.manager.setAdapterAddresses({kAdapter});
    CHECK_EQ(fixtureDiff(h.doc(), "status_v3_wf.json"), std::string(""));
}

TEST(BothHeadsetsMatchFixture) {
    Harness h(FakeHeadsetProfile::wh1000xm5Capture());
    h.manager.setAdapterAddresses({kAdapter});
    h.fake.useFixtureValues();
    h.fake.devices = {{kAdapter, "laptop", 1}, {"5C:E9:1E:BB:00:02", "phone", 2}};
    h.fake.playbackSlot = 2;
    Device& wf = h.add(wfProfile(), kWfAddress);
    wf.fake.useFixtureValues();
    wf.fake.devices = {{kAdapter, "laptop", 1}};
    wf.fake.assignable = {0x35, 0x20};
    h.connect(wf, "WF-1000XM5");
    h.advance(300);
    h.unixClock += 30;
    h.connect();
    h.advance(300);
    Json d = h.doc();
    CHECK_EQ(fixtureDiff(d, "status_v3_both.json"), std::string(""));
    CHECK_EQ(d["primary"].string, std::string(kWfAddress));
    CHECK_EQ(d["headsets"].array[0]["connected_since"].number, 1760438430.0);
    CHECK_EQ(d["headsets"].array[1]["connected_since"].number, 1760438400.0);
}

TEST(FullStatusKeepsV2Fields) {
    Harness h;
    h.fake.ncMode = 0x01;
    h.fake.ncAmbientMode = 0x01;
    h.connect();
    h.advance(200);
    CHECK_EQ(v2FixtureDiff(h.status(), "status_v2_full.json"), std::string(""));
}

TEST(MinimalStatusKeepsV2Fields) {
    auto profile = FakeHeadsetProfile::minimal();
    profile.firmware.clear();
    Harness h(profile);
    h.fake.battery = 45;
    h.fake.charging = 0x01;
    h.fake.ncLevel = 20;
    h.connect();
    h.advance(1500);
    CHECK(h.session->initComplete());
    CHECK_EQ(v2FixtureDiff(h.status(), "status_v2_minimal.json"), std::string(""));
}

TEST(MultipointStatusKeepsV2Fields) {
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
    CHECK_EQ(v2FixtureDiff(h.status(), "status_v2_multipoint.json"), std::string(""));
}

TEST(StatusIsWrittenOnlyWhenSomethingChanges) {
    Harness h;
    h.connect();
    h.advance(200);
    int before = h.writes;
    h.push(Table::T1, B({0x25, 0x00, 80, 0x00}));
    CHECK_EQ(h.writes, before);
    h.push(Table::T1, B({0x25, 0x00, 79, 0x00}));
    CHECK_EQ(h.writes, before + 1);
    CHECK_EQ(h.status()["battery_level"].number, 79.0);
}

TEST(DeadlineIsNotInThePastWhileARequestIsInFlight) {
    Harness h;
    h.connect();
    h.advance(200);
    h.advance(29600);
    h.first.forwardLimit = h.first.forwarded;
    auto parsed = cmd::parseLine("dsee on", false);
    h.manager.execute(*parsed.command, [](std::string) {}, h.now);
    h.advance(400);
    auto deadline = h.manager.nextDeadline();
    REQUIRE(deadline.has_value());
    CHECK(*deadline > h.now);
    h.advance(2000);
    deadline = h.manager.nextDeadline();
    REQUIRE(deadline.has_value());
    CHECK(*deadline > h.now);
}

TEST(StatusHookGetsFinishedDocumentOnlyOnChange) {
    Harness h;
    h.connect();
    h.advance(200);
    int before = h.writes;
    auto written = testutil::parseJson(h.lastWritten);
    REQUIRE(written.has_value());
    CHECK_EQ((*written)["last_updated"].number, 1760438400.0);
    CHECK_EQ(testutil::jsonDiff(*written, h.doc(), {"last_updated"}), std::string(""));
    Bytes line = B({0xC9, 0x01, 0x00, 'x'});
    h.push(Table::T1, line);
    CHECK_EQ(h.writes, before);
}

TEST(DisconnectRemovesTheEntry) {
    Harness h;
    h.connect();
    h.advance(200);
    h.session->onTransportDisconnected(h.now);
    Json d = h.doc();
    CHECK(!d["connected"].boolean);
    CHECK(d["primary"].isNull());
    CHECK(d["headsets"].array.empty());
    CHECK_EQ(h.run("noise anc"), std::string("ERR not connected"));
    CHECK_EQ(h.run("probe"), std::string("ERR not connected"));
}

// ---------------------------------------------------------------------------
// Several headsets: routing, primary, attach and detach
// ---------------------------------------------------------------------------

namespace {

// A full WH (rich fake) and a WF (capture), both ready.
struct PairHarness : Harness {
    Device& wf;
    PairHarness() : Harness(), wf(add(wfProfile(), kWfAddress)) {
        wf.fake.useFixtureValues();
        connect();
        advance(300);
        unixClock += 10;
        connect(wf, "WF-1000XM5");
        advance(300);
    }
};

} // namespace

TEST(SelectorRoutesToOneHeadset) {
    PairHarness h;
    REQUIRE(h.doc()["headsets"].array.size() == 2);
    size_t whMark = h.fake.received.size();
    size_t wfMark = h.wf.fake.received.size();
    CHECK_EQ(h.run("@wf-1000xm5 volume 5"), std::string("OK"));
    CHECK_EQ(h.sentSince(whMark).size(), size_t{0});
    CHECK(sentPayload(h.wf, Table::T1, B({0xA8, 0x20, 5})));
    CHECK_EQ(h.entry(kWfAddress)["volume"].number, 5.0);
    CHECK_EQ(h.run("@ac:80:0a:12:34:56 volume 7"), std::string("OK"));
    CHECK_EQ(h.entry(kAddress)["volume"].number, 7.0);
    CHECK_EQ(h.entry(kWfAddress)["volume"].number, 5.0);
    CHECK_EQ(Harness::sentSince(h.wf, wfMark).size(), size_t{1});
    CHECK_EQ(h.run("@WH-1000XM5 volume 9"), std::string("OK"));
    CHECK_EQ(h.entry(kAddress)["volume"].number, 9.0);

    CHECK_EQ(h.run("@AA:BB:CC:DD:EE:FF volume 10"), std::string("ERR no headset AA:BB:CC:DD:EE:FF"));
    CHECK_EQ(h.run("@xm4 volume 10"), std::string("ERR no headset xm4"));
    CHECK_EQ(h.run("@wh-1000xm5 touch-function left none"), std::string("ERR unsupported touch_function"));
    CHECK_EQ(h.run("@wf-1000xm5 nc-amb-button nc_off"), std::string("ERR unsupported nc_amb_button"));
    CHECK_EQ(h.run("@x status"), std::string("ERR usage: status"));
    CHECK_EQ(h.run("@ volume 3"), std::string("ERR usage: @<address|model> <verb> [args]"));
    CHECK_EQ(h.run("@wf-1000xm5"), std::string("ERR usage: @<address|model> <verb> [args]"));
    CHECK_EQ(h.run("@wf-1000xm5 frob"), std::string("ERR unknown command 'frob'"));
    CHECK_EQ(h.run("@wf-1000xm5 volume 99"), std::string("ERR usage: volume <n>"));

    Json probe = *testutil::parseJson(h.run("@wf-1000xm5 probe"));
    CHECK_EQ(probe["id"].string, std::string(kWfAddress));
    CHECK_EQ(probe["kind"].string, std::string("earbuds"));
}

TEST(NoSelectorGoesToPrimary) {
    PairHarness h;
    CHECK_EQ(h.doc()["primary"].string, std::string(kWfAddress));
    size_t whMark = h.fake.received.size();
    CHECK_EQ(h.run("volume 4"), std::string("OK"));
    CHECK_EQ(h.sentSince(whMark).size(), size_t{0});
    CHECK_EQ(h.entry(kWfAddress)["volume"].number, 4.0);
}

TEST(AmbiguousModelSelector) {
    Harness h(wfProfile(), kWfAddress);
    Device& other = h.add(wfProfile(), "AC:80:0A:00:00:02");
    h.connect("WF-1000XM5");
    h.connect(other, "WF-1000XM5");
    h.advance(300);
    CHECK_EQ(h.run("@WF-1000XM5 volume 3"), std::string("ERR ambiguous headset WF-1000XM5"));
    CHECK_EQ(h.run("@ac:80:0a:00:00:02 volume 3"), std::string("OK"));
    Json d = h.doc();
    REQUIRE(d["headsets"].array.size() == 2);
    CHECK_EQ(d["headsets"].array[0]["id"].string, std::string("AC:80:0A:00:00:02"));
}

TEST(EmptyHeadsetListAnswersNotConnected) {
    Harness h;
    CHECK_EQ(h.run("@wf-1000xm5 volume 3"), std::string("ERR not connected"));
    CHECK_EQ(h.run("volume 3"), std::string("ERR not connected"));
    CHECK_EQ(h.run("probe"), std::string("ERR not connected"));
    Json s = *testutil::parseJson(h.run("status"));
    CHECK_EQ(s["schema_version"].number, 3.0);
    CHECK(s["headsets"].array.empty());
}

TEST(PrimaryRule) {
    // Neither plays from this computer: the most recently connected wins.
    PairHarness h;
    h.manager.setAdapterAddresses({"11:22:33:44:55:66"});
    CHECK_EQ(h.doc()["primary"].string, std::string(kWfAddress));
    // Both play from the adapter: the most recently connected wins.
    h.manager.setAdapterAddresses({kAdapter});
    CHECK_EQ(h.doc()["primary"].string, std::string(kWfAddress));
    // The WF switches to the phone: only the WH plays here and wins although it connected first.
    CHECK_EQ(h.run("@wf-1000xm5 source 5C:E9:1E:BB:00:02"), std::string("OK"));
    h.advance(200);
    CHECK(!h.entry(kWfAddress)["devices"].array[0]["active"].boolean);
    CHECK_EQ(h.doc()["primary"].string, std::string(kAddress));
    // Adapter addresses compare without case.
    h.manager.setAdapterAddresses({"f0:1d:bc:aa:00:01"});
    CHECK_EQ(h.doc()["primary"].string, std::string(kAddress));
}

TEST(PrimaryUsesCodecWithoutDeviceList) {
    auto profile = FakeHeadsetProfile::minimal();
    profile.table1 = {0x20, 0x6B, 0x12};
    Harness h(profile);
    Device& other = h.add(profile, "AC:80:0A:00:00:09");
    h.fake.codec = 0x02;
    other.fake.codec = 0x00;
    h.connect();
    h.advance(1500);
    h.unixClock += 5;
    h.connect(other, "WH-1000XM5");
    h.advance(1500);
    CHECK_EQ(h.doc()["primary"].string, std::string(kAddress));
    other.fake.codec = 0x10;
    h.push(other, Table::T1, B({0x15, 0x02, 0x10}));
    CHECK_EQ(h.doc()["primary"].string, std::string("AC:80:0A:00:00:09"));
}

TEST(PrimaryTieKeepsListOrder) {
    Harness h(wfProfile(), kWfAddress);
    Device& wh = h.add(FakeHeadsetProfile::wh1000xm5Capture(), kAddress);
    h.connect("WF-1000XM5");
    h.connect(wh, "WH-1000XM5");
    h.advance(300);
    Json d = h.doc();
    CHECK_EQ(d["headsets"].array[0]["kind"].string, std::string("headphones"));
    CHECK_EQ(d["primary"].string, std::string(kAddress));
}

TEST(DetachFailsQueuedRequestsAndRewritesStatus) {
    PairHarness h;
    h.wf.forwardLimit = h.wf.forwarded;
    auto parsed = cmd::parseLine("@wf-1000xm5 volume 3", false);
    auto reply = std::make_shared<std::optional<std::string>>();
    h.manager.execute(*parsed.command, [reply](std::string r) { *reply = std::move(r); }, h.now);
    auto parsed2 = cmd::parseLine("@wf-1000xm5 touch-function left none", false);
    auto reply2 = std::make_shared<std::optional<std::string>>();
    h.manager.execute(*parsed2.command, [reply2](std::string r) { *reply2 = std::move(r); }, h.now);
    CHECK(!reply->has_value());
    int before = h.writes;
    h.manager.remove(kWfAddress, h.now);
    CHECK_EQ(reply->value_or(""), std::string("ERR not connected"));
    CHECK_EQ(reply2->value_or(""), std::string("ERR not connected"));
    CHECK_EQ(h.writes, before + 1);
    Json d = h.doc();
    REQUIRE(d["headsets"].array.size() == 1);
    CHECK_EQ(d["primary"].string, std::string(kAddress));
    CHECK_EQ(h.run("@wf-1000xm5 volume 3"), std::string("ERR no headset wf-1000xm5"));
    h.manager.remove(kAddress, h.now);
    CHECK_EQ(fixtureDiff(h.doc(), "status_v3_disconnected.json"), std::string(""));
}

TEST(SessionsKeepIndependentLinks) {
    PairHarness h;
    // The WH stops answering; the WF is not blocked by its queue.
    h.first.forwardLimit = h.first.forwarded;
    auto parsed = cmd::parseLine("@wh-1000xm5 dsee on", false);
    auto reply = std::make_shared<std::optional<std::string>>();
    h.manager.execute(*parsed.command, [reply](std::string r) { *reply = std::move(r); }, h.now);
    CHECK_EQ(h.run("@wf-1000xm5 volume 2", 200), std::string("OK"));
    CHECK(!reply->has_value());
    h.advance(2000);
    CHECK_EQ(reply->value_or(""), std::string("ERR device timeout"));
    // Reconnecting one headset restarts only its own sequence number.
    const uint8_t whSeq = h.first.session->linkSequence();
    h.connect(h.wf, "WF-1000XM5");
    CHECK_EQ(h.wf.fake.receivedSeqs.back(), uint8_t{0});
    CHECK_EQ(h.first.session->linkSequence(), whSeq);
    CHECK(h.entry(kAddress).has("id"));
}

TEST(ReattachStartsAFreshEntry) {
    PairHarness h;
    h.manager.remove(kWfAddress, h.now);
    Device& again = h.add(wfProfile(), kWfAddress);
    again.fake.useFixtureValues();
    h.unixClock += 100;
    h.connect(again, "WF-1000XM5");
    h.advance(300);
    Json e = h.entry(kWfAddress);
    CHECK_EQ(e["connected_since"].number, 1760438510.0);
    CHECK_EQ(e["battery_left"].number, 70.0);
    CHECK_EQ(again.fake.received[0].second, B({0x00, 0x00}));
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
        {"touch-panel off", Table::T1, B({0xD8, 0xD2, 0x00, 0x01})},
        {"quick-access double spotify", Table::T1, B({0xF8, 0x0D, 0x02, 0x01, 0x01})},
        {"quick-access triple off", Table::T1, B({0xF8, 0x0D, 0x02, 0x01, 0x00})},
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
    CHECK(!j["touch_panel"].boolean);
    CHECK(!j["touch_panel"].isNull());
    CHECK_EQ(j["quick_access_double_tap"].string, std::string("spotify"));
    CHECK_EQ(j["quick_access_triple_tap"].string, std::string("off"));
}

TEST(WfSetterVerbsSendProtocolBytes) {
    WfHarness h;
    struct Case {
        const char* line;
        Table table;
        Bytes payload;
    };
    const Case cases[] = {
        {"touch-function left ambient_sound_control", Table::T1, B({0xF8, 0x03, 0x02, 0x35, 0x20})},
        {"touch-function right none", Table::T1, B({0xF8, 0x03, 0x02, 0x35, 0xFF})},
        {"touch-function left playback_control", Table::T1, B({0xF8, 0x03, 0x02, 0x20, 0xFF})},
        {"sidetone off", Table::T1, B({0xD8, 0xD1, 0x00, 0x01})},
        {"voice-guidance-volume 2", Table::T2, B({0x48, 0x20, 0x02, 0x00})},
        {"multipoint off", Table::T1, B({0xD8, 0xD2, 0x00, 0x01})},
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
    CHECK_EQ(j["touch_function_left"].string, std::string("playback_control"));
    CHECK_EQ(j["touch_function_right"].string, std::string("none"));
    CHECK(!j["sidetone"].boolean);
    CHECK(!j["sidetone"].isNull());
    CHECK_EQ(j["voice_guidance_volume"].number, 2.0);

    CHECK_EQ(h.run("touch-function left volume_control"),
             std::string("ERR unsupported touch_function left volume_control"));
    CHECK_EQ(h.run("touch-function right google_assistant"),
             std::string("ERR unsupported touch_function right google_assistant"));
    CHECK_EQ(h.run("touch-function middle none"), std::string("ERR usage: touch-function left|right <value>"));
    CHECK_EQ(h.run("touch-function left"), std::string("ERR usage: touch-function left|right <value>"));
    CHECK_EQ(h.run("touch-function left other"), std::string("ERR usage: touch-function left|right <value>"));
    CHECK_EQ(h.run("sidetone maybe"), std::string("ERR usage: sidetone on|off"));
    CHECK_EQ(h.run("quick-access double spotify"), std::string("ERR unsupported quick_access"));
    CHECK_EQ(h.run("connection-quality sound"), std::string("ERR unsupported connection_quality"));
    CHECK_EQ(h.run("touch-panel on"), std::string("ERR unsupported touch_panel"));
    CHECK_EQ(h.run("button-function none"), std::string("ERR unsupported button_function"));
}

TEST(TouchFunctionKeepsTheOtherSide) {
    WfHarness h;
    h.fake.assignable = {0xFF, 0x35};
    h.session->onTransportDisconnected(h.now);
    h.connect("WF-1000XM5");
    h.advance(300);
    CHECK_EQ(h.status()["touch_function_left"].string, std::string("none"));
    CHECK_EQ(h.status()["touch_function_right"].string, std::string("ambient_sound_control"));
    size_t mark = h.fake.received.size();
    CHECK_EQ(h.run("touch-function left playback_control"), std::string("OK"));
    auto sent = h.sentSince(mark);
    REQUIRE(!sent.empty());
    CHECK_EQ(sent[0].second, B({0xF8, 0x03, 0x02, 0x20, 0x35}));
}

TEST(SidetoneIsGatedBySlotSubject) {
    Harness h(FakeHeadsetProfile::wh1000xm5Capture());
    h.connect();
    h.advance(300);
    CHECK(!h.status()["supported"]["sidetone"].boolean);
    CHECK(h.status()["sidetone"].isNull());
    CHECK_EQ(h.run("sidetone on"), std::string("ERR unsupported sidetone"));
    CHECK_EQ(h.run("touch-function left none"), std::string("ERR unsupported touch_function"));
}

TEST(NewVerbsFollowGating) {
    Harness h(FakeHeadsetProfile::minimal());
    CHECK_EQ(h.run("touch-panel maybe"), std::string("ERR usage: touch-panel on|off"));
    CHECK_EQ(h.run("quick-access double"), std::string("ERR usage: quick-access double|triple <value>"));
    CHECK_EQ(h.run("quick-access single spotify"), std::string("ERR usage: quick-access double|triple <value>"));
    CHECK_EQ(h.run("quick-access double endel"), std::string("ERR usage: quick-access double|triple <value>"));
    h.connect();
    h.advance(1500);
    CHECK_EQ(h.run("touch-panel on"), std::string("ERR unsupported touch_panel"));
    CHECK_EQ(h.run("quick-access double spotify"), std::string("ERR unsupported quick_access"));
}

TEST(QuickAccessSetKeepsTheOtherPress) {
    Harness h;
    h.connect();
    h.advance(200);
    h.fake.quickAccess = {0x01, 0x01};
    h.push(Table::T1, B({0xF9, 0x0D, 0x02, 0x01, 0x01}));
    size_t mark = h.fake.received.size();
    CHECK_EQ(h.run("quick-access double off"), std::string("OK"));
    auto sent = h.sentSince(mark);
    REQUIRE(sent.size() == 1);
    CHECK_EQ(sent[0].second, B({0xF8, 0x0D, 0x02, 0x00, 0x01}));
    CHECK_EQ(h.status()["quick_access_double_tap"].string, std::string("off"));
    CHECK_EQ(h.status()["quick_access_triple_tap"].string, std::string("spotify"));
}

TEST(Xm5Firmware251SupportListMatchesCapture) {
    Harness h(FakeHeadsetProfile::wh1000xm5Capture());
    h.fake.eqPreset = 0x00;
    h.connect();
    h.advance(300);
    CHECK(h.session->initComplete());
    Json sup = h.status()["supported"];
    CHECK(sup["battery"].boolean);
    CHECK(!sup["battery_lr"].boolean);
    CHECK(sup["noise_control"].boolean);
    CHECK(sup["eq_custom"].boolean);
    CHECK(sup["dsee"].boolean);
    CHECK(!sup["voice_guidance_volume"].boolean);
    CHECK(!sup["button_function"].boolean);
    CHECK(!sup["touch_function"].boolean);
    CHECK(!sup["worn"].boolean);
    CHECK(sup["multipoint"].boolean);
    CHECK(sup["touch_panel"].boolean);
    CHECK(sup["quick_access"].boolean);
    CHECK(sentPayload(h, Table::T1, B({0xD6, 0xD1})));
    CHECK(sentPayload(h, Table::T1, B({0xF0, 0x0D})));
    CHECK(!sentPayload(h, Table::T1, B({0xF0, 0x03})));
    CHECK(!sentPayload(h, Table::T1, B({0xE0, 0x05})));
    Json j = h.status();
    CHECK(j["touch_panel"].boolean);
    CHECK(!j["multipoint"].boolean);
    CHECK_EQ(j["eq_preset"].string, std::string("off"));
    CHECK_EQ(j["eq_custom_bands"].array.size(), size_t{5});
    CHECK_EQ(h.run("touch-panel off"), std::string("OK"));
    CHECK(sentPayload(h, Table::T1, B({0xD8, 0xD1, 0x00, 0x01})));
    CHECK_EQ(h.run("multipoint off"), std::string("OK"));
    CHECK(sentPayload(h, Table::T1, B({0xD8, 0xD2, 0x00, 0x01})));
    h.advance(31000);
    CHECK(!sentPayload(h, Table::T2, B({0xF2, 0x00})));
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
    h.first.forwardLimit = h.first.forwarded;
    auto parsed = cmd::parseLine("dsee on", false);
    auto reply = std::make_shared<std::optional<std::string>>();
    size_t writes = static_cast<size_t>(h.first.forwarded);
    h.manager.execute(*parsed.command, [reply](std::string r) { *reply = std::move(r); }, h.now);
    h.advance(850);
    CHECK(!reply->has_value());
    h.advance(900);
    CHECK(!reply->has_value());
    h.advance(100);
    CHECK_EQ(reply->value_or(""), std::string("ERR device timeout"));
    CHECK_EQ(static_cast<size_t>(h.first.forwarded), writes);
}

TEST(LateEqValueTurnsEqCustomOn) {
    FakeHeadsetProfile profile;
    profile.table1 = {0x20, 0x50};
    profile.table2 = {};
    Harness h(profile);
    h.first.forwardLimit = 7;
    h.connect();
    h.advance(3500);
    h.first.forwardLimit = -1;
    REQUIRE(h.status()["supported"].has("eq_custom"));
    CHECK(!h.status()["supported"]["eq_custom"].boolean);
    h.push(Table::T1, B({0x59, 0x00, 0xA0, 0x06, 10, 10, 10, 10, 10, 12}));
    CHECK(h.status()["supported"]["eq_custom"].boolean);
    const Json bands = h.status()["eq_custom_bands"];
    CHECK_EQ(bands.array[4].number, 2.0);
}

TEST(LateAssignableCapabilityTurnsTouchFunctionOn) {
    auto profile = wfProfile();
    // The capability reply lists no key; a later full reply turns the feature on.
    Harness late(profile, kWfAddress);
    late.fake.useFixtureValues();
    late.fake.profile.assignableCapability = {0xF1, 0x03, 0x00};
    late.connect("WF-1000XM5");
    late.advance(300);
    CHECK(!late.status()["supported"]["touch_function"].boolean);
    late.push(Table::T1, profile.assignableCapability);
    CHECK(late.status()["supported"]["touch_function"].boolean);
    CHECK_EQ(late.status()["touch_function_left"].string, std::string("playback_control"));
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

TEST(WfPeriodicSyncRereadsBothBatteries) {
    WfHarness h;
    size_t mark = h.fake.received.size();
    h.fake.batteryLeft = 40;
    h.fake.caseCharging = 0x01;
    h.advance(30500);
    auto sent = h.sentSince(mark);
    REQUIRE(sent.size() == 4);
    CHECK_EQ(sent[0].second, B({0x22, 0x09}));
    CHECK_EQ(sent[1].second, B({0x22, 0x0A}));
    CHECK_EQ(sent[2].second, B({0xA6, 0x20}));
    CHECK_EQ(sent[3].second, B({0xF2, 0x00}));
    CHECK_EQ(h.status()["battery_left"].number, 40.0);
    CHECK(h.status()["battery_case_charging"].boolean);
}

TEST(WfBatteryNotificationsUpdateStatus) {
    WfHarness h;
    h.push(Table::T1, B({0x25, 0x09, 0x00, 0x00, 0x64, 0x01, 0x64, 0x64}));
    Json s = h.status();
    CHECK_EQ(s["battery_left"].number, 0.0);
    CHECK(!s["battery_left_charging"].boolean);
    CHECK_EQ(s["battery_right"].number, 100.0);
    CHECK(s["battery_right_charging"].boolean);
    h.push(Table::T1, B({0x25, 0x0A, 0x35, 0x03, 0x1E}));
    CHECK_EQ(h.status()["battery_case"].number, 53.0);
    CHECK(h.status()["battery_case_charging"].boolean);
    h.push(Table::T1, B({0x25, 0x09, 0x10, 0x02, 0x20, 0x00, 0x64, 0x64}));
    CHECK(h.status()["battery_left_charging"].isNull());
    CHECK(h.status()["battery_level"].isNull());
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
    CHECK(h.status()["worn_left"].isNull());
}

TEST(WfWearLogLinesMatchWithoutCase) {
    WfHarness h;
    struct Case {
        const char* text;
        uint8_t status;
        bool worn, left, right;
    };
    const Case cases[] = {
        {"rUnitRemove", 0x03, true, true, false},
        {"lUnitRemove", 0x04, false, false, false},
        {"lUnitWear", 0x02, true, false, true},
        {"rUnitWear", 0x00, true, true, true},
    };
    for (const auto& c : cases) {
        h.fake.worn = c.status;
        size_t mark = h.fake.received.size();
        h.push(Table::T1, withText(B({0xC9, 0x01}), c.text, B({0x00, 0x00})));
        h.advance(100);
        auto sent = h.sentSince(mark);
        REQUIRE(sent.size() == 1);
        CHECK(sent[0].first == Table::T2);
        CHECK_EQ(sent[0].second, B({0xF2, 0x00}));
        Json s = h.status();
        CHECK_EQ(s["worn"].boolean, c.worn);
        CHECK_EQ(s["worn_left"].boolean, c.left);
        CHECK_EQ(s["worn_right"].boolean, c.right);
    }
    h.push(Table::T2, B({0xF5, 0x00, 0x01}));
    CHECK(h.status()["worn"].isNull());
    CHECK(h.status()["worn_left"].isNull());
    CHECK(h.status()["worn_right"].isNull());
}

TEST(HeadGestureNotificationIsIgnored) {
    WfHarness h;
    int before = h.writes;
    h.push(Table::T1, B({0xF5, 0x10, 0x01, 0x00}));
    h.push(Table::T1, B({0xF5, 0x10, 0x01, 0x01}));
    CHECK_EQ(h.writes, before);
}

TEST(ProbeReportsDeviceData) {
    Harness h;
    h.connect();
    h.advance(200);
    std::string reply = h.run("probe");
    auto j = testutil::parseJson(reply);
    REQUIRE(j.has_value());
    CHECK_EQ((*j)["id"].string, std::string(kAddress));
    CHECK_EQ((*j)["model"].string, std::string("wh-1000xm5"));
    CHECK_EQ((*j)["kind"].string, std::string("headphones"));
    CHECK_EQ((*j)["address"].string, std::string(kAddress));
    CHECK_EQ((*j)["protocol"]["version"].number, 4096.0);
    CHECK((*j)["protocol"]["table2"].boolean);
    CHECK_EQ((*j)["unique_id"].string, std::string("FAKE-XM5-0001"));
    CHECK_EQ((*j)["color"].string, std::string("0x01"));
    CHECK_EQ((*j)["support_table1"].array.size(), size_t{17});
    CHECK_EQ((*j)["support_table2"].array.size(), size_t{4});
    CHECK_EQ((*j)["general_settings"].array[0]["subject"].string, std::string("MULTIPOINT_SETTING"));
    CHECK_EQ((*j)["eq_capability"]["bands"].number, 6.0);
    CHECK_EQ((*j)["eq_capability"]["presets"].array.size(), size_t{10});
    CHECK_EQ((*j)["assignable"]["type"].string, std::string("0x03"));
    CHECK_EQ((*j)["assignable"]["keys"].array[0]["key"].string, std::string("0x04"));
    const Json assignableValue = (*j)["assignable"]["value"];
    CHECK_EQ(assignableValue.array[0].string, std::string("0x00"));
    CHECK((*j)["connection_mode"]["capability"].isNull());
    CHECK((*j)["connection_mode"]["status"].isNull());
    CHECK_EQ((*j)["recent_frames"].array.size(), size_t{64});
    CHECK(reply.find('\n') == std::string::npos);
}

TEST(WfProbeReportsAssignableKeysAndConnectionMode) {
    WfHarness h;
    auto j = testutil::parseJson(h.run("probe"));
    REQUIRE(j.has_value());
    CHECK_EQ((*j)["model"].string, std::string("wf-1000xm5"));
    CHECK_EQ((*j)["kind"].string, std::string("earbuds"));
    CHECK_EQ((*j)["protocol"]["version"].number, 50343960.0);
    const Json keys = (*j)["assignable"]["keys"];
    REQUIRE(keys.array.size() == 2);
    CHECK_EQ(keys.array[0]["key"].string, std::string("0x00"));
    CHECK_EQ(keys.array[0]["default"].string, std::string("0x35"));
    CHECK_EQ(keys.array[1]["default"].string, std::string("0x20"));
    const Json rightPresets = keys.array[1]["presets"];
    REQUIRE(rightPresets.array.size() == 3);
    CHECK_EQ(rightPresets.array[2].string, std::string("0xFF"));
    const Json mode = (*j)["connection_mode"];
    CHECK_EQ(mode["capability"].string, std::string("E1 05 02 00 01 01 00"));
    CHECK_EQ(mode["status"].string, std::string("E3 05 00 01"));
    const Json settings = (*j)["general_settings"];
    REQUIRE(!settings.array.empty());
    const std::string subject = settings.array[0]["subject"].string;
    CHECK_EQ(subject, std::string("SIDETONE_SETTING"));
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
