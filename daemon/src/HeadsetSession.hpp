#pragma once

#include "Commands.hpp"
#include "HeadphoneState.hpp"
#include "MdrLink.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace omarchy::sony {

// One connected headset: its MDR link, sequence number, init sequence, state
// and request queue (contract 2.1).
class HeadsetSession {
public:
    using Clock = protocol::MdrLink::Clock;
    using Reply = std::function<void(std::string reply)>;

    struct Hooks {
        std::function<bool(const protocol::Bytes& bytes)> write;
        std::function<void()> changed;
        std::function<void(const std::string& reason)> dropLink;
        std::function<void(const std::string& line)> log;
        std::function<int64_t()> unixNow;
    };

    struct Timing {
        int handshakeAckMs{1000};
        int handshakeAttempts{10};
        int initAckMs{1000};
        int initAttempts{3};
        int replyTimeoutMs{700};
        int commandAckMs{900};
        int commandAttempts{2};
        int syncIntervalMs{30000};
        int alertWindowMs{15000};
    };

    explicit HeadsetSession(Hooks hooks);
    HeadsetSession(Hooks hooks, Timing timing);

    HeadsetSession(const HeadsetSession&) = delete;
    HeadsetSession& operator=(const HeadsetSession&) = delete;

    void onTransportConnected(const std::string& alias, const std::string& bluezName,
                              const std::optional<std::string>& address, Clock::time_point now);
    void onTransportDisconnected(Clock::time_point now);
    void onBytes(std::span<const uint8_t> data, Clock::time_point now);
    void tick(Clock::time_point now);
    [[nodiscard]] std::optional<Clock::time_point> nextDeadline() const;

    // Verbs after selector resolution: `probe` and the setters.
    void execute(const cmd::Command& command, Reply done, Clock::time_point now);

    [[nodiscard]] std::string headsetJson() const;
    [[nodiscard]] std::string probeJson() const;
    [[nodiscard]] const protocol::HeadphoneState& state() const noexcept { return state_; }
    // True while the headset belongs in `headsets[]` (handshake answered).
    [[nodiscard]] bool listed() const noexcept { return state_.connected; }
    [[nodiscard]] bool initComplete() const noexcept { return initDone_; }
    [[nodiscard]] uint8_t linkSequence() const noexcept { return link_.sequence(); }

private:
    struct Step {
        protocol::Table table{protocol::Table::T1};
        protocol::Bytes payload;
        bool expectReply{false};
        bool handshake{false};
        std::function<void(protocol::LinkResult)> after;
    };

    void queueStep(Step step);
    void queueRead(protocol::Table table, protocol::Bytes payload, std::function<void(protocol::LinkResult)> after = {});
    void queueLocal(std::function<void()> action);
    void runSteps();

    void afterHandshake(protocol::LinkResult result);
    void planCapabilities();
    void publishSupported();
    void queueValueReads();
    void queueFallbackReads();
    void queueTail(const char* doneMessage);

    void onPayload(protocol::Table table, const protocol::Bytes& payload);
    void handleEvents(protocol::Table table, const protocol::Bytes& payload, const protocol::InboundEvents& events);
    void updateLateSupport();
    void publishIfChanged();

    void submitBackground(protocol::Table table, protocol::Bytes payload);
    void submitCommand(protocol::Table table, protocol::Bytes payload, Reply done, std::function<void()> afterOk = {});
    void refreshThen(bool needed, protocol::Table table, protocol::Bytes getPayload, std::function<void()> then);
    void runSetter(const cmd::Command& command, Reply done);
    void log(const std::string& line) const;
    int64_t unixNow() const;

    Hooks hooks_;
    Timing timing_;
    protocol::MdrLink link_;
    protocol::HeadphoneState state_;
    std::deque<Step> steps_;
    bool stepRunning_{false};
    bool fallbackPending_{false};
    bool initDone_{false};
    uint64_t gen_{0};
    Clock::time_point now_{};
    Clock::time_point nextSync_{};
    Clock::time_point alertWindowUntil_{};
    std::string lastPublished_;
};

} // namespace omarchy::sony
