#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omarchy::sony::cmd {

enum class Verb {
    Status,
    Probe,
    Noise,
    AmbientLevel,
    FocusVoice,
    Eq,
    EqCustom,
    Dsee,
    SpeakToChat,
    SpeakToChatConfig,
    VoiceGuidance,
    VoiceGuidanceVolume,
    AutoPowerOff,
    NcAmbButton,
    ButtonFunction,
    EarDetect,
    Volume,
    ConnectionQuality,
    Multipoint,
    Source,
    TestSetBattery,
    TestDisconnect,
    TestReconnect,
};

struct Command {
    Verb verb{Verb::Status};
    std::string name;
    std::vector<std::string> args;
    std::vector<int> numbers;

    [[nodiscard]] std::string line() const;
    [[nodiscard]] bool isSetter() const;
};

struct ParseResult {
    std::optional<Command> command;
    std::string error;
};

std::vector<std::string> tokenize(std::string_view line);
ParseResult parse(const std::vector<std::string>& tokens, bool allowTestVerbs);
ParseResult parseLine(std::string_view line, bool allowTestVerbs);

std::string_view usage(Verb verb);
std::optional<std::string> normalizeAddress(std::string_view text);

} // namespace omarchy::sony::cmd
