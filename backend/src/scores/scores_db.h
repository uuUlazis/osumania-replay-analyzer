#pragma once
// Reader for osu! (stable) local scores database "scores.db".
//
// Byte layout (validated byte-exact against a live scores.db):
//   i32 dbVersion;
//   i32 groupCount;
//   group: osu!string beatmapMd5; i32 scoreCount; score x scoreCount;
//   score: u8 playMode; i32 clientVersion; osu!string beatmapMd5;
//          osu!string playerName; osu!string replayMd5;
//          u16 c300, c100, c50, geki, katu, miss;
//          i32 totalScore; u16 maxCombo; u8 fullCombo; i32 mods;
//          osu!string hpGraph (empty; occasionally missing entirely);
//          i64 playedTicks (.NET ticks); i32 sentinel (always -1);
//          i64 onlineId; f64 targetPractice accumulator (only when mods
//          carry the TargetPractice bit 1<<23).
//
// The strict -1 sentinel after the ticks field keeps the tolerant
// HP-graph fallback safe: a mis-guessed string boundary is caught there.

#include <cstdint>
#include <string>
#include <vector>

namespace mra {

struct ScoreEntry {
    int mode = 0;                  // 0=osu!, 1=taiko, 2=catch, 3=mania
    int32_t clientVersion = 0;
    std::string beatmapMd5;
    std::string playerName;
    std::string replayMd5;
    int64_t c300 = 0;
    int64_t c100 = 0;
    int64_t c50 = 0;
    int64_t geki = 0;
    int64_t katu = 0;
    int64_t miss = 0;
    int64_t totalScore = 0;        // on-wire i32, widened
    int maxCombo = 0;
    bool fullCombo = false;
    int64_t mods = 0;
    std::string hpGraph;           // usually empty in scores.db
    int64_t playedTicks = 0;       // .NET ticks (0001-01-01 epoch)
    int64_t playedMs = 0;          // derived Unix epoch milliseconds
    int64_t onlineId = 0;
};

struct ScoreGroup {
    std::string beatmapMd5;
    std::vector<ScoreEntry> scores;
};

struct ScoresDb {
    int64_t version = 0;
    std::vector<ScoreGroup> groups;
};

// Parses an osu! stable scores.db. Returns false and fills |err| on any
// structural problem (trailing bytes, broken sentinel, truncated file, ...).
bool parseScoresDb(const std::string& path, ScoresDb& out, std::string& err);

}  // namespace mra
