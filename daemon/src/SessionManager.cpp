#include "SessionManager.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace omarchy::sony {

using protocol::HeadphoneState;

SessionManager::SessionManager(Hooks hooks) : hooks_(std::move(hooks)) {}

int64_t SessionManager::unixNow() const {
    if (hooks_.unixNow) return hooks_.unixNow();
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

HeadsetSession& SessionManager::add(const std::string& id, HeadsetSession::Hooks hooks, HeadsetSession::Timing timing) {
    hooks.changed = [this] { publish(); };
    if (!hooks.unixNow) hooks.unixNow = [this] { return unixNow(); };
    auto session = std::make_unique<HeadsetSession>(std::move(hooks), timing);
    auto& ref = *session;
    sessions_[id] = std::move(session);
    return ref;
}

void SessionManager::remove(const std::string& id, Clock::time_point now) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    std::unique_ptr<HeadsetSession> session = std::move(it->second);
    sessions_.erase(it);
    session->onTransportDisconnected(now);
    publish();
}

HeadsetSession* SessionManager::find(const std::string& id) {
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : it->second.get();
}

void SessionManager::setAdapterAddresses(std::vector<std::string> addresses) {
    for (auto& a : addresses) {
        if (auto normalized = cmd::normalizeAddress(a)) a = *normalized;
    }
    if (addresses == adapters_) return;
    adapters_ = std::move(addresses);
    publish();
}

std::vector<const HeadphoneState*> SessionManager::listedStates() const {
    std::vector<const HeadphoneState*> out;
    for (const auto& [id, session] : sessions_) {
        if (session->listed()) out.push_back(&session->state());
    }
    return protocol::orderHeadsets(std::move(out));
}

std::optional<std::string> SessionManager::primary() const {
    return protocol::choosePrimary(listedStates(), adapters_);
}

std::string SessionManager::statusJson(int64_t lastUpdated) const {
    auto ordered = listedStates();
    return protocol::statusDocument(ordered, protocol::choosePrimary(ordered, adapters_), lastUpdated);
}

void SessionManager::publish() {
    std::string document = statusJson(0);
    if (document == lastDocument_) return;
    lastDocument_ = document;
    if (!hooks_.write) return;
    constexpr std::string_view kPlaceholder = "\"last_updated\":0";
    auto pos = document.find(kPlaceholder);
    if (pos != std::string::npos) {
        document.replace(pos, kPlaceholder.size(), "\"last_updated\":" + std::to_string(unixNow()));
    }
    hooks_.write(document);
}

bool SessionManager::allInitComplete() const {
    if (sessions_.empty()) return false;
    return std::all_of(sessions_.begin(), sessions_.end(),
                       [](const auto& entry) { return entry.second->initComplete(); });
}

SessionManager::Resolution SessionManager::resolve(const std::optional<std::string>& selector, bool listedOnly) const {
    Resolution out;
    std::vector<std::pair<std::string, HeadsetSession*>> pool;
    for (const auto& [id, session] : sessions_) {
        if (!listedOnly || session->listed()) pool.emplace_back(id, session.get());
    }
    if (pool.empty()) {
        out.error = "ERR not connected";
        return out;
    }
    if (!selector) {
        auto id = primary();
        for (const auto& [sid, session] : pool) {
            if (id && sid == *id) {
                out.id = sid;
                out.session = session;
                return out;
            }
        }
        out.error = "ERR not connected";
        return out;
    }

    std::vector<std::pair<std::string, HeadsetSession*>> matches;
    if (auto address = cmd::normalizeAddress(*selector)) {
        for (const auto& entry : pool) {
            if (entry.first == *address) matches.push_back(entry);
        }
    } else {
        for (const auto& entry : pool) {
            if (protocol::equalsIgnoreCase(entry.second->state().model(), *selector)) matches.push_back(entry);
        }
    }
    if (matches.empty()) {
        out.error = "ERR no headset " + *selector;
    } else if (matches.size() > 1) {
        out.error = "ERR ambiguous headset " + *selector;
    } else {
        out.id = matches.front().first;
        out.session = matches.front().second;
    }
    return out;
}

void SessionManager::execute(const cmd::Command& command, Reply done, Clock::time_point now) {
    if (command.verb == cmd::Verb::Status) {
        done(statusJson(unixNow()));
        return;
    }
    auto target = resolve(command.selector, true);
    if (!target.session) {
        done(target.error);
        return;
    }
    target.session->execute(command, std::move(done), now);
}

void SessionManager::tick(Clock::time_point now) {
    for (auto& [id, session] : sessions_) session->tick(now);
}

std::optional<SessionManager::Clock::time_point> SessionManager::nextDeadline() const {
    std::optional<Clock::time_point> best;
    for (const auto& [id, session] : sessions_) {
        auto d = session->nextDeadline();
        if (d && (!best || *d < *best)) best = d;
    }
    return best;
}

} // namespace omarchy::sony
