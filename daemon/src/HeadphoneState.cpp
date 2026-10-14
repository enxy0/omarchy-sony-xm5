#include "HeadphoneState.hpp"
#include "Json.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace omarchy::sony::protocol {

namespace {

constexpr std::string_view kMultipointSubject = "MULTIPOINT_SETTING";

// Bounds-checked reader over one payload.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] size_t remaining() const noexcept { return ok_ ? data_.size() - pos_ : 0; }

    uint8_t u8() {
        if (!ok_ || pos_ >= data_.size()) { ok_ = false; return 0; }
        return data_[pos_++];
    }

    std::string bytesAsString(size_t n) {
        if (!ok_ || pos_ + n > data_.size()) { ok_ = false; return {}; }
        std::string out(reinterpret_cast<const char*>(data_.data() + pos_), n);
        pos_ += n;
        return out;
    }

    std::string prefixedString() {
        uint8_t n = u8();
        return bytesAsString(n);
    }

    void skip(size_t n) {
        if (!ok_ || pos_ + n > data_.size()) { ok_ = false; return; }
        pos_ += n;
    }

private:
    std::span<const uint8_t> data_;
    size_t pos_{0};
    bool ok_{true};
};

std::string normalizeAddress(std::string address) {
    for (auto& c : address) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return address;
}

bool isNcAsmType(uint8_t type) { return type == 0x15 || type == 0x17 || type == 0x19; }

bool containsText(std::span<const uint8_t> data, std::string_view needle) {
    if (needle.empty() || data.size() < needle.size()) return false;
    auto it = std::search(data.begin(), data.end(), needle.begin(), needle.end(),
                          [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); });
    return it != data.end();
}

std::optional<std::vector<SupportEntry>> parseSupportList(Reader& r) {
    uint8_t n = r.u8();
    std::vector<SupportEntry> out;
    for (uint8_t i = 0; i < n && r.ok(); ++i) {
        SupportEntry e;
        e.id = r.u8();
        e.priority = r.u8();
        if (r.ok()) out.push_back(e);
    }
    if (!r.ok()) return std::nullopt;
    return out;
}

bool parseAssignableKey(Reader& r, AssignableKey& key) {
    key.key = r.u8();
    key.type = r.u8();
    key.defaultPreset = r.u8();
    uint8_t presetCount = r.u8();
    for (uint8_t p = 0; p < presetCount && r.ok(); ++p) {
        uint8_t preset = r.u8();
        uint8_t actionCount = r.u8();
        r.skip(static_cast<size_t>(actionCount) * 2);
        uint8_t customizableCount = r.u8();
        for (uint8_t c = 0; c < customizableCount && r.ok(); ++c) {
            r.skip(2);
            uint8_t functionCount = r.u8();
            r.skip(functionCount);
        }
        if (r.ok()) key.presets.push_back(preset);
    }
    return r.ok();
}

// ---- Table 1 handlers --------------------------------------------------------

