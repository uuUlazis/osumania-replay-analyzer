#pragma once
// Judgement counts + accuracy (spec 03-scoring-acc.md).
//
// Two views live here:
//   * `ScoreCounts`   — the source's own counter set: EVERY count is a `ushort`
//                       (each increment is `... add; conv.u2; stfld`, so
//                       65535 + 1 wraps to 0), MaxCombo is int32 and
//                       CurrentCombo is ushort. acc / grade are computed from
//                       this view with the source's precision path
//                       (int32 numerator -> conv.r4 -> single-precision div).
//   * `JudgementCounts` — the legacy int64 view the analysis / HTTP layer has
//                       always used; converted with the SAME arithmetic order
//                       and grouping so both views agree.
#include "mania/judgement.h"

#include <cstdint>

namespace mra {

// ---- ScoreAndReplayRecord counts (spec 03 §0.1) ----
struct ScoreCounts {
    uint16_t count300 = 0;      // 0x0400295A
    uint16_t count100 = 0;      // 0x04002959
    uint16_t count50 = 0;       // 0x0400295B
    uint16_t countMax = 0;      // 0x0400295C (mania MAX / 320)
    uint16_t count200 = 0;      // 0x0400295D (mania 200)
    uint16_t countMiss = 0;     // 0x0400295E
    int32_t maxCombo = 0;       // 0x04002965 int32
    uint16_t currentCombo = 0;  // 0x0400297A ushort

    // Every `CountX++` is `int32 add; conv.u2` — keep the wrap.
    void add300() { count300 = static_cast<uint16_t>(count300 + 1); }
    void add100() { count100 = static_cast<uint16_t>(count100 + 1); }
    void add50() { count50 = static_cast<uint16_t>(count50 + 1); }
    void addMax() { countMax = static_cast<uint16_t>(countMax + 1); }
    void add200() { count200 = static_cast<uint16_t>(count200 + 1); }
    void addMiss() { countMiss = static_cast<uint16_t>(countMiss + 1); }

    // 0x060019E3 mania override, int32 additions in this exact order:
    //   C50 + C100 + C300 + CMiss + CMax + C200
    int32_t total() const;
    // 0x060019E4 mania override: C50 + C100 + C300 + CMax + C200
    int32_t nonMissTotal() const;

    // 0x060019E2 — mania acc, returned as a single-precision fraction.
    //   V1: n = C50*50 + C100*100 + C200*200 + (C300+CMax)*300
    //       d = total * 300
    //   V2: n = C50*50 + C100*100 + C200*200 + C300*300 + CMax*305
    //       d = total * 305
    //   (int32 arithmetic) -> conv.r4 -> float / float (single precision).
    // Zero objects returns exactly 1.0f (the entry test is `== 0`, not `<= 0`).
    float accuracy(bool v2) const;
};

// 0x060019E5 — grade (Obf001147: XH=0 SH=1 X=2 S=3 A=4 B=5 C=6 D=7 F=8).
// `acc` must be the record's own accuracy() result: the 1.0f test is an exact
// single-precision compare, the 0.95/0.9/0.8/0.7 tests are `conv.r8` double
// compares (NaN falls through every branch to D).
int maniaGrade(float acc, uint32_t activeMods, bool passed);

// ---- Legacy percent view (analysis / HTTP layer) ----
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

enum class AccuracyProfile { LegacyScoreV1, LegacyScoreV2, ModernRuleset };

// acc in PERCENT (the API/UI unit) computed through the source's arithmetic
// order and precision path:
//   V1  := (50*C50 + 100*C100 + 200*C200 + 300*(C300+CMax)) / (total*300)
//   V2  := (50*C50 + 100*C100 + 200*C200 + 300*C300 + 305*CMax) / (total*305)
//   Modern := 320-weight display variant (legacy surface, not a source path)
// num/den are integer sums, converted to float only at the division; the
// percent scaling is applied afterwards in double.
// Empty counts return 100.0 (source: `if (Obf008187() == 0) return 1.0f;`).
double accuracyPercent(const JudgementCounts& c, AccuracyProfile profile);

}  // namespace mra
