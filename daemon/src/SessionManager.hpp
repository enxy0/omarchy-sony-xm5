#pragma once

#include "Commands.hpp"
#include "HeadsetSession.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace omarchy::sony {

// Owns one HeadsetSession per managed headset, keyed by Bluetooth address.
// Writes the schema 3 status document (contract 3) and routes requests by
// selector (contract 2.5).
class SessionManager {
public:
    using Clock = HeadsetSession::Clock;
    using Reply = HeadsetSession::Reply;

    struct Hooks {
        // Receives the finished document; called only when the content changed.
        std::function<void(const std::string& statusJson)> write;
        std::function<int64_t()> unixNow;
    };

    struct Resolution {
        HeadsetSession* session{nullptr};
        std::string id;
        std::string error;
    };

    explicit SessionManager(Hooks hooks);

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // Creates the session of `id` (a normalized address). The manager owns it and
    // sets its `changed` and `unixNow` hooks.
    HeadsetSession& add(const std::string& id, HeadsetSession::Hooks hooks,
                        HeadsetSession::Timing timing = HeadsetSession::Timing{});
    // Closes the session (queued requests fail) and forgets it.
    void remove(const std::string& id, Clock::time_point now);
    [[nodiscard]] HeadsetSession* find(const std::string& id);

    void setAdapterAddresses(std::vector<std::string> addresses);

    // `status` and every verb that goes to one headset.
    void execute(const cmd::Command& command, Reply done, Clock::time_point now);
    // Selector resolution (contract 2.5). With `listedOnly` false, sessions that
    // are not in `headsets[]` also match (used by the test-only verbs).
    [[nodiscard]] Resolution resolve(const std::optional<std::string>& selector, bool listedOnly) const;

    void tick(Clock::time_point now);
    [[nodiscard]] std::optional<Clock::time_point> nextDeadline() const;

    [[nodiscard]] std::string statusJson(int64_t lastUpdated) const;
    [[nodiscard]] std::optional<std::string> primary() const;
    [[nodiscard]] bool allInitComplete() const;
    // Recomputes the document and hands it to `write` if it changed.
    void publish();

private:
    [[nodiscard]] std::vector<const protocol::HeadphoneState*> listedStates() const;
    int64_t unixNow() const;

    Hooks hooks_;
    std::map<std::string, std::unique_ptr<HeadsetSession>> sessions_;
    std::vector<std::string> adapters_;
    std::string lastDocument_;
};

} // namespace omarchy::sony
