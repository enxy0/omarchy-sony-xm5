#include "Commands.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace omarchy::sony::cmd {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::optional<int> parseInt(std::string_view text) {
    if (text.empty()) return std::nullopt;
    int value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

bool oneOf(const std::string& value, std::initializer_list<std::string_view> options) {
    return std::find(options.begin(), options.end(), value) != options.end();
}

struct VerbSpec {
    std::string_view name;
    Verb verb;
    std::string_view usage;
};

constexpr VerbSpec kVerbs[] = {
    {"status", Verb::Status, "status"},
    {"probe", Verb::Probe, "probe"},
    {"noise", Verb::Noise, "noise anc|ambient|off"},
    {"ambient-level", Verb::AmbientLevel, "ambient-level <n>"},
    {"focus-voice", Verb::FocusVoice, "focus-voice on|off"},
    {"eq", Verb::Eq, "eq <preset>"},
    {"dsee", Verb::Dsee, "dsee on|off"},
    {"speak-to-chat", Verb::SpeakToChat, "speak-to-chat on|off"},
    {"speak-to-chat-config", Verb::SpeakToChatConfig, "speak-to-chat-config <sensitivity> <timeout>"},
    {"voice-guidance", Verb::VoiceGuidance, "voice-guidance on|off"},
    {"voice-guidance-volume", Verb::VoiceGuidanceVolume, "voice-guidance-volume <n>"},
    {"auto-power-off", Verb::AutoPowerOff, "auto-power-off <value>"},
    {"nc-amb-button", Verb::NcAmbButton, "nc-amb-button <value>"},
    {"button-function", Verb::ButtonFunction, "button-function <value>"},
    {"ear-detect", Verb::EarDetect, "ear-detect on|off"},
    {"ear-detection", Verb::EarDetect, "ear-detect on|off"},
    {"volume", Verb::Volume, "volume <n>"},
    {"connection-quality", Verb::ConnectionQuality, "connection-quality sound|connection"},
    {"multipoint", Verb::Multipoint, "multipoint on|off"},
    {"source", Verb::Source, "source <address>"},
    {"_set_battery", Verb::TestSetBattery, "_set_battery <level> [charging]"},
    {"_disconnect", Verb::TestDisconnect, "_disconnect"},
    {"_reconnect", Verb::TestReconnect, "_reconnect"},
};

constexpr std::string_view kEqCustomUsage = "eq custom <b1> <b2> <b3> <b4> <b5> <clear_bass>";

const VerbSpec* findVerb(const std::string& name) {
    for (const auto& spec : kVerbs) {
        if (spec.name == name) return &spec;
    }
    return nullptr;
}

ParseResult usageError(std::string_view text) {
    return ParseResult{std::nullopt, "usage: " + std::string(text)};
}

} // namespace

std::string Command::line() const {
    std::string out = name;
    for (const auto& a : args) {
        out.push_back(' ');
        out += a;
    }
    return out;
}

bool Command::isSetter() const {
    switch (verb) {
        case Verb::Status:
        case Verb::Probe:
        case Verb::TestSetBattery:
        case Verb::TestDisconnect:
        case Verb::TestReconnect:
            return false;
        default:
            return true;
    }
}

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i > start) tokens.emplace_back(line.substr(start, i - start));
    }
    return tokens;
}

