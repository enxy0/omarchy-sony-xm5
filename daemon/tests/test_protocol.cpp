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
    Json j = parsed(statusJson(s, 1));
    CHECK_EQ(j["eq_preset"].string, std::string("off"));
    CHECK(j["supported"]["eq_custom"].boolean);
    REQUIRE(j["eq_custom_bands"].array.size() == 5);
    for (const auto& band : j["eq_custom_bands"].array) CHECK_EQ(band.number, 0.0);
    REQUIRE(j["eq_bands"].array.size() == 5);
    CHECK_EQ(j["clear_bass"].number, 0.0);
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
    Bytes cap03 = B({0xF1, 0x03, 0x02,
                     0x02, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x20, 0x00,
                     0x04, 0x01, 0x00, 0x03,
                     0x00, 0x00, 0x00,
                     0x31, 0x01, 0x01, 0x21, 0x01, 0x01, 0x21, 0x02, 0x21, 0x22,
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
    CHECK(sup.worn);
    CHECK(sup.volume);
    CHECK(sup.connection_quality);
    CHECK(sup.codec);
    CHECK(!sup.multipoint);
    CHECK(sup.devices);
    CHECK(sup.source_switch);

    s.eqCapability = EqCapability{6, 21, {}};
    CHECK(deriveSupported(s).eq_custom);
    s.eqCapability = EqCapability{10, 13, {}};
    CHECK(!deriveSupported(s).eq_custom);
    s.eqCustomSeen = true;
    CHECK(deriveSupported(s).eq_custom);

    auto noTable2 = withSupport({0xF1}, {}, false);
    CHECK(!deriveSupported(noTable2).worn);
    auto sourceOnly = withSupport({}, {0x31});
    CHECK(!deriveSupported(sourceOnly).source_switch);
    auto volumeOnly = withSupport({}, {0x43});
    CHECK(volumeOnly.variants.voiceGuidanceType == 0x01);
    CHECK(deriveSupported(volumeOnly).voice_guidance_volume);
    CHECK(!deriveSupported(volumeOnly).voice_guidance);
}

TEST(FallbackListMatchesContract) {
    Supported f = Supported::fallback();
    Supported expected;
    expected.battery = expected.noise_control = expected.eq_preset = expected.eq_custom = true;
    expected.speak_to_chat = expected.voice_guidance = expected.auto_power_off = true;
    expected.nc_amb_button = expected.ear_detection = expected.volume = true;
    CHECK(f == expected);
}

// ---------------------------------------------------------------------------
// status.json and probe
// ---------------------------------------------------------------------------

TEST(DisconnectedStatusHasThreeKeys) {
    HeadphoneState s;
    Json j = parsed(statusJson(s, 1760438400));
    CHECK_EQ(j.keyOrder.size(), size_t{3});
    auto fixture = testutil::parseJson(testutil::readFile(SONY_FIXTURES_DIR "/status_v2_disconnected.json"));
    REQUIRE(fixture.has_value());
    CHECK_EQ(testutil::jsonDiff(j, *fixture, {}), std::string(""));
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
    Json j = parsed(statusJson(s, 1));
    CHECK(j["dsee_extreme"].isNull());
    CHECK(j["dsee"].isNull());
    CHECK(j["volume"].isNull());
    CHECK(j["codec"].isNull());
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
    Json j = parsed(statusJson(s, 1));
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
    s.deviceName = std::string("My \"XM5\"\n\xff");
    Json j = parsed(statusJson(s, 1));
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
    const std::vector<std::string> keys = {"probe_version", "address", "protocol", "model_name", "firmware_version",
                                           "series", "color", "unique_id", "nc_asm_type", "support_table1",
                                           "support_table2", "general_settings", "eq_capability", "assignable",
                                           "recent_frames"};
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

std::optional<std::string> pick(const std::vector<BluetoothDeviceInfo>& devices, const std::string& mac = "") {
    MockDeviceDiscovery discovery;
    discovery.devices = devices;
    auto dev = discovery.findSonyHeadphones(mac);
    if (!dev) return std::nullopt;
    return dev->macAddress;
}

} // namespace

TEST(SelectionPrefersConnectedWh1000xm5OverEarbuds) {
    std::vector<BluetoothDeviceInfo> devices = {
        btDevice("AA:BB:CC:DD:EE:02", "WF-1000XM5", true),
        btDevice("AA:BB:CC:DD:EE:01", "WH-1000XM5", true),
    };
    CHECK_EQ(pick(devices), std::optional<std::string>("AA:BB:CC:DD:EE:01"));
    devices[1].connected = false;
    CHECK_EQ(pick(devices), std::optional<std::string>("AA:BB:CC:DD:EE:01"));
    devices.push_back(btDevice("AA:BB:CC:DD:EE:03", "WH-1000XM5", true));
    CHECK_EQ(pick(devices), std::optional<std::string>("AA:BB:CC:DD:EE:03"));
}

TEST(SelectionNeverPicksOtherModelsWithoutDevice) {
    std::vector<BluetoothDeviceInfo> devices = {
        btDevice("AA:BB:CC:DD:EE:02", "WF-1000XM5", true),
        btDevice("AA:BB:CC:DD:EE:04", "WH-1000XM4", true),
        btDevice("AA:BB:CC:DD:EE:05", "Phone", true, false),
    };
    CHECK(!pick(devices).has_value());
    CHECK_EQ(pick(devices, "aa:bb:cc:dd:ee:02"), std::optional<std::string>("AA:BB:CC:DD:EE:02"));
    CHECK(!pick(devices, "AA:BB:CC:DD:EE:09").has_value());
}

TEST(SelectionMatchesNameCaseInsensitiveAndIgnoresUnpaired) {
    std::vector<BluetoothDeviceInfo> devices = {
        btDevice("AA:BB:CC:DD:EE:01", "WH-1000XM5", true, true, false),
        btDevice("AA:BB:CC:DD:EE:06", "My wh-1000xm5", false, false),
    };
    CHECK_EQ(pick(devices), std::optional<std::string>("AA:BB:CC:DD:EE:06"));
}

TEST(SelectionUsesUuidOnlyForUnnamedDevices) {
    std::vector<BluetoothDeviceInfo> devices = {
        btDevice("AA:BB:CC:DD:EE:07", "", false),
        btDevice("AA:BB:CC:DD:EE:08", "", true),
        btDevice("AA:BB:CC:DD:EE:09", "", true, false),
    };
    CHECK_EQ(pick(devices), std::optional<std::string>("AA:BB:CC:DD:EE:08"));
    devices.push_back(btDevice("AA:BB:CC:DD:EE:01", "WH-1000XM5", false));
    CHECK_EQ(pick(devices), std::optional<std::string>("AA:BB:CC:DD:EE:01"));
    std::vector<BluetoothDeviceInfo> legacy = {btDevice("AA:BB:CC:DD:EE:0A", "", true, false)};
    legacy[0].uuids.emplace_back(MDR_UUID_LEGACY);
    CHECK_EQ(pick(legacy), std::optional<std::string>("AA:BB:CC:DD:EE:0A"));
}

int main() {
    return testutil::runAll("ProtocolTest");
}
