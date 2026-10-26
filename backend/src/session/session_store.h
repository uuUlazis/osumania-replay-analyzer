#pragma once
// In-memory session state: one loaded replay+beatmap pair plus analysis
// results and segment bookmarks. Single-user desktop app: no locking beyond a
// mutex for HTTP handler thread safety.
#include "beatmap/osu_parser.h"
#include "lazer/lazer_format.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "replay/osr_reader.h"
#include "replay/replay_frames.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mra {

struct ReplaySegment {
    int64_t id = 0;
    int64_t startTimeMs = 0;
    int64_t endTimeMs = 0;
    std::string label;
};

struct ReplaySession {
    std::string id;                       // session uuid
    std::string replayPath;
    std::string beatmapPath;
    std::optional<std::string> skinFolder;
    OsrParseResult replay;
    std::vector<ReplayFrame> frames;
    int keyCount = 0;   // resolved from mods, falling back to beatmap
    OsuBeatmap beatmap;
    std::string beatmapMd5;   // MD5 of raw .osu bytes (osu! beatmap hash)
    MatchResult match;     // V1 (stable) rules — powers all stats
    MatchResult matchV2;   // ScoreV2 rules — renderer 判定规则 V2 view
    GlobalAnalysis analysis;
    // true: the .osr counters use stable's one-judgement-per-object model
    // (LN = single unit, the default / install base); false: lazer-style
    // heads+tails. Decided at load time by matching the declared total.
    bool stableCountMode = true;
    // Which rules pass powers summary/holds/segments (acc-semantics §0.1:
    // 同一局全链路同口径). true = ScoreV2 (matchV2), false = V1 (match).
    bool rulesScoreV2 = false;
    // ---- lazer 数据通道 (Phase L1): 声明值层 (判定引擎仍为 stable 口径,
    // lazer 局由前端标注「stable 判定引擎不适用」) ----
    bool isLazer = false;              // gameVersion >= kFirstLazerVersion
    LazerManiaCounts lazerCounts;      // 尾块 JSON 六档 (valid=false = 无尾块)
    bool lazerCountsFromJson = false;  // true: declared counts 以 JSON 为准
    std::string lazerRank;             // 尾块 rank (空 = 无)
    // L3: lazer 转换 mod 复现备注 (RD 缺 seed 无法核对等); 空 = 无备注。
    std::vector<std::string> lazerModNotes;
    std::vector<ReplaySegment> segments;
    std::optional<std::string> audioPath;  // optional music for synced playback
    bool ready = false;
    // Creation order (set by SessionStore::create). Eviction always removes
    // the smallest seq - the oldest session - never the active one.
    uint64_t insertSeq = 0;
};

// Thread-safe session registry (single session for v1, keyed by id).
class SessionStore {
  public:
    static SessionStore& instance();

    // Creates a session id and stores the data.
    std::string create(ReplaySession session);
    bool get(const std::string& id, ReplaySession& out) const;
    bool erase(const std::string& id);
    std::vector<std::pair<std::string, std::string>> list() const;  // id -> title

    void addSegment(const std::string& id, ReplaySegment seg);
    bool removeSegment(const std::string& id, int64_t segmentId);

    // Replaces the session's skin folder (skin hot-swap without a reload).
    bool setSkinFolder(const std::string& id, const std::string& folder);

  private:
    SessionStore() = default;
    mutable std::mutex mutex_;
    std::map<std::string, ReplaySession> sessions_;
    int64_t nextSegmentId_ = 1;
    uint64_t insertSeq_ = 0;
};

}  // namespace mra
