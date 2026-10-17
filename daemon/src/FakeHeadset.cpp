#include "FakeHeadset.hpp"

#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace omarchy::sony::protocol {

namespace {

void appendString(Bytes& out, const std::string& text) {
    out.push_back(static_cast<uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

struct PresetName {
    uint8_t id;
    const char* name;
};

constexpr PresetName kEqPresetNames[] = {
    {0x00, "Off"}, {0x10, "Bright"}, {0x11, "Excited"}, {0x12, "Mellow"}, {0x13, "Relaxed"},
    {0x14, "Vocal"}, {0x15, "Treble Boost"}, {0x16, "Bass Boost"}, {0x17, "Speech"}, {0xA0, "Custom"},
};

constexpr uint8_t kAssignablePresets[] = {0x00, 0x31, 0x32, 0x36};

constexpr uint8_t kQuickAccessCapability[] = {0xF1, 0x0D, 0x01, 0x01, 0x02, 0x01, 0x00, 0x04, 0x00, 0x05, 0x01,
                                              0x02, 0x02, 0x00, 0x04, 0x00, 0x05, 0x01, 0x02};

} // namespace

FakeHeadsetProfile FakeHeadsetProfile::xm5() {
    FakeHeadsetProfile p;
    p.table1 = {0x12, 0x20, 0x25, 0x50, 0x69, 0x6B, 0x90, 0xA1, 0xD1, 0xD2, 0xE1, 0xE2, 0xF1, 0xF3, 0xF6, 0xFC, 0xFD};
    p.table2 = {0x32, 0x31, 0x42, 0xF0};
    return p;
}

FakeHeadsetProfile FakeHeadsetProfile::minimal() {
    FakeHeadsetProfile p;
    p.table1 = {0x20, 0x6B};
    p.table2Supported = false;
    return p;
}

namespace {

std::vector<FakeEqPreset> capturedEqPresets() {
    std::vector<FakeEqPreset> out;
    for (uint8_t id : {0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0xA0, 0xA1, 0xA2}) out.push_back({id, ""});
    return out;
}

} // namespace

FakeHeadsetProfile FakeHeadsetProfile::wh1000xm5Capture() {
    FakeHeadsetProfile p;
    p.table1 = {0x10, 0x12, 0x13, 0x14, 0x23, 0x25, 0x20, 0x27, 0xD1, 0x32, 0x50, 0x6B, 0x70, 0x90, 0xA1, 0xC1, 0xC2,
                0xE1, 0xE2, 0xF4, 0x93, 0x69, 0xD2, 0xF9, 0xB0, 0xB2, 0xB6, 0xF1, 0xFD, 0xFC, 0xB8, 0xB7, 0x19};
    p.table2 = {0x41, 0x50, 0xF2, 0x32, 0x31, 0xF8};
    p.answerWornStatus = false;
    p.multipointSlot = 0xD2;
    p.firmware = "2.5.1";
    p.uniqueId = "AC:80:0A:12:34:56";
    p.protocolVersion = {0x03, 0x00, 0x20, 0x16};
    p.color = 0x03;
    p.eqPresets = capturedEqPresets();
    return p;
}

FakeHeadsetProfile FakeHeadsetProfile::wf1000xm5Capture() {
    FakeHeadsetProfile p;
    p.table1 = {0x10, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x23, 0x25, 0x11, 0x27, 0x29, 0x2A, 0x92, 0x50, 0x6B,
                0x70, 0x90, 0xA1, 0xC1, 0xC2, 0xE2, 0xF1, 0xFC, 0x93, 0xF6, 0xF9, 0xD1, 0x94, 0x40, 0x42, 0x44,
                0xFF, 0x19, 0xF3, 0xE7, 0xD2, 0xFD, 0xB6, 0xB0, 0xB2, 0xB8, 0xB7, 0x34, 0xF4, 0xF5};
    p.table2 = {0x42, 0x53, 0x54, 0xF1, 0x60, 0x61, 0xF2, 0x63, 0xF8, 0x32, 0x31};
    p.multipointSlot = 0xD2;
    p.ldacMultipointCaution = true;
    p.sidetoneSlot = 0xD1;
    p.firmware = "6.1.0";
    p.modelName = "WF-1000XM5";
    p.uniqueId = "AC:80:0A:65:43:21";
    p.protocolVersion = {0x03, 0x00, 0x30, 0x18};
    p.color = 0x03;
    p.eqPresets = capturedEqPresets();
    p.assignableCapability = {
        0xF1, 0x03, 0x02,
        0x00, 0x00, 0x35, 0x03,
        0x35, 0x04, 0x01, 0x01, 0x43, 0x02, 0x44, 0x22, 0x10, 0x03, 0x24, 0x00, 0x02, 0x04, 0x01, 0x02, 0x03, 0x04,
        0x20, 0x04, 0x00, 0x00, 0x20, 0x01, 0x21, 0x02, 0x22, 0x03, 0x24,
        0xFF, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x20, 0x03,
        0x35, 0x04, 0x01, 0x01, 0x43, 0x02, 0x44, 0x22, 0x10, 0x03, 0x23, 0x00, 0x02, 0x04, 0x01, 0x02, 0x03, 0x04,
        0x20, 0x04, 0x00, 0x00, 0x20, 0x01, 0x21, 0x02, 0x22, 0x03, 0x23,
        0xFF, 0x01, 0x00, 0x00, 0x00,
    };
    p.quickAccessCapability = {0xF1, 0x0D, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00};
    return p;
}

FakeHeadset::FakeHeadset(FakeHeadsetProfile p) : profile(std::move(p)) {}

bool FakeHeadset::advertises(uint8_t t1Id) const {
    return std::find(profile.table1.begin(), profile.table1.end(), t1Id) != profile.table1.end();
}

void FakeHeadset::useFixtureValues() {
    charging = 0x00;
    ncTotalEffect = 0x00;
    ncMode = 0x00;
    ncLevel = 20;
    eqPreset = 0x00;
    dsee = false;
    connectionQuality = 0x00;
    codec = 0x10;
    volume = 18;
    speakToChat = false;
    s2cSensitivity = 0x00;
    s2cTimeout = 0x01;
    voiceGuidance = true;
    voiceGuidanceVolume = 0;
    autoPowerOff = 0x10;
    earDetection = true;
    multipoint = true;
    quickAccess = {0x00, 0x00};
    playbackSlot = 1;
    if (profile.modelName.rfind("WF", 0) == 0) {
        batteryLeft = 70;
        batteryRight = 65;
        batteryCase = 53;
        leftCharging = rightCharging = caseCharging = 0x00;
        ncAmbientMode = 0x00;
        assignable = {0x20, 0x20};
        worn = 0x00;
        sidetone = true;
        devices = {{"F0:1D:BC:AA:00:01", "laptop", 1}, {"5C:E9:1E:BB:00:02", "phone", 2},
                   {"3C:22:FB:CC:00:03", "tablet", 0}, {"A4:83:E7:DD:00:04", "desktop", 0}};
    } else {
        battery = 53;
        ncAmbientMode = 0x01;
        ncAmbButton = 0x01;
        touchPanel = true;
        devices = {{"F0:1D:BC:AA:00:01", "laptop", 1}, {"5C:E9:1E:BB:00:02", "phone", 0},
                   {"3C:22:FB:CC:00:03", "tablet", 0}};
    }
}

void FakeHeadset::reconnect() {
    framer_.reset();
    deviceSeq_ = 0;
    pendingMultipoint_ = false;
}

void FakeHeadset::emit(Table table, const Bytes& payload, Bytes& out) {
    Bytes frame = packFrame(packetTypeFor(table), deviceSeq_, payload);
    deviceSeq_ ^= 1;
    out.insert(out.end(), frame.begin(), frame.end());
}

Bytes FakeHeadset::pushPayload(Table table, const Bytes& payload) {
    Bytes out;
    emit(table, payload, out);
    return out;
}

Bytes FakeHeadset::pushBattery(int level, bool isCharging) {
    const uint8_t status = isCharging ? 0x01 : 0x00;
    if (advertises(0x29)) {
        batteryLeft = batteryRight = std::clamp(level, 0, 100);
        leftCharging = rightCharging = status;
        return pushPayload(Table::T1, batteryReply(0x25, 0x09));
    }
    battery = std::clamp(level, 0, 100);
    charging = status;
    return pushPayload(Table::T1, batteryReply(0x25, 0x00));
}

Bytes FakeHeadset::batteryReply(uint8_t cmd, uint8_t type) const {
    auto level = [](int v) { return static_cast<uint8_t>(v); };
    switch (type) {
    case 0x01: return {cmd, type, level(batteryLeft), leftCharging, level(batteryRight), rightCharging};
    case 0x09: return {cmd, type, level(batteryLeft), leftCharging, level(batteryRight), rightCharging, 0x64, 0x64};
    case 0x02: return {cmd, type, level(batteryCase), caseCharging};
    case 0x0A: return {cmd, type, level(batteryCase), caseCharging, 0x1E};
    case 0x08: return {cmd, type, level(battery), charging, 0x14};
    default:   return {cmd, 0x00, level(battery), charging};
    }
}

Bytes FakeHeadset::receive(std::span<const uint8_t> hostBytes) {
    Bytes out;
    framer_.append(hostBytes);
    while (auto raw = framer_.nextFrame()) {
        auto frame = unpackFrame(*raw);
        if (!frame) continue;
        if (frame->type == PacketType::ACK) {
            hostAckSeqs.push_back(frame->seq);
            continue;
        }
        auto table = tableFor(frame->type);
        if (!table) continue;
        if (dropped_ < profile.dropFirstDataPackets) {
            ++dropped_;
            ++droppedPackets;
            continue;
        }
        received.emplace_back(*table, frame->payload);
        receivedSeqs.push_back(frame->seq);

        Bytes ack = packFrame(PacketType::ACK, static_cast<uint8_t>(1 - frame->seq), {});
        Bytes body;
        bool ackFirst = !profile.replyBeforeAck;
        handle(*table, frame->payload, body, ackFirst);
        if (ackFirst) {
            out.insert(out.end(), ack.begin(), ack.end());
            out.insert(out.end(), body.begin(), body.end());
        } else {
            out.insert(out.end(), body.begin(), body.end());
            out.insert(out.end(), ack.begin(), ack.end());
        }
    }
    return out;
}

Bytes FakeHeadset::eqValue() const {
    Bytes out{0x57, 0x00, eqPreset, 0x06};
    if (eqPreset == 0xA0) {
        out.insert(out.end(), eqCustom.begin(), eqCustom.end());
    } else {
        out.insert(out.end(), 6, 10);
    }
    return out;
}

Bytes FakeHeadset::deviceList(uint8_t type) const {
    Bytes out{0x37, type, static_cast<uint8_t>(devices.size())};
    for (const auto& d : devices) {
        out.insert(out.end(), d.address.begin(), d.address.end());
        out.push_back(d.connectionStatus);
        if (type == 0x02) {
            out.push_back(0x5A);
            out.push_back(0x01);
            out.push_back(0x0C);
        }
        appendString(out, d.name);
    }
    out.push_back(playbackSlot);
    return out;
}

Bytes FakeHeadset::ncAsm(uint8_t cmd, uint8_t type) const {
    Bytes out{cmd, type, 0x01, ncTotalEffect, ncMode, ncAmbientMode, ncLevel};
    if (type == 0x19) {
        out.push_back(0x00);
        out.push_back(0x00);
    }
    return out;
}

void FakeHeadset::handle(Table table, const Bytes& p, Bytes& out, bool& ackFirst) {
    if (p.size() < 2) return;
    const uint8_t cmd = p[0];
    const uint8_t type = p[1];
    auto reply = [&](const Bytes& payload) { emit(table, payload, out); };
    auto notify = [&](const Bytes& payload) {
        ackFirst = false;
        emit(table, payload, out);
    };
    auto at = [&](size_t i) -> uint8_t { return i < p.size() ? p[i] : 0; };

    if (table == Table::T2) {
        switch (cmd) {
        case 0x06: {
            if (!profile.answerSupportFunction) return;
            Bytes r{0x07, 0x00, static_cast<uint8_t>(profile.table2.size())};
            for (uint8_t id : profile.table2) { r.push_back(id); r.push_back(0x01); }
            reply(r);
            return;
        }
        case 0x32: reply({0x33, type, 0x00, 0x00}); return;
        case 0x36:
            if (type == 0x01) reply({0x37, 0x01, 0x01});
            else reply(deviceList(type));
            return;
        case 0x3C: {
            std::string address(p.begin() + 2, p.end());
            Bytes r{0x3D, 0x01, 0x01};
            for (const auto& d : devices) {
                if (d.address == address && d.connectionStatus != 0) {
                    playbackSlot = d.connectionStatus;
                    r[2] = 0x00;
                }
            }
            r.insert(r.end(), address.begin(), address.end());
            notify(r);
            return;
        }
        case 0xF2:
            if (profile.answerWornStatus) reply({0xF3, 0x00, worn});
            return;
        case 0x46:
            if (type == 0x20) reply({0x47, 0x20, static_cast<uint8_t>(voiceGuidanceVolume)});
            else reply({0x47, type, static_cast<uint8_t>(voiceGuidance ? 0x00 : 0x01), 0x01});
            return;
        case 0x48:
            if (type == 0x20) voiceGuidanceVolume = static_cast<int8_t>(at(2));
            else voiceGuidance = (at(2) == 0x00);
            return;
        default:
            return;
        }
    }

    switch (cmd) {
    case 0x00: {
        const auto& v = profile.protocolVersion;
        reply({0x01, 0x00, v[0], v[1], v[2], v[3], 0x00, static_cast<uint8_t>(profile.table2Supported ? 0x00 : 0x01)});
        return;
    }
    case 0x02: {
        Bytes r{0x03, 0x00, 0x01};
        appendString(r, profile.uniqueId);
        reply(r);
        return;
    }
    case 0x04: {
        Bytes r{0x05, type};
        if (type == 0x01) appendString(r, profile.modelName);
        else if (type == 0x02) {
            if (profile.firmware.empty()) return;
            appendString(r, profile.firmware);
        }
        else if (type == 0x03) { r.push_back(0x30); r.push_back(profile.color); }
        else return;
        reply(r);
        return;
    }
    case 0x06: {
        if (!profile.answerSupportFunction) return;
        Bytes r{0x07, 0x00, static_cast<uint8_t>(profile.table1.size())};
        for (uint8_t id : profile.table1) { r.push_back(id); r.push_back(0x01); }
        reply(r);
        return;
    }
    case 0x12: reply({0x13, 0x02, codec}); return;
    case 0x22: reply(batteryReply(0x23, type)); return;
    case 0x26:
        reply({0x27, type, autoPowerOff, static_cast<uint8_t>(autoPowerOff <= 0x04 ? autoPowerOff : 0x00)});
        return;
    case 0x28:
        autoPowerOff = at(2);
        notify({0x29, type, autoPowerOff, at(3)});
        return;
    case 0x50: {
        std::vector<FakeEqPreset> presets = profile.eqPresets;
        if (presets.empty()) {
            for (const auto& preset : kEqPresetNames) presets.push_back({preset.id, preset.name});
        }
        Bytes r{0x51, 0x00, 0x06, 0x15, static_cast<uint8_t>(presets.size())};
        for (const auto& preset : presets) {
            r.push_back(preset.id);
            appendString(r, preset.name);
        }
        reply(r);
        return;
    }
    case 0x52: reply({0x53, 0x00, 0x00}); return;
    case 0x56: reply(eqValue()); return;
    case 0x58: {
        eqPreset = at(2);
        if (at(3) == 0x06) {
            for (size_t i = 0; i < 6; ++i) eqCustom[i] = at(4 + i);
        }
        Bytes r = eqValue();
        r[0] = 0x59;
        notify(r);
        return;
    }
    case 0x66:
        if (type == 0x30) reply({0x67, 0x30, ncAmbButton});
        else reply(ncAsm(0x67, type));
        return;
    case 0x68:
        if (type == 0x30) {
            ncAmbButton = at(2);
            notify({0x69, 0x30, ncAmbButton});
            return;
        }
        ncTotalEffect = at(3);
        ncMode = at(4);
        ncAmbientMode = at(5);
        ncLevel = at(6);
        notify(ncAsm(0x69, type));
        return;
    case 0x98:
        alertsAnswered.push_back(p);
        if (pendingMultipoint_ && at(3) == 0x01 && at(2) == 0x07 && pendingMultipointValue_ &&
            profile.ldacMultipointCaution) {
            // The real WF-1000XM5 asks a second question before it turns
            // multipoint on, and keeps the old value unless it is answered.
            notify({0x99, 0x00, 0x70, 0x01});
            return;
        }
        if (pendingMultipoint_ && at(3) == 0x01 && (at(2) == 0x07 || at(2) == 0x70)) {
            multipoint = pendingMultipointValue_;
            notify({0xD9, profile.multipointSlot, 0x00, static_cast<uint8_t>(multipoint ? 0x00 : 0x01)});
        }
        pendingMultipoint_ = false;
        return;
    case 0xA6: reply({0xA7, 0x20, volume}); return;
    case 0xA8:
        volume = at(2);
        notify({0xA9, 0x20, volume});
        return;
    case 0xD0: {
        Bytes r{0xD1, type, 0x00, 0x01};
        if (type == profile.multipointSlot) {
            appendString(r, "MULTIPOINT_SETTING");
            appendString(r, "MULTIPOINT_SETTING_SUMMARY");
        } else if (type == profile.sidetoneSlot) {
            appendString(r, "SIDETONE_SETTING");
            appendString(r, "SIDETONE_SETTING_SUMMARY");
        } else {
            appendString(r, "TOUCH_PANEL_SETTING");
            appendString(r, "TOUCH_PANEL_SETTING_SUMMARY");
        }
        reply(r);
        return;
    }
    case 0xD6:
        if (type == profile.multipointSlot) reply({0xD7, type, 0x00, static_cast<uint8_t>(multipoint ? 0x00 : 0x01)});
        else if (type == profile.sidetoneSlot) reply({0xD7, type, 0x00, static_cast<uint8_t>(sidetone ? 0x00 : 0x01)});
        else reply({0xD7, type, 0x00, static_cast<uint8_t>(touchPanel ? 0x00 : 0x01)});
        return;
    case 0xD8:
        if (type == profile.multipointSlot) {
            pendingMultipoint_ = true;
            pendingMultipointValue_ = (at(3) == 0x00);
            notify({0x99, 0x00, 0x07, 0x01});
        } else if (type == profile.sidetoneSlot) {
            sidetone = (at(3) == 0x00);
            notify({0xD9, type, 0x00, at(3)});
        } else {
            touchPanel = (at(3) == 0x00);
            notify({0xD9, type, 0x00, at(3)});
        }
        return;
    case 0xE0:
        if (type == 0x05) reply({0xE1, 0x05, 0x02, 0x00, 0x01, 0x01, 0x00});
        else reply({0xE1, 0x01, 0x02});
        return;
    case 0xE2:
        if (type == 0x05) reply({0xE3, 0x05, 0x00, 0x01});
        else reply({0xE3, 0x01, 0x00});
        return;
    case 0xE6:
        if (type == 0x01) reply({0xE7, 0x01, static_cast<uint8_t>(dsee ? 0x01 : 0x00)});
        else reply({0xE7, type, connectionQuality});
        return;
    case 0xE8:
        if (type == 0x01) {
            dsee = (at(2) == 0x01);
            notify({0xE9, 0x01, static_cast<uint8_t>(dsee ? 0x01 : 0x00)});
        } else {
            connectionQuality = at(2);
            notify({0xE9, type, connectionQuality});
        }
        return;
    case 0xF0: {
        if (type == 0x0D) {
            if (!profile.quickAccessCapability.empty()) reply(profile.quickAccessCapability);
            else reply(Bytes(std::begin(kQuickAccessCapability), std::end(kQuickAccessCapability)));
            return;
        }
        if (!profile.assignableCapability.empty()) {
            reply(profile.assignableCapability);
            return;
        }
        Bytes r{0xF1, type};
        if (type == 0x0E) r.push_back(0x00);
        r.push_back(0x01);
        r.push_back(0x04);
        r.push_back(0x01);
        r.push_back(0x00);
        r.push_back(static_cast<uint8_t>(std::size(kAssignablePresets)));
        for (uint8_t preset : kAssignablePresets) {
            r.push_back(preset);
            r.push_back(0x00);
            r.push_back(0x00);
        }
        reply(r);
        return;
    }
    case 0xF6:
        if (type == 0x01) reply({0xF7, 0x01, static_cast<uint8_t>(earDetection ? 0x00 : 0x01)});
        else if (type == 0x0C) reply({0xF7, 0x0C, static_cast<uint8_t>(speakToChat ? 0x00 : 0x01), 0x01});
        else if (type == 0x03 || type == 0x0E) {
            Bytes r{0xF7, type, static_cast<uint8_t>(assignable.size())};
            r.insert(r.end(), assignable.begin(), assignable.end());
            reply(r);
        } else if (type == 0x0D) {
            Bytes r{0xF7, 0x0D, static_cast<uint8_t>(quickAccess.size())};
            r.insert(r.end(), quickAccess.begin(), quickAccess.end());
            reply(r);
        }
        return;
    case 0xF8:
        if (type == 0x01) {
            earDetection = (at(2) == 0x00);
            notify({0xF9, 0x01, at(2)});
        } else if (type == 0x0C) {
            speakToChat = (at(2) == 0x00);
            notify({0xF9, 0x0C, at(2), at(3)});
        } else if (type == 0x03 || type == 0x0E) {
            assignable.assign(p.begin() + 3, p.end());
            Bytes r{0xF9, type, static_cast<uint8_t>(assignable.size())};
            r.insert(r.end(), assignable.begin(), assignable.end());
            notify(r);
        } else if (type == 0x0D) {
            quickAccess.assign(p.begin() + 3, p.end());
            Bytes r{0xF9, 0x0D, static_cast<uint8_t>(quickAccess.size())};
            r.insert(r.end(), quickAccess.begin(), quickAccess.end());
            notify(r);
        }
        return;
    case 0xFA: reply({0xFB, 0x0C, s2cSensitivity, s2cTimeout}); return;
    case 0xFC:
        s2cSensitivity = at(2);
        s2cTimeout = at(3);
        notify({0xFD, 0x0C, s2cSensitivity, s2cTimeout});
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// FakeHeadsetTransport
// ---------------------------------------------------------------------------

FakeHeadsetTransport::FakeHeadsetTransport(std::shared_ptr<FakeHeadset> headset) : headset_(std::move(headset)) {}

FakeHeadsetTransport::~FakeHeadsetTransport() {
    disconnect();
}

int FakeHeadsetTransport::connect(const std::string&, uint8_t) {
    if (fd_ < 0) {
        int sv[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) != 0) {
            lastError_ = std::string("socketpair failed: ") + std::strerror(errno);
            return -1;
        }
        fd_ = sv[0];
        peerFd_ = sv[1];
    }
    headset_->reconnect();
    return 0;
}

void FakeHeadsetTransport::disconnect() {
    if (fd_ >= 0) ::close(fd_);
    if (peerFd_ >= 0) ::close(peerFd_);
    fd_ = -1;
    peerFd_ = -1;
}

void FakeHeadsetTransport::inject(const Bytes& bytes) {
    size_t off = 0;
    while (peerFd_ >= 0 && off < bytes.size()) {
        ssize_t n = ::send(peerFd_, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        off += static_cast<size_t>(n);
    }
}

ssize_t FakeHeadsetTransport::send(const uint8_t* data, size_t length) {
    if (fd_ < 0) return -1;
    inject(headset_->receive(std::span<const uint8_t>(data, length)));
    return static_cast<ssize_t>(length);
}

ssize_t FakeHeadsetTransport::recv(uint8_t* buffer, size_t maxLength) {
    if (fd_ < 0) return -1;
    ssize_t n = ::recv(fd_, buffer, maxLength, 0);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        lastError_ = std::string("recv failed: ") + std::strerror(errno);
    }
    return n;
}

} // namespace omarchy::sony::protocol
