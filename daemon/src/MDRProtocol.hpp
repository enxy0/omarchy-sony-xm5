#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omarchy::sony::protocol {

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// Wire Framing Constants
// ---------------------------------------------------------------------------
inline constexpr uint8_t kStartMarker     = 0x3E;
inline constexpr uint8_t kEndMarker       = 0x3C;
inline constexpr uint8_t kEscapeSentry    = 0x3D;
inline constexpr uint8_t kEscaped3C       = 0x2C;
inline constexpr uint8_t kEscaped3D       = 0x2D;
inline constexpr uint8_t kEscaped3E       = 0x2E;

enum class PacketType : uint8_t {
    ACK          = 0x01,
    DATA_MDR     = 0x0C,
    DATA_MDR_NO2 = 0x0E,
    UNKNOWN      = 0xFF
};

enum class Table : uint8_t { T1, T2 };

PacketType packetTypeFor(Table table) noexcept;
std::optional<Table> tableFor(PacketType type) noexcept;

struct UnpackedFrame {
    PacketType type{PacketType::UNKNOWN};
    uint8_t seq{0};
    Bytes payload;
};

// ---------------------------------------------------------------------------
// Low-Level Framing & Checksum
// ---------------------------------------------------------------------------
uint8_t calculateChecksum(std::span<const uint8_t> data) noexcept;
Bytes escapeBytes(std::span<const uint8_t> unescaped);
Bytes unescapeBytes(std::span<const uint8_t> escaped);
Bytes packFrame(PacketType type, uint8_t seq, std::span<const uint8_t> payload);
std::optional<UnpackedFrame> unpackFrame(std::span<const uint8_t> frameBytes);
Bytes serializeACK(uint8_t rxSeq);

class StreamFramer {
public:
    void append(std::span<const uint8_t> incoming);
    std::optional<Bytes> nextFrame();
    void reset();
    [[nodiscard]] size_t bufferedBytes() const noexcept { return buffer_.size(); }

private:
    Bytes buffer_;
};

// ---------------------------------------------------------------------------
// Function IDs
// ---------------------------------------------------------------------------
namespace fn1 {
inline constexpr uint8_t CODEC_INDICATOR = 0x12;
inline constexpr uint8_t BATTERY_LEVEL_INDICATOR = 0x20;
inline constexpr uint8_t LEFT_RIGHT_BATTERY_LEVEL_INDICATOR = 0x21;
inline constexpr uint8_t CRADLE_BATTERY_LEVEL_INDICATOR = 0x22;
inline constexpr uint8_t AUTO_POWER_OFF = 0x24;
inline constexpr uint8_t AUTO_POWER_OFF_WITH_WEARING_DETECTION = 0x25;
inline constexpr uint8_t BATTERY_LEVEL_WITH_THRESHOLD = 0x28;
inline constexpr uint8_t LR_BATTERY_LEVEL_WITH_THRESHOLD = 0x29;
inline constexpr uint8_t CRADLE_BATTERY_LEVEL_WITH_THRESHOLD = 0x2A;
inline constexpr uint8_t PRESET_EQ = 0x50;
inline constexpr uint8_t NC_ASM_AUTO = 0x68;
inline constexpr uint8_t AMBIENT_SOUND_CONTROL_MODE_SELECT = 0x69;
inline constexpr uint8_t NC_ASM_DUAL = 0x6B;
inline constexpr uint8_t NC_ASM_NOISE_ADAPTATION = 0x6D;
inline constexpr uint8_t FIXED_MESSAGE = 0x90;
inline constexpr uint8_t PLAYBACK_CONTROLLER_FIRST = 0xA1;
inline constexpr uint8_t PLAYBACK_CONTROLLER_LAST = 0xA4;
inline constexpr uint8_t GENERAL_SETTING_FIRST = 0xD1;
inline constexpr uint8_t GENERAL_SETTING_LAST = 0xD4;
inline constexpr uint8_t CONNECTION_MODE = 0xE1;
inline constexpr uint8_t UPSCALING_AUTO_OFF = 0xE2;
inline constexpr uint8_t CONNECTION_MODE_WITH_LDAC_STATUS = 0xE3;
inline constexpr uint8_t CONNECTION_MODE_CLASSIC_AUDIO_LE_AUDIO = 0xE7;
inline constexpr uint8_t PLAYBACK_CONTROL_BY_WEARING = 0xF1;
inline constexpr uint8_t ASSIGNABLE_SETTING = 0xF3;
inline constexpr uint8_t WEARING_STATUS_DETECTOR = 0xF6;
inline constexpr uint8_t SMART_TALKING_MODE_TYPE2 = 0xFC;
inline constexpr uint8_t QUICK_ACCESS = 0xFD;
inline constexpr uint8_t ASSIGNABLE_SETTING_WITH_LIMITATION = 0xFE;
} // namespace fn1

