#include "mania/official_sr.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace mra {
namespace {

using official_sr_detail::kComparisonToleranceMs;
using official_sr_detail::kDifficultyMultiplier;
using official_sr_detail::kHoldOverlapFactor;
using official_sr_detail::kIndividualBaseStrain;
using official_sr_detail::kIndividualDecayBase;
using official_sr_detail::kLogisticGrowth;
using official_sr_detail::kLogisticOffsetMs;
using official_sr_detail::kOverallDecayBase;
using official_sr_detail::kSectionDecayWeight;
using official_sr_detail::kSectionLengthMs;

// osu.Framework.Utils.Precision.DefinitelyBigger(value1, value2, precision)
//     => value1 - precision > value2, with precision = 1 ms in both evaluators.
inline bool definitelyBigger(double value1, double value2) {
    return value1 - kComparisonToleranceMs > value2;
}

// One ManiaDifficultyHitObject: only note heads become objects (LN tails do not).
struct DObj {
    int column;
    double startTime;        // clock-rate adjusted (raw / rate)
    double endTime;          // clock-rate adjusted; rice: == startTime (HitCircle.GetEndTime)
    double deltaTime;        // (rawStart_i - rawStart_{i-1}) / clockRate  (DifficultyHitObject ctor)
    double columnStrainTime; // StartTime - prev-in-column StartTime; first in column: StartTime
    std::vector<const DObj*> previousHitObjects;  // per-column previous note head (may hold null)
};

// IndividualStrainEvaluator.EvaluateDifficultyOf
double individualStrainOf(const DObj& current) {
    double holdFactor = 1.0;
    // Bonus when this note starts and ends inside another hold note.
    for (const DObj* prev : current.previousHitObjects) {
        if (prev == nullptr) continue;
        if (definitelyBigger(prev->endTime, current.endTime) &&
            definitelyBigger(current.startTime, prev->startTime)) {
            holdFactor = kHoldOverlapFactor;
            break;
        }
    }
    return kIndividualBaseStrain * holdFactor;
}

// OverallStrainEvaluator.EvaluateDifficultyOf
double overallStrainOf(const DObj& current) {
    bool isOverlapping = false;
    double closestEndTime = std::fabs(current.endTime - current.startTime);
    double holdFactor = 1.0;

    for (const DObj* prev : current.previousHitObjects) {
        if (prev == nullptr) continue;

        // Current note is overlapped if a previous note end overlaps the current note body.
        isOverlapping = isOverlapping ||
                        (definitelyBigger(prev->endTime, current.startTime) &&
                         definitelyBigger(current.endTime, prev->endTime) &&
                         definitelyBigger(current.startTime, prev->startTime));

        // Slight bonus for everything if something is held meanwhile.
        if (definitelyBigger(prev->endTime, current.endTime) &&
            definitelyBigger(current.startTime, prev->startTime))
            holdFactor = kHoldOverlapFactor;

        closestEndTime = std::min(closestEndTime, std::fabs(current.endTime - prev->endTime));
    }

    // DiffUtils.Logistic(x, midpointOffset, multiplier) = 1 / (1 + exp(multiplier * (offset - x)))
    double holdAddition = 0.0;
    if (isOverlapping)
        holdAddition = 1.0 / (1.0 + std::exp(kLogisticGrowth * (kLogisticOffsetMs - closestEndTime)));

    return (1.0 + holdAddition) * holdFactor;
}

}  // namespace

