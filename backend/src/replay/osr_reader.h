#pragma once
// .osr replay file parser per the official osu! format specification.
#include <cstdint>
#include <string>
#include <vector>

namespace mra {

// Official .osr mod bit flags (legacy stable set).
enum OsrModBits : uint32_t {
    kModNoFail      = 1u << 0,
    kModEasy        = 1u << 1,
    kModTouch       = 1u << 2,
    kModHidden      = 1u << 3,
    kModHardRock    = 1u << 4,
    kModSuddenDeath = 1u << 5,
    kModDoubleTime  = 1u << 6,
    kModRelax       = 1u << 7,
    kModHalfTime    = 1u << 8,
    kModNightcore   = 1u << 9,
    kModFlashlight  = 1u << 10,
    kModAutoplay    = 1u << 11,
    kModSpunOut     = 1u << 12,
    kModAutopilot   = 1u << 13,
    kModPerfect     = 1u << 14,
    kModKey4        = 1u << 15,
    kModKey5        = 1u << 16,
    kModKey6        = 1u << 17,
    kModKey7        = 1u << 18,
    kModKey8        = 1u << 19,
    kModFadeIn      = 1u << 20,
    kModRandom      = 1u << 21,
    kModCinema      = 1u << 22,
    kModTarget      = 1u << 23,
    kModKey9        = 1u << 24,
    kModKeyCoop     = 1u << 25,
    kModKey1        = 1u << 26,
    kModKey3        = 1u << 27,
    kModKey2        = 1u << 28,
    kModScoreV2     = 1u << 29,
    kModMirror      = 1u << 30,
};

struct OsrHeader {
    uint8_t mode = 0;
    uint32_t gameVersion = 0;
    std::string beatmapMd5;
    std::string playerName;
    std::string replayMd5;
    uint16_t count300 = 0;
    uint16_t count100 = 0;
    uint16_t count50 = 0;
    uint16_t countGeki = 0;   // mania: MAX (300g)
    uint16_t countKatu = 0;   // mania: 200
    uint16_t countMiss = 0;
    uint32_t score = 0;
    uint16_t maxCombo = 0;
    bool perfect = false;
    uint32_t mods = 0;
    std::string lifeGraph;
    int64_t timestamp = 0;
    std::vector<uint8_t> compressedData;
    uint64_t onlineId = 0;
    // Target practice extra mod info (double), present only when kModTarget set.
    bool hasTargetPracticeInfo = false;
    double targetPracticeAccuracy = 0.0;
    // Forward-compatibility diagnostics: bytes after the last known field.
    size_t trailingBytes = 0;
};

// Returns the effective key count implied by key mods, or 0 if no key mod set.
// Key1/Key2/Key3 sum (1/2/3); Key4..Key9 map to their own count.
int keyCountFromMods(uint32_t mods);

struct OsrParseResult {
    OsrHeader header;
    std::string error;       // empty on success
};

// Parses the full .osr structure. Throws nothing; error text on failure.
OsrParseResult parseOsr(const std::vector<uint8_t>& bytes);

}  // namespace mra
