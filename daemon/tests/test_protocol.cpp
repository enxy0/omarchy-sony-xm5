#include "BluetoothManager.hpp"
#include "HeadphoneState.hpp"
#include "MDRProtocol.hpp"
#include "TestUtil.hpp"

#include <algorithm>
#include <string>

using namespace omarchy::sony::protocol;
using testutil::Json;

namespace {

Bytes B(std::initializer_list<uint8_t> bytes) { return Bytes(bytes); }

Bytes withString(Bytes prefix, const std::string& text) {
    prefix.push_back(static_cast<uint8_t>(text.size()));
    prefix.insert(prefix.end(), text.begin(), text.end());
    return prefix;
}

bool apply(Table table, const Bytes& payload, HeadphoneState& state) {
    InboundEvents ev;
    return applyInbound(table, payload, state, ev);
}

bool apply1(const Bytes& payload, HeadphoneState& state) { return apply(Table::T1, payload, state); }
bool apply2(const Bytes& payload, HeadphoneState& state) { return apply(Table::T2, payload, state); }

HeadphoneState withSupport(std::vector<uint8_t> t1, std::vector<uint8_t> t2 = {}, bool table2 = true) {
    HeadphoneState s;
    s.table2Supported = table2;
    s.support1 = std::vector<SupportEntry>{};
    for (uint8_t id : t1) s.support1->push_back({id, 1});
    if (table2) {
        s.support2 = std::vector<SupportEntry>{};
        for (uint8_t id : t2) s.support2->push_back({id, 1});
    }
    selectVariants(s);
    return s;
}

Json parsed(const std::string& text) {
    auto j = testutil::parseJson(text);
    return j ? *j : Json{};
}

Json entryOf(const HeadphoneState& s) { return parsed(headsetJson(s)); }

} // namespace

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

TEST(ChecksumWrapsModulo256) {
    CHECK_EQ(calculateChecksum(B({0xFF, 0x02})), uint8_t{0x01});
    CHECK_EQ(calculateChecksum(Bytes{}), uint8_t{0x00});
}

TEST(BatteryGetFrameMatchesProtocolExample) {
    CHECK_EQ(packFrame(PacketType::DATA_MDR, 0, req::getBattery(0x00)),
             B({0x3E, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x22, 0x00, 0x30, 0x3C}));
}

TEST(AckFrameMatchesProtocolExample) {
    CHECK_EQ(serializeACK(0), B({0x3E, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x3C}));
    auto ack = unpackFrame(serializeACK(1));
    REQUIRE(ack.has_value());
    CHECK(ack->type == PacketType::ACK);
    CHECK_EQ(ack->seq, uint8_t{0});
    CHECK(ack->payload.empty());
}

TEST(EscapingRoundTripsMarkers) {
    Bytes raw = B({0x10, 0x3C, 0x20, 0x3D, 0x30, 0x3E, 0x40});
    Bytes escaped = escapeBytes(raw);
    CHECK_EQ(escaped, B({0x10, 0x3D, 0x2C, 0x20, 0x3D, 0x2D, 0x30, 0x3D, 0x2E, 0x40}));
    CHECK_EQ(unescapeBytes(escaped), raw);
    CHECK(unescapeBytes(B({0x01, 0x3D})).empty());
    CHECK(unescapeBytes(B({0x01, 0x3D, 0xAA})).empty());
}

TEST(SourceSwitchCommand3CIsEscapedOnTheWire) {
    Bytes frame = packFrame(PacketType::DATA_MDR_NO2, 1, req::setSourceSwitch("F0:1D:BC:AA:00:01"));
    CHECK_EQ(frame[7], uint8_t{0x3D});
    CHECK_EQ(frame[8], uint8_t{0x2C});
    CHECK(std::count(frame.begin() + 1, frame.end() - 1, kEndMarker) == 0);
    auto back = unpackFrame(frame);
    REQUIRE(back.has_value());
    CHECK_EQ(back->payload, req::setSourceSwitch("F0:1D:BC:AA:00:01"));
}

TEST(UnpackRejectsDamagedFrames) {
    Bytes frame = packFrame(PacketType::DATA_MDR, 1, B({0x68, 0x17, 0x01}));
    auto ok = unpackFrame(frame);
    REQUIRE(ok.has_value());
    CHECK_EQ(ok->seq, uint8_t{1});
    Bytes bad = frame;
    bad[4] ^= 0xFF;
    CHECK(!unpackFrame(bad).has_value());
    CHECK(!unpackFrame(B({0x3E, 0x0C, 0x3C})).has_value());
}

TEST(StreamFramerSplitsAndJoins) {
    StreamFramer framer;
    Bytes f1 = packFrame(PacketType::DATA_MDR, 0, B({0xAA}));
    Bytes f2 = packFrame(PacketType::DATA_MDR_NO2, 1, B({0xBB, 0xCC}));
    for (size_t i = 0; i + 1 < f1.size(); ++i) {
        framer.append(std::span<const uint8_t>(&f1[i], 1));
        CHECK(!framer.nextFrame().has_value());
    }
    framer.append(std::span<const uint8_t>(&f1.back(), 1));
    CHECK_EQ(framer.nextFrame().value_or(Bytes{}), f1);

    Bytes joined = B({0xFF, 0x00});
    joined.insert(joined.end(), f1.begin(), f1.end());
    joined.insert(joined.end(), f2.begin(), f2.end());
    framer.append(joined);
    CHECK_EQ(framer.nextFrame().value_or(Bytes{}), f1);
    CHECK_EQ(framer.nextFrame().value_or(Bytes{}), f2);
    CHECK(!framer.nextFrame().has_value());
}

// ---------------------------------------------------------------------------
// Request builders (protocol.md sections 3 and 5)
// ---------------------------------------------------------------------------

TEST(InitRequestsMatchProtocolTable) {
    CHECK_EQ(req::getProtocolInfo(), B({0x00, 0x00}));
    CHECK_EQ(req::getCapabilityInfo(), B({0x02, 0x00}));
    CHECK_EQ(req::getDeviceInfo(0x02), B({0x04, 0x02}));
    CHECK_EQ(req::getDeviceInfo(0x01), B({0x04, 0x01}));
    CHECK_EQ(req::getDeviceInfo(0x03), B({0x04, 0x03}));
    CHECK_EQ(req::getSupportFunction(), B({0x06, 0x00}));
    CHECK_EQ(req::getGeneralSettingCapability(0xD2), B({0xD0, 0xD2, 0x01}));
    CHECK_EQ(req::getGeneralSettingValue(0xD2), B({0xD6, 0xD2}));
    CHECK_EQ(req::getUpscalingCapability(), B({0xE0, 0x01}));
    CHECK_EQ(req::getAssignableCapability(0x0E), B({0xF0, 0x0E}));
    CHECK_EQ(req::getAssignableCapability(0x03), B({0xF0, 0x03}));
    CHECK_EQ(req::enableFixedMessageAlerts(), B({0x94, 0x00, 0x00}));
    CHECK_EQ(req::getCodec(), B({0x12, 0x02}));
    CHECK_EQ(req::getVolume(), B({0xA6, 0x20}));
    CHECK_EQ(req::getNcAsm(0x17), B({0x66, 0x17}));
    CHECK_EQ(req::getNcAsm(0x19), B({0x66, 0x19}));
    CHECK_EQ(req::getPairingMode(0x02), B({0x32, 0x02}));
    CHECK_EQ(req::getPairedDevices(0x00), B({0x36, 0x00}));
    CHECK_EQ(req::getSourceSwitchAuto(), B({0x36, 0x01}));
    CHECK_EQ(req::getSpeakToChat(), B({0xF6, 0x0C}));
    CHECK_EQ(req::getSpeakToChatConfig(), B({0xFA, 0x0C}));
    CHECK_EQ(req::getEqCapability(), B({0x50, 0x00, 0x01}));
    CHECK_EQ(req::getEqStatus(), B({0x52, 0x00}));
    CHECK_EQ(req::getEqValue(), B({0x56, 0x00}));
    CHECK_EQ(req::getConnectionQuality(0x00), B({0xE6, 0x00}));
    CHECK_EQ(req::getConnectionQuality(0x02), B({0xE6, 0x02}));
    CHECK_EQ(req::getUpscalingStatus(), B({0xE2, 0x01}));
    CHECK_EQ(req::getUpscaling(), B({0xE6, 0x01}));
    CHECK_EQ(req::getAssignableValue(0x03), B({0xF6, 0x03}));
    CHECK_EQ(req::getNcAmbButton(), B({0x66, 0x30}));
    CHECK_EQ(req::getAutoPowerOff(0x05), B({0x26, 0x05}));
    CHECK_EQ(req::getAutoPowerOff(0x04), B({0x26, 0x04}));
    CHECK_EQ(req::getEarDetection(), B({0xF6, 0x01}));
    CHECK_EQ(req::getWornStatus(), B({0xF2, 0x00}));
    CHECK_EQ(req::getVoiceGuidance(0x01), B({0x46, 0x01}));
    CHECK_EQ(req::getVoiceGuidanceVolume(), B({0x46, 0x20}));
    CHECK_EQ(req::enableOperationLog(), B({0xC4, 0x01, 0x00}));
    CHECK_EQ(req::getBattery(0x00), B({0x22, 0x00}));
    CHECK_EQ(req::getBattery(0x08), B({0x22, 0x08}));
    CHECK_EQ(req::getQuickAccessCapability(), B({0xF0, 0x0D}));
    CHECK_EQ(req::getQuickAccess(), B({0xF6, 0x0D}));
}

TEST(EqQueryCarriesInquiredType) {
    CHECK_EQ(req::getEqValue(), B({0x56, 0x00}));
}