bool applyT1(std::span<const uint8_t> p, HeadphoneState& s, InboundEvents& ev) {
    if (p.size() < 2) return false;
    const uint8_t cmd = p[0];
    const uint8_t type = p[1];
    Reader r(p.subspan(2));

    switch (cmd) {
    case 0x01: { // CONNECT_RET_PROTOCOL_INFO
        if (type != 0x00 || p.size() != 8) return false;
        s.protocolVersion = (static_cast<uint32_t>(p[2]) << 24) | (static_cast<uint32_t>(p[3]) << 16) |
                            (static_cast<uint32_t>(p[4]) << 8) | static_cast<uint32_t>(p[5]);
        s.table1Supported = (p[6] == 0x00);
        s.table2Supported = (p[7] == 0x00);
        s.protocolKnown = true;
        return true;
    }
    case 0x03: { // CONNECT_RET_CAPABILITY_INFO
        if (type != 0x00) return false;
        uint8_t counter = r.u8();
        std::string id = r.prefixedString();
        if (!r.ok()) return false;
        s.capabilityCounter = counter;
        s.uniqueId = id;
        return true;
    }
    case 0x05: { // CONNECT_RET_DEVICE_INFO
        if (type == 0x01 || type == 0x02) {
            std::string text = r.prefixedString();
            if (!r.ok()) return false;
            if (type == 0x01) s.modelName = text; else s.firmwareVersion = text;
            return true;
        }
        if (type == 0x03) {
            uint8_t series = r.u8();
            uint8_t color = r.u8();
            if (!r.ok()) return false;
            s.series = series;
            s.color = color;
            return true;
        }
        return false;
    }
    case 0x07: { // CONNECT_RET_SUPPORT_FUNCTION
        if (type != 0x00) return false;
        auto list = parseSupportList(r);
        if (!list) return false;
        s.support1 = std::move(list);
        return true;
    }
    case 0x13: case 0x15: { // COMMON_RET_STATUS / COMMON_NTFY_STATUS (codec)
        if (type != 0x02) return false;
        uint8_t codec = r.u8();
        if (!r.ok()) return false;
        s.codec = codec;
        return true;
    }
    case 0x23: case 0x25: { // POWER_RET_STATUS / POWER_NTFY_STATUS
        if (type != 0x00 && type != 0x08) return false;
        uint8_t level = r.u8();
        uint8_t charging = r.u8();
        if (type == 0x08) r.u8();
        if (!r.ok()) return false;
        s.batteryLevel = std::min<int>(level, 100);
        s.batteryChargingRaw = charging;
        return true;
    }
    case 0x27: case 0x29: { // POWER_RET_PARAM / POWER_NTFY_PARAM (auto power off)
        if (type != 0x04 && type != 0x05) return false;
        uint8_t current = r.u8();
        r.u8();
        if (!r.ok()) return false;
        s.autoPowerOff = current;
        return true;
    }
    case 0x51: { // EQEBB_RET_CAPABILITY
        if (type != 0x00) return false;
        EqCapability cap;
        cap.bands = r.u8();
        cap.steps = r.u8();
        uint8_t n = r.u8();
        for (uint8_t i = 0; i < n && r.ok(); ++i) {
            EqPresetCapability preset;
            preset.id = r.u8();
            preset.name = r.prefixedString();
            if (r.ok()) cap.presets.push_back(std::move(preset));
        }
        if (!r.ok()) return false;
        s.eqCapability = std::move(cap);
        return true;
    }
    case 0x53: case 0x55: { // EQEBB_RET_STATUS / EQEBB_NTFY_STATUS
        if (type != 0x00) return false;
        uint8_t available = r.u8();
        if (!r.ok()) return false;
        s.eqAvailable = available;
        return true;
    }
    case 0x57: case 0x59: { // EQEBB_RET_PARAM / EQEBB_NTFY_PARAM
        if (type != 0x00) return false;
        uint8_t preset = r.u8();
        uint8_t steps = r.u8();
        std::vector<uint8_t> values;
        for (uint8_t i = 0; i < steps && r.ok(); ++i) values.push_back(r.u8());
        if (!r.ok()) return false;
        s.eqPresetIdValue = preset;
        if (steps == 6) {
            s.eqCustomSeen = true;
            if (preset == 0xA0) {
                s.clearBass = std::clamp(static_cast<int>(values[0]) - 10, -10, 10);
                std::array<int, 5> bands{};
                for (size_t i = 0; i < 5; ++i) bands[i] = std::clamp(static_cast<int>(values[i + 1]) - 10, -10, 10);
                s.eqCustomBands = bands;
            }
        }
        return true;
    }
    case 0x67: case 0x69: { // NCASM_RET_PARAM / NCASM_NTFY_PARAM
        if (type == 0x30) {
            uint8_t function = r.u8();
            if (!r.ok()) return false;
            s.ncAmbButton = function;
            return true;
        }
        if (!isNcAsmType(type)) return false;
        r.u8(); // ValueChangeStatus
        uint8_t te = r.u8();
        uint8_t mo = r.u8();
        uint8_t am = r.u8();
        uint8_t lv = r.u8();
        uint8_t na = 0, ns = 0;
        if (type == 0x19) { na = r.u8(); ns = r.u8(); }
        if (!r.ok()) return false;
        s.ncTotalEffect = te;
        s.ncMode = mo;
        s.ncAmbientMode = am;
        s.ncLevel = lv;
        if (type == 0x19) { s.ncAutoAmbient = na; s.ncNoiseAdaptive = ns; }
        return true;
    }
    case 0x99: { // ALERT_NTFY_PARAM
        if (type != 0x00) return false;
        uint8_t mt = r.u8();
        uint8_t at = r.u8();
        if (!r.ok()) return false;
        ev.alert = InboundEvents::Alert{mt, at};
        return true;
    }
    case 0xA7: case 0xA9: { // PLAY_RET_PARAM / PLAY_NTFY_PARAM (volume)
        if (type != 0x20) return false;
        uint8_t volume = r.u8();
        if (!r.ok()) return false;
        s.volume = volume;
        return true;
    }
    case 0xC9: { // LOG_NTFY_PARAM
        if (containsText(p, "unitWear") || containsText(p, "unitRemove")) ev.wearLogLine = true;
        return true;
    }
    case 0xD1: { // GENERAL_SETTING_RET_CAPABILITY
        if (type < fn1::GENERAL_SETTING_FIRST || type > fn1::GENERAL_SETTING_LAST) return false;
        uint8_t settingType = r.u8();
        uint8_t format = r.u8();
        std::string subject = r.prefixedString();
        std::string summary = r.prefixedString();
        if (!r.ok()) return false;
        auto& slot = s.generalSettings[type - fn1::GENERAL_SETTING_FIRST];
        slot.slot = type;
        slot.capabilityRead = true;
        slot.type = settingType;
        slot.format = format;
        slot.subject = std::move(subject);
        slot.summary = std::move(summary);
        return true;
    }
    case 0xD7: case 0xD9: { // GENERAL_SETTING_RET_PARAM / NTFY_PARAM
        if (type < fn1::GENERAL_SETTING_FIRST || type > fn1::GENERAL_SETTING_LAST) return false;
        uint8_t settingType = r.u8();
        if (!r.ok() || settingType != 0x00) return false;
        uint8_t value = r.u8();
        if (!r.ok()) return false;
        auto& slot = s.generalSettings[type - fn1::GENERAL_SETTING_FIRST];
        slot.slot = type;
        slot.value = (value == 0x00);
        return true;
    }
    case 0xE1: { // AUDIO_RET_CAPABILITY
        if (type != 0x01) return false;
        uint8_t upscaling = r.u8();
        if (!r.ok()) return false;
        s.upscalingType = upscaling;
        return true;
    }
    case 0xE3: case 0xE5: { // AUDIO_RET_STATUS / AUDIO_NTFY_STATUS
        if (type != 0x01) return false;
        uint8_t available = r.u8();
        if (!r.ok()) return false;
        s.dseeAvailable = available;
        return true;
    }
    case 0xE7: case 0xE9: { // AUDIO_RET_PARAM / AUDIO_NTFY_PARAM
        uint8_t value = r.u8();
        if (!r.ok()) return false;
        if (type == 0x01) { s.dsee = (value == 0x01); return true; }
        if (type == 0x00 || type == 0x02) { s.connectionQuality = value; return true; }
        return false;
    }
    case 0xF1: { // SYSTEM_RET_CAPABILITY (assignable)
        if (type != 0x03 && type != 0x0E) return false;
        AssignableCapability cap;
        cap.type = type;
        if (type == 0x0E) r.u8(); // limitation type
        uint8_t keyCount = r.u8();
        for (uint8_t k = 0; k < keyCount && r.ok(); ++k) {
            AssignableKey key;
            if (parseAssignableKey(r, key)) cap.keys.push_back(std::move(key));
        }
        if (!r.ok()) return false;
        s.assignableCapability = std::move(cap);
        return true;
    }
    case 0xF7: case 0xF9: { // SYSTEM_RET_PARAM / SYSTEM_NTFY_PARAM
        if (type == 0x01) {
            uint8_t value = r.u8();
            if (!r.ok()) return false;
            s.earDetection = (value == 0x00);
            return true;
        }
        if (type == 0x0C) {
            uint8_t value = r.u8();
            if (!r.ok()) return false;
            s.speakToChat = (value == 0x00);
            return true;
        }
        if (type == 0x03 || type == 0x0E) {
            uint8_t n = r.u8();
            std::vector<uint8_t> presets;
            for (uint8_t i = 0; i < n && r.ok(); ++i) presets.push_back(r.u8());
            if (!r.ok()) return false;
            s.assignableValue = std::move(presets);
            return true;
        }
        return false;
    }
    case 0xFB: case 0xFD: { // SYSTEM_RET_EXT_PARAM / SYSTEM_NTFY_EXT_PARAM
        if (type != 0x0C) return false;
        uint8_t sensitivity = r.u8();
        uint8_t timeout = r.u8();
        if (!r.ok()) return false;
        s.speakToChatSensitivity = sensitivity;
        s.speakToChatTimeout = timeout;
        return true;
    }
    default:
        return false;
    }
}

