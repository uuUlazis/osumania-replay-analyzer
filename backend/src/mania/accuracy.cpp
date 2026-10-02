#include "mania/accuracy.h"

namespace mra {

double accuracyPercent(const JudgementCounts& c, AccuracyProfile profile) {
    int64_t total = c.judged();
    if (total == 0) return 0.0;
    int64_t maxWeight;
    switch (profile) {
        case AccuracyProfile::LegacyScoreV1: maxWeight = 300; break;
        case AccuracyProfile::LegacyScoreV2: maxWeight = 305; break;
        case AccuracyProfile::ModernRuleset: maxWeight = 320; break;
        default: maxWeight = 300; break;
    }
    // Judgement numeric values (display names) map to classic weights:
    // MAX->maxWeight, 300->300, 200->200, 100->100, 50->50, Miss->0.
    // Normalised by the profile's own MAX weight so all-MAX is exactly 100%
    // (ScoreV2 305s slightly beat plain 300s when mixed).
    int64_t sum = maxWeight * c.perfect + 300 * c.great + 200 * c.good +
                  100 * c.ok + 50 * c.meh;
    return static_cast<double>(sum) / (static_cast<double>(maxWeight) *
                                      static_cast<double>(total)) * 100.0;
}

}  // namespace mra
