#pragma once
// Accuracy formulas for the three supported judgement-weight profiles.
#include "mania/judgement.h"

#include <cstdint>

namespace mra {

enum class AccuracyProfile { LegacyScoreV1, LegacyScoreV2, ModernRuleset };

struct JudgementCounts {
    int64_t perfect = 0;      // MAX / 300g
    int64_t great = 0;        // 300
    int64_t good = 0;         // 200
    int64_t ok = 0;           // 100
    int64_t meh = 0;          // 50
    int64_t miss = 0;

    int64_t judged() const { return perfect + great + good + ok + meh + miss; }
    int64_t passed() const { return judged() - miss; }

    void add(HitResult r) {
        switch (r) {
            case HitResult::Perfect: ++perfect; break;
            case HitResult::Great: ++great; break;
            case HitResult::Good: ++good; break;
            case HitResult::Ok: ++ok; break;
            case HitResult::Meh: ++meh; break;
            case HitResult::Miss: ++miss; break;
        }
    }
};

// Classic: acc = sum(weights) / (50 * total) * 100.
// ScoreV1 profile weights MAX as 300; ScoreV2 weights MAX as 305;
// modern weights MAX as 320 (display only for legacy replays).
double accuracyPercent(const JudgementCounts& c, AccuracyProfile profile);

}  // namespace mra