// ---- Table 2 handlers --------------------------------------------------------

bool applyT2(std::span<const uint8_t> p, HeadphoneState& s, InboundEvents& ev) {
    if (p.size() < 2) return false;
    const uint8_t cmd = p[0];
    const uint8_t type = p[1];
    Reader r(p.subspan(2));

    switch (cmd) {
    case 0x07: { // CONNECT_RET_SUPPORT_FUNCTION (table 2)
        if (type != 0x00) return false;
        auto list = parseSupportList(r);
        if (!list) return false;
        s.support2 = std::move(list);
        return true;
    }
    case 0x33: case 0x35: { // PERI_RET_STATUS / PERI_NTFY_STATUS (pairing mode)
        if (type != 0x00 && type != 0x02) return false;
        uint8_t mode = r.u8();
        if (!r.ok()) return false;
        s.pairingMode = mode;
        return true;
    }
    case 0x37: case 0x39: { // PERI_RET_PARAM / PERI_NTFY_PARAM
        if (type == 0x01) {
            uint8_t value = r.u8();
            if (!r.ok()) return false;
            s.sourceSwitchAuto = (value != 0x00);
            return true;
        }
        if (type != 0x00 && type != 0x02) return false;
        uint8_t n = r.u8();
        std::vector<PairedDevice> devices;
        for (uint8_t i = 0; i < n && r.ok(); ++i) {
            PairedDevice d;
            d.address = normalizeAddress(r.bytesAsString(17));
            d.connectionStatus = r.u8();
            if (type == 0x02) r.skip(3);
            d.name = r.prefixedString();
            if (r.ok()) devices.push_back(std::move(d));
        }
        uint8_t playback = r.u8();
        if (!r.ok()) return false;
        s.devices = std::move(devices);
        s.playbackSlot = playback;
        return true;
    }
    case 0x3D: { // PERI_NTFY_EXTENDED_PARAM (source switch result)
        if (type != 0x01) return false;
        uint8_t result = r.u8();
        std::string address = r.bytesAsString(17);
        if (!r.ok()) return false;
        ev.sourceSwitch = InboundEvents::SourceSwitchResult{result, normalizeAddress(address)};
        return true;
    }
    case 0x47: case 0x49: { // VOICE_GUIDANCE_RET_PARAM / NTFY_PARAM
        if (type == 0x20) {
            uint8_t raw = r.u8();
            if (!r.ok()) return false;
            s.voiceGuidanceVolume = static_cast<int8_t>(raw);
            return true;
        }
        if (type > 0x03) return false;
        uint8_t value = r.u8();
        if (!r.ok()) return false;
        s.voiceGuidance = (value == 0x00);
        if (type == 0x01 || type == 0x02) {
            uint8_t language = r.u8();
            if (r.ok()) s.voiceGuidanceLanguage = language;
        }
        return true;
    }
    case 0xF3: case 0xF5: { // SYSTEM_RET_STATUS / SYSTEM_NTFY_STATUS (worn)
        if (type != 0x00) return false;
        uint8_t status = r.u8();
        if (!r.ok()) return false;
        s.wornStatus = status;
        return true;
    }
    default:
        return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Supported
// ---------------------------------------------------------------------------

Supported Supported::fallback() {
    Supported s;
    s.battery = true;
    s.noise_control = true;
    s.eq_preset = true;
    s.eq_custom = true;
    s.speak_to_chat = true;
    s.voice_guidance = true;
    s.auto_power_off = true;
    s.nc_amb_button = true;
    s.ear_detection = true;
    s.volume = true;
    return s;
}

// ---------------------------------------------------------------------------
// HeadphoneState helpers
// ---------------------------------------------------------------------------

bool HeadphoneState::hasFunction(Table table, uint8_t id) const {
    const auto& list = table == Table::T1 ? support1 : support2;
    if (!list) return false;
    return std::any_of(list->begin(), list->end(), [id](const SupportEntry& e) { return e.id == id; });
}

const GeneralSettingSlot* HeadphoneState::multipointSlot() const {
    for (const auto& slot : generalSettings) {
        if (slot.advertised && slot.capabilityRead && slot.type == 0x00 && slot.subject == kMultipointSubject) {
            return &slot;
        }
    }
    return nullptr;
}

std::optional<uint8_t> HeadphoneState::ncAmbKeyIndex() const {
    if (!assignableCapability) return std::nullopt;
    const auto& keys = assignableCapability->keys;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].key == 0x04 || keys[i].key == 0x05) return static_cast<uint8_t>(i);
    }
    return std::nullopt;
}

