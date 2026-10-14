#include "MDRProtocol.hpp"

#include <algorithm>
#include <cstdio>

namespace omarchy::sony::protocol {

PacketType packetTypeFor(Table table) noexcept {
    return table == Table::T1 ? PacketType::DATA_MDR : PacketType::DATA_MDR_NO2;
}

std::optional<Table> tableFor(PacketType type) noexcept {
    if (type == PacketType::DATA_MDR) return Table::T1;
    if (type == PacketType::DATA_MDR_NO2) return Table::T2;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

uint8_t calculateChecksum(std::span<const uint8_t> data) noexcept {
    uint8_t sum = 0;
    for (uint8_t b : data) {
        sum += b;
    }
    return sum;
}

Bytes escapeBytes(std::span<const uint8_t> unescaped) {
    Bytes out;
    out.reserve(unescaped.size() * 2);
    for (uint8_t b : unescaped) {
        switch (b) {
            case kEndMarker:    out.push_back(kEscapeSentry); out.push_back(kEscaped3C); break;
            case kEscapeSentry: out.push_back(kEscapeSentry); out.push_back(kEscaped3D); break;
            case kStartMarker:  out.push_back(kEscapeSentry); out.push_back(kEscaped3E); break;
            default:            out.push_back(b); break;
        }
    }
    return out;
}

Bytes unescapeBytes(std::span<const uint8_t> escaped) {
    Bytes out;
    out.reserve(escaped.size());
    for (size_t i = 0; i < escaped.size(); ++i) {
        uint8_t b = escaped[i];
        if (b == kEscapeSentry) {
            if (i + 1 >= escaped.size()) return {};
            uint8_t next = escaped[++i];
            switch (next) {
                case kEscaped3C: out.push_back(kEndMarker); break;
                case kEscaped3D: out.push_back(kEscapeSentry); break;
                case kEscaped3E: out.push_back(kStartMarker); break;
                default: return {};
            }
        } else {
            out.push_back(b);
        }
    }
    return out;
}

Bytes packFrame(PacketType type, uint8_t seq, std::span<const uint8_t> payload) {
    Bytes unescaped;
    unescaped.reserve(6 + payload.size() + 1);
    unescaped.push_back(static_cast<uint8_t>(type));
    unescaped.push_back(seq);

    auto len = static_cast<uint32_t>(payload.size());
    unescaped.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    unescaped.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    unescaped.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    unescaped.push_back(static_cast<uint8_t>(len & 0xFF));
    unescaped.insert(unescaped.end(), payload.begin(), payload.end());
    unescaped.push_back(calculateChecksum(unescaped));

    Bytes frame;
    frame.reserve(unescaped.size() * 2 + 2);
    frame.push_back(kStartMarker);
    Bytes escaped = escapeBytes(unescaped);
    frame.insert(frame.end(), escaped.begin(), escaped.end());
    frame.push_back(kEndMarker);
    return frame;
}

std::optional<UnpackedFrame> unpackFrame(std::span<const uint8_t> frameBytes) {
    if (frameBytes.size() < 9) return std::nullopt;
    if (frameBytes.front() != kStartMarker || frameBytes.back() != kEndMarker) return std::nullopt;

    std::span<const uint8_t> inner = frameBytes.subspan(1, frameBytes.size() - 2);
    Bytes unescaped = unescapeBytes(inner);
    if (unescaped.size() < 7) return std::nullopt;

    uint8_t receivedCsum = unescaped.back();
    std::span<const uint8_t> checkSpan(unescaped.data(), unescaped.size() - 1);
    if (calculateChecksum(checkSpan) != receivedCsum) return std::nullopt;

    auto type = static_cast<PacketType>(unescaped[0]);
    uint8_t seq = unescaped[1];
    uint32_t len = (static_cast<uint32_t>(unescaped[2]) << 24) |
                   (static_cast<uint32_t>(unescaped[3]) << 16) |
                   (static_cast<uint32_t>(unescaped[4]) << 8)  |
                   static_cast<uint32_t>(unescaped[5]);

    if (unescaped.size() - 7 != len) return std::nullopt;

    Bytes payload(unescaped.begin() + 6, unescaped.begin() + 6 + len);
    return UnpackedFrame{type, seq, std::move(payload)};
}

Bytes serializeACK(uint8_t rxSeq) {
    return packFrame(PacketType::ACK, static_cast<uint8_t>(1 - (rxSeq & 1)), {});
}

void StreamFramer::append(std::span<const uint8_t> incoming) {
    constexpr size_t kMaxBufferSize = 64 * 1024;
    if (buffer_.size() + incoming.size() > kMaxBufferSize) {
        buffer_.clear();
    }
    buffer_.insert(buffer_.end(), incoming.begin(), incoming.end());
}

std::optional<Bytes> StreamFramer::nextFrame() {
    auto startIt = std::find(buffer_.begin(), buffer_.end(), kStartMarker);
    if (startIt == buffer_.end()) {
        buffer_.clear();
        return std::nullopt;
    }
    if (startIt != buffer_.begin()) {
        buffer_.erase(buffer_.begin(), startIt);
        startIt = buffer_.begin();
    }

    auto endIt = std::find(startIt + 1, buffer_.end(), kEndMarker);
    if (endIt == buffer_.end()) {
        return std::nullopt;
    }

    Bytes frame(startIt, endIt + 1);
    buffer_.erase(buffer_.begin(), endIt + 1);
    return frame;
}

void StreamFramer::reset() {
    buffer_.clear();
}

// ---------------------------------------------------------------------------
// Function names (mos9527 libmdr ProtocolV2.hpp, enum FunctionType)
// ---------------------------------------------------------------------------

namespace {

struct NamedId {
    uint8_t id;
    std::string_view name;
};

constexpr NamedId kTable1Names[] = {
    {0x10, "CONCIERGE_DATA"}, {0x11, "CONNECTION_STATUS"}, {0x12, "CODEC_INDICATOR"},
    {0x13, "UPSCALING_INDICATOR"}, {0x14, "BLE_SETUP"}, {0x15, "TUTORIAL_CONTENTS_SELECT_ON_CONCIERGE"},
    {0x16, "CONNECTION_ESTABLISHED_TIME"}, {0x17, "UNNECESSARY_AUTO_RECONNECTION"},
    {0x18, "DEVICE_SPECIAL_MODE"}, {0x19, "PHONE_AND_CONNECTED_DEVICE_INFOMATION_FOR_CLASSIC"},
    {0x1A, "TANDEM_RECONNECTION_REQUEST"}, {0x1B, "DISPLAY_FW_VERSION"},
    {0x20, "BATTERY_LEVEL_INDICATOR"}, {0x21, "LEFT_RIGHT_BATTERY_LEVEL_INDICATOR"},
    {0x22, "CRADLE_BATTERY_LEVEL_INDICATOR"}, {0x23, "POWER_OFF"}, {0x24, "AUTO_POWER_OFF"},
    {0x25, "AUTO_POWER_OFF_WITH_WEARING_DETECTION"}, {0x26, "POWER_SAVING_MODE_ON_OFF"},
    {0x27, "TANDEM_KEEP_ALIVE"}, {0x28, "BATTERY_LEVEL_WITH_THRESHOLD"},
    {0x29, "LR_BATTERY_LEVEL_WITH_THRESHOLD"}, {0x2A, "CRADLE_BATTERY_LEVEL_WITH_THRESHOLD"},
    {0x2B, "BATTERY_SAFE_MODE"}, {0x2C, "CARING_CHARGE"}, {0x2D, "BT_STANDBY"}, {0x2E, "STAMINA"},
    {0x2F, "AUTOMATIC_TOUCH_PANEL_BACKLIGHT_TURN_OFF"}, {0x30, "FW_UPDATE_TANDEM"},
    {0x32, "FW_UPDATE_MTK_TRANSFER_WITHOUT_DISCONNECTION"},
    {0x34, "FW_UPDATE_MTK_TRANSFER_WITHOUT_DISCONNECTION_AUTO_UPDATE"},
    {0x35, "FW_UPDATE_MTK_TRANSFER_WITH_REPAIR_MODE"},
    {0x36, "FW_UPDATE_MTK_TRANSFER_WITH_AC_CONNECTION_CHECK"},
    {0x37, "FW_UPDATE_TANDEM_TRANSFER_USING_COMMON_TABLE"}, {0x38, "FW_UPDATE_USING_MC_APP"},
    {0x40, "TWS_SUPPORTS_A2DP_LEA_UNI_LEA_BROAD_WITH_CTKD"},
    {0x41, "HBS_SUPPORTS_A2DP_LEA_UNI_LEA_BROAD_WITH_CTKD"}, {0x42, "CLASSIC_ONLY_LE_CLASSIC_SETTING"},
    {0x43, "TWS_SUPPORTS_LEA_UNI_LEA_BROAD"}, {0x44, "CHANGE_TANDEM_CONNECTION_PROFILE_FOR_ANDROID"},
    {0x45, "BGM_MODE_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x46, "HEAD_TRACKER_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x47, "PAIRING_DEVICE_MANAGEMENT_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x48, "SOUND_AR_CANT_BE_USED_WITH_LEA_CONNECTION"}, {0x49, "AUTO_PLAY_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x4A, "GATT_CONNECTABLE_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x4B, "SOUND_AR_OPTIMIZATION_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x4C, "QUICK_ACCESS_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x4D, "CONNECTION_MODE_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x4E, "VOICE_ASSISTANT_SETTINGS_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x4F, "VOICE_ASSISTANT_WAKE_WORD_CANT_BE_USED_WITH_LEA_CONNECTION"},
    {0x50, "PRESET_EQ"}, {0x51, "EBB"}, {0x52, "PRESET_EQ_NON_CUSTOMIZABLE"},
    {0x53, "PRESET_EQ_AND_ULT_MODE"}, {0x54, "SOUND_EFFECT"}, {0x55, "CUSTOM_EQ"},
    {0x56, "TURN_KEY_EQ"}, {0x57, "PRESET_EQ_AND_ERRORCODE"}, {0x58, "ULT_SOUND_EFFECT_ASSIGN"},
    {0x59, "CUSTOMIZABLE_SOUND_EFFECT"}, {0x61, "NOISE_CANCELLING_ONOFF"},
    {0x62, "NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_ONOFF"},
    {0x63, "NOISE_CANCELLING_DUAL_SINGLE_OFF_AND_AMBIENT_SOUND_MODE_ONOFF"},
    {0x64, "NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT"},
    {0x65, "NOISE_CANCELLING_DUAL_SINGLE_OFF_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT"},
    {0x66, "AMBIENT_SOUND_MODE_ONOFF"}, {0x67, "AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT"},
    {0x68, "MODE_NC_ASM_NOISE_CANCELLING_DUAL_AUTO_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT"},
    {0x69, "AMBIENT_SOUND_CONTROL_MODE_SELECT"},
    {0x6A, "MODE_NC_ASM_NOISE_CANCELLING_DUAL_SINGLE_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT"},
    {0x6B, "MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT"},
    {0x6C, "MODE_NC_NCSS_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT_WITH_TEST_MODE"},
    {0x6D, "MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT_NOISE_ADAPTATION"},
    {0x70, "AUTO_NCASM"}, {0x71, "ADAPTIVE_CONTROL_WITH_PARAMETER_NOTIFICATION"},
    {0x72, "HEART_RATE_SENSOR_SETTING"}, {0x73, "HEART_RATE_PROFILE_SETTING"},
    {0x74, "HEART_RATE_SENSOR_TEST"}, {0x75, "HEART_RATE_SENSOR_GREEN_LIGHT"},
    {0x80, "NC_OPTIMIZER_PERSONAL_BAROMETRIC"}, {0x81, "NC_OPTIMIZER_PERSONAL"},
    {0x82, "NC_OPTIMIZER_BAROMETRIC"}, {0x83, "SOUND_FIELD_OPTIMIZATION"}, {0x84, "TV_SOUND_BOOSTER"},
    {0x90, "FIXED_MESSAGE"}, {0x91, "VIBRATOR_ALERT_NOTIFICATION"}, {0x92, "FIXED_MESSAGE_WITH_LR_SELECTION"},
    {0x93, "VOICE_ASSISTANT_ALERT_NOTIFICATION"}, {0x94, "LE_AUDIO_ALERT_NOTIFICATION"},
    {0xA1, "PLAYBACK_CONTROLLER_WITH_CALL_VOLUME_ADJUSTMENT"},
    {0xA2, "PLAYBACK_CONTROLLER_WITH_CALL_VOLUME_ADJUSTMENT_AND_MUTE"},
    {0xA3, "PLAYBACK_CONTROLLER_WITH_CALL_VOLUME_ADJUSTMENT_AND_FUNCTION_CHANGE"},
    {0xA4, "PLAYBACK_CONTROLLER_WITH_FUNCTION_CHANGE"}, {0xB0, "SAR"}, {0xB1, "AUTO_PLAY"},
    {0xB2, "GATT_CONNECTABLE"}, {0xB3, "SAR_OPTIMIZATION_COMPASS_ACCEL_TYPE"},
    {0xB5, "HEAD_TRACKER_COMPASS_ACCEL_TYPE"}, {0xB6, "SAR_OPTIMIZATION_ACCEL_TYPE"},
    {0xB7, "HEAD_TRACKER_ACCEL_TYPE"}, {0xB8, "INTEGRATED_AUTO_PLAY"}, {0xC1, "ACTION_LOG_NOTIFIER"},
    {0xC2, "TIME_SERIES_OPERATIONLOG_NOTIFIER"}, {0xC3, "SOUND_DROPOUT_NOTIFIER"},
    {0xD1, "GENERAL_SETTING_1"}, {0xD2, "GENERAL_SETTING_2"}, {0xD3, "GENERAL_SETTING_3"},
    {0xD4, "GENERAL_SETTING_4"}, {0xE1, "CONNECTION_MODE_SOUND_QUALITY_CONNECTION_QUALITY"},
    {0xE2, "UPSCALING_AUTO_OFF"},
    {0xE3, "CONNECTION_MODE_SOUND_QUALITY_SOUND_WITH_LDAC_STATUS_QUALITY_CONNECTION_QUALITY"},
    {0xE4, "BGM_MODE_SMALL_MIDDLE_LARGE"}, {0xE5, "UPMIX_CINEMA"}, {0xE6, "LISTENING_OPTION"},
    {0xE7, "CONNECTION_MODE_CLASSIC_AUDIO_LE_AUDIO"}, {0xE8, "VOICE_CONTENTS"},
    {0xE9, "SOUND_LEAKAGE_REDUCTION"}, {0xEA, "LISTENING_OPTION_ASSIGN_CUSTOMIZABLE"},
    {0xEB, "BGM_MODE_SMALL_MIDDLE_LARGE_AND_ERRORCODE"}, {0xEC, "UPMIX_SERIES"},
    {0xED, "UPSCALING_AUTO_OFF_WITH_STATUS_DISABLE_REASON"}, {0xF0, "VIBRATOR_ON_OFF"},
    {0xF1, "PLAYBACK_CONTROL_BY_WEARING_REMOVING_HEADPHONE_ON_OFF"}, {0xF2, "SMART_TALKING_MODE_TYPE1"},
    {0xF3, "ASSIGNABLE_SETTING"}, {0xF4, "VOICE_ASSISTANT_SETTINGS"},
    {0xF5, "VOICE_ASSISTANT_WAKE_WORD_ON_OFF"}, {0xF6, "WEARING_STATUS_DETECTOR"},
    {0xF7, "EARPIECE_SELECTION"}, {0xF8, "CALL_SETTINGS"}, {0xF9, "RESET_SETTINGS"},
    {0xFA, "AUTO_VOLUME"}, {0xFB, "FACE_TAP_TEST_MODE"}, {0xFC, "SMART_TALKING_MODE_TYPE2"},
    {0xFD, "QUICK_ACCESS"}, {0xFE, "ASSIGNABLE_SETTING_WITH_LIMITATION"},
    {0xFF, "HEAD_GESTURE_ON_OFF_TRAINING"},
};

constexpr NamedId kTable2Names[] = {
    {0x20, "AUTO_STANDBY"}, {0x21, "CHARGE_IN_USE"}, {0x22, "CARING_CHARGE_WITH_THRESHOLD"},
    {0x23, "USB_SUBMERSION"}, {0x24, "USB_OVERHEAT_DETECTION"},
    {0x30, "PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT"}, {0x31, "SOURCE_SWITCH_CONTROL"},
    {0x32, "PAIRING_DEVICE_MANAGEMENT_WITH_BLUETOOTH_CLASS_OF_DEVICE_CLASSIC_BT"},
    {0x33, "PAIRING_DEVICE_MANAGEMENT_WITH_BLUETOOTH_CLASS_OF_DEVICE_CLASSIC_LE"},
    {0x34, "MUSIC_HAND_OVER_SETTING"},
    {0x40, "VOICE_GUIDANCE_SETTING_MTK_TRANSFER_WITHOUT_DISCONNECTION_NOT_SUPPORT_LANGUAGE_SWITCH"},
    {0x41, "VOICE_GUIDANCE_SETTING_MTK_TRANSFER_WITHOUT_DISCONNECTION_SUPPORT_LANGUAGE_SWITCH"},
    {0x42, "VOICE_GUIDANCE_SETTING_MTK_TRANSFER_WITHOUT_DISCONNECTION_SUPPORT_LANGUAGE_SWITCH_AND_VOLUME_ADJUSTMENT"},
    {0x43, "VOICE_GUIDANCE_VOLUME_SETTING_MTK_FIXED_TO_5_STEPS"},
    {0x44, "VOICE_GUIDANCE_SETTING_SUPPORT_LANGUAGE_SWITCH"}, {0x45, "VOICE_GUIDANCE_SETTING_ONLY_ON_OFF_SWITCH"},
    {0x46, "VOICE_GUIDANCE_BATTERY_LEVEL_VOICE"}, {0x47, "VOICE_GUIDANCE_POWER_ON_OFF_SOUND"},
    {0x48, "VOICE_GUIDANCE_SOUND_EFFECT_ULT_BEEP_ON_OFF"}, {0x50, "SAFE_LISTENING_HBS_1"},
    {0x51, "SAFE_LISTENING_TWS_1"}, {0x52, "SAFE_LISTENING_HBS_2"}, {0x53, "SAFE_LISTENING_TWS_2"},
    {0x54, "SAFE_VOLUME_CONTROL"}, {0x55, "MAX_VOLUME_LEVEL_LIMIT"},
    {0x60, "LE_AUDIO_CONNECTION_STATE_NOTIFICATION"}, {0x61, "LE_AUDIO_SWITCH_SUPPORTED_COMPATIBILITY"},
    {0x62, "LE_AUDIO_CONNECTION_MODE"}, {0x63, "GET_IDENTITY_RESOLVING_KEY"},
    {0x64, "PAS_SUPPORTS_A2DP_LEA_UNI_LEA_BROAD_WITH_CTKD"},
    {0x6F, "LINK_AUTO_SWITCH_CANT_BE_USED_WITH_LEA_CONNECTION"}, {0x70, "DJ_CONTROL"},
    {0x71, "ILLUMINATION"}, {0x72, "KARAOKE"}, {0x73, "DJ_CONTROL_WITH_STATUS_DISABLE_REASON"},
    {0x74, "KARAOKE_WITH_STATUS_DISABLE_REASON"}, {0x75, "LIVE_KARAOKE"},
    {0xF0, "WEARING_STATUS_CHECKER"}, {0xF1, "REPEAT_TAP_TRAINING_MODE"},
    {0xF2, "QUICK_ACCESS_EASY_SETTING"}, {0xF3, "AUTO_VOLUME_OPTIMIZER"},
    {0xF4, "AUTO_VOLUME_WITH_LIMITATION"}, {0xF5, "SONY_VOICE_ASSISTANT"}, {0xF6, "WEARING_POSITION"},
    {0xF7, "LINK_AUTO_SWITCH_FOR_SPEAKER"}, {0xF8, "LINK_AUTO_SWITCH_FOR_HEADSETS"},
    {0xF9, "MIC_ON_OFF_BY_HEADPHONE_OPERATION"}, {0xFA, "FUNCTION_CHANGE"}, {0xFB, "USB_BROWSER"},
    {0xFC, "LIGHTING_MODE"}, {0xFD, "VOICE_ASSISTANT_WITH_SPECIFIC_SETUP_LINK_SUPPORT"},
    {0xFE, "LIGHTING_DEFAULT_COLOR"}, {0xFF, "WEARING_POSITION_WITHOUT_FITTING_SUPPORTER"},
};

template <size_t N>
std::string_view lookupName(const NamedId (&table)[N], uint8_t id) {
    for (const auto& entry : table) {
        if (entry.id == id) return entry.name;
    }
    return "UNKNOWN";
}

uint8_t clampToByte(int value, int lo, int hi, int offset) {
    return static_cast<uint8_t>(std::clamp(value, lo, hi) + offset);
}

} // namespace

std::string_view functionName(Table table, uint8_t id) noexcept {
    return table == Table::T1 ? lookupName(kTable1Names, id) : lookupName(kTable2Names, id);
}

// ---------------------------------------------------------------------------
// Request builders
// ---------------------------------------------------------------------------

namespace req {

Bytes getProtocolInfo() { return {0x00, 0x00}; }
Bytes getCapabilityInfo() { return {0x02, 0x00}; }
Bytes getDeviceInfo(uint8_t infoType) { return {0x04, infoType}; }
Bytes getSupportFunction() { return {0x06, 0x00}; }
Bytes getBattery(uint8_t type) { return {0x22, type}; }

Bytes getGeneralSettingCapability(uint8_t slot) { return {0xD0, slot, 0x01}; }
Bytes getGeneralSettingValue(uint8_t slot) { return {0xD6, slot}; }
Bytes setGeneralSettingBool(uint8_t slot, bool on) {
    return {0xD8, slot, 0x00, static_cast<uint8_t>(on ? 0x00 : 0x01)};
}

Bytes getUpscalingCapability() { return {0xE0, 0x01}; }
Bytes getUpscalingStatus() { return {0xE2, 0x01}; }
Bytes getUpscaling() { return {0xE6, 0x01}; }
Bytes setUpscaling(bool on) { return {0xE8, 0x01, static_cast<uint8_t>(on ? 0x01 : 0x00)}; }

Bytes getAssignableCapability(uint8_t type) { return {0xF0, type}; }
Bytes getAssignableValue(uint8_t type) { return {0xF6, type}; }
Bytes setAssignableValue(uint8_t type, const std::vector<uint8_t>& presets) {
    Bytes out{0xF8, type, static_cast<uint8_t>(presets.size())};
    out.insert(out.end(), presets.begin(), presets.end());
    return out;
}

Bytes enableFixedMessageAlerts() { return {0x94, 0x00, 0x00}; }
Bytes answerAlert(uint8_t messageType, bool positive) {
    return {0x98, 0x00, messageType, static_cast<uint8_t>(positive ? 0x01 : 0x00)};
}

Bytes getCodec() { return {0x12, 0x02}; }
Bytes getVolume() { return {0xA6, 0x20}; }
Bytes setVolume(int volume) { return {0xA8, 0x20, clampToByte(volume, 0, 30, 0)}; }

Bytes getNcAsm(uint8_t type) { return {0x66, type}; }
Bytes setNcAsm(const NcAsmParams& p) {
    Bytes out{0x68, p.type, p.valueChange, p.totalEffect, p.mode, p.ambientMode, p.level};
    if (p.type == 0x19) {
        out.push_back(p.autoAmbient);
        out.push_back(p.noiseAdaptive);
    }
    return out;
}

Bytes getNcAmbButton() { return {0x66, 0x30}; }
Bytes setNcAmbButton(uint8_t function) { return {0x68, 0x30, function}; }

Bytes getSpeakToChat() { return {0xF6, 0x0C}; }
Bytes setSpeakToChat(bool on) { return {0xF8, 0x0C, static_cast<uint8_t>(on ? 0x00 : 0x01), 0x01}; }
Bytes getSpeakToChatConfig() { return {0xFA, 0x0C}; }
Bytes setSpeakToChatConfig(uint8_t sensitivity, uint8_t timeout) { return {0xFC, 0x0C, sensitivity, timeout}; }

Bytes getEqCapability() { return {0x50, 0x00, 0x01}; }
Bytes getEqStatus() { return {0x52, 0x00}; }
Bytes getEqValue() { return {0x56, 0x00}; }
Bytes setEqPreset(uint8_t presetId) { return {0x58, 0x00, presetId, 0x00}; }
Bytes setEqCustom(const std::array<int, 5>& bands, int clearBass) {
    return {0x58, 0x00, 0xA0, 0x06,
            clampToByte(clearBass, -10, 10, 10),
            clampToByte(bands[0], -10, 10, 10), clampToByte(bands[1], -10, 10, 10),
            clampToByte(bands[2], -10, 10, 10), clampToByte(bands[3], -10, 10, 10),
            clampToByte(bands[4], -10, 10, 10)};
}

Bytes getConnectionQuality(uint8_t type) { return {0xE6, type}; }
Bytes setConnectionQuality(uint8_t type, uint8_t priorMode) { return {0xE8, type, priorMode}; }

Bytes getAutoPowerOff(uint8_t type) { return {0x26, type}; }
Bytes setAutoPowerOff(uint8_t type, uint8_t current) {
    uint8_t last = current <= 0x04 ? current : 0x00;
    return {0x28, type, current, last};
}

Bytes getEarDetection() { return {0xF6, 0x01}; }
Bytes setEarDetection(bool on) { return {0xF8, 0x01, static_cast<uint8_t>(on ? 0x00 : 0x01)}; }

Bytes enableOperationLog() { return {0xC4, 0x01, 0x00}; }

Bytes getPairingMode(uint8_t listType) { return {0x32, listType}; }
Bytes getPairedDevices(uint8_t listType) { return {0x36, listType}; }
Bytes getSourceSwitchAuto() { return {0x36, 0x01}; }
Bytes setSourceSwitch(const std::string& address) {
    Bytes out{0x3C, 0x01};
    out.insert(out.end(), address.begin(), address.end());
    return out;
}

Bytes getWornStatus() { return {0xF2, 0x00}; }
Bytes getVoiceGuidance(uint8_t type) { return {0x46, type}; }
Bytes setVoiceGuidance(uint8_t type, bool on, uint8_t language) {
    Bytes out{0x48, type, static_cast<uint8_t>(on ? 0x00 : 0x01)};
    if (type == 0x02) out.push_back(language);
    return out;
}
Bytes getVoiceGuidanceVolume() { return {0x46, 0x20}; }
Bytes setVoiceGuidanceVolume(int volume) {
    return {0x48, 0x20, static_cast<uint8_t>(static_cast<int8_t>(std::clamp(volume, -2, 2))), 0x00};
}

} // namespace req

// ---------------------------------------------------------------------------
// Value maps
// ---------------------------------------------------------------------------

namespace {

struct NamedValue {
    uint8_t value;
    std::string_view name;
};

constexpr NamedValue kEqPresets[] = {
    {0x00, "off"}, {0x10, "bright"}, {0x11, "excited"}, {0x12, "mellow"}, {0x13, "relaxed"},
    {0x14, "vocal"}, {0x15, "treble"}, {0x16, "bass"}, {0x17, "speech"}, {0xA0, "custom"},
    {0xA1, "user1"}, {0xA2, "user2"}, {0xA3, "user3"}, {0xA4, "user4"}, {0xA5, "user5"},
};

constexpr NamedValue kCodecs[] = {
    {0x00, "NONE"}, {0x01, "SBC"}, {0x02, "AAC"}, {0x10, "LDAC"}, {0x20, "APTX"}, {0x21, "APTX_HD"},
    {0x30, "LC3"},
};

constexpr NamedValue kAutoPowerOff[] = {
    {0x11, "off"}, {0x00, "5min"}, {0x04, "15min"}, {0x01, "30min"}, {0x02, "60min"}, {0x03, "180min"},
    {0x10, "when_taken_off"},
};

constexpr NamedValue kNcAmbButton[] = {
    {0x01, "nc_ambient_off"}, {0x02, "nc_ambient"}, {0x03, "nc_off"}, {0x04, "ambient_off"},
};

constexpr NamedValue kButtonFunctions[] = {
    {0x00, "ambient_sound_control"}, {0x30, "voice_recognition"}, {0x31, "google_assistant"},
    {0x32, "amazon_alexa"}, {0x36, "quick_access"}, {0xFF, "none"},
};

constexpr NamedValue kSensitivity[] = {{0x00, "auto"}, {0x01, "high"}, {0x02, "low"}};
constexpr NamedValue kTimeout[] = {{0x00, "short"}, {0x01, "standard"}, {0x02, "long"}, {0x03, "off"}};
constexpr NamedValue kConnectionQuality[] = {{0x00, "sound"}, {0x01, "connection"}};

template <size_t N>
std::optional<std::string> nameOf(const NamedValue (&table)[N], uint8_t value) {
    for (const auto& entry : table) {
        if (entry.value == value) return std::string(entry.name);
    }
    return std::nullopt;
}

template <size_t N>
std::optional<uint8_t> valueOf(const NamedValue (&table)[N], std::string_view name) {
    for (const auto& entry : table) {
        if (entry.name == name) return entry.value;
    }
    return std::nullopt;
}

} // namespace

std::string eqPresetName(uint8_t presetId) { return nameOf(kEqPresets, presetId).value_or("unknown"); }
std::optional<uint8_t> eqPresetId(std::string_view name) { return valueOf(kEqPresets, name); }
std::string codecName(uint8_t codec) { return nameOf(kCodecs, codec).value_or("OTHER"); }
std::optional<std::string> autoPowerOffName(uint8_t value) { return nameOf(kAutoPowerOff, value); }
std::optional<uint8_t> autoPowerOffValue(std::string_view name) { return valueOf(kAutoPowerOff, name); }
std::optional<std::string> ncAmbButtonName(uint8_t value) { return nameOf(kNcAmbButton, value); }
std::optional<uint8_t> ncAmbButtonValue(std::string_view name) { return valueOf(kNcAmbButton, name); }
std::string buttonFunctionName(uint8_t preset) { return nameOf(kButtonFunctions, preset).value_or("other"); }
std::optional<uint8_t> buttonFunctionValue(std::string_view name) { return valueOf(kButtonFunctions, name); }
std::optional<std::string> speakToChatSensitivityName(uint8_t value) { return nameOf(kSensitivity, value); }
std::optional<uint8_t> speakToChatSensitivityValue(std::string_view name) { return valueOf(kSensitivity, name); }
std::optional<std::string> speakToChatTimeoutName(uint8_t value) { return nameOf(kTimeout, value); }
std::optional<uint8_t> speakToChatTimeoutValue(std::string_view name) { return valueOf(kTimeout, name); }
std::string connectionQualityName(uint8_t priorMode) {
    return nameOf(kConnectionQuality, priorMode).value_or("other");
}
std::optional<uint8_t> connectionQualityValue(std::string_view name) { return valueOf(kConnectionQuality, name); }

std::string hexByte(uint8_t value) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", value);
    return buf;
}

std::string hexBytes(std::span<const uint8_t> data) {
    std::string out;
    out.reserve(data.size() * 3);
    char buf[4];
    for (size_t i = 0; i < data.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%02X", data[i]);
        if (i > 0) out.push_back(' ');
        out += buf;
    }
    return out;
}

} // namespace omarchy::sony::protocol