TEST(NcAsmSetKeepsAmbientValues) {
    NcAsmParams p;
    p.type = 0x17;
    p.totalEffect = 0x01;
    p.mode = 0x00;
    p.ambientMode = 0x01;
    p.level = 14;
    CHECK_EQ(req::setNcAsm(p), B({0x68, 0x17, 0x01, 0x01, 0x00, 0x01, 0x0E}));
    p.type = 0x19;
    p.autoAmbient = 0x01;
    p.noiseAdaptive = 0x02;
    CHECK_EQ(req::setNcAsm(p), B({0x68, 0x19, 0x01, 0x01, 0x00, 0x01, 0x0E, 0x01, 0x02}));
    p.type = 0x15;
    CHECK_EQ(req::setNcAsm(p).size(), size_t{7});
}

TEST(SetterPayloadsMatchProtocol) {
    CHECK_EQ(req::setEqPreset(0x10), B({0x58, 0x00, 0x10, 0x00}));
    CHECK_EQ(req::setEqPreset(0xA1), B({0x58, 0x00, 0xA1, 0x00}));
    CHECK_EQ(req::setEqCustom({-10, 0, 10, -5, 5}, 3), B({0x58, 0x00, 0xA0, 0x06, 13, 0, 10, 20, 5, 15}));
    CHECK_EQ(req::setEqCustom({-15, 0, 0, 0, 15}, 20), B({0x58, 0x00, 0xA0, 0x06, 20, 0, 10, 10, 10, 20}));
    CHECK_EQ(req::setUpscaling(true), B({0xE8, 0x01, 0x01}));
    CHECK_EQ(req::setUpscaling(false), B({0xE8, 0x01, 0x00}));
    CHECK_EQ(req::setSpeakToChat(true), B({0xF8, 0x0C, 0x00, 0x01}));
    CHECK_EQ(req::setSpeakToChat(false), B({0xF8, 0x0C, 0x01, 0x01}));
    CHECK_EQ(req::setSpeakToChatConfig(0x02, 0x03), B({0xFC, 0x0C, 0x02, 0x03}));
    CHECK_EQ(req::setVoiceGuidance(0x01, true, 0x00), B({0x48, 0x01, 0x00}));
    CHECK_EQ(req::setVoiceGuidance(0x01, false, 0x00), B({0x48, 0x01, 0x01}));
    CHECK_EQ(req::setVoiceGuidance(0x02, true, 0x01), B({0x48, 0x02, 0x00, 0x01}));
    CHECK_EQ(req::setVoiceGuidanceVolume(-2), B({0x48, 0x20, 0xFE, 0x00}));
    CHECK_EQ(req::setVoiceGuidanceVolume(2), B({0x48, 0x20, 0x02, 0x00}));
    CHECK_EQ(req::setAutoPowerOff(0x05, 0x01), B({0x28, 0x05, 0x01, 0x01}));
    CHECK_EQ(req::setAutoPowerOff(0x05, 0x10), B({0x28, 0x05, 0x10, 0x00}));
    CHECK_EQ(req::setAutoPowerOff(0x04, 0x11), B({0x28, 0x04, 0x11, 0x00}));
    CHECK_EQ(req::setNcAmbButton(0x03), B({0x68, 0x30, 0x03}));
    CHECK_EQ(req::setAssignableValue(0x03, {0x31, 0xFF}), B({0xF8, 0x03, 0x02, 0x31, 0xFF}));
    CHECK_EQ(req::setEarDetection(true), B({0xF8, 0x01, 0x00}));
    CHECK_EQ(req::setEarDetection(false), B({0xF8, 0x01, 0x01}));
    CHECK_EQ(req::setVolume(30), B({0xA8, 0x20, 30}));
    CHECK_EQ(req::setConnectionQuality(0x00, 0x01), B({0xE8, 0x00, 0x01}));
    CHECK_EQ(req::setConnectionQuality(0x02, 0x00), B({0xE8, 0x02, 0x00}));
    CHECK_EQ(req::setGeneralSettingBool(0xD3, true), B({0xD8, 0xD3, 0x00, 0x00}));
    CHECK_EQ(req::setGeneralSettingBool(0xD1, false), B({0xD8, 0xD1, 0x00, 0x01}));
    CHECK_EQ(req::answerAlert(0x07, true), B({0x98, 0x00, 0x07, 0x01}));
    CHECK_EQ(req::setQuickAccess({0x00, 0x01}), B({0xF8, 0x0D, 0x02, 0x00, 0x01}));
    CHECK_EQ(req::setQuickAccess({0x01, 0x00}), B({0xF8, 0x0D, 0x02, 0x01, 0x00}));
    Bytes source = req::setSourceSwitch("5C:E9:1E:BB:00:02");
    CHECK_EQ(Bytes(source.begin(), source.begin() + 2), B({0x3C, 0x01}));
    CHECK_EQ(std::string(source.begin() + 2, source.end()), std::string("5C:E9:1E:BB:00:02"));
}

// ---------------------------------------------------------------------------
// Parsers (protocol.md section 5)
// ---------------------------------------------------------------------------

TEST(IdentityRepliesParse) {
    HeadphoneState s;
    CHECK(apply1(B({0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00}), s));
    CHECK(s.protocolKnown);
    CHECK_EQ(s.protocolVersion, uint32_t{4096});
    CHECK(s.table1Supported);
    CHECK(s.table2Supported);
    CHECK(!apply1(B({0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00}), s));

    CHECK(apply1(withString(B({0x03, 0x00, 0x05}), "ID-1"), s));
    CHECK_EQ(s.capabilityCounter, std::optional<uint8_t>(0x05));
    CHECK_EQ(s.uniqueId, std::optional<std::string>("ID-1"));
    CHECK(apply1(withString(B({0x05, 0x02}), "2.3.1"), s));
    CHECK(apply1(withString(B({0x05, 0x01}), "WH-1000XM5"), s));
    CHECK(apply1(B({0x05, 0x03, 0x30, 0x01}), s));
    CHECK_EQ(s.firmwareVersion, std::optional<std::string>("2.3.1"));
    CHECK_EQ(s.modelName, std::optional<std::string>("WH-1000XM5"));
    CHECK_EQ(s.series, std::optional<uint8_t>(0x30));
    CHECK_EQ(s.color, std::optional<uint8_t>(0x01));
    CHECK(!apply1(B({0x05, 0x02, 0x09, 'x'}), s));
}

TEST(SupportListsDispatchOnTable) {
    HeadphoneState s;
    CHECK(apply1(B({0x07, 0x00, 0x02, 0x20, 0x01, 0x6B, 0x01}), s));
    CHECK(apply2(B({0x07, 0x00, 0x01, 0x32, 0x02}), s));
    REQUIRE(s.support1.has_value());
    REQUIRE(s.support2.has_value());
    CHECK_EQ(s.support1->size(), size_t{2});
    CHECK_EQ(s.support2->at(0).id, uint8_t{0x32});
    CHECK_EQ(s.support2->at(0).priority, uint8_t{0x02});
    CHECK(s.hasFunction(Table::T1, 0x6B));
    CHECK(!s.hasFunction(Table::T2, 0x6B));
    CHECK(s.hasFunction(Table::T2, 0x32));
}

TEST(BatteryParsesBothVariantsAndChargingStates) {
    HeadphoneState s;
    CHECK(apply1(B({0x23, 0x00, 85, 0x01}), s));
    CHECK_EQ(s.batteryLevel, std::optional<int>(85));
    CHECK_EQ(s.batteryCharging(), std::optional<bool>(true));
    CHECK(apply1(B({0x25, 0x00, 100, 0x03}), s));
    CHECK_EQ(s.batteryCharging(), std::optional<bool>(true));
    CHECK(apply1(B({0x25, 0x00, 100, 0x02}), s));
    CHECK(!s.batteryCharging().has_value());
    CHECK(apply1(B({0x23, 0x08, 40, 0x00, 0x01}), s));
    CHECK_EQ(s.batteryLevel, std::optional<int>(40));
    CHECK_EQ(s.batteryCharging(), std::optional<bool>(false));
    CHECK(apply1(B({0x23, 0x08, 41, 0x01, 0x00}), s));
    CHECK_EQ(s.batteryCharging(), std::optional<bool>(true));
    CHECK(!apply1(B({0x23, 0x08, 41, 0x01}), s));
}

TEST(NcAsmParsesModesWithoutWind) {
    HeadphoneState s;
    CHECK(apply1(B({0x67, 0x17, 0x01, 0x00, 0x01, 0x00, 0x14}), s));
    CHECK_EQ(s.noiseModeString(), std::optional<std::string>("off"));
    CHECK_EQ(s.ambientLevelValue(), std::optional<int>(20));
    CHECK(apply1(B({0x69, 0x17, 0x01, 0x01, 0x00, 0x00, 0x05}), s));
    CHECK_EQ(s.noiseModeString(), std::optional<std::string>("anc"));
    CHECK_EQ(s.ambientLevelValue(), std::optional<int>(5));
    CHECK(apply1(B({0x69, 0x17, 0x01, 0x01, 0x01, 0x01, 0x0C}), s));
    CHECK_EQ(s.noiseModeString(), std::optional<std::string>("ambient"));
    CHECK_EQ(s.ncAmbientMode, std::optional<uint8_t>(0x01));
    CHECK(apply1(B({0x69, 0x17, 0x01, 0x01, 0x01, 0x00, 0x00}), s));
    CHECK_EQ(s.noiseModeString(), std::optional<std::string>("ambient"));
    CHECK(!s.ambientLevelValue().has_value());
    CHECK(apply1(B({0x67, 0x19, 0x01, 0x01, 0x00, 0x00, 0x0A, 0x01, 0x02}), s));
    CHECK_EQ(s.ncAutoAmbient, std::optional<uint8_t>(0x01));
    CHECK_EQ(s.ncNoiseAdaptive, std::optional<uint8_t>(0x02));
    CHECK(!apply1(B({0x67, 0x19, 0x01, 0x01, 0x00, 0x00, 0x0A}), s));
    CHECK(apply1(B({0x67, 0x15, 0x01, 0x01, 0x01, 0x00, 0x0B}), s));
    CHECK_EQ(s.ambientLevelValue(), std::optional<int>(11));
}