double OfficialStarRating(int keyCount, const std::vector<SrNote>& notes, double rate) {
    if (keyCount < 1) return -1.0;
    if (!(rate > 0.0)) return -1.0;  // clock rate must be positive to divide the timeline
    for (const SrNote& n : notes)
        if (n.column < 0 || n.column >= keyCount) return -1.0;

    // ManiaDifficultyCalculator.CreateDifficultyAttributes: empty beatmap keeps the default
    // (zero) StarRating. A single-note map creates no difficulty hit objects (loop starts at
    // index 1) and therefore also yields exactly 0 through the code path below.
    if (notes.empty()) return 0.0;

    // ---- CreateDifficultyHitObjects: sort by (int)Math.Round(StartTime) ----
    // headMs is an int here, so Math.Round is the identity; stable sort keeps chord order
    // deterministic (lazer's LegacySortHelper result is order-independent for chords because
    // chords take the max individual strain and per-column previous notes are unambiguous).
    std::vector<const SrNote*> sorted;
    sorted.reserve(notes.size());
    for (const SrNote& n : notes) sorted.push_back(&n);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const SrNote* a, const SrNote* b) { return a->headMs < b->headMs; });

    std::vector<DObj> objects;
    objects.reserve(sorted.size() - 1);  // reserve keeps DObj pointers stable
    std::vector<const DObj*> prevInColumn(keyCount, nullptr);

    for (size_t i = 1; i < sorted.size(); ++i) {  // first object of the map is never a DObj
        const SrNote* cur = sorted[i];
        const SrNote* prevRaw = sorted[i - 1];

        DObj obj;
        obj.column = cur->column;
        obj.startTime = static_cast<double>(cur->headMs) / rate;
        const double rawEnd = cur->tailMs < 0 ? static_cast<double>(cur->headMs)
                                              : static_cast<double>(cur->tailMs);
        obj.endTime = rawEnd / rate;
        obj.deltaTime = static_cast<double>(cur->headMs - prevRaw->headMs) / rate;

        // ColumnStrainTime = StartTime - PrevInColumn(0)?.StartTime ?? StartTime
        const DObj* colPrev = prevInColumn[obj.column];
        obj.columnStrainTime =
            colPrev ? (obj.startTime - colPrev->startTime) : obj.startTime;

        // PreviousHitObjects: copy of the previous object's array, overwritten at the
        // previous object's own column ("intentionally depends on processing order").
        if (!objects.empty()) {
            obj.previousHitObjects = objects.back().previousHitObjects;
            obj.previousHitObjects[objects.back().column] = &objects.back();
        } else {
            obj.previousHitObjects.assign(keyCount, nullptr);
        }

        const int objColumn = obj.column;
        objects.push_back(std::move(obj));
        prevInColumn[objColumn] = &objects.back();  // stable: capacity reserved above
    }

    // ---- Strain skill (mania Strain + StrainDecaySkill + StrainSkill bases) ----
    std::vector<double> individualStrains(keyCount, 0.0);  // Strain ctor zero-initialises
    double highestIndividualStrain = 0.0;                  // field default
    double overallStrain = 1.0;                            // Strain ctor: overallStrain = 1
    double currentStrain = 0.0;                            // StrainDecaySkill.CurrentStrain default 0

    std::vector<double> strainPeaks;
    double currentSectionPeak = 0.0;  // StrainSkill field default
    double currentSectionEnd = 0.0;

    for (size_t idx = 0; idx < objects.size(); ++idx) {
        const DObj& current = objects[idx];

        // StrainSkill.ProcessInternal section maintenance runs BEFORE the strain update.
        if (idx == 0)
            currentSectionEnd = std::ceil(current.startTime / kSectionLengthMs) * kSectionLengthMs;
        while (current.startTime > currentSectionEnd) {
            strainPeaks.push_back(currentSectionPeak);
            // Mania Strain.CalculateInitialStrain(offset, current): decays the pre-update
            // highestIndividualStrain / overallStrain from the previous note's StartTime.
            const double offset =
                currentSectionEnd - objects[idx - 1].startTime;
            currentSectionPeak =
                highestIndividualStrain * std::pow(kIndividualDecayBase, offset / 1000.0) +
                overallStrain * std::pow(kOverallDecayBase, offset / 1000.0);
            currentSectionEnd += kSectionLengthMs;
        }

        // StrainDecaySkill.StrainValueAt -> Strain.StrainValueOf
        individualStrains[current.column] =
            individualStrains[current.column] *
                std::pow(kIndividualDecayBase, current.columnStrainTime / 1000.0) +
            individualStrainOf(current);

        // Chords (DeltaTime <= 1 ms) keep the hardest individual strain so that processing
        // order within a chord does not matter.
        highestIndividualStrain =
            current.deltaTime <= 1.0
                ? std::max(highestIndividualStrain, individualStrains[current.column])
                : individualStrains[current.column];

        overallStrain = overallStrain * std::pow(kOverallDecayBase, current.deltaTime / 1000.0) +
                        overallStrainOf(current);

        // StrainValueOf returns highestIndividualStrain + overallStrain - CurrentStrain and the
        // base adds it onto CurrentStrain (decay base 1 => no-op), so after the update
        // CurrentStrain == highestIndividualStrain + overallStrain.
        const double strainValue =
            highestIndividualStrain + overallStrain - currentStrain;
        currentStrain += strainValue * 1.0;  // SkillMultiplier = 1, StrainDecayBase = 1

        currentSectionPeak = std::max(currentStrain, currentSectionPeak);
    }
    strainPeaks.push_back(currentSectionPeak);  // GetCurrentStrainPeaks()

    // ---- StrainSkill.DifficultyValue ----
    std::vector<double> peaks;
    peaks.reserve(strainPeaks.size());
    for (double p : strainPeaks)
        if (p > 0.0) peaks.push_back(p);
    std::sort(peaks.begin(), peaks.end(), std::greater<double>());

    double difficulty = 0.0;
    double weight = 1.0;
    for (double p : peaks) {
        difficulty += p * weight;
        weight *= kSectionDecayWeight;
    }

    const double starRating = difficulty * kDifficultyMultiplier;
    return std::isfinite(starRating) ? starRating : -1.0;
}

}  // namespace mra
