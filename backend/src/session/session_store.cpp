#include "session/session_store.h"

#include <chrono>
#include <cstdio>
#include <random>

namespace mra {

SessionStore& SessionStore::instance() {
    static SessionStore store;
    return store;
}

namespace {

std::string makeId() {
    static std::mt19937_64 rng(
        std::chrono::steady_clock::now().time_since_epoch().count());
    uint64_t v = rng();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(v));
    return std::string("s") + buf;
}

}  // namespace

std::string SessionStore::create(ReplaySession session) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = makeId();
    session.id = id;
    sessions_[id] = std::move(session);
    // Keep only the most recent 8 sessions alive.
    while (sessions_.size() > 8) sessions_.erase(sessions_.begin());
    return id;
}

bool SessionStore::get(const std::string& id, ReplaySession& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;
    out = it->second;
    return true;
}

bool SessionStore::erase(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.erase(id) > 0;
}

std::vector<std::pair<std::string, std::string>> SessionStore::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& [id, s] : sessions_)
        out.emplace_back(id,
                         s.beatmap.title + " [" + s.beatmap.version + "]");
    return out;
}

void SessionStore::addSegment(const std::string& id, ReplaySegment seg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    seg.id = nextSegmentId_++;
    it->second.segments.push_back(std::move(seg));
}

bool SessionStore::removeSegment(const std::string& id, int64_t segmentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;
    auto& segs = it->second.segments;
    for (auto s = segs.begin(); s != segs.end(); ++s) {
        if (s->id == segmentId) {
            segs.erase(s);
            return true;
        }
    }
    return false;
}

bool SessionStore::setSkinFolder(const std::string& id,
                                 const std::string& folder) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;
    it->second.skinFolder =
        folder.empty() ? std::nullopt : std::optional<std::string>(folder);
    return true;
}

}  // namespace mra