namespace fn2 {
inline constexpr uint8_t PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT = 0x30;
inline constexpr uint8_t SOURCE_SWITCH_CONTROL = 0x31;
inline constexpr uint8_t PAIRING_DEVICE_MANAGEMENT_WITH_COD_CLASSIC_BT = 0x32;
inline constexpr uint8_t PAIRING_DEVICE_MANAGEMENT_WITH_COD_CLASSIC_LE = 0x33;
inline constexpr uint8_t VOICE_GUIDANCE_NOT_SUPPORT_LANGUAGE_SWITCH = 0x40;
inline constexpr uint8_t VOICE_GUIDANCE_SUPPORT_LANGUAGE_SWITCH = 0x41;
inline constexpr uint8_t VOICE_GUIDANCE_WITH_VOLUME = 0x42;
inline constexpr uint8_t VOICE_GUIDANCE_VOLUME_FIXED_5_STEPS = 0x43;
inline constexpr uint8_t VOICE_GUIDANCE_SETTING_SUPPORT_LANGUAGE_SWITCH = 0x44;
inline constexpr uint8_t VOICE_GUIDANCE_ONLY_ON_OFF = 0x45;
inline constexpr uint8_t WEARING_STATUS_CHECKER = 0xF0;
} // namespace fn2

std::string_view functionName(Table table, uint8_t id) noexcept;

// ---------------------------------------------------------------------------
// Request Payload Builders
// ---------------------------------------------------------------------------
struct NcAsmParams {
    uint8_t type{0x17};
    uint8_t valueChange{0x01};
    uint8_t totalEffect{0x01};
    uint8_t mode{0x00};
    uint8_t ambientMode{0x00};
    uint8_t level{20};
    uint8_t autoAmbient{0x00};
    uint8_t noiseAdaptive{0x00};
};

namespace req {
Bytes getProtocolInfo();
Bytes getCapabilityInfo();
Bytes getDeviceInfo(uint8_t infoType);
Bytes getSupportFunction();
Bytes getBattery(uint8_t type);
Bytes getGeneralSettingCapability(uint8_t slot);
Bytes getGeneralSettingValue(uint8_t slot);
Bytes setGeneralSettingBool(uint8_t slot, bool on);
Bytes getUpscalingCapability();
Bytes getUpscalingStatus();
Bytes getUpscaling();
Bytes setUpscaling(bool on);
Bytes getAssignableCapability(uint8_t type);
Bytes getAssignableValue(uint8_t type);
Bytes setAssignableValue(uint8_t type, const std::vector<uint8_t>& presets);
Bytes enableFixedMessageAlerts();
Bytes answerAlert(uint8_t messageType, bool positive);
Bytes getCodec();
Bytes getVolume();
Bytes setVolume(int volume);
Bytes getNcAsm(uint8_t type);
Bytes setNcAsm(const NcAsmParams& params);
Bytes getNcAmbButton();
Bytes setNcAmbButton(uint8_t function);
Bytes getSpeakToChat();
Bytes setSpeakToChat(bool on);
Bytes getSpeakToChatConfig();
Bytes setSpeakToChatConfig(uint8_t sensitivity, uint8_t timeout);
Bytes getEqCapability();
Bytes getEqStatus();
Bytes getEqValue();
Bytes setEqPreset(uint8_t presetId);
Bytes setEqCustom(const std::array<int, 5>& bands, int clearBass);
Bytes getConnectionModeCapability();
Bytes getConnectionModeStatus();
Bytes getConnectionQuality(uint8_t type);
Bytes setConnectionQuality(uint8_t type, uint8_t priorMode);
Bytes getAutoPowerOff(uint8_t type);
Bytes setAutoPowerOff(uint8_t type, uint8_t current);
Bytes getEarDetection();
Bytes setEarDetection(bool on);
Bytes enableOperationLog();
Bytes getPairingMode(uint8_t listType);
Bytes getPairedDevices(uint8_t listType);
Bytes getSourceSwitchAuto();
Bytes setSourceSwitch(const std::string& address);
Bytes getWornStatus();
Bytes getVoiceGuidance(uint8_t type);
Bytes setVoiceGuidance(uint8_t type, bool on, uint8_t language);
Bytes getVoiceGuidanceVolume();
Bytes setVoiceGuidanceVolume(int volume);
Bytes getQuickAccessCapability();
Bytes getQuickAccess();
Bytes setQuickAccess(const std::vector<uint8_t>& functions);
} // namespace req

// ---------------------------------------------------------------------------
// Value <-> Contract String Maps
// ---------------------------------------------------------------------------
std::string eqPresetName(uint8_t presetId);
std::optional<uint8_t> eqPresetId(std::string_view name);
std::string codecName(uint8_t codec);
std::optional<std::string> autoPowerOffName(uint8_t value);
std::optional<uint8_t> autoPowerOffValue(std::string_view name);
std::optional<std::string> ncAmbButtonName(uint8_t value);
std::optional<uint8_t> ncAmbButtonValue(std::string_view name);
std::string buttonFunctionName(uint8_t preset);
std::optional<uint8_t> buttonFunctionValue(std::string_view name);
std::string touchFunctionName(uint8_t preset);
std::optional<uint8_t> touchFunctionValue(std::string_view name);
std::optional<std::string> speakToChatSensitivityName(uint8_t value);
std::optional<uint8_t> speakToChatSensitivityValue(std::string_view name);
std::optional<std::string> speakToChatTimeoutName(uint8_t value);
std::optional<uint8_t> speakToChatTimeoutValue(std::string_view name);
std::string connectionQualityName(uint8_t priorMode);
std::optional<uint8_t> connectionQualityValue(std::string_view name);
std::string quickAccessName(uint8_t function);
std::optional<uint8_t> quickAccessValue(std::string_view name);

std::string hexByte(uint8_t value);
std::string hexBytes(std::span<const uint8_t> data);

} // namespace omarchy::sony::protocol

namespace sony = omarchy::sony;