TEST(OldCompactCommandsNoLongerTouchState) {
    HeadphoneState s;
    CHECK(apply1(B({0x67, 0x17, 0x01, 0x01, 0x00, 0x00, 0x05}), s));
    CHECK(!apply1(B({0x11, 0x02, 0x0A, 0x01}), s));
    CHECK(!apply1(B({0x10, 0x50, 0x01}), s));
    CHECK(!apply1(B({0x14, 0x01, 0x01, 0x01, 0x01}), s));
    CHECK_EQ(s.noiseModeString(), std::optional<std::string>("anc"));
    CHECK(!s.batteryLevel.has_value());
}

TEST(EqParsesCapabilityStatusAndValues) {
    HeadphoneState s;
    Bytes full = B({0x51, 0x00, 0x06, 0x15, 0x03, 0x00});
    full = withString(full, "Off");
    full.push_back(0x10);
    full = withString(full, "Bright");
    full.push_back(0x77);
    full = withString(full, "New");
    CHECK(apply1(full, s));
    REQUIRE(s.eqCapability.has_value());
    CHECK_EQ(s.eqCapability->bands, uint8_t{6});
    CHECK_EQ(s.eqCapability->steps, uint8_t{0x15});
    CHECK_EQ(s.eqCapability->presets.size(), size_t{3});
    CHECK_EQ(s.eqCapability->presets[1].name, std::string("Bright"));
    auto names = s.eqPresetNames();
    REQUIRE(names.has_value());
    CHECK_EQ(names->size(), size_t{2});

    CHECK(apply1(B({0x53, 0x00, 0x00}), s));
    CHECK_EQ(s.eqAvailable, std::optional<uint8_t>(0x00));

    CHECK(apply1(B({0x57, 0x00, 0xA0, 0x06, 0x14, 0x0B, 0x0A, 0x0D, 0x09, 0x0C}), s));
    CHECK_EQ(eqPresetName(*s.eqPresetIdValue), std::string("custom"));
    CHECK_EQ(s.clearBass, std::optional<int>(10));
    CHECK((s.eqCustomBands == std::optional<std::array<int, 5>>(std::array<int, 5>{1, 0, 3, -1, 2})));

    CHECK(apply1(B({0x59, 0x00, 0x10, 0x06, 0x0B, 0x0C, 0x0A, 0x09, 0x0A, 0x0E}), s));
    CHECK_EQ(eqPresetName(*s.eqPresetIdValue), std::string("bright"));
    CHECK_EQ(s.clearBass, std::optional<int>(1));
    CHECK((s.eqCustomBands == std::optional<std::array<int, 5>>(std::array<int, 5>{2, 0, -1, 0, 4})));

    CHECK(apply1(B({0x59, 0x00, 0xA2, 0x00}), s));
    CHECK_EQ(s.clearBass, std::optional<int>(1));
    CHECK_EQ(eqPresetName(*s.eqPresetIdValue), std::string("user2"));
    CHECK(apply1(B({0x59, 0x00, 0x42, 0x00}), s));
    CHECK_EQ(eqPresetName(*s.eqPresetIdValue), std::string("unknown"));
    CHECK(!apply1(B({0x57, 0x00, 0xA0, 0x06, 0x14}), s));
}

TEST(EqValueForPresetOffCarriesBandsFromXm5Capture) {
    HeadphoneState s = withSupport({0x50});
    s.connected = true;
    CHECK(apply1(B({0x57, 0x00, 0x00, 0x06, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A}), s));
    s.supported = deriveSupported(s);
    Json j = entryOf(s);
    CHECK_EQ(j["eq_preset"].string, std::string("off"));
    CHECK(j["supported"]["eq_custom"].boolean);
    REQUIRE(j["eq_custom_bands"].array.size() == 5);
    for (const auto& band : j["eq_custom_bands"].array) CHECK_EQ(band.number, 0.0);
    CHECK(!j.has("eq_bands"));
    CHECK_EQ(j["clear_bass"].number, 0.0);
}

TEST(QuickAccessParsesXm5Capture) {
    HeadphoneState s = withSupport({0xFD});
    s.connected = true;
    CHECK(apply1(B({0xF1, 0x0D, 0x01, 0x01, 0x02, 0x01, 0x00, 0x04, 0x00, 0x05, 0x01, 0x02,
                    0x02, 0x00, 0x04, 0x00, 0x05, 0x01, 0x02}), s));
    REQUIRE(s.quickAccessCapability.has_value());
    CHECK_EQ(s.quickAccessCapability->key, uint8_t{0x01});
    CHECK_EQ(s.quickAccessCapability->keyType, uint8_t{0x01});
    REQUIRE(s.quickAccessCapability->actions.size() == 2);
    CHECK_EQ(s.quickAccessCapability->actions[1].action, uint8_t{0x02});
    CHECK_EQ(s.quickAccessCapability->actions[1].functions, B({0x00, 0x05, 0x01, 0x02}));
    CHECK(!s.assignableCapability.has_value());

    CHECK(apply1(B({0xF7, 0x0D, 0x02, 0x00, 0x00}), s));
    CHECK(!s.assignableValue.has_value());
    CHECK_EQ(s.quickAccessString(kActionDoubleTap), std::optional<std::string>("off"));
    CHECK_EQ(s.quickAccessString(kActionTripleTap), std::optional<std::string>("off"));
    CHECK(apply1(B({0xF9, 0x0D, 0x02, 0x05, 0x01}), s));
    CHECK_EQ(s.quickAccessString(kActionDoubleTap), std::optional<std::string>("other"));
    CHECK_EQ(s.quickAccessString(kActionTripleTap), std::optional<std::string>("spotify"));

    s.supported = deriveSupported(s);
    CHECK(s.supported->quick_access);
    Json j = entryOf(s);
    CHECK_EQ(j["quick_access_double_tap"].string, std::string("other"));
    CHECK_EQ(j["quick_access_triple_tap"].string, std::string("spotify"));
    const Json options = j["quick_access_double_tap_options"];
    REQUIRE(options.array.size() == 2);
    CHECK_EQ(options.array[0].string, std::string("off"));
    CHECK_EQ(options.array[1].string, std::string("spotify"));
    CHECK_EQ(j["quick_access_triple_tap_options"].array.size(), size_t{2});

    CHECK(!apply1(B({0xF1, 0x0D, 0x01, 0x01, 0x02, 0x01, 0x00, 0x04, 0x00}), s));
    CHECK(!apply1(B({0xF7, 0x0D, 0x02, 0x00}), s));
}

TEST(QuickAccessWithoutCapabilityUsesDoubleThenTriple) {
    HeadphoneState s;
    CHECK(apply1(B({0xF7, 0x0D, 0x02, 0x01, 0x00}), s));
    CHECK_EQ(s.quickAccessIndex(kActionDoubleTap), std::optional<size_t>(0));
    CHECK_EQ(s.quickAccessIndex(kActionTripleTap), std::optional<size_t>(1));
    CHECK_EQ(s.quickAccessString(kActionDoubleTap), std::optional<std::string>("spotify"));
    CHECK(!s.quickAccessOptions(kActionDoubleTap).has_value());
    CHECK_EQ(quickAccessValue("spotify"), std::optional<uint8_t>(0x01));
    CHECK_EQ(quickAccessValue("off"), std::optional<uint8_t>(0x00));
    CHECK(!quickAccessValue("other").has_value());
}

TEST(TouchPanelSlotIsFoundBySubject) {
    HeadphoneState s = withSupport({0xD1, 0xD2});
    s.connected = true;
    CHECK(apply1(B({0xD1, 0xD1, 0x00, 0x01, 0x13, 'T', 'O', 'U', 'C', 'H', '_', 'P', 'A', 'N', 'E', 'L', '_',
                    'S', 'E', 'T', 'T', 'I', 'N', 'G', 0x00}), s));
    CHECK(apply1(withString(withString(B({0xD1, 0xD2, 0x00, 0x01}), "MULTIPOINT_SETTING"),
                            "MULTIPOINT_SETTING_SUMMARY_LDAC_AVAILABLE"), s));
    selectVariants(s);
    CHECK_EQ(s.variants.touchPanelSlot, std::optional<uint8_t>(0xD1));
    CHECK_EQ(s.variants.multipointSlot, std::optional<uint8_t>(0xD2));
    CHECK(apply1(B({0xD7, 0xD1, 0x00, 0x00}), s));
    CHECK(apply1(B({0xD7, 0xD2, 0x00, 0x00}), s));
    CHECK_EQ(s.touchPanelValue(), std::optional<bool>(true));
    CHECK(apply1(B({0xD9, 0xD1, 0x00, 0x01}), s));
    CHECK_EQ(s.touchPanelValue(), std::optional<bool>(false));
    CHECK_EQ(s.multipointValue(), std::optional<bool>(true));
    s.supported = deriveSupported(s);
    CHECK(s.supported->touch_panel);
    Json j = entryOf(s);
    CHECK(!j["touch_panel"].boolean);
    CHECK(!j["touch_panel"].isNull());
    CHECK_EQ(req::setGeneralSettingBool(0xD1, false), B({0xD8, 0xD1, 0x00, 0x01}));
}

