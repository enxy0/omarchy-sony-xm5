#include "HeadsetSession.hpp"

#include <algorithm>
#include <utility>

namespace omarchy::sony {

using namespace protocol;
using cmd::Verb;

namespace {

const char* featureKey(Verb verb) {
    switch (verb) {
        case Verb::Noise:
        case Verb::AmbientLevel:
        case Verb::FocusVoice:          return "noise_control";
        case Verb::Eq:                  return "eq_preset";
        case Verb::EqCustom:            return "eq_custom";
        case Verb::Dsee:                return "dsee";
        case Verb::SpeakToChat:
        case Verb::SpeakToChatConfig:   return "speak_to_chat";
        case Verb::VoiceGuidance:       return "voice_guidance";
        case Verb::VoiceGuidanceVolume: return "voice_guidance_volume";
        case Verb::AutoPowerOff:        return "auto_power_off";
        case Verb::NcAmbButton:         return "nc_amb_button";
        case Verb::ButtonFunction:      return "button_function";
        case Verb::TouchFunction:       return "touch_function";
        case Verb::EarDetect:           return "ear_detection";
        case Verb::Volume:              return "volume";
        case Verb::ConnectionQuality:   return "connection_quality";
        case Verb::Multipoint:          return "multipoint";
        case Verb::Source:              return "source_switch";
        case Verb::TouchPanel:          return "touch_panel";
        case Verb::Sidetone:            return "sidetone";
        case Verb::QuickAccess:         return "quick_access";
        default:                        return nullptr;
    }
}

bool featureEnabled(const Supported& s, Verb verb) {
    switch (verb) {
        case Verb::Noise:
        case Verb::AmbientLevel:
        case Verb::FocusVoice:          return s.noise_control;
        case Verb::Eq:                  return s.eq_preset;
        case Verb::EqCustom:            return s.eq_custom;
        case Verb::Dsee:                return s.dsee;
        case Verb::SpeakToChat:
        case Verb::SpeakToChatConfig:   return s.speak_to_chat;
        case Verb::VoiceGuidance:       return s.voice_guidance;
        case Verb::VoiceGuidanceVolume: return s.voice_guidance_volume;
        case Verb::AutoPowerOff:        return s.auto_power_off;
        case Verb::NcAmbButton:         return s.nc_amb_button;
        case Verb::ButtonFunction:      return s.button_function;
        case Verb::TouchFunction:       return s.touch_function;
        case Verb::EarDetect:           return s.ear_detection;
        case Verb::Volume:              return s.volume;
        case Verb::ConnectionQuality:   return s.connection_quality;
        case Verb::Multipoint:          return s.multipoint;
        case Verb::Source:              return s.source_switch;
        case Verb::TouchPanel:          return s.touch_panel;
        case Verb::Sidetone:            return s.sidetone;
        case Verb::QuickAccess:         return s.quick_access;
        default:                        return false;
    }
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

uint8_t sideKey(const std::string& side) { return side == "left" ? kKeyLeftSide : kKeyRightSide; }

} // namespace

HeadsetSession::HeadsetSession(Hooks hooks) : HeadsetSession(std::move(hooks), Timing{}) {}

HeadsetSession::HeadsetSession(Hooks hooks, Timing timing)
    : hooks_(std::move(hooks)),
      timing_(timing),
      link_([this](const Bytes& frame) { return hooks_.write ? hooks_.write(frame) : false; },
            [this](Table table, const Bytes& payload) { onPayload(table, payload); },
            [this](const std::string& line) { log(line); }) {}

int64_t HeadsetSession::unixNow() const {
    if (hooks_.unixNow) return hooks_.unixNow();
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

void HeadsetSession::onTransportConnected(const std::string& alias, const std::string& bluezName,
                                          const std::optional<std::string>& address, Clock::time_point now) {
    now_ = now;
    ++gen_;
    steps_.clear();
    stepRunning_ = false;
    link_.reset();
    state_.resetForConnection(alias, bluezName, address);
    fallbackPending_ = false;
    initDone_ = false;
    alertWindowUntil_ = {};
    publishIfChanged();

    Step handshake;
    handshake.payload = req::getProtocolInfo();
    handshake.expectReply = true;
    handshake.handshake = true;
    handshake.after = [this](LinkResult r) { afterHandshake(r); };
    queueStep(std::move(handshake));
    runSteps();
}

void HeadsetSession::onTransportDisconnected(Clock::time_point now) {
    now_ = now;
    ++gen_;
    steps_.clear();
    stepRunning_ = false;
    initDone_ = false;
    state_.connected = false;
    link_.reset();
    publishIfChanged();
}

void HeadsetSession::onBytes(std::span<const uint8_t> data, Clock::time_point now) {
    now_ = now;
    link_.onBytes(data, now);
}

void HeadsetSession::tick(Clock::time_point now) {
    now_ = now;
    link_.tick(now);
    if (initDone_ && state_.connected && state_.supported && link_.idle() && now >= nextSync_) {
        nextSync_ = now + std::chrono::milliseconds(timing_.syncIntervalMs);
        const auto& sup = *state_.supported;
        const Variants& v = state_.variants;
        if (sup.battery) submitBackground(Table::T1, req::getBattery(v.batteryType));
        if (sup.battery_lr) submitBackground(Table::T1, req::getBattery(v.lrBatteryType));
        if (sup.battery_case) submitBackground(Table::T1, req::getBattery(v.caseBatteryType));
        if (sup.volume) submitBackground(Table::T1, req::getVolume());
        if (sup.worn) submitBackground(Table::T2, req::getWornStatus());
    }
}

std::optional<HeadsetSession::Clock::time_point> HeadsetSession::nextDeadline() const {
    if (!link_.idle()) return link_.nextDeadline();
    if (initDone_ && state_.connected) return nextSync_;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Init steps
// ---------------------------------------------------------------------------

void HeadsetSession::queueStep(Step step) {
    steps_.push_back(std::move(step));
}

void HeadsetSession::queueRead(Table table, Bytes payload, std::function<void(LinkResult)> after) {
    Step step;
    step.table = table;
    step.payload = std::move(payload);
    step.expectReply = true;
    step.after = std::move(after);
    queueStep(std::move(step));
}

void HeadsetSession::queueLocal(std::function<void()> action) {
    Step step;
    step.after = [action = std::move(action)](LinkResult) { action(); };
    queueStep(std::move(step));
}

void HeadsetSession::runSteps() {
    while (!stepRunning_ && !steps_.empty()) {
        Step step = std::move(steps_.front());
        steps_.pop_front();

        if (step.payload.empty()) {
            if (step.after) step.after(LinkResult::Ok);
            continue;
        }

        LinkRequest request;
        request.table = step.table;
        request.payload = step.payload;
        request.ackTimeoutMs = step.handshake ? timing_.handshakeAckMs : timing_.initAckMs;
        request.maxAttempts = step.handshake ? timing_.handshakeAttempts : timing_.initAttempts;
        request.replyTimeoutMs = timing_.replyTimeoutMs;
        if (step.expectReply) request.reply = replyFor(step.table, step.payload);
        const uint64_t gen = gen_;
        request.done = [this, gen, after = std::move(step.after)](LinkResult r) {
            if (gen != gen_ || r == LinkResult::Cancelled) return;
            stepRunning_ = false;
            if (after) after(r);
            if (gen == gen_) runSteps();
        };
        stepRunning_ = true;
        link_.submit(std::move(request), now_);
    }
}

void HeadsetSession::afterHandshake(LinkResult result) {
    if (result != LinkResult::Ok || !state_.protocolKnown) {
        log(std::string("protocol handshake failed (") + linkResultName(result) + "); dropping the link");
        steps_.clear();
        if (hooks_.dropLink) hooks_.dropLink("MDR protocol handshake not answered");
        return;
    }

    state_.connected = true;
    state_.connectedSince = unixNow();
    publishIfChanged();
    log("protocol version " + std::to_string(state_.protocolVersion) + ", table1=" +
        (state_.table1Supported ? "yes" : "no") + ", table2=" + (state_.table2Supported ? "yes" : "no"));

    if (!state_.table1Supported) {
        log("headset reports table 1 as unsupported; stopping the init sequence");
        steps_.clear();
        return;
    }

    queueRead(Table::T1, req::getCapabilityInfo());
    queueRead(Table::T1, req::getDeviceInfo(0x02));
    queueRead(Table::T1, req::getDeviceInfo(0x01));
    queueRead(Table::T1, req::getDeviceInfo(0x03));
    queueRead(Table::T1, req::getSupportFunction(), [this](LinkResult r) {
        if (r != LinkResult::Ok || !state_.support1) {
            log("support function list not received; using the fallback feature list");
            fallbackPending_ = true;
            return;
        }
        if (state_.table2Supported) {
            Step t2;
            t2.table = Table::T2;
            t2.payload = req::getSupportFunction();
            t2.expectReply = true;
            steps_.push_front(std::move(t2));
        }
    });
    queueLocal([this] { planCapabilities(); });
}

void HeadsetSession::planCapabilities() {
    if (fallbackPending_) {
        applyFallback(state_);
        publishIfChanged();
        queueFallbackReads();
        return;
    }

    selectVariants(state_);
    for (const auto& slot : state_.generalSettings) {
        if (slot.advertised) queueRead(Table::T1, req::getGeneralSettingCapability(slot.slot));
    }
    if (state_.hasFunction(Table::T1, fn1::UPSCALING_AUTO_OFF)) {
        queueRead(Table::T1, req::getUpscalingCapability());
    }
    if (state_.hasFunction(Table::T1, fn1::ASSIGNABLE_SETTING) ||
        state_.hasFunction(Table::T1, fn1::ASSIGNABLE_SETTING_WITH_LIMITATION)) {
        queueRead(Table::T1, req::getAssignableCapability(state_.variants.assignableType));
    }
    if (state_.hasFunction(Table::T1, fn1::PRESET_EQ)) {
        queueRead(Table::T1, req::getEqCapability());
    }
    if (state_.hasFunction(Table::T1, fn1::QUICK_ACCESS)) {
        queueRead(Table::T1, req::getQuickAccessCapability());
    }
    queueLocal([this] { publishSupported(); });
}

void HeadsetSession::publishSupported() {
    selectVariants(state_);
    state_.supported = deriveSupported(state_);
    publishIfChanged();
    queueValueReads();
}

void HeadsetSession::queueValueReads() {
    const Supported sup = *state_.supported;
    const Variants& v = state_.variants;
    auto t1 = [this](uint8_t id) { return state_.hasFunction(Table::T1, id); };

    if (sup.battery) queueRead(Table::T1, req::getBattery(v.batteryType));
    if (sup.battery_lr) queueRead(Table::T1, req::getBattery(v.lrBatteryType));
    if (sup.battery_case) queueRead(Table::T1, req::getBattery(v.caseBatteryType));
    if (t1(fn1::FIXED_MESSAGE)) {
        Step alerts;
        alerts.payload = req::enableFixedMessageAlerts();
        queueStep(std::move(alerts));
    }
    for (const auto& slot : state_.generalSettings) {
        if (slot.advertised && slot.capabilityRead && slot.type == 0x00) {
            queueRead(Table::T1, req::getGeneralSettingValue(slot.slot));
        }
    }
    if (sup.codec) queueRead(Table::T1, req::getCodec());
    if (sup.volume) queueRead(Table::T1, req::getVolume());
    if (sup.noise_control) queueRead(Table::T1, req::getNcAsm(*v.ncAsmType));
    if (sup.devices) {
        queueRead(Table::T2, req::getPairingMode(v.deviceListType));
        queueRead(Table::T2, req::getPairedDevices(v.deviceListType));
    }
    if (state_.hasFunction(Table::T2, fn2::SOURCE_SWITCH_CONTROL)) queueRead(Table::T2, req::getSourceSwitchAuto());
    if (sup.speak_to_chat) {
        queueRead(Table::T1, req::getSpeakToChat());
        queueRead(Table::T1, req::getSpeakToChatConfig());
    }
    if (sup.eq_preset) {
        queueRead(Table::T1, req::getEqStatus());
        queueRead(Table::T1, req::getEqValue());
    }
    if (sup.connection_quality) queueRead(Table::T1, req::getConnectionQuality(v.connectionQualityType));
    if (sup.dsee) {
        queueRead(Table::T1, req::getUpscalingStatus());
        queueRead(Table::T1, req::getUpscaling());
    }
    if (t1(fn1::ASSIGNABLE_SETTING) || t1(fn1::ASSIGNABLE_SETTING_WITH_LIMITATION)) {
        queueRead(Table::T1, req::getAssignableValue(v.assignableType));
    }
    if (sup.nc_amb_button) queueRead(Table::T1, req::getNcAmbButton());
    if (sup.auto_power_off) queueRead(Table::T1, req::getAutoPowerOff(v.autoPowerOffType));
    if (sup.ear_detection) queueRead(Table::T1, req::getEarDetection());
    if (sup.worn) queueRead(Table::T2, req::getWornStatus());
    if (sup.voice_guidance) queueRead(Table::T2, req::getVoiceGuidance(v.voiceGuidanceType));
    if (sup.voice_guidance_volume) queueRead(Table::T2, req::getVoiceGuidanceVolume());
    if (sup.quick_access) queueRead(Table::T1, req::getQuickAccess());
    if (t1(fn1::CONNECTION_MODE_CLASSIC_AUDIO_LE_AUDIO)) {
        queueRead(Table::T1, req::getConnectionModeCapability());
        queueRead(Table::T1, req::getConnectionModeStatus());
    }
    queueTail("init sequence complete");
}

void HeadsetSession::queueFallbackReads() {
    const auto model = state_.model();
    if (model == kModelWh || model == kModelWf) {
        if (model == kModelWh) {
            queueRead(Table::T1, req::getBattery(0x00));
        } else {
            queueRead(Table::T1, req::getBattery(0x09));
            queueRead(Table::T1, req::getBattery(0x0A));
        }
        queueRead(Table::T1, req::getNcAsm(0x17));
        queueRead(Table::T1, req::getEqStatus());
        queueRead(Table::T1, req::getEqValue());
        queueRead(Table::T1, req::getSpeakToChat());
        queueRead(Table::T1, req::getSpeakToChatConfig());
        if (state_.table2Supported) {
            queueRead(Table::T2, req::getVoiceGuidance(0x01));
            if (model == kModelWf) queueRead(Table::T2, req::getVoiceGuidanceVolume());
        }
        queueRead(Table::T1, req::getAutoPowerOff(0x05));
        if (model == kModelWh) queueRead(Table::T1, req::getNcAmbButton());
        queueRead(Table::T1, req::getEarDetection());
        queueRead(Table::T1, req::getVolume());
    }
    queueTail("init sequence complete (fallback feature list)");
}

void HeadsetSession::queueTail(const char* doneMessage) {
    Step operationLog;
    operationLog.payload = req::enableOperationLog();
    queueStep(std::move(operationLog));
    queueLocal([this, doneMessage] {
        initDone_ = true;
        nextSync_ = now_ + std::chrono::milliseconds(timing_.syncIntervalMs);
        log(doneMessage);
    });
}

// ---------------------------------------------------------------------------
// Inbound
// ---------------------------------------------------------------------------

void HeadsetSession::onPayload(Table table, const Bytes& payload) {
    InboundEvents events;
    if (!applyInbound(table, payload, state_, events)) {
        log("unhandled payload: " + hexBytes(payload));
    }
    handleEvents(table, payload, events);
    updateLateSupport();
    publishIfChanged();
}

void HeadsetSession::handleEvents(Table table, const Bytes& payload, const InboundEvents& events) {
    if (events.alert) {
        const auto& a = *events.alert;
        log("fixed message alert: type " + hexByte(a.messageType) + ", action " + hexByte(a.actionType));
        if (a.actionType == 0x01 && now_ < alertWindowUntil_) {
            log("confirming alert " + hexByte(a.messageType));
            LinkRequest request;
            request.payload = req::answerAlert(a.messageType, true);
            request.priority = true;
            request.ackTimeoutMs = timing_.commandAckMs;
            request.maxAttempts = timing_.commandAttempts;
            link_.submit(std::move(request), now_);
            alertWindowUntil_ = {};
        }
    }
    if (events.wearLogLine && state_.supported && state_.supported->worn) {
        submitBackground(Table::T2, req::getWornStatus());
    }
    if (events.sourceSwitch) {
        log("source switch to " + events.sourceSwitch->address + ": result " + hexByte(events.sourceSwitch->result));
        if (state_.supported && state_.supported->devices) {
            submitBackground(Table::T2, req::getPairedDevices(state_.variants.deviceListType));
        }
    }

    if (table == Table::T1 && payload.size() >= 2 && payload[0] == 0xD1 && state_.supported && !state_.fallback) {
        uint8_t slot = payload[1];
        if (slot >= fn1::GENERAL_SETTING_FIRST && slot <= fn1::GENERAL_SETTING_LAST) {
            const auto& gs = state_.generalSettings[slot - fn1::GENERAL_SETTING_FIRST];
            if (gs.advertised && gs.type == 0x00 && !gs.value) {
                submitBackground(Table::T1, req::getGeneralSettingValue(slot));
            }
        }
    }
}

void HeadsetSession::updateLateSupport() {
    if (!state_.supported || state_.fallback) return;
    Supported derived = deriveSupported(state_);
    Supported& current = *state_.supported;
    if (!current.eq_custom && derived.eq_custom) current.eq_custom = true;
    if (!current.button_function && derived.button_function) current.button_function = true;
    if (!current.touch_function && derived.touch_function) current.touch_function = true;
    if (!current.quick_access && derived.quick_access) current.quick_access = true;
    if (!current.multipoint && derived.multipoint) {
        current.multipoint = true;
        selectVariants(state_);
    }
    if (!current.touch_panel && derived.touch_panel) {
        current.touch_panel = true;
        selectVariants(state_);
    }
    if (!current.sidetone && derived.sidetone) {
        current.sidetone = true;
        selectVariants(state_);
    }
}

void HeadsetSession::publishIfChanged() {
    std::string json = state_.connected ? headsetJson() : std::string();
    if (json == lastPublished_) return;
    lastPublished_ = std::move(json);
    if (hooks_.changed) hooks_.changed();
}

std::string HeadsetSession::headsetJson() const {
    return protocol::headsetJson(state_);
}

std::string HeadsetSession::probeJson() const {
    auto frames = link_.recentFrames();
    return protocol::probeJson(state_, frames);
}

// ---------------------------------------------------------------------------
// Requests outside the init sequence
// ---------------------------------------------------------------------------

void HeadsetSession::submitBackground(Table table, Bytes payload) {
    LinkRequest request;
    request.table = table;
    request.reply = replyFor(table, payload);
    request.payload = std::move(payload);
    request.ackTimeoutMs = timing_.initAckMs;
    request.maxAttempts = timing_.initAttempts;
    request.replyTimeoutMs = timing_.replyTimeoutMs;
    link_.submit(std::move(request), now_);
}

void HeadsetSession::submitCommand(Table table, Bytes payload, Reply done, std::function<void()> afterOk) {
    LinkRequest request;
    request.table = table;
    request.payload = std::move(payload);
    request.priority = true;
    request.ackTimeoutMs = timing_.commandAckMs;
    request.maxAttempts = timing_.commandAttempts;
    const uint64_t gen = gen_;
    request.done = [this, gen, done = std::move(done), afterOk = std::move(afterOk)](LinkResult r) {
        if (r == LinkResult::Cancelled) {
            done("ERR not connected");
            return;
        }
        if (r != LinkResult::Ok) {
            done("ERR device timeout");
            return;
        }
        done("OK");
        if (gen == gen_ && afterOk) afterOk();
    };
    link_.submit(std::move(request), now_);
}

void HeadsetSession::refreshThen(bool needed, Table table, Bytes getPayload, std::function<void()> then) {
    if (!needed) {
        then();
        return;
    }
    LinkRequest request;
    request.table = table;
    request.reply = replyFor(table, getPayload);
    request.payload = std::move(getPayload);
    request.priority = true;
    request.ackTimeoutMs = timing_.commandAckMs;
    request.maxAttempts = 1;
    request.replyTimeoutMs = timing_.replyTimeoutMs;
    request.done = [then = std::move(then)](LinkResult) { then(); };
    link_.submit(std::move(request), now_);
}

// ---------------------------------------------------------------------------
// Verbs
// ---------------------------------------------------------------------------

void HeadsetSession::execute(const cmd::Command& command, Reply done, Clock::time_point now) {
    now_ = now;
    if (!state_.connected) { done("ERR not connected"); return; }
    switch (command.verb) {
    case Verb::Probe:
        done(probeJson());
        return;
    case Verb::Status:
    case Verb::TestSetBattery:
    case Verb::TestDisconnect:
    case Verb::TestReconnect:
        done("ERR unknown command '" + command.name + "'");
        return;
    default:
        break;
    }

    if (!state_.supported) { done("ERR not ready"); return; }
    const char* key = featureKey(command.verb);
    if (!key || !featureEnabled(*state_.supported, command.verb)) {
        done(std::string("ERR unsupported ") + (key ? key : command.name.c_str()));
        return;
    }

    const std::string& arg = command.args.empty() ? std::string() : command.args[0];
    auto unsupportedValue = [&](const std::string& value) {
        done(std::string("ERR unsupported ") + key + " " + value);
    };

    switch (command.verb) {
    case Verb::Eq: {
        auto presets = state_.eqPresetNames();
        if (presets && !contains(*presets, arg)) { unsupportedValue(arg); return; }
        break;
    }
    case Verb::AutoPowerOff:
        if (!contains(state_.autoPowerOffOptions(), arg)) { unsupportedValue(arg); return; }
        break;
    case Verb::ButtonFunction: {
        auto options = state_.buttonFunctionOptions();
        if (options && !contains(*options, arg)) { unsupportedValue(arg); return; }
        break;
    }
    case Verb::TouchFunction: {
        const std::string& value = command.args[1];
        auto options = state_.touchFunctionOptions(sideKey(arg));
        if (options && !contains(*options, value)) { unsupportedValue(arg + " " + value); return; }
        break;
    }
    case Verb::QuickAccess: {
        const std::string& value = command.args[1];
        auto options = state_.quickAccessOptions(command.args[0] == "double" ? kActionDoubleTap : kActionTripleTap);
        if (options && !contains(*options, value)) { unsupportedValue(arg + " " + value); return; }
        break;
    }
    case Verb::Source: {
        bool known = false;
        if (state_.devices) {
            for (const auto& d : *state_.devices) {
                if (d.address == arg && d.connectionStatus != 0x00) known = true;
            }
        }
        if (!known) { unsupportedValue(arg); return; }
        break;
    }
    default:
        break;
    }

    runSetter(command, std::move(done));
}

void HeadsetSession::runSetter(const cmd::Command& c, Reply done) {
    const Variants& v = state_.variants;
    const std::string arg = c.args.empty() ? std::string() : c.args[0];
    const bool on = (arg == "on");
    const uint64_t gen = gen_;

    switch (c.verb) {
    case Verb::Noise:
    case Verb::AmbientLevel:
    case Verb::FocusVoice: {
        const uint8_t type = v.ncAsmType.value_or(0x17);
        const bool unknown = !state_.ncTotalEffect || !state_.ncMode || !state_.ncAmbientMode || !state_.ncLevel;
        refreshThen(unknown, Table::T1, req::getNcAsm(type), [this, gen, c, type, done = std::move(done)]() mutable {
            if (gen != gen_) { done("ERR not connected"); return; }
            NcAsmParams p;
            p.type = type;
            p.totalEffect = state_.ncTotalEffect.value_or(0x01);
            p.mode = state_.ncMode.value_or(0x00);
            p.ambientMode = state_.ncAmbientMode.value_or(0x00);
            p.level = state_.ncLevel.value_or(20);
            p.autoAmbient = state_.ncAutoAmbient.value_or(0x00);
            p.noiseAdaptive = state_.ncNoiseAdaptive.value_or(0x00);
            if (c.verb == Verb::Noise) {
                const std::string& mode = c.args[0];
                if (mode == "anc") { p.totalEffect = 0x01; p.mode = 0x00; }
                else if (mode == "ambient") { p.totalEffect = 0x01; p.mode = 0x01; }
                else { p.totalEffect = 0x00; }
            } else if (c.verb == Verb::AmbientLevel) {
                p.totalEffect = 0x01;
                p.mode = 0x01;
                p.level = static_cast<uint8_t>(c.numbers[0]);
            } else {
                p.ambientMode = (c.args[0] == "on") ? 0x01 : 0x00;
            }
            submitCommand(Table::T1, req::setNcAsm(p), std::move(done));
        });
        return;
    }
    case Verb::Eq:
        submitCommand(Table::T1, req::setEqPreset(*eqPresetId(arg)), std::move(done),
                      [this] { submitBackground(Table::T1, req::getEqValue()); });
        return;
    case Verb::EqCustom: {
        std::array<int, 5> bands{c.numbers[0], c.numbers[1], c.numbers[2], c.numbers[3], c.numbers[4]};
        submitCommand(Table::T1, req::setEqCustom(bands, c.numbers[5]), std::move(done),
                      [this] { submitBackground(Table::T1, req::getEqValue()); });
        return;
    }
    case Verb::Dsee:
        submitCommand(Table::T1, req::setUpscaling(on), std::move(done));
        return;
    case Verb::SpeakToChat:
        submitCommand(Table::T1, req::setSpeakToChat(on), std::move(done));
        return;
    case Verb::SpeakToChatConfig:
        submitCommand(Table::T1,
                      req::setSpeakToChatConfig(*speakToChatSensitivityValue(c.args[0]),
                                                *speakToChatTimeoutValue(c.args[1])),
                      std::move(done));
        return;
    case Verb::VoiceGuidance: {
        const uint8_t type = v.voiceGuidanceType;
        submitCommand(Table::T2, req::setVoiceGuidance(type, on, state_.voiceGuidanceLanguage.value_or(0x00)),
                      std::move(done), [this, type] { submitBackground(Table::T2, req::getVoiceGuidance(type)); });
        return;
    }
    case Verb::VoiceGuidanceVolume:
        submitCommand(Table::T2, req::setVoiceGuidanceVolume(c.numbers[0]), std::move(done),
                      [this] { submitBackground(Table::T2, req::getVoiceGuidanceVolume()); });
        return;
    case Verb::AutoPowerOff:
        submitCommand(Table::T1, req::setAutoPowerOff(v.autoPowerOffType, *autoPowerOffValue(arg)), std::move(done));
        return;
    case Verb::NcAmbButton:
        submitCommand(Table::T1, req::setNcAmbButton(*ncAmbButtonValue(arg)), std::move(done));
        return;
    case Verb::ButtonFunction: {
        const uint8_t type = v.assignableType;
        refreshThen(!state_.assignableValue, Table::T1, req::getAssignableValue(type),
                    [this, gen, arg, type, done = std::move(done)]() mutable {
            if (gen != gen_) { done("ERR not connected"); return; }
            auto idx = state_.ncAmbKeyIndex();
            if (!idx || !state_.assignableValue || *idx >= state_.assignableValue->size()) {
                done("ERR not ready");
                return;
            }
            std::vector<uint8_t> presets = *state_.assignableValue;
            presets[*idx] = *buttonFunctionValue(arg);
            submitCommand(Table::T1, req::setAssignableValue(type, presets), std::move(done));
        });
        return;
    }
    case Verb::TouchFunction: {
        const uint8_t type = v.assignableType;
        const uint8_t key = sideKey(arg);
        const std::string value = c.args[1];
        refreshThen(!state_.assignableValue, Table::T1, req::getAssignableValue(type),
                    [this, gen, key, value, type, done = std::move(done)]() mutable {
            if (gen != gen_) { done("ERR not connected"); return; }
            auto idx = state_.assignableKeyIndex(key);
            if (!idx || !state_.assignableValue || *idx >= state_.assignableValue->size()) {
                done("ERR not ready");
                return;
            }
            std::vector<uint8_t> presets = *state_.assignableValue;
            presets[*idx] = state_.touchPresetFor(key, value);
            submitCommand(Table::T1, req::setAssignableValue(type, presets), std::move(done),
                          [this, type] { submitBackground(Table::T1, req::getAssignableValue(type)); });
        });
        return;
    }
    case Verb::EarDetect:
        submitCommand(Table::T1, req::setEarDetection(on), std::move(done));
        return;
    case Verb::Volume:
        submitCommand(Table::T1, req::setVolume(c.numbers[0]), std::move(done));
        return;
    case Verb::ConnectionQuality:
        alertWindowUntil_ = now_ + std::chrono::milliseconds(timing_.alertWindowMs);
        submitCommand(Table::T1, req::setConnectionQuality(v.connectionQualityType, *connectionQualityValue(arg)),
                      std::move(done));
        return;
    case Verb::Multipoint:
        alertWindowUntil_ = now_ + std::chrono::milliseconds(timing_.alertWindowMs);
        submitCommand(Table::T1, req::setGeneralSettingBool(*v.multipointSlot, on), std::move(done));
        return;
    case Verb::Source:
        submitCommand(Table::T2, req::setSourceSwitch(arg), std::move(done));
        return;
    case Verb::TouchPanel:
        submitCommand(Table::T1, req::setGeneralSettingBool(*v.touchPanelSlot, on), std::move(done));
        return;
    case Verb::Sidetone:
        submitCommand(Table::T1, req::setGeneralSettingBool(*v.sidetoneSlot, on), std::move(done));
        return;
    case Verb::QuickAccess: {
        const uint8_t action = arg == "double" ? kActionDoubleTap : kActionTripleTap;
        const uint8_t function = *quickAccessValue(c.args[1]);
        refreshThen(!state_.quickAccessValue, Table::T1, req::getQuickAccess(),
                    [this, gen, action, function, done = std::move(done)]() mutable {
            if (gen != gen_) { done("ERR not connected"); return; }
            auto idx = state_.quickAccessIndex(action);
            if (!idx || !state_.quickAccessValue || *idx >= state_.quickAccessValue->size()) {
                done("ERR not ready");
                return;
            }
            std::vector<uint8_t> functions = *state_.quickAccessValue;
            functions[*idx] = function;
            submitCommand(Table::T1, req::setQuickAccess(functions), std::move(done));
        });
        return;
    }
    default:
        done("ERR unknown command '" + c.name + "'");
        return;
    }
}

void HeadsetSession::log(const std::string& line) const {
    if (hooks_.log) hooks_.log(line);
}

} // namespace omarchy::sony
