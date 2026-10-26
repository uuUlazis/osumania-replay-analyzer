#pragma once
// lazer .osr extra tail block (LegacyReplaySoloScoreInfo) + version gate.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mra {

inline constexpr uint32_t kFirstLazerVersion = 30000000;

// True when the .osr was written by osu!lazer.
bool isLazerGameVersion(uint32_t gameVersion);

struct LazerMod {
    std::string acronym;                       // "DT", "RD", "MR", ...
    std::map<std::string, std::string> settings; // key -> raw JSON value text
                                                 // (e.g. "1.25", "12345", "true")
};

struct LazerManiaCounts {
    int64_t perfect = 0, great = 0, good = 0, ok = 0, meh = 0, miss = 0;
    bool fromJson = false;   // true: derived from tail JSON statistics
    bool valid = false;      // false: tail absent/corrupt for counts purposes
};

struct LazerScoreExtra {
    bool present = false;      // tail block found and framed
    std::string error;         // non-empty when present==false but a tail seemed to exist
    std::string jsonRaw;       // decompressed JSON text ("" when absent)
    std::vector<LazerMod> mods;
    std::map<std::string, int64_t> statistics;         // normalized lowercase keys
    std::map<std::string, int64_t> maximumStatistics;  // normalized lowercase keys
    std::string rank;
    int64_t totalScoreWithoutMods = -1;
    bool hasStatistics = false;
    bool hasMaximumStatistics = false;
};

// Parses the tail that follows onlineId (+ optional target-practice double).
// bytes = exactly the bytes after the last stable field. Never throws; on any
// structural problem returns present=false with error filled (best effort).
LazerScoreExtra parseLazerExtra(const uint8_t* data, size_t size);

// Parses already-decompressed JSON (used by tests and by callers that decoded
// elsewhere). Same tolerance contract as parseLazerExtra.
LazerScoreExtra parseLazerExtraJson(const std::string& json);

// mania counts from tail statistics: only Perfect/Great/Good/Ok/Meh/Miss count
// (case-insensitive); IgnoreHit/ComboBreak/anything else is ignored. When the
// six mania keys are all absent the result is valid=false.
LazerManiaCounts lazerManiaCountsFromStatistics(const std::map<std::string, int64_t>& statistics);

// lazer mania accuracy percent: (305p+300gr+200go+100ok+50meh)/(305*N)*100.
// N = 0 -> 100.0.
double lazerAccuracy305Percent(const LazerManiaCounts& c);

}  // namespace mra