TEST(DseeAndConnectionQualityShareAudioCommands) {
    HeadphoneState s;
    CHECK(apply1(B({0xE1, 0x01, 0x02}), s));
    CHECK_EQ(s.upscalingType, std::optional<uint8_t>(0x02));
    CHECK(apply1(B({0xE3, 0x01, 0x00}), s));
    CHECK_EQ(s.dseeAvailable, std::optional<uint8_t>(0x00));
    CHECK(apply1(B({0xE7, 0x01, 0x01}), s));
    CHECK_EQ(s.dsee, std::optional<bool>(true));
    CHECK(apply1(B({0xE9, 0x01, 0x00}), s));
    CHECK_EQ(s.dsee, std::optional<bool>(false));
    CHECK(apply1(B({0xE7, 0x00, 0x01}), s));
    CHECK_EQ(connectionQualityName(*s.connectionQuality), std::string("connection"));
    CHECK(apply1(B({0xE9, 0x02, 0x02}), s));
    CHECK_EQ(connectionQualityName(*s.connectionQuality), std::string("other"));
    CHECK_EQ(s.dsee, std::optional<bool>(false));
}

TEST(SystemParamsParse) {
    HeadphoneState s;
    CHECK(apply1(B({0xF7, 0x0C, 0x00, 0x01}), s));
    CHECK_EQ(s.speakToChat, std::optional<bool>(true));
    CHECK(apply1(B({0xF9, 0x0C, 0x01, 0x01}), s));
    CHECK_EQ(s.speakToChat, std::optional<bool>(false));
    CHECK(apply1(B({0xFB, 0x0C, 0x01, 0x03}), s));
    CHECK_EQ(speakToChatSensitivityName(*s.speakToChatSensitivity), std::optional<std::string>("high"));
    CHECK_EQ(speakToChatTimeoutName(*s.speakToChatTimeout), std::optional<std::string>("off"));
    CHECK(apply1(B({0xF7, 0x01, 0x01}), s));
    CHECK_EQ(s.earDetection, std::optional<bool>(false));
    CHECK(apply1(B({0xF9, 0x01, 0x00}), s));
    CHECK_EQ(s.earDetection, std::optional<bool>(true));
}

TEST(AssignableParsesBothCapabilityTypes) {
    HeadphoneState s;
    // preset = PRESET NA NC (ACTION FUNCTION) x NA (ACTION DEFAULT NF (FUNCTION) x NF) x NC
    Bytes cap03 = B({0xF1, 0x03, 0x02,
                     0x02, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x20,
                     0x04, 0x01, 0x00, 0x03,
                     0x00, 0x00, 0x00,
                     0x31, 0x01, 0x01, 0x01, 0x21, 0x01, 0x21, 0x02, 0x21, 0x22,
                     0x36, 0x00, 0x00});
    CHECK(apply1(cap03, s));
    REQUIRE(s.assignableCapability.has_value());
    CHECK_EQ(s.assignableCapability->keys.size(), size_t{2});
    CHECK_EQ(s.ncAmbKeyIndex(), std::optional<uint8_t>(1));
    CHECK_EQ(s.assignableCapability->keys[1].presets, B({0x00, 0x31, 0x36}));

    CHECK(apply1(B({0xF7, 0x03, 0x02, 0x00, 0x36}), s));
    CHECK_EQ(s.buttonFunctionString(), std::optional<std::string>("quick_access"));
    auto opts = s.buttonFunctionOptions();
    REQUIRE(opts.has_value());
    CHECK_EQ(opts->size(), size_t{3});

    HeadphoneState t;
    CHECK(apply1(B({0xF1, 0x0E, 0x00, 0x01, 0x05, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x99, 0x00, 0x00}), t));
    REQUIRE(t.assignableCapability.has_value());
    CHECK_EQ(t.assignableCapability->type, uint8_t{0x0E});
    CHECK(apply1(B({0xF9, 0x0E, 0x01, 0x99}), t));
    CHECK_EQ(t.buttonFunctionString(), std::optional<std::string>("other"));
    CHECK(!apply1(B({0xF1, 0x03, 0x01, 0x04, 0x01}), t));
}

TEST(AssignableParsesWf1000xm5Capture) {
    // protocol.md 9.3, WF-1000XM5 fw 6.1.0: two touch sensor keys, the old
    // parser rejected this reply.
    const Bytes cap = B({
        0xF1, 0x03, 0x02,
        0x00, 0x00, 0x35, 0x03,
        0x35, 0x04, 0x01, 0x01, 0x43, 0x02, 0x44, 0x22, 0x10, 0x03, 0x24, 0x00, 0x02, 0x04, 0x01, 0x02, 0x03, 0x04,
        0x20, 0x04, 0x00, 0x00, 0x20, 0x01, 0x21, 0x02, 0x22, 0x03, 0x24,
        0xFF, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x20, 0x03,
        0x35, 0x04, 0x01, 0x01, 0x43, 0x02, 0x44, 0x22, 0x10, 0x03, 0x23, 0x00, 0x02, 0x04, 0x01, 0x02, 0x03, 0x04,
        0x20, 0x04, 0x00, 0x00, 0x20, 0x01, 0x21, 0x02, 0x22, 0x03, 0x23,
        0xFF, 0x01, 0x00, 0x00, 0x00});
    HeadphoneState s = withSupport({0xF3});
    s.connected = true;
    CHECK(apply1(cap, s));
    REQUIRE(s.assignableCapability.has_value());
    REQUIRE(s.assignableCapability->keys.size() == 2);
    CHECK_EQ(s.assignableCapability->keys[0].key, uint8_t{0x00});
    CHECK_EQ(s.assignableCapability->keys[0].defaultPreset, uint8_t{0x35});
    CHECK_EQ(s.assignableCapability->keys[0].presets, B({0x35, 0x20, 0xFF}));
    CHECK_EQ(s.assignableCapability->keys[1].key, uint8_t{0x01});
    CHECK_EQ(s.assignableCapability->keys[1].presets, B({0x35, 0x20, 0xFF}));
    CHECK(!s.ncAmbKeyIndex().has_value());
    CHECK(!apply1(Bytes(cap.begin(), cap.end() - 1), s));

    CHECK(apply1(B({0xF7, 0x03, 0x02, 0x20, 0x20}), s));
    CHECK_EQ(s.touchFunctionString(kKeyLeftSide), std::optional<std::string>("playback_control"));
    CHECK(apply1(B({0xF9, 0x03, 0x02, 0x35, 0x20}), s));
    CHECK_EQ(s.touchFunctionString(kKeyLeftSide), std::optional<std::string>("ambient_sound_control"));
    CHECK_EQ(s.touchFunctionString(kKeyRightSide), std::optional<std::string>("playback_control"));
    auto options = s.touchFunctionOptions(kKeyRightSide);
    REQUIRE(options.has_value());
    CHECK((*options == std::vector<std::string>{"ambient_sound_control", "playback_control", "none"}));
    CHECK_EQ(s.touchPresetFor(kKeyLeftSide, "ambient_sound_control"), uint8_t{0x35});
    CHECK_EQ(s.touchPresetFor(kKeyLeftSide, "none"), uint8_t{0xFF});
    CHECK_EQ(req::setAssignableValue(0x03, {0x35, 0x20}), B({0xF8, 0x03, 0x02, 0x35, 0x20}));

    s.supported = deriveSupported(s);
    CHECK(s.supported->touch_function);
    CHECK(!s.supported->button_function);
    Json j = entryOf(s);
    CHECK_EQ(j["touch_function_left"].string, std::string("ambient_sound_control"));
    CHECK_EQ(j["touch_function_right_options"].array.size(), size_t{3});
    CHECK(j["button_function"].isNull());

    // Preset names and the "other" fallback; plain 00 is ambient sound control too.
    CHECK(apply1(B({0xF9, 0x03, 0x02, 0x00, 0x77}), s));
    CHECK_EQ(s.touchFunctionString(kKeyLeftSide), std::optional<std::string>("ambient_sound_control"));
    CHECK_EQ(s.touchFunctionString(kKeyRightSide), std::optional<std::string>("other"));
    CHECK_EQ(touchFunctionName(0x10), std::string("volume_control"));
    CHECK_EQ(touchFunctionName(0x36), std::string("quick_access"));
    CHECK_EQ(touchFunctionValue("ambient_sound_control"), std::optional<uint8_t>(0x35));
    CHECK_EQ(touchFunctionValue("voice_recognition"), std::optional<uint8_t>(0x30));
}

