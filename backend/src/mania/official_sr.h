#pragma once
// Official osu!lazer mania star rating (difficulty value), algorithm version 20241007.
//
// Ported 1:1 from ppy/osu master (fetched 2026-10-20):
//   osu.Game.Rulesets.Mania/Difficulty/ManiaDifficultyCalculator.cs
//   osu.Game.Rulesets.Mania/Difficulty/Skills/Strain.cs
//   osu.Game.Rulesets.Mania/Difficulty/Evaluators/IndividualStrainEvaluator.cs
//   osu.Game.Rulesets.Mania/Difficulty/Evaluators/OverallStrainEvaluator.cs
//   osu.Game.Rulesets.Mania/Difficulty/Preprocessing/ManiaDifficultyHitObject.cs
//   osu.Game/Rulesets/Difficulty/Skills/StrainSkill.cs
//   osu.Game/Rulesets/Difficulty/Skills/StrainDecaySkill.cs
//   osu.Game/Rulesets/Difficulty/Preprocessing/DifficultyHitObject.cs
//   osu.Game/Rulesets/Difficulty/Utils/DiffUtils.cs (Logistic)
// Mechanism reference: docs/pp-sr-notes.md section 3 (constants table in section 3.6).
#include <vector>

namespace mra {

// Shared note input for the star-rating modules.
// tailMs < 0 means a rice (circle / non-LN); otherwise a long note (LN) ending at tailMs.
struct SrNote {
    int column;
    int headMs;
    int tailMs;
};

// Constants mirrored from the lazer sources; kept here (not only in the .cpp) so the
// unit tests can assert the table in docs/pp-sr-notes.md section 3.6 verbatim.
namespace official_sr_detail {
inline constexpr double kIndividualDecayBase   = 0.125;  // Strain.cs: individual_decay_base
inline constexpr double kOverallDecayBase      = 0.30;   // Strain.cs: overall_decay_base
inline constexpr double kSectionLengthMs       = 400.0;  // StrainSkill.cs: SectionLength
inline constexpr double kSectionDecayWeight    = 0.9;    // StrainSkill.cs: DecayWeight
inline constexpr double kDifficultyMultiplier  = 0.018;  // ManiaDifficultyCalculator.cs: difficulty_multiplier
inline constexpr double kIndividualBaseStrain  = 2.0;    // IndividualStrainEvaluator: base strain
inline constexpr double kHoldOverlapFactor     = 1.25;   // Individual/OverallStrainEvaluator: holdFactor
inline constexpr double kOverallHoldBase       = 1.0;    // OverallStrainEvaluator: base holdFactor
inline constexpr double kLogisticGrowth        = 0.27;   // OverallStrainEvaluator: Logistic multiplier
inline constexpr double kLogisticOffsetMs      = 30.0;   // OverallStrainEvaluator: release_threshold
inline constexpr double kComparisonToleranceMs = 1.0;    // Precision.DefinitelyBigger(..., precision: 1)
}  // namespace official_sr_detail

// Official lazer star rating for a mania map.
//   keyCount : number of columns (must be >= 1)
//   notes    : all notes of the (already converted) map
//   rate     : clock rate; 1.0 = NM, 1.5 = DT/NC, 0.75 = HT (lazer ModUtils.CalculateRateWithMods)
// Returns -1 for unsupported input (keyCount < 1, non-positive rate, column out of range,
// or a non-finite intermediate result). Never returns NaN/Inf.
double OfficialStarRating(int keyCount, const std::vector<SrNote>& notes, double rate);

}  // namespace mra
