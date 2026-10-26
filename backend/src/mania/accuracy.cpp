#include "mania/accuracy.h"

namespace mra {

int32_t ScoreCounts::total() const {
    // 0x060019E3: 六项 int32 加法，顺序固定（不可改写为其它分组）
    return static_cast<int32_t>(count50) + static_cast<int32_t>(count100) +
           static_cast<int32_t>(count300) + static_cast<int32_t>(countMiss) +
           static_cast<int32_t>(countMax) + static_cast<int32_t>(count200);
}

int32_t ScoreCounts::nonMissTotal() const {
    // 0x060019E4
    return static_cast<int32_t>(count50) + static_cast<int32_t>(count100) +
           static_cast<int32_t>(count300) + static_cast<int32_t>(countMax) +
           static_cast<int32_t>(count200);
}

float ScoreCounts::accuracy(bool v2) const {
    // IL_0000: `if (Obf008187() == 0) return 1.0f;`  (== 0, not <= 0)
    const int32_t objects = total();
    if (objects == 0) return 1.0f;

    int32_t numerator = 0;
    int32_t denominator = 0;
    if (!v2) {
        // IL_007A V1 分支: C50*50 + C100*100 + C200*200 + (C300 + CMax)*300
        numerator = static_cast<int32_t>(count50) * 50 +
                    static_cast<int32_t>(count100) * 100 +
                    static_cast<int32_t>(count200) * 200 +
                    (static_cast<int32_t>(count300) +
                     static_cast<int32_t>(countMax)) * 300;
        denominator = objects * 300;
    } else {
        // IL_0030 V2 分支: C50*50 + C100*100 + C200*200 + C300*300 + CMax*305
        numerator = static_cast<int32_t>(count50) * 50 +
                    static_cast<int32_t>(count100) * 100 +
                    static_cast<int32_t>(count200) * 200 +
                    static_cast<int32_t>(count300) * 300 +
                    static_cast<int32_t>(countMax) * 305;
        denominator = objects * 305;
    }
    // conv.r4 on both sides AFTER the integer arithmetic, then a single
    // precision divide (spec 03 §2.1 / §3.1: 先整数算完再转单精度).
    return static_cast<float>(numerator) / static_cast<float>(denominator);
}

int maniaGrade(float acc, uint32_t activeMods, bool passed) {
    // 0x060019E5
    if (!passed) return 8;  // F
    if (acc == 1.0f) {
        // 0x100408 = Hidden | Flashlight | FadeIn
        return (activeMods & 0x100408u) > 0u ? 0 : 2;  // XH : X
    }
    if (static_cast<double>(acc) > 0.95) {
        return (activeMods & 0x100408u) > 0u ? 1 : 3;  // SH : S
    }
    if (static_cast<double>(acc) > 0.9) return 4;  // A
    if (static_cast<double>(acc) > 0.8) return 5;  // B
    if (static_cast<double>(acc) > 0.7) return 6;  // C
    return 7;                                      // D
}

double accuracyPercent(const JudgementCounts& c, AccuracyProfile profile) {
    const int64_t objects = c.judged();
    // 0x060019E2 entry test: == 0 -> 1.0f  (100%)
    if (objects == 0) return 100.0;
    int64_t numerator = 0;
    int64_t denominator = 0;
    switch (profile) {
        case AccuracyProfile::LegacyScoreV1:
            numerator = 50 * c.meh + 100 * c.ok + 200 * c.good +
                        300 * (c.great + c.perfect);
            denominator = objects * 300;
            break;
        case AccuracyProfile::LegacyScoreV2:
            numerator = 50 * c.meh + 100 * c.ok + 200 * c.good + 300 * c.great +
                        305 * c.perfect;
            denominator = objects * 305;
            break;
        case AccuracyProfile::ModernRuleset:
            // Legacy display variant (not a source path): MAX weighted 320.
            numerator = 50 * c.meh + 100 * c.ok + 200 * c.good + 300 * c.great +
                        320 * c.perfect;
            denominator = objects * 320;
            break;
    }
    const float fraction =
        static_cast<float>(numerator) / static_cast<float>(denominator);
    // Percent scaling only (the API unit); does not touch the judgement math.
    return static_cast<double>(fraction) * 100.0;
}

}  // namespace mra