TEST(EarbudBatteriesParse) {
    HeadphoneState s;
    // mos9527 WF-1000XM5 fw 6.1.0 capture (protocol.md 9.2).
    CHECK(apply1(B({0x23, 0x09, 0x00, 0x00, 0x64, 0x00, 0x64, 0x64}), s));
    CHECK_EQ(s.batteryLeft, std::optional<int>(0));
    CHECK_EQ(chargingFromRaw(s.batteryLeftChargingRaw), std::optional<bool>(false));
    CHECK_EQ(s.batteryRight, std::optional<int>(100));
    CHECK(apply1(B({0x23, 0x0A, 0x35, 0x00, 0x1E}), s));
    CHECK_EQ(s.batteryCase, std::optional<int>(53));
    CHECK_EQ(chargingFromRaw(s.batteryCaseChargingRaw), std::optional<bool>(false));
    // This project's WF-1000XM5 run: right earbud out of the ear reads 0 %.
    CHECK(apply1(B({0x23, 0x09, 0x61, 0x00, 0x00, 0x00, 0x64, 0x64}), s));
    CHECK_EQ(s.batteryLeft, std::optional<int>(97));
    CHECK_EQ(s.batteryRight, std::optional<int>(0));
    CHECK(apply1(B({0x23, 0x0A, 0x57, 0x00, 0x1E}), s));
    CHECK_EQ(s.batteryCase, std::optional<int>(87));
    // Notifications and the variants without thresholds.
    CHECK(apply1(B({0x25, 0x09, 0x32, 0x01, 0x33, 0x03, 0x64, 0x64}), s));
    CHECK_EQ(chargingFromRaw(s.batteryLeftChargingRaw), std::optional<bool>(true));
    CHECK_EQ(chargingFromRaw(s.batteryRightChargingRaw), std::optional<bool>(true));
    CHECK(apply1(B({0x25, 0x01, 0x10, 0x02, 0x11, 0x00}), s));
    CHECK_EQ(s.batteryLeft, std::optional<int>(16));
    CHECK(!chargingFromRaw(s.batteryLeftChargingRaw).has_value());
    CHECK(apply1(B({0x25, 0x02, 0x40, 0x01}), s));
    CHECK_EQ(s.batteryCase, std::optional<int>(64));
    CHECK(!apply1(B({0x23, 0x09, 0x00, 0x00, 0x64, 0x00, 0x64}), s));
    CHECK(!apply1(B({0x23, 0x0A, 0x35, 0x00}), s));
    CHECK(!s.batteryLevel.has_value());

    s = withSupport({0x29, 0x2A});
    s.connected = true;
    s.supported = deriveSupported(s);
    CHECK(apply1(B({0x23, 0x09, 0x46, 0x00, 0x41, 0x01, 0x64, 0x64}), s));
    CHECK(apply1(B({0x23, 0x0A, 0x35, 0x01, 0x1E}), s));
    Json j = entryOf(s);
    CHECK_EQ(j["battery_left"].number, 70.0);
    CHECK(!j["battery_left_charging"].boolean);
    CHECK_EQ(j["battery_right"].number, 65.0);
    CHECK(j["battery_right_charging"].boolean);
    CHECK_EQ(j["battery_case"].number, 53.0);
    CHECK(j["battery_case_charging"].boolean);
    CHECK(j["battery_level"].isNull());
    CHECK_EQ(j["kind"].string, std::string("earbuds"));
}

TEST(WornStatusPerEarbud) {
    HeadphoneState s = withSupport({0xF6}, {});
    s.modelName = "WF-1000XM5";
    s.supported = deriveSupported(s);
    struct Case {
        uint8_t ws;
        std::optional<bool> worn, left, right;
    };
    const Case cases[] = {
        {0x00, true, true, true}, {0x01, std::nullopt, std::nullopt, std::nullopt},
        {0x02, true, false, true}, {0x03, true, true, false}, {0x04, false, false, false},
    };
    for (const auto& c : cases) {
        CHECK(apply2(B({0xF3, 0x00, c.ws}), s));
        CHECK_EQ(s.worn(), c.worn);
        CHECK_EQ(s.wornLeft(), c.left);
        CHECK_EQ(s.wornRight(), c.right);
    }
    HeadphoneState wh = withSupport({}, {0xF0});
    wh.modelName = "WH-1000XM5";
    CHECK(apply2(B({0xF5, 0x00, 0x03}), wh));
    CHECK_EQ(wh.worn(), std::optional<bool>(false));
    CHECK(!wh.wornLeft().has_value());
    CHECK(!wh.wornRight().has_value());
}

TEST(SidetoneSlotIsFoundBySubject) {
    HeadphoneState s = withSupport({0xD1, 0xD2});
    s.connected = true;
    // WF-1000XM5 fw 6.1.0 capture (protocol.md 9.1).
    CHECK(apply1(withString(withString(B({0xD1, 0xD1, 0x00, 0x01}), "SIDETONE_SETTING"), "SIDETONE_SETTING_SUMMARY"), s));
    CHECK(apply1(withString(withString(B({0xD1, 0xD2, 0x00, 0x01}), "MULTIPOINT_SETTING"),
                            "MULTIPOINT_SETTING_SUMMARY_LDAC_AVAILABLE"), s));
    selectVariants(s);
    CHECK_EQ(s.variants.sidetoneSlot, std::optional<uint8_t>(0xD1));
    CHECK_EQ(s.variants.multipointSlot, std::optional<uint8_t>(0xD2));
    CHECK(!s.variants.touchPanelSlot.has_value());
    CHECK(apply1(B({0xD7, 0xD1, 0x00, 0x00}), s));
    CHECK_EQ(s.sidetoneValue(), std::optional<bool>(true));
    CHECK(apply1(B({0xD9, 0xD1, 0x00, 0x01}), s));
    CHECK_EQ(s.sidetoneValue(), std::optional<bool>(false));
    s.supported = deriveSupported(s);
    CHECK(s.supported->sidetone);
    CHECK(!s.supported->touch_panel);
    Json j = entryOf(s);
    CHECK(!j["sidetone"].boolean);
    CHECK(!j["sidetone"].isNull());
    CHECK(j["touch_panel"].isNull());
    CHECK_EQ(req::setGeneralSettingBool(0xD1, true), B({0xD8, 0xD1, 0x00, 0x00}));
}

TEST(ConnectionModeRepliesAreKeptRaw) {
    HeadphoneState s;
    // This project's WF-1000XM5 run (protocol.md 9.5).
    CHECK(apply1(B({0xE1, 0x05, 0x03, 0x00, 0x01, 0x02, 0x00}), s));
    CHECK(apply1(B({0xE3, 0x05, 0x00, 0x00}), s));
    REQUIRE(s.connectionModeCapability.has_value());
    CHECK_EQ(hexBytes(*s.connectionModeCapability), std::string("E1 05 03 00 01 02 00"));
    CHECK_EQ(hexBytes(*s.connectionModeStatus), std::string("E3 05 00 00"));
    CHECK(apply1(B({0xE5, 0x05, 0x01, 0x00}), s));
    CHECK_EQ(hexBytes(*s.connectionModeStatus), std::string("E5 05 01 00"));
    CHECK(!s.upscalingType.has_value());
    CHECK(!s.dseeAvailable.has_value());
    Json probe = parsed(probeJson(s, {}));
    CHECK_EQ(probe["connection_mode"]["capability"].string, std::string("E1 05 03 00 01 02 00"));
    CHECK_EQ(probe["connection_mode"]["status"].string, std::string("E5 05 01 00"));
}

TEST(SmallParamsParse) {
    HeadphoneState s;
    CHECK(apply1(B({0xA7, 0x20, 18}), s));
    CHECK_EQ(s.volume, std::optional<int>(18));
    CHECK(apply1(B({0x29, 0x05, 0x10, 0x00}), s));
    CHECK_EQ(autoPowerOffName(*s.autoPowerOff), std::optional<std::string>("when_taken_off"));
    CHECK(apply1(B({0x27, 0x04, 0x04, 0x04}), s));
    CHECK_EQ(autoPowerOffName(*s.autoPowerOff), std::optional<std::string>("15min"));
    CHECK(apply1(B({0x67, 0x30, 0x04}), s));
    CHECK_EQ(ncAmbButtonName(*s.ncAmbButton), std::optional<std::string>("ambient_off"));
    CHECK(apply1(B({0x13, 0x02, 0x10}), s));
    CHECK_EQ(codecName(*s.codec), std::string("LDAC"));
    CHECK(apply1(B({0x15, 0x02, 0x99}), s));
    CHECK_EQ(codecName(*s.codec), std::string("OTHER"));
    CHECK_EQ(codecName(0x00), std::string("NONE"));
    CHECK_EQ(codecName(0x21), std::string("APTX_HD"));
}

TEST(AutoPowerOffValuesMapBothWays) {
    const std::pair<uint8_t, const char*> pairs[] = {
        {0x00, "5min"}, {0x04, "15min"}, {0x01, "30min"}, {0x02, "60min"}, {0x03, "180min"},
        {0x10, "when_taken_off"}, {0x11, "off"}};
    for (const auto& [value, name] : pairs) {
        CHECK_EQ(autoPowerOffName(value), std::optional<std::string>(name));
        CHECK_EQ(autoPowerOffValue(name), std::optional<uint8_t>(value));
    }
}

TEST(GeneralSettingsFindMultipointSlot) {
    HeadphoneState s = withSupport({0xD1, 0xD2});
    Bytes touch = withString(withString(B({0xD1, 0xD1, 0x00, 0x01}), "TOUCH_PANEL_SETTING"), "S");
    Bytes multi = withString(withString(B({0xD1, 0xD2, 0x00, 0x01}), "MULTIPOINT_SETTING"), "SUMMARY");
    CHECK(apply1(touch, s));
    CHECK(s.multipointSlot() == nullptr);
    CHECK(apply1(multi, s));
    REQUIRE(s.multipointSlot() != nullptr);
    CHECK_EQ(s.multipointSlot()->slot, uint8_t{0xD2});
    CHECK(apply1(B({0xD7, 0xD2, 0x00, 0x00}), s));
    CHECK_EQ(s.multipointValue(), std::optional<bool>(true));
    CHECK(apply1(B({0xD9, 0xD2, 0x00, 0x01}), s));
    CHECK_EQ(s.multipointValue(), std::optional<bool>(false));
    CHECK(!apply1(B({0xD7, 0xD2, 0x01, 0x00}), s));
    selectVariants(s);
    CHECK_EQ(s.variants.multipointSlot, std::optional<uint8_t>(0xD2));
}

TEST(MultipointNeedsBooleanSlotWithSubject) {
    HeadphoneState s = withSupport({0xD1});
    CHECK(apply1(withString(withString(B({0xD1, 0xD1, 0x01, 0x01}), "MULTIPOINT_SETTING"), ""), s));
    CHECK(s.multipointSlot() == nullptr);
    CHECK(!deriveSupported(s).multipoint);
}

