#pragma once
// osu!mania judgement windows and hit result model.
// Windows follow the official mania judgement table; the ACC weights follow
// the legacy ScoreV1/ScoreV2 profiles (see accuracy.h) - legacy replays never
// use the modern 320/300/200/100/50 weights directly.
#include <string>
#include <vector>

namespace mra {

enum class HitResult {
    Perfect,  // legacy MAX / 300g
    Great,    // 300
    Good,     // 200
    Ok,       // 100
    Meh,      // 50
    Miss,
};

enum class HitPhase { NoteHead, HoldHead, HoldTail };

const char* hitResultName(HitResult r);       // "MAX" | "300" | "200" | "100" | "50" | "MISS"
int hitResultScoreValue(HitResult r);         // 320-style numeric (display only)

struct ManiaWindows {
    double perfect = 16;
    double great = 40;
    double good = 73;
    double ok = 103;
    double meh = 127;
    double miss = 158;

    static ManiaWindows fromOD(double od);
};

HitResult judgeOffset(double offsetMs, const ManiaWindows& windows);

}  // namespace mra
