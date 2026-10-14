#pragma once

#include "MDRProtocol.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omarchy::sony::protocol {

struct SupportEntry {
    uint8_t id{0};
    uint8_t priority{0};
};

struct GeneralSettingSlot {
    uint8_t slot{0};
    bool advertised{false};
    bool capabilityRead{false};
    uint8_t type{0};
    uint8_t format{0};
    std::string subject;
    std::string summary;
    std::optional<bool> value;
};

struct EqPresetCapability {
    uint8_t id{0};
    std::string name;
};

struct EqCapability {
    uint8_t bands{0};
    uint8_t steps{0};
    std::vector<EqPresetCapability> presets;
};

struct AssignableKey {
    uint8_t key{0};
    uint8_t type{0};
    uint8_t defaultPreset{0};
    std::vector<uint8_t> presets;
};

struct AssignableCapability {
    uint8_t type{0};
    std::vector<AssignableKey> keys;
};

inline constexpr uint8_t kActionDoubleTap = 0x01;
inline constexpr uint8_t kActionTripleTap = 0x02;

inline constexpr uint8_t kKeyLeftSide = 0x00;
inline constexpr uint8_t kKeyRightSide = 0x01;

struct QuickAccessAction {
    uint8_t action{0};
    uint8_t defaultFunction{0};
    std::vector<uint8_t> functions;
};

struct QuickAccessCapability {
    uint8_t key{0};
    uint8_t keyType{0};
    std::vector<QuickAccessAction> actions;
};

struct PairedDevice {
    std::string address;
    std::string name;
    uint8_t connectionStatus{0};
};

struct Supported {
    bool battery{false};
    bool battery_lr{false};
    bool battery_case{false};
    bool noise_control{false};
    bool eq_preset{false};
    bool eq_custom{false};
    bool dsee{false};
    bool speak_to_chat{false};
    bool voice_guidance{false};
    bool voice_guidance_volume{false};
    bool auto_power_off{false};
    bool nc_amb_button{false};
    bool button_function{false};
    bool touch_function{false};
    bool ear_detection{false};
    bool worn{false};
    bool volume{false};
    bool connection_quality{false};
    bool codec{false};
    bool multipoint{false};
    bool devices{false};
    bool source_switch{false};
    bool touch_panel{false};
    bool sidetone{false};
    bool quick_access{false};

    static Supported fallback(std::string_view model);
    bool operator==(const Supported&) const = default;
};

struct Variants {
    uint8_t batteryType{0x00};
    uint8_t lrBatteryType{0x09};
    uint8_t caseBatteryType{0x0A};
    std::optional<uint8_t> ncAsmType;
    uint8_t autoPowerOffType{0x05};
    uint8_t voiceGuidanceType{0x01};
    uint8_t connectionQualityType{0x00};
    uint8_t assignableType{0x03};
    uint8_t deviceListType{0x02};
    std::optional<uint8_t> multipointSlot;
    std::optional<uint8_t> touchPanelSlot;
    std::optional<uint8_t> sidetoneSlot;
};

inline constexpr std::string_view kModelWh = "wh-1000xm5";
inline constexpr std::string_view kModelWf = "wf-1000xm5";
inline constexpr std::string_view kModelOther = "other";

struct HeadphoneState {
    bool connected{false};
    std::string alias;
    std::string bluezName;
    std::optional<std::string> address;
    int64_t connectedSince{0};

    bool protocolKnown{false};
    uint32_t protocolVersion{0};
    bool table1Supported{false};
    bool table2Supported{false};
    std::optional<uint8_t> capabilityCounter;
    std::optional<std::string> uniqueId;
    std::optional<std::string> modelName;
    std::optional<std::string> firmwareVersion;
    std::optional<uint8_t> series;
    std::optional<uint8_t> color;
    std::optional<std::vector<SupportEntry>> support1;
    std::optional<std::vector<SupportEntry>> support2;

    std::array<GeneralSettingSlot, 4> generalSettings{};
    std::optional<EqCapability> eqCapability;
    std::optional<AssignableCapability> assignableCapability;
    std::optional<QuickAccessCapability> quickAccessCapability;
    std::optional<uint8_t> upscalingType;
    std::optional<Bytes> connectionModeCapability;
    std::optional<Bytes> connectionModeStatus;

    std::optional<Supported> supported;
    bool fallback{false};
    Variants variants;
    bool eqCustomSeen{false};

    std::optional<int> batteryLevel;
    std::optional<uint8_t> batteryChargingRaw;
    std::optional<int> batteryLeft;
    std::optional<uint8_t> batteryLeftChargingRaw;
    std::optional<int> batteryRight;
    std::optional<uint8_t> batteryRightChargingRaw;
    std::optional<int> batteryCase;
    std::optional<uint8_t> batteryCaseChargingRaw;
    std::optional<uint8_t> ncTotalEffect;
    std::optional<uint8_t> ncMode;
    std::optional<uint8_t> ncAmbientMode;
    std::optional<uint8_t> ncLevel;
    std::optional<uint8_t> ncAutoAmbient;
    std::optional<uint8_t> ncNoiseAdaptive;
    std::optional<uint8_t> eqPresetIdValue;
    std::optional<uint8_t> eqAvailable;
    std::optional<std::array<int, 5>> eqCustomBands;
    std::optional<int> clearBass;
    std::optional<bool> dsee;
    std::optional<uint8_t> dseeAvailable;
    std::optional<uint8_t> connectionQuality;
    std::optional<uint8_t> codec;
    std::optional<int> volume;
    std::optional<bool> speakToChat;
    std::optional<uint8_t> speakToChatSensitivity;
    std::optional<uint8_t> speakToChatTimeout;
    std::optional<bool> voiceGuidance;
    std::optional<uint8_t> voiceGuidanceLanguage;
    std::optional<int> voiceGuidanceVolume;
    std::optional<uint8_t> autoPowerOff;
    std::optional<uint8_t> ncAmbButton;
    std::optional<std::vector<uint8_t>> assignableValue;
    std::optional<bool> earDetection;
    std::optional<uint8_t> wornStatus;
    std::optional<std::vector<PairedDevice>> devices;
    uint8_t playbackSlot{0};
    std::optional<bool> sourceSwitchAuto;
    std::optional<uint8_t> pairingMode;
    std::optional<std::vector<uint8_t>> quickAccessValue;