TEST(PairedDevicesParseBothListTypes) {
    HeadphoneState s;
    Bytes list = B({0x37, 0x02, 0x02});
    std::string a1 = "f0:1d:bc:aa:00:01";
    list.insert(list.end(), a1.begin(), a1.end());
    list.push_back(0x01);
    list.insert(list.end(), {0x5A, 0x01, 0x0C});
    list = withString(list, "laptop");
    std::string a2 = "5C:E9:1E:BB:00:02";
    list.insert(list.end(), a2.begin(), a2.end());
    list.push_back(0x02);
    list.insert(list.end(), {0x5A, 0x02, 0x0C});
    list = withString(list, "Pixel 8");
    list.push_back(0x02);
    CHECK(apply2(list, s));
    REQUIRE(s.devices.has_value());
    CHECK_EQ(s.devices->size(), size_t{2});
    CHECK_EQ(s.devices->at(0).address, std::string("F0:1D:BC:AA:00:01"));
    CHECK_EQ(s.playbackSlot, uint8_t{2});

    Bytes plain = B({0x39, 0x00, 0x01});
    plain.insert(plain.end(), a2.begin(), a2.end());
    plain.push_back(0x00);
    plain = withString(plain, "");
    plain.push_back(0x00);
    CHECK(apply2(plain, s));
    CHECK_EQ(s.devices->size(), size_t{1});
    CHECK_EQ(s.devices->at(0).name, std::string(""));
    CHECK(!apply2(B({0x37, 0x00, 0x01, 'A'}), s));
}

TEST(Table2RepliesDoNotLeakIntoTable1State) {
    HeadphoneState s;
    CHECK(apply2(B({0xF3, 0x00, 0x04}), s));
    CHECK_EQ(s.worn(), std::optional<bool>(false));
    CHECK(!s.assignableValue.has_value());
    CHECK(!apply2(B({0x23, 0x00, 50, 0x00}), s));
    CHECK(!s.batteryLevel.has_value());
    CHECK(apply2(B({0x47, 0x01, 0x00, 0x01}), s));
    CHECK_EQ(s.voiceGuidance, std::optional<bool>(true));
    CHECK_EQ(s.voiceGuidanceLanguage, std::optional<uint8_t>(0x01));
    CHECK(apply2(B({0x47, 0x00, 0x01}), s));
    CHECK_EQ(s.voiceGuidance, std::optional<bool>(false));
    CHECK(apply2(B({0x47, 0x20, 0xFE}), s));
    CHECK_EQ(s.voiceGuidanceVolume, std::optional<int>(-2));
    CHECK(!s.volume.has_value());
    CHECK(apply2(B({0xF5, 0x00, 0x00}), s));
    CHECK_EQ(s.worn(), std::optional<bool>(true));
    CHECK(apply2(B({0xF5, 0x00, 0x01}), s));
    CHECK(!s.worn().has_value());
    CHECK(apply2(B({0x37, 0x01, 0x01}), s));
    CHECK_EQ(s.sourceSwitchAuto, std::optional<bool>(true));
}

TEST(EventsAreReported) {
    HeadphoneState s;
    InboundEvents ev;
    CHECK(applyInbound(Table::T1, B({0x99, 0x00, 0x07, 0x01}), s, ev));
    REQUIRE(ev.alert.has_value());
    CHECK_EQ(ev.alert->messageType, uint8_t{0x07});
    CHECK_EQ(ev.alert->actionType, uint8_t{0x01});

    InboundEvents log;
    Bytes line = B({0xC9, 0x01, 0x00});
    std::string text = "{\"unitWear\":1}";
    line.insert(line.end(), text.begin(), text.end());
    CHECK(applyInbound(Table::T1, line, s, log));
    CHECK(log.wearLogLine);

    // WF-1000XM5 lines carry the side and an upper case U (protocol.md 9.4).
    for (const char* text : {"lUnitWear", "rUnitWear", "lUnitRemove", "rUnitRemove", "UNITWEAR"}) {
        InboundEvents wf;
        CHECK(applyInbound(Table::T1, withString(B({0xC9, 0x01}), text), s, wf));
        CHECK(wf.wearLogLine);
    }
    InboundEvents other;
    CHECK(applyInbound(Table::T1, withString(B({0xC9, 0x01}), "unitWea"), s, other));
    CHECK(!other.wearLogLine);

    InboundEvents src;
    Bytes result = B({0x3D, 0x01, 0x00});
    std::string addr = "5c:e9:1e:bb:00:02";
    result.insert(result.end(), addr.begin(), addr.end());
    CHECK(applyInbound(Table::T2, result, s, src));
    REQUIRE(src.sourceSwitch.has_value());
    CHECK_EQ(src.sourceSwitch->address, std::string("5C:E9:1E:BB:00:02"));
}

// ---------------------------------------------------------------------------
// Variant selection and `supported`
// ---------------------------------------------------------------------------

TEST(VariantsFollowSupportList) {
    auto a = withSupport({0x20, 0x6B, 0x25, 0xE1, 0xF3}, {0x32, 0x41});
    CHECK_EQ(a.variants.batteryType, uint8_t{0x00});
    CHECK_EQ(a.variants.ncAsmType, std::optional<uint8_t>(0x17));
    CHECK_EQ(a.variants.autoPowerOffType, uint8_t{0x05});
    CHECK_EQ(a.variants.connectionQualityType, uint8_t{0x00});
    CHECK_EQ(a.variants.assignableType, uint8_t{0x03});
    CHECK_EQ(a.variants.deviceListType, uint8_t{0x02});
    CHECK_EQ(a.variants.voiceGuidanceType, uint8_t{0x01});

    auto b = withSupport({0x28, 0x6D, 0x24, 0xE3, 0xFE, 0xF3}, {0x30, 0x40});
    CHECK_EQ(b.variants.batteryType, uint8_t{0x08});
    CHECK_EQ(b.variants.ncAsmType, std::optional<uint8_t>(0x19));
    CHECK_EQ(b.variants.autoPowerOffType, uint8_t{0x04});
    CHECK_EQ(b.variants.connectionQualityType, uint8_t{0x02});
    CHECK_EQ(b.variants.assignableType, uint8_t{0x0E});
    CHECK_EQ(b.variants.deviceListType, uint8_t{0x00});
    CHECK_EQ(b.variants.voiceGuidanceType, uint8_t{0x00});

    auto c = withSupport({0x68}, {0x44});
    CHECK_EQ(c.variants.ncAsmType, std::optional<uint8_t>(0x15));
    CHECK_EQ(c.variants.voiceGuidanceType, uint8_t{0x02});
    auto d = withSupport({0x67}, {0x45});
    CHECK(!d.variants.ncAsmType.has_value());
    CHECK_EQ(d.variants.voiceGuidanceType, uint8_t{0x03});
    CHECK(!deriveSupported(d).noise_control);
}

TEST(SupportedFollowsContractTable) {
    auto s = withSupport({0x12, 0x20, 0x25, 0x50, 0x69, 0x6B, 0xA1, 0xE1, 0xE2, 0xF1, 0xFC}, {0x32, 0x31, 0x42});
    Supported sup = deriveSupported(s);
    CHECK(sup.battery);
    CHECK(sup.noise_control);
    CHECK(sup.eq_preset);
    CHECK(!sup.eq_custom);
    CHECK(sup.dsee);
    CHECK(sup.speak_to_chat);
    CHECK(sup.voice_guidance);
    CHECK(sup.voice_guidance_volume);
    CHECK(sup.auto_power_off);
    CHECK(sup.nc_amb_button);
    CHECK(!sup.button_function);
    CHECK(sup.ear_detection);
    CHECK(!sup.worn);
    CHECK(sup.volume);
    CHECK(sup.connection_quality);
    CHECK(sup.codec);
    CHECK(!sup.multipoint);
    CHECK(sup.devices);
    CHECK(sup.source_switch);
    CHECK(!sup.touch_panel);
    CHECK(!sup.quick_access);

    s.eqCapability = EqCapability{6, 21, {}};
    CHECK(deriveSupported(s).eq_custom);
    s.eqCapability = EqCapability{10, 13, {}};
    CHECK(!deriveSupported(s).eq_custom);
    s.eqCustomSeen = true;
    CHECK(deriveSupported(s).eq_custom);

    auto noTable2 = withSupport({0xF6}, {}, false);
    CHECK(!deriveSupported(noTable2).worn);
    CHECK(deriveSupported(withSupport({0xF6}, {})).worn);
    CHECK(deriveSupported(withSupport({}, {0xF0})).worn);
    CHECK(!deriveSupported(withSupport({0xF1}, {0x41})).worn);
    auto quickAccess = withSupport({0xFD});
    CHECK(!deriveSupported(quickAccess).quick_access);
    quickAccess.quickAccessCapability = QuickAccessCapability{0x00, 0x00, {{0x01, 0x00, {}}, {0x02, 0x00, {}}}};
    CHECK(!deriveSupported(quickAccess).quick_access);
    quickAccess.quickAccessCapability->actions[1].functions = {0x00, 0x01};
    CHECK(deriveSupported(quickAccess).quick_access);
    auto sourceOnly = withSupport({}, {0x31});
    CHECK(!deriveSupported(sourceOnly).source_switch);
    auto volumeOnly = withSupport({}, {0x43});
    CHECK(volumeOnly.variants.voiceGuidanceType == 0x01);
    CHECK(deriveSupported(volumeOnly).voice_guidance_volume);
    CHECK(!deriveSupported(volumeOnly).voice_guidance);
}

