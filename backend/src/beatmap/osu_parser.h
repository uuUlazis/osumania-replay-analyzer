#pragma once
// .osu beatmap difficulty parser (legacy stable format), mania-focused.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mra {

enum class ManiaObjectType { Note, Hold };

struct ManiaHitObject {
    ManiaObjectType type = ManiaObjectType::Note;
    int column = 0;
    int64_t startTimeMs = 0;
    std::optional<int64_t> endTimeMs;  // hold notes only
    int sourceIndex = 0;               // index within [HitObjects]
    // Raw fields preserved for diagnostics / compatibility.
    int rawX = 0;
    int rawY = 0;
    int rawType = 0;
    int rawHitSound = 0;
    std::string rawLine;
};

struct TimingPoint {
    double timeMs = 0;
    double beatLength = 0;      // positive: BPM length; negative: multiplier
    double sliderMultiplier = 0;
    int meter = 4;
    bool uninherited = true;
};

struct OsuBeatmap {
    // [General]
    std::string audioFilename;
    int mode = 0;                 // 3 = osu!mania
    // [Metadata]
    std::string title, titleUnicode, artist, artistUnicode, creator, version;
    std::string beatmapId, beatmapSetId;
    std::string beatmapMd5;       // filled separately from file bytes
    // [Difficulty]
    int keyCount = 0;             // CircleSize for mania
    double overallDifficulty = 0;
    double hpDrain = 0;
    // [TimingPoints]
    std::vector<TimingPoint> timingPoints;
    // [Events]
    std::string backgroundFile;
    // [HitObjects]
    std::vector<ManiaHitObject> objects;
    // Diagnostics
    size_t malformedObjectLines = 0;
    std::vector<std::string> parseWarnings;
    bool isMania() const { return mode == 3; }
};

struct OsuParseResult {
    bool ok = false;
    std::string error;
    OsuBeatmap beatmap;
};

// Parses .osu text (UTF-8, possibly UTF-8 BOM).
OsuParseResult parseOsu(const std::string& text);

// column = clamp(floor(x * keyCount / 512), 0, keyCount - 1)
int maniaColumnFromX(int x, int keyCount);

}  // namespace mra