const AssignableKey* HeadphoneState::ncAmbKey() const {
    auto idx = ncAmbKeyIndex();
    if (!idx) return nullptr;
    return &assignableCapability->keys[*idx];
}

std::optional<std::string> HeadphoneState::noiseModeString() const {
    if (!ncTotalEffect) return std::nullopt;
    if (*ncTotalEffect == 0x00) return std::string("off");
    if (!ncMode) return std::nullopt;
    if (*ncMode == 0x00) return std::string("anc");
    if (*ncMode == 0x01) return std::string("ambient");
    return std::nullopt;
}

std::optional<int> HeadphoneState::ambientLevelValue() const {
    if (!ncLevel || *ncLevel < 1 || *ncLevel > 20) return std::nullopt;
    return static_cast<int>(*ncLevel);
}

std::optional<bool> HeadphoneState::batteryCharging() const {
    if (!batteryChargingRaw) return std::nullopt;
    switch (*batteryChargingRaw) {
        case 0x00: return false;
        case 0x01: return true;
        case 0x03: return true;
        default:   return std::nullopt;
    }
}

std::optional<std::vector<std::string>> HeadphoneState::eqPresetNames() const {
    if (!eqCapability) return std::nullopt;
    std::vector<std::string> names;
    for (const auto& preset : eqCapability->presets) {
        auto name = eqPresetName(preset.id);
        if (name == "unknown") continue;
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    return names;
}

std::vector<std::string> HeadphoneState::autoPowerOffOptions() const {
    std::vector<std::string> options = {"off", "5min", "15min", "30min", "60min", "180min"};
    if (variants.autoPowerOffType == 0x05) options.emplace_back("when_taken_off");
    return options;
}

std::optional<std::string> HeadphoneState::buttonFunctionString() const {
    auto idx = ncAmbKeyIndex();
    if (!idx || !assignableValue || *idx >= assignableValue->size()) return std::nullopt;
    return buttonFunctionName((*assignableValue)[*idx]);
}

std::optional<std::vector<std::string>> HeadphoneState::buttonFunctionOptions() const {
    const auto* key = ncAmbKey();
    if (!key) return std::nullopt;
    std::vector<std::string> names;
    for (uint8_t preset : key->presets) {
        auto name = buttonFunctionName(preset);
        if (name == "other") continue;
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    return names;
}

std::optional<bool> HeadphoneState::multipointValue() const {
    const auto* slot = multipointSlot();
    if (!slot) return std::nullopt;
    return slot->value;
}

std::optional<bool> HeadphoneState::worn() const {
    if (!wornStatus) return std::nullopt;
    switch (*wornStatus) {
        case 0x00: return true;
        case 0x02: case 0x03: case 0x04: return false;
        default: return std::nullopt;
    }
}

void HeadphoneState::resetForConnection(const std::string& name, const std::optional<std::string>& addr) {
    HeadphoneState fresh;
    fresh.deviceName = name.empty() ? "WH-1000XM5" : name;
    if (addr) fresh.address = normalizeAddress(*addr);
    *this = std::move(fresh);
}

// ---------------------------------------------------------------------------
// Variant selection & supported derivation
// ---------------------------------------------------------------------------

void selectVariants(HeadphoneState& s) {
    auto& v = s.variants;
    using fn1::BATTERY_LEVEL_INDICATOR;
    v.batteryType = s.hasFunction(Table::T1, BATTERY_LEVEL_INDICATOR) ? 0x00
                  : s.hasFunction(Table::T1, fn1::BATTERY_LEVEL_WITH_THRESHOLD) ? 0x08 : 0x00;

    if (s.hasFunction(Table::T1, fn1::NC_ASM_DUAL)) v.ncAsmType = 0x17;
    else if (s.hasFunction(Table::T1, fn1::NC_ASM_NOISE_ADAPTATION)) v.ncAsmType = 0x19;
    else if (s.hasFunction(Table::T1, fn1::NC_ASM_AUTO)) v.ncAsmType = 0x15;
    else v.ncAsmType.reset();

    v.autoPowerOffType = s.hasFunction(Table::T1, fn1::AUTO_POWER_OFF_WITH_WEARING_DETECTION) ? 0x05 : 0x04;

    if (s.hasFunction(Table::T2, fn2::VOICE_GUIDANCE_SUPPORT_LANGUAGE_SWITCH) ||
        s.hasFunction(Table::T2, fn2::VOICE_GUIDANCE_WITH_VOLUME)) v.voiceGuidanceType = 0x01;
    else if (s.hasFunction(Table::T2, fn2::VOICE_GUIDANCE_NOT_SUPPORT_LANGUAGE_SWITCH)) v.voiceGuidanceType = 0x00;
    else if (s.hasFunction(Table::T2, fn2::VOICE_GUIDANCE_SETTING_SUPPORT_LANGUAGE_SWITCH)) v.voiceGuidanceType = 0x02;
    else if (s.hasFunction(Table::T2, fn2::VOICE_GUIDANCE_ONLY_ON_OFF)) v.voiceGuidanceType = 0x03;

    v.connectionQualityType = s.hasFunction(Table::T1, fn1::CONNECTION_MODE) ? 0x00
                            : s.hasFunction(Table::T1, fn1::CONNECTION_MODE_WITH_LDAC_STATUS) ? 0x02 : 0x00;

    v.assignableType = s.hasFunction(Table::T1, fn1::ASSIGNABLE_SETTING_WITH_LIMITATION) ? 0x0E : 0x03;

    v.deviceListType = (s.hasFunction(Table::T2, fn2::PAIRING_DEVICE_MANAGEMENT_WITH_COD_CLASSIC_BT) ||
                        s.hasFunction(Table::T2, fn2::PAIRING_DEVICE_MANAGEMENT_WITH_COD_CLASSIC_LE)) ? 0x02 : 0x00;

    for (auto& slot : s.generalSettings) slot.advertised = false;
    for (uint8_t id = fn1::GENERAL_SETTING_FIRST; id <= fn1::GENERAL_SETTING_LAST; ++id) {
        auto& slot = s.generalSettings[id - fn1::GENERAL_SETTING_FIRST];
        slot.slot = id;
        slot.advertised = s.hasFunction(Table::T1, id);
    }
    const auto* mp = s.multipointSlot();
    if (mp) v.multipointSlot = mp->slot; else v.multipointSlot.reset();
}

Supported deriveSupported(const HeadphoneState& s) {
    auto t1 = [&](uint8_t id) { return s.hasFunction(Table::T1, id); };
    auto t2 = [&](uint8_t id) { return s.hasFunction(Table::T2, id); };

    Supported out;
    out.battery = t1(fn1::BATTERY_LEVEL_INDICATOR) || t1(fn1::BATTERY_LEVEL_WITH_THRESHOLD);
    out.noise_control = t1(fn1::NC_ASM_DUAL) || t1(fn1::NC_ASM_NOISE_ADAPTATION) || t1(fn1::NC_ASM_AUTO);
    out.eq_preset = t1(fn1::PRESET_EQ);
    out.eq_custom = out.eq_preset &&
        ((s.eqCapability && (s.eqCapability->bands == 5 || s.eqCapability->bands == 6)) || s.eqCustomSeen);
    out.dsee = t1(fn1::UPSCALING_AUTO_OFF);
    out.speak_to_chat = t1(fn1::SMART_TALKING_MODE_TYPE2);
    out.voice_guidance = t2(fn2::VOICE_GUIDANCE_NOT_SUPPORT_LANGUAGE_SWITCH) ||
                         t2(fn2::VOICE_GUIDANCE_SUPPORT_LANGUAGE_SWITCH) || t2(fn2::VOICE_GUIDANCE_WITH_VOLUME) ||
                         t2(fn2::VOICE_GUIDANCE_SETTING_SUPPORT_LANGUAGE_SWITCH) || t2(fn2::VOICE_GUIDANCE_ONLY_ON_OFF);
    out.voice_guidance_volume = t2(fn2::VOICE_GUIDANCE_WITH_VOLUME) || t2(fn2::VOICE_GUIDANCE_VOLUME_FIXED_5_STEPS);
    out.auto_power_off = t1(fn1::AUTO_POWER_OFF) || t1(fn1::AUTO_POWER_OFF_WITH_WEARING_DETECTION);
    out.nc_amb_button = t1(fn1::AMBIENT_SOUND_CONTROL_MODE_SELECT);
    out.button_function = (t1(fn1::ASSIGNABLE_SETTING) || t1(fn1::ASSIGNABLE_SETTING_WITH_LIMITATION)) &&
                          s.ncAmbKey() != nullptr;
    out.ear_detection = t1(fn1::PLAYBACK_CONTROL_BY_WEARING);
    out.worn = s.table2Supported && s.support2.has_value() &&
               (t2(fn2::WEARING_STATUS_CHECKER) || t1(fn1::WEARING_STATUS_DETECTOR) || t1(fn1::PLAYBACK_CONTROL_BY_WEARING));
    bool anyPlayback = false;
    for (uint8_t id = fn1::PLAYBACK_CONTROLLER_FIRST; id <= fn1::PLAYBACK_CONTROLLER_LAST; ++id) anyPlayback |= t1(id);
    out.volume = anyPlayback;
    out.connection_quality = t1(fn1::CONNECTION_MODE) || t1(fn1::CONNECTION_MODE_WITH_LDAC_STATUS);
    out.codec = t1(fn1::CODEC_INDICATOR);
    out.multipoint = s.multipointSlot() != nullptr;
    out.devices = t2(fn2::PAIRING_DEVICE_MANAGEMENT_CLASSIC_BT) ||
                  t2(fn2::PAIRING_DEVICE_MANAGEMENT_WITH_COD_CLASSIC_BT) ||
                  t2(fn2::PAIRING_DEVICE_MANAGEMENT_WITH_COD_CLASSIC_LE);
    out.source_switch = t2(fn2::SOURCE_SWITCH_CONTROL) && out.devices;
    return out;
}

void applyFallback(HeadphoneState& s) {
    s.fallback = true;
    s.supported = Supported::fallback();
    s.variants.batteryType = 0x00;
    s.variants.ncAsmType = 0x17;
    s.variants.autoPowerOffType = 0x05;
    s.variants.voiceGuidanceType = 0x01;
}

bool applyInbound(Table table, std::span<const uint8_t> payload, HeadphoneState& state, InboundEvents& events) {
    return table == Table::T1 ? applyT1(payload, state, events) : applyT2(payload, state, events);
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace {

template <typename T>
std::optional<T> gate(bool on, const std::optional<T>& v) {
    return on ? v : std::nullopt;
}

void writeStringArray(json::Writer& w, const std::optional<std::vector<std::string>>& values) {
    if (!values) { w.null(); return; }
    w.beginArray();
    for (const auto& v : *values) w.value(v);
    w.endArray();
}

void writeIntArray(json::Writer& w, const std::optional<std::array<int, 5>>& values) {
    if (!values) { w.null(); return; }
    w.beginArray();
    for (int v : *values) w.value(v);
    w.endArray();
}

} // namespace

std::string statusJson(const HeadphoneState& s, int64_t lastUpdated) {
    json::Writer w;
    w.beginObject();
    w.field("schema_version", 2);
    w.field("connected", s.connected);
    w.field("last_updated", lastUpdated);
    if (!s.connected) {
        w.endObject();
        return w.str();
    }

    const Supported sup = s.supported.value_or(Supported{});
    const bool ready = s.supported.has_value();

    w.field("device_name", s.deviceName);
    w.field("address", s.address);
    w.field("model_name", s.modelName);
    w.field("firmware_version", s.firmwareVersion);
    if (ready) w.field("supported_source", s.fallback ? "fallback" : "device");
    else w.nullField("supported_source");

    w.key("supported");
    if (!ready) {
        w.null();
    } else {
        w.beginObject();
        w.field("battery", sup.battery);
        w.field("noise_control", sup.noise_control);
        w.field("eq_preset", sup.eq_preset);
        w.field("eq_custom", sup.eq_custom);
        w.field("dsee", sup.dsee);
        w.field("speak_to_chat", sup.speak_to_chat);
        w.field("voice_guidance", sup.voice_guidance);
        w.field("voice_guidance_volume", sup.voice_guidance_volume);
        w.field("auto_power_off", sup.auto_power_off);
        w.field("nc_amb_button", sup.nc_amb_button);
        w.field("button_function", sup.button_function);
        w.field("ear_detection", sup.ear_detection);
        w.field("worn", sup.worn);
        w.field("volume", sup.volume);
        w.field("connection_quality", sup.connection_quality);
        w.field("codec", sup.codec);
        w.field("multipoint", sup.multipoint);
        w.field("devices", sup.devices);
        w.field("source_switch", sup.source_switch);
        w.endObject();
    }

    auto battery = gate(sup.battery, s.batteryLevel);
    auto charging = gate(sup.battery, s.batteryCharging());
    w.field("battery_level", battery);
    w.field("battery_charging", charging);
    w.field("charging", charging);

    auto mode = gate(sup.noise_control, s.noiseModeString());
    auto level = gate(sup.noise_control, s.ambientLevelValue());
    std::optional<bool> voice;
    if (sup.noise_control && s.ncAmbientMode) voice = (*s.ncAmbientMode == 0x01);
    w.field("noise_mode", mode);
    w.field("ambient_sound_level", level);
    w.field("ambient_level", level);
    w.field("voice_passthrough", voice);

    std::optional<std::string> preset;
    if (sup.eq_preset && s.eqPresetIdValue) preset = eqPresetName(*s.eqPresetIdValue);
    w.field("eq_preset", preset);
    w.key("eq_presets");
    writeStringArray(w, sup.eq_preset ? s.eqPresetNames() : std::nullopt);
    auto bands = sup.eq_custom ? s.eqCustomBands : std::nullopt;
    w.key("eq_custom_bands");
    writeIntArray(w, bands);
    w.key("eq_bands");
    writeIntArray(w, bands);
    w.field("clear_bass", gate(sup.eq_custom, s.clearBass));

    auto dsee = gate(sup.dsee, s.dsee);
    w.field("dsee_extreme", dsee);
    w.field("dsee", dsee);

    std::optional<std::string> quality;
    if (sup.connection_quality && s.connectionQuality) quality = connectionQualityName(*s.connectionQuality);
    w.field("connection_quality", quality);
    std::optional<std::string> codec;
    if (sup.codec && s.codec) codec = codecName(*s.codec);
    w.field("codec", codec);
    w.field("volume", gate(sup.volume, s.volume));

    w.field("speak_to_chat", gate(sup.speak_to_chat, s.speakToChat));
    std::optional<std::string> sensitivity, timeout;
    if (sup.speak_to_chat && s.speakToChatSensitivity) sensitivity = speakToChatSensitivityName(*s.speakToChatSensitivity);
    if (sup.speak_to_chat && s.speakToChatTimeout) timeout = speakToChatTimeoutName(*s.speakToChatTimeout);
    w.field("speak_to_chat_sensitivity", sensitivity);
    w.field("speak_to_chat_timeout", timeout);

    w.field("voice_guidance", gate(sup.voice_guidance, s.voiceGuidance));
    std::optional<int> vgVolume;
    if (sup.voice_guidance_volume && s.voiceGuidanceVolume && *s.voiceGuidanceVolume >= -2 && *s.voiceGuidanceVolume <= 2) {
        vgVolume = s.voiceGuidanceVolume;
    }
    w.field("voice_guidance_volume", vgVolume);

    std::optional<std::string> apo;
    if (sup.auto_power_off && s.autoPowerOff) {
        apo = autoPowerOffName(*s.autoPowerOff);
        if (apo && *apo == "when_taken_off" && s.variants.autoPowerOffType != 0x05) apo.reset();
    }
    w.field("auto_power_off", apo);
    w.key("auto_power_off_options");
    writeStringArray(w, sup.auto_power_off ? std::optional(s.autoPowerOffOptions()) : std::nullopt);

    std::optional<std::string> ncAmb;
    if (sup.nc_amb_button && s.ncAmbButton) ncAmb = ncAmbButtonName(*s.ncAmbButton);
    w.field("nc_amb_button", ncAmb);
    w.field("button_function", gate(sup.button_function, s.buttonFunctionString()));
    w.key("button_functions");
    writeStringArray(w, sup.button_function ? s.buttonFunctionOptions() : std::nullopt);

    w.field("ear_detection", gate(sup.ear_detection, s.earDetection));
    w.field("worn", gate(sup.worn, s.worn()));
    w.field("multipoint", gate(sup.multipoint, s.multipointValue()));

    w.key("devices");
    if (!sup.devices || !s.devices) {
        w.null();
    } else {
        w.beginArray();
        bool activeSeen = false;
        for (const auto& d : *s.devices) {
            bool connected = d.connectionStatus != 0x00;
            bool active = connected && d.connectionStatus == s.playbackSlot && !activeSeen;
            activeSeen |= active;
            w.beginObject();
            w.field("address", d.address);
            w.field("name", d.name);
            w.field("connected", connected);
            w.field("active", active);
            w.endObject();
        }
        w.endArray();
    }

    w.endObject();
    return w.str();
}

std::string probeJson(const HeadphoneState& s, std::span<const FrameRecord> recentFrames) {
    json::Writer w;
    w.beginObject();
    w.field("probe_version", 1);
    w.field("address", s.address);

    w.key("protocol");
    if (!s.protocolKnown) {
        w.null();
    } else {
        w.beginObject();
        w.field("version", static_cast<int64_t>(s.protocolVersion));
        w.field("table1", s.table1Supported);
        w.field("table2", s.table2Supported);
        w.endObject();
    }

    w.field("model_name", s.modelName);
    w.field("firmware_version", s.firmwareVersion);
    if (s.series) w.field("series", hexByte(*s.series)); else w.nullField("series");
    if (s.color) w.field("color", hexByte(*s.color)); else w.nullField("color");
    w.field("unique_id", s.uniqueId);
    if (s.variants.ncAsmType && s.supported && s.supported->noise_control) {
        w.field("nc_asm_type", hexByte(*s.variants.ncAsmType));
    } else {
        w.nullField("nc_asm_type");
    }

    auto writeSupport = [&](std::string_view key, Table table, const std::optional<std::vector<SupportEntry>>& list) {
        w.key(key);
        if (!list) { w.null(); return; }
        w.beginArray();
        for (const auto& e : *list) {
            w.beginObject();
            w.field("id", hexByte(e.id));
            w.field("name", functionName(table, e.id));
            w.field("priority", static_cast<int>(e.priority));
            w.endObject();
        }
        w.endArray();
    };
    writeSupport("support_table1", Table::T1, s.support1);
    writeSupport("support_table2", Table::T2, s.support2);

    w.key("general_settings");
    if (!s.support1) {
        w.null();
    } else {
        w.beginArray();
        for (const auto& slot : s.generalSettings) {
            if (!slot.advertised) continue;
            w.beginObject();
            w.field("slot", hexByte(slot.slot));
            if (!slot.capabilityRead) {
                w.nullField("type");
                w.nullField("format");
                w.nullField("subject");
                w.nullField("summary");
                w.nullField("value");
            } else {
                if (slot.type == 0x00) w.field("type", "boolean");
                else if (slot.type == 0x01) w.field("type", "list");
                else w.field("type", hexByte(slot.type));
                if (slot.format == 0x00) w.field("format", "raw_name");
                else if (slot.format == 0x01) w.field("format", "enum_name");
                else w.field("format", hexByte(slot.format));
                w.field("subject", slot.subject);
                w.field("summary", slot.summary);
                if (slot.type == 0x00) w.field("value", slot.value); else w.nullField("value");
            }
            w.endObject();
        }
        w.endArray();
    }

    w.key("eq_capability");
    if (!s.eqCapability) {
        w.null();
    } else {
        w.beginObject();
        w.field("bands", static_cast<int>(s.eqCapability->bands));
        w.field("steps", static_cast<int>(s.eqCapability->steps));
        w.key("presets").beginArray();
        for (const auto& p : s.eqCapability->presets) {
            w.beginObject();
            w.field("id", hexByte(p.id));
            w.field("name", p.name);
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }

    w.key("assignable");
    if (!s.assignableCapability && !s.assignableValue) {
        w.null();
    } else {
        w.beginObject();
        if (s.assignableCapability) {
            w.field("type", hexByte(s.assignableCapability->type));
            w.key("keys").beginArray();
            for (const auto& k : s.assignableCapability->keys) {
                w.beginObject();
                w.field("key", hexByte(k.key));
                w.field("type", hexByte(k.type));
                w.field("default", hexByte(k.defaultPreset));
                w.key("presets").beginArray();
                for (uint8_t p : k.presets) w.value(hexByte(p));
                w.endArray();
                w.endObject();
            }
            w.endArray();
        } else {
            w.nullField("type");
            w.nullField("keys");
        }
        w.key("value");
        if (s.assignableValue) {
            w.beginArray();
            for (uint8_t p : *s.assignableValue) w.value(hexByte(p));
            w.endArray();
        } else {
            w.null();
        }
        w.endObject();
    }

    w.key("recent_frames").beginArray();
    for (const auto& f : recentFrames) {
        w.beginObject();
        w.field("dir", f.tx ? "tx" : "rx");
        w.field("type", hexByte(static_cast<uint8_t>(f.type)));
        w.field("seq", static_cast<int>(f.seq));
        w.field("payload", hexBytes(f.payload));
        w.endObject();
    }
    w.endArray();

    w.endObject();
    return w.str();
}

} // namespace omarchy::sony::protocol