TEST(EarbudFeaturesFollowContractTable) {
    auto wf = withSupport({0x29, 0x2A, 0xF3, 0xD1, 0xFD, 0xF6, 0xE7}, {0x42, 0x32, 0x31});
    Supported sup = deriveSupported(wf);
    CHECK(!sup.battery);
    CHECK(sup.battery_lr);
    CHECK(sup.battery_case);
    CHECK(sup.worn);
    CHECK(!sup.touch_function);
    CHECK(!sup.sidetone);
    CHECK(!sup.quick_access);
    CHECK(!sup.connection_quality);
    CHECK_EQ(wf.variants.lrBatteryType, uint8_t{0x09});
    CHECK_EQ(wf.variants.caseBatteryType, uint8_t{0x0A});
    auto older = withSupport({0x21, 0x22});
    CHECK(deriveSupported(older).battery_lr);
    CHECK(deriveSupported(older).battery_case);
    CHECK_EQ(older.variants.lrBatteryType, uint8_t{0x01});
    CHECK_EQ(older.variants.caseBatteryType, uint8_t{0x02});
    CHECK_EQ(req::getBattery(0x09), B({0x22, 0x09}));
    CHECK_EQ(req::getBattery(0x0A), B({0x22, 0x0A}));
    CHECK_EQ(req::getConnectionModeCapability(), B({0xE0, 0x05}));
    CHECK_EQ(req::getConnectionModeStatus(), B({0xE2, 0x05}));

    // One key only (the NC/AMB key) is not a touch sensor pair.
    wf.assignableCapability = AssignableCapability{0x03, {{0x00, 0x00, 0x35, {0x35}}}};
    CHECK(!deriveSupported(wf).touch_function);
    wf.assignableCapability->keys.push_back({0x01, 0x00, 0x20, {0x20}});
    CHECK(deriveSupported(wf).touch_function);
    CHECK(!deriveSupported(wf).button_function);
}

TEST(FallbackListMatchesContract) {
    Supported wh;
    wh.battery = wh.noise_control = wh.eq_preset = wh.eq_custom = true;
    wh.speak_to_chat = wh.voice_guidance = wh.auto_power_off = true;
    wh.nc_amb_button = wh.ear_detection = wh.volume = true;
    CHECK(Supported::fallback("wh-1000xm5") == wh);
    Supported wf;
    wf.battery_lr = wf.battery_case = wf.noise_control = wf.eq_preset = wf.eq_custom = true;
    wf.speak_to_chat = wf.voice_guidance = wf.voice_guidance_volume = wf.auto_power_off = true;
    wf.ear_detection = wf.volume = true;
    CHECK(Supported::fallback("wf-1000xm5") == wf);
    CHECK(Supported::fallback("other") == Supported{});
}

// ---------------------------------------------------------------------------
// status.json and probe
// ---------------------------------------------------------------------------

TEST(DisconnectedDocumentHasFiveKeys) {
    Json j = parsed(statusDocument({}, std::nullopt, 1760438400));
    const std::vector<std::string> keys = {"schema_version", "connected", "last_updated", "primary", "headsets"};
    CHECK(j.keyOrder == keys);
    auto fixture = testutil::parseJson(testutil::readFile(SONY_FIXTURES_DIR "/status_v3_disconnected.json"));
    REQUIRE(fixture.has_value());
    CHECK_EQ(testutil::jsonDiff(j, *fixture, {}), std::string(""));
}

TEST(HeadsetEntryHasEveryContractKeyInFixtureOrder) {
    auto fixture = testutil::parseJson(testutil::readFile(SONY_FIXTURES_DIR "/status_v3_wf.json"));
    REQUIRE(fixture.has_value());
    const Json expected = (*fixture)["headsets"].array[0];
    HeadphoneState s = withSupport({0x29});
    s.connected = true;
    s.address = "AC:80:0A:65:43:21";
    s.supported = deriveSupported(s);
    Json j = entryOf(s);
    CHECK(j.keyOrder == expected.keyOrder);
    CHECK(j["supported"].keyOrder == expected["supported"].keyOrder);
    HeadphoneState initializing;
    initializing.connected = true;
    initializing.address = "AC:80:0A:65:43:21";
    CHECK(entryOf(initializing).keyOrder == expected.keyOrder);
}

TEST(ModelKindAndNameFollowContract) {
    HeadphoneState s;
    s.bluezName = "Living room wf-1000xm5";
    CHECK_EQ(std::string(s.model()), std::string("wf-1000xm5"));
    CHECK_EQ(std::string(s.kind()), std::string("earbuds"));
    CHECK_EQ(s.displayName(), std::string("WF-1000XM5"));
    s.modelName = "wh-1000XM5";
    CHECK_EQ(std::string(s.model()), std::string("wh-1000xm5"));
    CHECK_EQ(std::string(s.kind()), std::string("headphones"));
    CHECK_EQ(s.displayName(), std::string("WH-1000XM5"));
    s.alias = "My XM5";
    CHECK_EQ(s.displayName(), std::string("My XM5"));
    s.alias.clear();
    s.modelName = "WH-1000XM6";
    CHECK_EQ(std::string(s.model()), std::string("other"));
    CHECK_EQ(s.displayName(), std::string("Sony headset"));
    CHECK_EQ(std::string(s.kind()), std::string("headphones"));
    s.supported = Supported{};
    s.supported->battery_lr = true;
    CHECK_EQ(std::string(s.kind()), std::string("earbuds"));
}

namespace {

HeadphoneState listed(const std::string& id, const std::string& model, int64_t since) {
    HeadphoneState s;
    s.connected = true;
    s.address = id;
    s.modelName = model;
    s.connectedSince = since;
    return s;
}

} // namespace

TEST(HeadsetsAreOrderedByKindThenId) {
    auto wf1 = listed("AC:80:0A:00:00:01", "WF-1000XM5", 1);
    auto wh2 = listed("AC:80:0A:00:00:02", "WH-1000XM5", 2);
    auto wh1 = listed("AC:80:0A:00:00:00", "WH-1000XM5", 3);
    auto ordered = orderHeadsets({&wf1, &wh2, &wh1});
    REQUIRE(ordered.size() == 3);
    CHECK(ordered[0] == &wh1);
    CHECK(ordered[1] == &wh2);
    CHECK(ordered[2] == &wf1);
}

TEST(PrimaryPrefersHeadsetPlayingHere) {
    const std::vector<std::string> adapters = {"F0:1D:BC:AA:00:01"};
    auto wh = listed("AC:80:0A:12:34:56", "WH-1000XM5", 1760438430);
    auto wf = listed("AC:80:0A:65:43:21", "WF-1000XM5", 1760438400);
    CHECK_EQ(choosePrimary({}, adapters), std::optional<std::string>());
    // Nobody plays here: the most recent connection.
    CHECK_EQ(choosePrimary(orderHeadsets({&wh, &wf}), adapters), std::optional<std::string>("AC:80:0A:12:34:56"));
    // Device lists: the WF plays from the adapter, the WH from a phone.
    for (auto* s : {&wh, &wf}) {
        s->support2 = std::vector<SupportEntry>{{0x32, 1}};
        s->supported = deriveSupported(*s);
    }
    wh.devices = std::vector<PairedDevice>{{"F0:1D:BC:AA:00:01", "laptop", 1}, {"5C:E9:1E:BB:00:02", "phone", 2}};
    wh.playbackSlot = 2;
    wf.devices = std::vector<PairedDevice>{{"F0:1D:BC:AA:00:01", "laptop", 1}};
    wf.playbackSlot = 1;
    CHECK(!playsFromHere(wh, adapters));
    CHECK(playsFromHere(wf, adapters));
    CHECK_EQ(choosePrimary(orderHeadsets({&wh, &wf}), adapters), std::optional<std::string>("AC:80:0A:65:43:21"));
    // Without a device list the codec decides.
    HeadphoneState codecOnly = listed("AC:80:0A:00:00:09", "WH-1000XM5", 5);
    codecOnly.support1 = std::vector<SupportEntry>{{0x12, 1}};
    codecOnly.supported = deriveSupported(codecOnly);
    CHECK(!playsFromHere(codecOnly, adapters));
    codecOnly.codec = 0x00;
    CHECK(!playsFromHere(codecOnly, adapters));
    codecOnly.codec = 0x10;
    CHECK(playsFromHere(codecOnly, adapters));
    // Tie on connected_since: the first in list order.
    wf.devices = std::vector<PairedDevice>{{"F0:1D:BC:AA:00:01", "laptop", 1}, {"5C:E9:1E:BB:00:02", "phone", 2}};
    wf.playbackSlot = 2;
    wf.connectedSince = wh.connectedSince;
    CHECK_EQ(choosePrimary(orderHeadsets({&wf, &wh}), adapters), std::optional<std::string>("AC:80:0A:12:34:56"));
    std::string doc = statusDocument(orderHeadsets({&wf, &wh}), choosePrimary(orderHeadsets({&wf, &wh}), adapters), 7);
    Json j = parsed(doc);
    CHECK(j["connected"].boolean);
    CHECK_EQ(j["primary"].string, std::string("AC:80:0A:12:34:56"));
    CHECK_EQ(j["headsets"].array.size(), size_t{2});
}

TEST(UnsupportedFeaturesAreNullEvenWhenKnown) {
    HeadphoneState s = withSupport({0x20, 0x6B});
    s.connected = true;
    s.supported = deriveSupported(s);
    s.dsee = true;
    s.volume = 7;
    s.codec = 0x10;
    s.batteryLevel = 50;
    s.batteryChargingRaw = 0x00;
    Json j = entryOf(s);
    CHECK(j["dsee_extreme"].isNull());
    CHECK(j["volume"].isNull());
    CHECK(j["codec"].isNull());
    CHECK(!j.has("dsee"));
    CHECK(!j.has("connected"));
    CHECK_EQ(j["battery_level"].number, 50.0);
    CHECK_EQ(j["supported_source"].string, std::string("device"));
    CHECK(!j["supported"]["dsee"].boolean);
}