    [[nodiscard]] bool hasFunction(Table table, uint8_t id) const;
    [[nodiscard]] const GeneralSettingSlot* settingSlot(std::string_view subject) const;
    [[nodiscard]] const GeneralSettingSlot* multipointSlot() const;
    [[nodiscard]] const GeneralSettingSlot* touchPanelSlot() const;
    [[nodiscard]] const GeneralSettingSlot* sidetoneSlot() const;
    [[nodiscard]] const AssignableKey* ncAmbKey() const;
    [[nodiscard]] std::optional<uint8_t> ncAmbKeyIndex() const;
    [[nodiscard]] std::optional<size_t> assignableKeyIndex(uint8_t key) const;

    [[nodiscard]] std::string_view model() const;
    [[nodiscard]] std::string_view kind() const;
    [[nodiscard]] std::string displayName() const;

    [[nodiscard]] std::optional<std::string> noiseModeString() const;
    [[nodiscard]] std::optional<int> ambientLevelValue() const;
    [[nodiscard]] std::optional<bool> batteryCharging() const;
    [[nodiscard]] std::optional<std::vector<std::string>> eqPresetNames() const;
    [[nodiscard]] std::vector<std::string> autoPowerOffOptions() const;
    [[nodiscard]] std::optional<std::string> buttonFunctionString() const;
    [[nodiscard]] std::optional<std::vector<std::string>> buttonFunctionOptions() const;
    [[nodiscard]] std::optional<std::string> touchFunctionString(uint8_t key) const;
    [[nodiscard]] std::optional<std::vector<std::string>> touchFunctionOptions(uint8_t key) const;
    [[nodiscard]] uint8_t touchPresetFor(uint8_t key, std::string_view name) const;
    [[nodiscard]] std::optional<bool> multipointValue() const;
    [[nodiscard]] std::optional<bool> touchPanelValue() const;
    [[nodiscard]] std::optional<bool> sidetoneValue() const;
    [[nodiscard]] std::optional<bool> worn() const;
    [[nodiscard]] std::optional<bool> wornLeft() const;
    [[nodiscard]] std::optional<bool> wornRight() const;
    [[nodiscard]] std::optional<size_t> quickAccessIndex(uint8_t action) const;
    [[nodiscard]] std::optional<std::string> quickAccessString(uint8_t action) const;
    [[nodiscard]] std::optional<std::vector<std::string>> quickAccessOptions(uint8_t action) const;

    void resetForConnection(const std::string& alias, const std::string& bluezName,
                            const std::optional<std::string>& addr);
};

std::optional<bool> chargingFromRaw(const std::optional<uint8_t>& raw);
// ASCII case-insensitive comparison (models, addresses).
bool equalsIgnoreCase(std::string_view a, std::string_view b);
// Contract 3.3: an earbud level of 0 with status "not charging" is unknown.
std::optional<int> earbudLevel(const std::optional<int>& level, const std::optional<uint8_t>& chargingRaw);

void selectVariants(HeadphoneState& state);
Supported deriveSupported(const HeadphoneState& state);
void applyFallback(HeadphoneState& state);

// ---------------------------------------------------------------------------
// Inbound parsing (headset -> host)
// ---------------------------------------------------------------------------
struct InboundEvents {
    struct Alert {
        uint8_t messageType{0};
        uint8_t actionType{0};
    };
    struct SourceSwitchResult {
        uint8_t result{0};
        std::string address;
    };

    std::optional<Alert> alert;
    bool wearLogLine{false};
    std::optional<SourceSwitchResult> sourceSwitch;
};

bool applyInbound(Table table, std::span<const uint8_t> payload, HeadphoneState& state, InboundEvents& events);

// ---------------------------------------------------------------------------
// JSON documents
// ---------------------------------------------------------------------------
struct FrameRecord {
    bool tx{false};
    PacketType type{PacketType::DATA_MDR};
    uint8_t seq{0};
    Bytes payload;
};

// One entry of `headsets` (contract 3.3).
std::string headsetJson(const HeadphoneState& state);

// Contract 2.4: does this headset play audio from one of the local adapters?
bool playsFromHere(const HeadphoneState& state, const std::vector<std::string>& adapterAddresses);
// Contract 3.2: headphones before earbuds, then id ascending.
std::vector<const HeadphoneState*> orderHeadsets(std::vector<const HeadphoneState*> headsets);
// Contract 2.4, on headsets already in `orderHeadsets` order.
std::optional<std::string> choosePrimary(const std::vector<const HeadphoneState*>& ordered,
                                         const std::vector<std::string>& adapterAddresses);
// The whole status.json document (schema 3).
std::string statusDocument(const std::vector<const HeadphoneState*>& ordered, const std::optional<std::string>& primary,
                           int64_t lastUpdated);

std::string probeJson(const HeadphoneState& state, std::span<const FrameRecord> recentFrames);

} // namespace omarchy::sony::protocol
