#pragma once

#include "HeadphoneState.hpp"
#include "MDRProtocol.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace omarchy::sony::protocol {

enum class LinkResult { Ok, NoAck, NoReply, Cancelled };

const char* linkResultName(LinkResult result) noexcept;

struct ReplyMatch {
    Table table{Table::T1};
    uint8_t command{0};
    uint8_t type{0};
};

struct LinkRequest {
    Table table{Table::T1};
    Bytes payload;
    bool priority{false};
    int ackTimeoutMs{1000};
    int maxAttempts{3};
    std::optional<ReplyMatch> reply;
    int replyTimeoutMs{700};
    std::function<void(LinkResult)> done;
};

std::optional<ReplyMatch> replyFor(Table table, const Bytes& payload);

class MdrLink {
public:
    using Clock = std::chrono::steady_clock;
    using Writer = std::function<bool(const Bytes& frame)>;
    using Dispatch = std::function<void(Table table, const Bytes& payload)>;
    using Logger = std::function<void(const std::string& line)>;

    static constexpr size_t kRecentFrameLimit = 64;

    MdrLink(Writer writer, Dispatch dispatch, Logger logger = {});

    void reset();
    void submit(LinkRequest request, Clock::time_point now);
    void onBytes(std::span<const uint8_t> data, Clock::time_point now);
    void tick(Clock::time_point now);

    [[nodiscard]] std::optional<Clock::time_point> nextDeadline() const;
    [[nodiscard]] bool idle() const noexcept { return !current_ && queue_.empty(); }
    [[nodiscard]] uint8_t sequence() const noexcept { return seq_; }
    [[nodiscard]] std::vector<FrameRecord> recentFrames() const { return {frames_.begin(), frames_.end()}; }

private:
    struct InFlight {
        LinkRequest request;
        Bytes frame;
        int attempts{0};
        bool acked{false};
        bool replied{false};
        Clock::time_point deadline{};
    };

    void pump(Clock::time_point now);
    void transmit(Clock::time_point now);
    void finish(LinkResult result, Clock::time_point now);
    void handleFrame(const UnpackedFrame& frame, Clock::time_point now);
    void record(bool tx, PacketType type, uint8_t seq, const Bytes& payload);
    void log(const std::string& line) const;

    Writer writer_;
    Dispatch dispatch_;
    Logger logger_;
    StreamFramer framer_;
    std::deque<LinkRequest> queue_;
    std::optional<InFlight> current_;
    std::deque<FrameRecord> frames_;
    uint8_t seq_{0};
    uint64_t generation_{0};
};

} // namespace omarchy::sony::protocol