TEST(EmptyOptionListsAreNull) {
    HeadphoneState s = withSupport({0x50, 0xF3});
    s.connected = true;
    s.eqCapability = EqCapability{6, 21, {{0x77, "New"}}};
    s.assignableCapability = AssignableCapability{0x03, {{0x04, 0x01, 0x00, {0x99}}}};
    s.supported = deriveSupported(s);
    CHECK(!s.eqPresetNames().has_value());
    CHECK(!s.buttonFunctionOptions().has_value());
    Json j = entryOf(s);
    CHECK(j["eq_presets"].isNull());
    CHECK(j["button_functions"].isNull());
    CHECK(j["supported"]["button_function"].boolean);
}

TEST(OutOfRangeValuesAreDropped) {
    HeadphoneState s;
    CHECK(apply1(B({0xA7, 0x20, 12}), s));
    CHECK(apply1(B({0xA9, 0x20, 31}), s));
    CHECK(!s.volume.has_value());
    Bytes list = B({0x37, 0x00, 0x02});
    std::string bad = "F0-1D-BC-AA-00-01";
    list.insert(list.end(), bad.begin(), bad.end());
    list.push_back(0x01);
    list = withString(list, "bad");
    std::string good = "5C:E9:1E:BB:00:02";
    list.insert(list.end(), good.begin(), good.end());
    list.push_back(0x01);
    list = withString(list, "good");
    list.push_back(0x01);
    CHECK(apply2(list, s));
    REQUIRE(s.devices.has_value());
    REQUIRE(s.devices->size() == 1);
    CHECK_EQ(s.devices->at(0).name, std::string("good"));
}

TEST(StatusEscapesDeviceStrings) {
    HeadphoneState s;
    s.connected = true;
    s.alias = std::string("My \"XM5\"\n\xff");
    Json j = entryOf(s);
    CHECK_EQ(j["device_name"].string, std::string("My \"XM5\"\n\xEF\xBF\xBD"));
    CHECK(j["supported"].isNull());
    CHECK(j["supported_source"].isNull());
}

TEST(FunctionNamesComeFromMos9527Enums) {
    CHECK_EQ(std::string(functionName(Table::T1, 0x6B)),
             std::string("MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT"));
    CHECK_EQ(std::string(functionName(Table::T2, 0x32)),
             std::string("PAIRING_DEVICE_MANAGEMENT_WITH_BLUETOOTH_CLASS_OF_DEVICE_CLASSIC_BT"));
    CHECK_EQ(std::string(functionName(Table::T1, 0x01)), std::string("UNKNOWN"));
    CHECK_EQ(std::string(functionName(Table::T2, 0x12)), std::string("UNKNOWN"));
}

TEST(ProbeListsRawData) {
    HeadphoneState s = withSupport({0x20, 0xD1, 0x6B}, {0x32});
    s.connected = true;
    s.protocolKnown = true;
    s.protocolVersion = 4096;
    s.table1Supported = s.table2Supported = true;
    s.series = 0x30;
    apply1(withString(withString(B({0xD1, 0xD1, 0x00, 0x01}), "MULTIPOINT_SETTING"), "SUM"), s);
    apply1(B({0xD7, 0xD1, 0x00, 0x01}), s);
    s.supported = deriveSupported(s);
    std::vector<FrameRecord> frames{{true, PacketType::DATA_MDR, 0, B({0x06, 0x00})},
                                    {false, PacketType::DATA_MDR, 1, B({0x07, 0x00, 0x01, 0x20, 0x01})}};
    Json j = parsed(probeJson(s, frames));
    CHECK_EQ(j["probe_version"].number, 1.0);
    CHECK_EQ(j["protocol"]["version"].number, 4096.0);
    CHECK_EQ(j["series"].string, std::string("0x30"));
    CHECK(j["color"].isNull());
    CHECK_EQ(j["nc_asm_type"].string, std::string("0x17"));
    CHECK_EQ(j["support_table1"].array.size(), size_t{3});
    CHECK_EQ(j["support_table1"].array[0]["name"].string, std::string("BATTERY_LEVEL_INDICATOR"));
    CHECK_EQ(j["support_table2"].array[0]["id"].string, std::string("0x32"));
    REQUIRE(j["general_settings"].array.size() == 1);
    const Json gs = j["general_settings"].array[0];
    CHECK_EQ(gs["slot"].string, std::string("0xD1"));
    CHECK_EQ(gs["type"].string, std::string("boolean"));
    CHECK_EQ(gs["format"].string, std::string("enum_name"));
    CHECK_EQ(gs["subject"].string, std::string("MULTIPOINT_SETTING"));
    CHECK(!gs["value"].boolean);
    CHECK(j["eq_capability"].isNull());
    CHECK(j["assignable"].isNull());
    REQUIRE(j["recent_frames"].array.size() == 2);
    CHECK_EQ(j["recent_frames"].array[1]["dir"].string, std::string("rx"));
    CHECK_EQ(j["recent_frames"].array[1]["type"].string, std::string("0x0C"));
    CHECK_EQ(j["recent_frames"].array[1]["payload"].string, std::string("07 00 01 20 01"));
    const std::vector<std::string> keys = {"probe_version", "id", "model", "kind", "address", "protocol",
                                           "model_name", "firmware_version", "series", "color", "unique_id",
                                           "nc_asm_type", "support_table1", "support_table2", "general_settings",
                                           "eq_capability", "assignable", "connection_mode", "recent_frames"};
    CHECK(j["connection_mode"]["capability"].isNull());
    CHECK(j["connection_mode"]["status"].isNull());
    CHECK(j.keyOrder == keys);
}

// ---------------------------------------------------------------------------
// Headset selection (BlueZ device list)
// ---------------------------------------------------------------------------

namespace {

BluetoothDeviceInfo btDevice(const std::string& mac, const std::string& name, bool connected,
                             bool mdrUuid = true, bool paired = true) {
    BluetoothDeviceInfo d;
    d.macAddress = mac;
    d.name = name;
    d.alias = name.empty() ? mac : name;
    d.paired = paired;
    d.connected = connected;
    if (mdrUuid) d.uuids.emplace_back(MDR_UUID_XM5_LOWER);
    return d;
}

std::vector<std::string> pick(const std::vector<BluetoothDeviceInfo>& devices,
                              const std::vector<std::string>& allowList = {}) {
    std::vector<std::string> out;
    for (const auto& dev : selectHeadsets(devices, allowList)) out.push_back(dev.macAddress);
    return out;
}

using Macs = std::vector<std::string>;

} // namespace

TEST(SelectionTakesEveryConnectedWhAndWf) {
    std::vector<BluetoothDeviceInfo> devices = {
        btDevice("AA:BB:CC:DD:EE:02", "WF-1000XM5", true),
        btDevice("AA:BB:CC:DD:EE:01", "WH-1000XM5", true),
        btDevice("AA:BB:CC:DD:EE:03", "WH-1000XM4", true),
        btDevice("AA:BB:CC:DD:EE:04", "Phone", true, false),
    };
    CHECK((pick(devices) == Macs{"AA:BB:CC:DD:EE:02", "AA:BB:CC:DD:EE:01"}));
}

TEST(SelectionNeverTakesADisconnectedHeadset) {
    std::vector<BluetoothDeviceInfo> devices = {
        btDevice("AA:BB:CC:DD:EE:01", "WH-1000XM5", false),
        btDevice("AA:BB:CC:DD:EE:02", "WF-1000XM5", true, true, false),
        btDevice("AA:BB:CC:DD:EE:06", "My wf-1000xm5", true, false),
    };
    CHECK((pick(devices) == Macs{"AA:BB:CC:DD:EE:06"}));
    CHECK((pick(devices, {"AA:BB:CC:DD:EE:01"}) == Macs{}));
}

TEST(SelectionAllowListIgnoresNamesAndUuids) {
    std::vector<BluetoothDeviceInfo> devices = {
        btDevice("AA:BB:CC:DD:EE:02", "WF-1000XM5", true),
        btDevice("AA:BB:CC:DD:EE:07", "", true),
        btDevice("AA:BB:CC:DD:EE:08", "Speaker", true, false),
    };
    CHECK((pick(devices) == Macs{"AA:BB:CC:DD:EE:02"}));
    CHECK((pick(devices, {"aa:bb:cc:dd:ee:07", "AA:BB:CC:DD:EE:08"}) == Macs{"AA:BB:CC:DD:EE:07", "AA:BB:CC:DD:EE:08"}));
    CHECK((pick(devices, {"AA:BB:CC:DD:EE:09"}) == Macs{}));
}

TEST(MockDiscoveryReportsAdapters) {
    MockDeviceDiscovery discovery;
    discovery.devices = {btDevice("AA:BB:CC:DD:EE:02", "WF-1000XM5", true)};
    discovery.adapterAddresses = {"F0:1D:BC:AA:00:01"};
    auto snap = discovery.snapshot();
    CHECK(snap.ok);
    CHECK_EQ(snap.devices.size(), size_t{1});
    CHECK((snap.adapterAddresses == Macs{"F0:1D:BC:AA:00:01"}));
    auto found = discovery.findSonyHeadphones("aa:bb:cc:dd:ee:02");
    REQUIRE(found.has_value());
    CHECK_EQ(found->macAddress, std::string("AA:BB:CC:DD:EE:02"));
    CHECK(!discovery.findSonyHeadphones("AA:BB:CC:DD:EE:09").has_value());
}

int main() {
    return testutil::runAll("ProtocolTest");
}
