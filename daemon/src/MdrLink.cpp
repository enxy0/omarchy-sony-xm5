#include "MdrLink.hpp"

#include <utility>

namespace omarchy::sony::protocol {

const char* linkResultName(LinkResult result) noexcept {
    switch (result) {
        case LinkResult::Ok:        return "ok";
        case LinkResult::NoAck:     return "no-ack";
        case LinkResult::NoReply:   return "no-reply";
        case LinkResult::Cancelled: return "cancelled";
    }
    return "?";
}

std::optional<ReplyMatch> replyFor(Table table, const Bytes& payload) {
    if (payload.size() < 2) return std::nullopt;
    return ReplyMatch{table, static_cast<uint8_t>(payload[0] + 1), payload[1]};
}

MdrLink::MdrLink(Writer writer, Dispatch dispatch, Logger logger)
    : writer_(std::move(writer)), dispatch_(std::move(dispatch)), logger_(std::move(logger)) {}

void MdrLink::reset() {
    ++generation_;
    std::deque<LinkRequest> pending;
    pending.swap(queue_);
    std::optional<InFlight> inFlight;
    inFlight.swap(current_);
    seq_ = 0;
    framer_.reset();
    frames_.clear();

    if (inFlight && inFlight->request.done) inFlight->request.done(LinkResult::Cancelled);
    for (auto& request : pending) {
        if (request.done) request.done(LinkResult::Cancelled);
    }
}

void MdrLink::submit(LinkRequest request, Clock::time_point now) {
    if (request.priority) {
        auto it = queue_.begin();
        while (it != queue_.end() && it->priority) ++it;
        queue_.insert(it, std::move(request));
    } else {
        queue_.push_back(std::move(request));
    }
    pump(now);
}

void MdrLink::pump(Clock::time_point now) {
    if (current_ || queue_.empty()) return;
    InFlight next;
    next.request = std::move(queue_.front());
    queue_.pop_front();
    next.frame = packFrame(packetTypeFor(next.request.table), seq_, next.request.payload);
    current_ = std::move(next);
    transmit(now);
}

void MdrLink::transmit(Clock::time_point now) {
    auto& c = *current_;
    c.attempts++;
    c.deadline = now + std::chrono::milliseconds(c.request.ackTimeoutMs);
    record(true, packetTypeFor(c.request.table), seq_, c.request.payload);
    log(std::string("TX ") + (c.request.table == Table::T1 ? "T1" : "T2") + " seq=" + std::to_string(seq_) +
        (c.attempts > 1 ? " retry=" + std::to_string(c.attempts - 1) : "") + ": " + hexBytes(c.request.payload));
    const Bytes frame = c.frame;
    const uint64_t gen = generation_;
    const bool written = writer_(frame);
    if (gen != generation_ || !current_) return;
    if (!written) {
        finish(LinkResult::NoAck, now);
    }
}

void MdrLink::finish(LinkResult result, Clock::time_point now) {
    if (!current_) return;
    InFlight done = std::move(*current_);
    current_.reset();
    if (result != LinkResult::Ok) {
        log(std::string("request ") + hexBytes(done.request.payload) + " failed: " + linkResultName(result));
    }
    uint64_t gen = generation_;
    if (done.request.done) done.request.done(result);
    if (gen == generation_) pump(now);
}

void MdrLink::onBytes(std::span<const uint8_t> data, Clock::time_point now) {
    framer_.append(data);
    uint64_t gen = generation_;
    while (auto raw = framer_.nextFrame()) {
        auto frame = unpackFrame(*raw);
        if (!frame) {
            log("RX dropped malformed frame: " + hexBytes(*raw));
            continue;
        }
        handleFrame(*frame, now);
        if (gen != generation_) return;
    }
}

void MdrLink::handleFrame(const UnpackedFrame& frame, Clock::time_point now) {
    if (frame.type == PacketType::ACK) {
        const uint8_t expected = static_cast<uint8_t>(1 - seq_);
        if (!current_ || current_->acked || frame.seq != expected) {
            log("RX stale ACK seq=" + std::to_string(frame.seq));
            return;
        }
        seq_ = expected;
        current_->acked = true;
        if (!current_->request.reply || current_->replied) {
            finish(LinkResult::Ok, now);
        } else {
            current_->deadline = now + std::chrono::milliseconds(current_->request.replyTimeoutMs);
        }
        return;
    }

    auto table = tableFor(frame.type);
    if (!table) {
        log("RX unknown packet type " + hexByte(static_cast<uint8_t>(frame.type)));
        return;
    }

    const uint64_t gen = generation_;
    writer_(serializeACK(frame.seq));
    if (gen != generation_) return;
    record(false, frame.type, frame.seq, frame.payload);
    log(std::string("RX ") + (*table == Table::T1 ? "T1" : "T2") + " seq=" + std::to_string(frame.seq) + ": " +
        hexBytes(frame.payload));

    dispatch_(*table, frame.payload);
    if (gen != generation_) return;

    if (current_ && current_->request.reply && !current_->replied && frame.payload.size() >= 2) {
        const auto& m = *current_->request.reply;
        if (m.table == *table && m.command == frame.payload[0] && m.type == frame.payload[1]) {
            current_->replied = true;
            if (current_->acked) finish(LinkResult::Ok, now);
        }
    }
}

void MdrLink::tick(Clock::time_point now) {
    if (!current_ || now < current_->deadline) return;
    if (current_->acked) {
        finish(LinkResult::NoReply, now);
        return;
    }
    if (current_->attempts < current_->request.maxAttempts) {
        transmit(now);
    } else {
        finish(LinkResult::NoAck, now);
    }
}

std::optional<MdrLink::Clock::time_point> MdrLink::nextDeadline() const {
    if (!current_) return std::nullopt;
    return current_->deadline;
}

void MdrLink::record(bool tx, PacketType type, uint8_t seq, const Bytes& payload) {
    frames_.push_back(FrameRecord{tx, type, seq, payload});
    while (frames_.size() > kRecentFrameLimit) frames_.pop_front();
}

void MdrLink::log(const std::string& line) const {
    if (logger_) logger_(line);
}

} // namespace omarchy::sony::protocol