std::optional<std::string> normalizeAddress(std::string_view text) {
    if (text.size() != 17) return std::nullopt;
    std::string out;
    out.reserve(17);
    for (size_t i = 0; i < 17; ++i) {
        char c = text[i];
        if (i % 3 == 2) {
            if (c != ':') return std::nullopt;
            out.push_back(':');
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string_view usage(Verb verb) {
    if (verb == Verb::EqCustom) return kEqCustomUsage;
    for (const auto& spec : kVerbs) {
        if (spec.verb == verb) return spec.usage;
    }
    return "";
}

ParseResult parse(const std::vector<std::string>& tokens, bool allowTestVerbs) {
    if (tokens.empty()) return ParseResult{std::nullopt, "empty command"};

    const std::string verbName = lower(tokens[0]);
    const VerbSpec* spec = findVerb(verbName);
    if (!spec || (!allowTestVerbs && verbName.front() == '_')) {
        return ParseResult{std::nullopt, "unknown command '" + verbName + "'"};
    }

    Command c;
    c.verb = spec->verb;
    c.name = std::string(spec->verb == Verb::EarDetect ? "ear-detect" : spec->name);
    const size_t argc = tokens.size() - 1;

    auto word = [&](size_t i) { return lower(tokens[i]); };
    auto single = [&](std::initializer_list<std::string_view> options) -> ParseResult {
        if (argc != 1) return usageError(spec->usage);
        std::string v = word(1);
        if (!oneOf(v, options)) return usageError(spec->usage);
        c.args.push_back(v);
        return ParseResult{c, {}};
    };
    auto integer = [&](int lo, int hi) -> ParseResult {
        if (argc != 1) return usageError(spec->usage);
        auto v = parseInt(tokens[1]);
        if (!v || *v < lo || *v > hi) return usageError(spec->usage);
        c.numbers.push_back(*v);
        c.args.push_back(std::to_string(*v));
        return ParseResult{c, {}};
    };

    switch (spec->verb) {
    case Verb::Status:
    case Verb::Probe:
    case Verb::TestDisconnect:
    case Verb::TestReconnect:
        if (argc != 0) return usageError(spec->usage);
        return ParseResult{c, {}};

    case Verb::Noise:
        return single({"anc", "ambient", "off"});
    case Verb::AmbientLevel:
        return integer(1, 20);
    case Verb::FocusVoice:
    case Verb::Dsee:
    case Verb::SpeakToChat:
    case Verb::VoiceGuidance:
    case Verb::EarDetect:
    case Verb::Multipoint:
        return single({"on", "off"});
    case Verb::VoiceGuidanceVolume:
        return integer(-2, 2);
    case Verb::Volume:
        return integer(0, 30);
    case Verb::ConnectionQuality:
        return single({"sound", "connection"});
    case Verb::AutoPowerOff:
        return single({"off", "5min", "15min", "30min", "60min", "180min", "when_taken_off"});
    case Verb::NcAmbButton:
        return single({"nc_ambient_off", "nc_ambient", "nc_off", "ambient_off"});
    case Verb::ButtonFunction:
        return single({"ambient_sound_control", "voice_recognition", "google_assistant", "amazon_alexa",
                       "quick_access", "none"});

    case Verb::SpeakToChatConfig: {
        if (argc != 2) return usageError(spec->usage);
        std::string sensitivity = word(1);
        std::string timeout = word(2);
        if (!oneOf(sensitivity, {"auto", "high", "low"}) || !oneOf(timeout, {"short", "standard", "long", "off"})) {
            return usageError(spec->usage);
        }
        c.args = {sensitivity, timeout};
        return ParseResult{c, {}};
    }

    case Verb::Eq: {
        if (argc >= 2 && word(1) == "custom") {
            c.verb = Verb::EqCustom;
            if (argc != 7) return usageError(kEqCustomUsage);
            c.args.emplace_back("custom");
            for (size_t i = 2; i <= 7; ++i) {
                auto v = parseInt(tokens[i]);
                if (!v || *v < -10 || *v > 10) return usageError(kEqCustomUsage);
                c.numbers.push_back(*v);
                c.args.push_back(std::to_string(*v));
            }
            return ParseResult{c, {}};
        }
        return single({"off", "bright", "excited", "mellow", "relaxed", "vocal", "treble", "bass", "speech",
                       "custom", "user1", "user2", "user3", "user4", "user5"});
    }

    case Verb::Source: {
        if (argc != 1) return usageError(spec->usage);
        auto address = normalizeAddress(tokens[1]);
        if (!address) return usageError(spec->usage);
        c.args.push_back(*address);
        return ParseResult{c, {}};
    }

    case Verb::TestSetBattery: {
        if (argc < 1 || argc > 2) return usageError(spec->usage);
        auto level = parseInt(tokens[1]);
        if (!level) return usageError(spec->usage);
        c.numbers.push_back(*level);
        c.args.push_back(std::to_string(*level));
        bool charging = argc == 2 && oneOf(word(2), {"true", "1", "yes"});
        c.numbers.push_back(charging ? 1 : 0);
        c.args.emplace_back(charging ? "true" : "false");
        return ParseResult{c, {}};
    }

    case Verb::EqCustom:
        break;
    }
    return usageError(spec->usage);
}

ParseResult parseLine(std::string_view line, bool allowTestVerbs) {
    return parse(tokenize(line), allowTestVerbs);
}

} // namespace omarchy::sony::cmd
