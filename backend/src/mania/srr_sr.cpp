#include "mania/srr_sr.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>

namespace mra {
namespace {

using srr_sr_detail::kAbarDeltaBias;
using srr_sr_detail::kAbarDeltaSoft;
using srr_sr_detail::kAbarLargeBase;
using srr_sr_detail::kAbarLargeD;
using srr_sr_detail::kAbarLargeSlope;
using srr_sr_detail::kAbarSmallBase;
using srr_sr_detail::kAbarSmallD;
using srr_sr_detail::kAbarSmallSlope;
using srr_sr_detail::kAbarSmoothMs;
using srr_sr_detail::kCWindowMs;
using srr_sr_detail::kCrossValScale;
using srr_sr_detail::kFastCrossFloor;
using srr_sr_detail::kFastCrossScale;
using srr_sr_detail::kFastCrossShift;
using srr_sr_detail::kFastCrossSlope;
using srr_sr_detail::kFinalDCoeff;
using srr_sr_detail::kFinalJackCapBase;
using srr_sr_detail::kFinalJackCapSlope;
using srr_sr_detail::kFinalLengthDiv;
using srr_sr_detail::kFinalMeanShare;
using srr_sr_detail::kFinalMultiplier;
using srr_sr_detail::kFinalP83Share;
using srr_sr_detail::kFinalP83Weight;
using srr_sr_detail::kFinalP93Share;
using srr_sr_detail::kFinalP93Weight;
using srr_sr_detail::kFinalReleaseDiv;
using srr_sr_detail::kFinalReleaseWeight;
using srr_sr_detail::kFinalSCoeff;
using srr_sr_detail::kFinalStreamWeight;
using srr_sr_detail::kFinalT1Weight;
using srr_sr_detail::kFinalT2Weight;
using srr_sr_detail::kJackNerfCenter;
using srr_sr_detail::kJackNerfOffset;
using srr_sr_detail::kJackNerfScale;
using srr_sr_detail::kJackValOffset;
using srr_sr_detail::kJbarPower;
using srr_sr_detail::kJbarSmoothMs;
using srr_sr_detail::kKeyUsageHalfWindowMs;
using srr_sr_detail::kLnBodyFirstAdd;
using srr_sr_detail::kLnBodyFirstMs;
using srr_sr_detail::kLnBodySaturation;
using srr_sr_detail::kLnBodySecondAdd;
using srr_sr_detail::kLnBodySecondMs;
using srr_sr_detail::kLnBodyTailAdd;
using srr_sr_detail::kLnBonusCapMs;
using srr_sr_detail::kLnBonusDiv;
using srr_sr_detail::kLnBonusHalf;
using srr_sr_detail::kMaxKeys;
using srr_sr_detail::kMinNotes;
using srr_sr_detail::kPbarAnchorGuard;
using srr_sr_detail::kPbarDeltaFrac;
using srr_sr_detail::kPbarLnWeight;
using srr_sr_detail::kPbarPeakCurv;
using srr_sr_detail::kPbarPeakScale;
using srr_sr_detail::kPbarSpikeMs;
using srr_sr_detail::kPbarSpikeScale;
using srr_sr_detail::kReleaseCenterMs;
using srr_sr_detail::kReleaseMidpoint;
using srr_sr_detail::kReleaseSlope;
using srr_sr_detail::kRescaleHighDiv;
using srr_sr_detail::kRescaleHighFrom;
using srr_sr_detail::kRbarReleaseScale;
using srr_sr_detail::kRbarScale;
using srr_sr_detail::kStreamBoosterHi;
using srr_sr_detail::kStreamBoosterLo;
using srr_sr_detail::kStreamBoosterRatio;
using srr_sr_detail::kStreamBoosterScale;
using srr_sr_detail::kUsage400CapMs;
using srr_sr_detail::kUsage400Core;
using srr_sr_detail::kUsage400CoreDiv;
using srr_sr_detail::kUsage400FlankMs;
using srr_sr_detail::kXCapOffset;
using srr_sr_detail::kXCapSlope;
using srr_sr_detail::kXScale;
using srr_sr_detail::kXWindowBase;
using srr_sr_detail::kXWindowDiv;

// ---------------------------------------------------------------------------
// Small helpers, 1:1 ports of StarRatingRebirth/Utils.cs
// ---------------------------------------------------------------------------

// Utils.SearchSortedLeft: index of the first element >= value.
int searchSortedLeft(const std::vector<int>& array, int value) {
    return static_cast<int>(std::lower_bound(array.begin(), array.end(), value) - array.begin());
}

// Utils.SearchSortedRight: index after the last element == value (upper_bound).
int searchSortedRight(const std::vector<int>& array, int value) {
    return static_cast<int>(std::upper_bound(array.begin(), array.end(), value) - array.begin());
}

// Utils.CumulativeSum: F[i] = F[i-1] + f[i-1] * (x[i] - x[i-1]), F[0] = 0.
std::vector<double> cumulativeSum(const std::vector<int>& x, const std::vector<double>& f) {
    std::vector<double> F(x.size(), 0.0);
    for (size_t i = 1; i < x.size(); ++i)
        F[i] = F[i - 1] + f[i - 1] * static_cast<double>(x[i] - x[i - 1]);
    return F;
}

// Utils.QueryCumsum
double queryCumsum(int q, const std::vector<int>& x, const std::vector<double>& F,
                   const std::vector<double>& f) {
    if (q <= x.front()) return 0.0;
    if (q >= x.back()) return F.back();
    const int i = searchSortedLeft(x, q) - 1;
    return F[i] + f[i] * static_cast<double>(q - x[i]);
}

// Utils.SmoothOnCorners. mode: "sum" -> scale * (F(s+w) - F(s-w)); "avg" -> val / span.
std::vector<double> smoothOnCorners(const std::vector<int>& x, const std::vector<double>& f,
                                    int window, double scale, bool avg) {
    const std::vector<double> F = cumulativeSum(x, f);
    std::vector<double> g(f.size(), 0.0);
    for (size_t i = 0; i < x.size(); ++i) {
        const int s = x[i];
        const int a = std::max(s - window, x.front());
        const int b = std::min(s + window, x.back());
        const double val = queryCumsum(b, x, F, f) - queryCumsum(a, x, F, f);
        g[i] = avg ? ((b - a) > 0 ? val / static_cast<double>(b - a) : 0.0) : scale * val;
    }
    return g;
}

// Utils.InterpValues: linear interpolation of oldVals onto newX grid.
std::vector<double> interpValues(const std::vector<int>& newX, const std::vector<int>& oldX,
                                 const std::vector<double>& oldVals) {
    std::vector<double> newVals(newX.size(), 0.0);
    for (size_t i = 0; i < newX.size(); ++i) {
        const int idx = searchSortedLeft(oldX, newX[i]);
        if (idx == 0) {
            newVals[i] = oldVals.front();
        } else if (idx >= static_cast<int>(oldX.size())) {
            newVals[i] = oldVals.back();
        } else {
            const double t = static_cast<double>(newX[i] - oldX[idx - 1]) /
                             static_cast<double>(oldX[idx] - oldX[idx - 1]);
            newVals[i] = oldVals[idx - 1] + t * (oldVals[idx] - oldVals[idx - 1]);
        }
    }
    return newVals;
}

// Utils.StepInterp: step (piecewise-constant) interpolation onto newX.
std::vector<double> stepInterp(const std::vector<int>& newX, const std::vector<int>& oldX,
                               const std::vector<double>& oldVals) {
    std::vector<double> newVals(newX.size(), 0.0);
    for (size_t i = 0; i < newX.size(); ++i) {
        int idx = searchSortedRight(oldX, newX[i]) - 1;
        idx = std::max(0, std::min(idx, static_cast<int>(oldVals.size()) - 1));
        newVals[i] = oldVals[idx];
    }
    return newVals;
}

// Utils.RescaleHigh
double rescaleHigh(double sr) {
    if (sr <= kRescaleHighFrom) return sr;
    return kRescaleHighFrom + (sr - kRescaleHighFrom) * (1.0 / kRescaleHighDiv);
}

// Utils.FindNextNoteInColumn: next note (by head) in the same column after \p note;
// sentinel {0, INT_MAX, INT_MAX} when there is none.
SrNote findNextNoteInColumn(const SrNote& note, const std::vector<int>& times,
                            const std::vector<std::vector<SrNote>>& noteSeqByColumn) {
    const auto it = std::lower_bound(times.begin(), times.end(), note.headMs);
    const int idx = static_cast<int>(it - times.begin());
    if (idx + 1 < static_cast<int>(noteSeqByColumn[note.column].size()))
        return noteSeqByColumn[note.column][idx + 1];
    return SrNote{0, std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
}

// ---------------------------------------------------------------------------
// Pipeline stages, 1:1 ports of SRCalculator.cs
// ---------------------------------------------------------------------------

void getCorners(const std::vector<SrNote>& noteSeq, int T, std::vector<int>& baseCorners,
                std::vector<int>& aCorners, std::vector<int>& allCorners) {
    std::vector<int> baseSet;  // kept sorted + unique (models the C# HashSet + OrderBy)
    auto insertUnique = [](std::vector<int>& set, int v) {
        if (v < 0) return;  // filtered by the [0, T] check below anyway
        auto it = std::lower_bound(set.begin(), set.end(), v);
        if (it == set.end() || *it != v) set.insert(it, v);
    };
    for (const SrNote& note : noteSeq) {
        insertUnique(baseSet, note.headMs);
        if (note.tailMs != -1) insertUnique(baseSet, note.tailMs);
    }
    // Expand only from the original elements (C# iterates over baseSet.ToArray()).
    std::vector<int> original = baseSet;
    for (int s : original) {
        insertUnique(baseSet, s + 501);
        insertUnique(baseSet, s - 499);
        insertUnique(baseSet, s + 1);  // resolves the Dirac delta at exact note positions
    }
    insertUnique(baseSet, 0);
    insertUnique(baseSet, T);
    baseCorners.clear();
    for (int s : baseSet)
        if (0 <= s && s <= T) baseCorners.push_back(s);

    std::vector<int> aSet;
    for (const SrNote& note : noteSeq) {
        insertUnique(aSet, note.headMs);
        if (note.tailMs != -1) insertUnique(aSet, note.tailMs);
    }
    original = aSet;
    for (int s : original) {
        insertUnique(aSet, s + 1000);
        insertUnique(aSet, s - 1000);
    }
    insertUnique(aSet, 0);
    insertUnique(aSet, T);
    aCorners.clear();
    for (int s : aSet)
        if (0 <= s && s <= T) aCorners.push_back(s);

    allCorners.clear();
    std::set_union(baseCorners.begin(), baseCorners.end(), aCorners.begin(), aCorners.end(),
                   std::back_inserter(allCorners));
}

void getKeyUsage(const std::vector<SrNote>& noteSeq, int K, int T,
                 const std::vector<int>& baseCorners, std::vector<unsigned char>& keyUsage) {
    const size_t nc = baseCorners.size();
    keyUsage.assign(static_cast<size_t>(K) * nc, 0);
    for (const SrNote& note : noteSeq) {
        const int startTime = std::max(note.headMs - kKeyUsageHalfWindowMs, 0);
        const int endTime = note.tailMs == -1 ? note.headMs + kKeyUsageHalfWindowMs
                                              : std::min(note.tailMs + kKeyUsageHalfWindowMs, T - 1);
        const int leftIdx = searchSortedLeft(baseCorners, startTime);
        const int rightIdx = searchSortedLeft(baseCorners, endTime);
        for (int i = leftIdx; i < rightIdx; ++i) keyUsage[static_cast<size_t>(note.column) * nc + i] = 1;
    }
}

void getKeyUsage400(const std::vector<SrNote>& noteSeq, int K, int T,
                    const std::vector<int>& baseCorners, std::vector<double>& keyUsage400) {
    const size_t nc = baseCorners.size();
    keyUsage400.assign(static_cast<size_t>(K) * nc, 0.0);
    for (const SrNote& note : noteSeq) {
        const int startTime = std::max(note.headMs, 0);
        const int endTime = note.tailMs == -1 ? note.headMs : std::min(note.tailMs, T - 1);
        const int left400Idx = searchSortedLeft(baseCorners, startTime - static_cast<int>(kUsage400FlankMs));
        const int leftIdx = searchSortedLeft(baseCorners, startTime);
        const int rightIdx = searchSortedLeft(baseCorners, endTime);
        const int right400Idx = searchSortedLeft(baseCorners, endTime + static_cast<int>(kUsage400FlankMs));
        for (int i = leftIdx; i < rightIdx; ++i)
            keyUsage400[static_cast<size_t>(note.column) * nc + i] +=
                kUsage400Core + std::min(endTime - startTime, static_cast<int>(kUsage400CapMs)) / kUsage400CoreDiv;
        for (int i = left400Idx; i < leftIdx; ++i) {
            const double diff = static_cast<double>(baseCorners[i] - startTime);
            keyUsage400[static_cast<size_t>(note.column) * nc + i] +=
                kUsage400Core - kUsage400Core / (kUsage400FlankMs * kUsage400FlankMs) * diff * diff;
        }
        for (int i = rightIdx; i < right400Idx; ++i) {
            const double diff = static_cast<double>(baseCorners[i] - endTime);
            keyUsage400[static_cast<size_t>(note.column) * nc + i] +=
                kUsage400Core - kUsage400Core / (kUsage400FlankMs * kUsage400FlankMs) * diff * diff;
        }
    }
}

void computeAnchor(int K, const std::vector<double>& keyUsage400,
                   const std::vector<int>& baseCorners, std::vector<double>& anchor) {
    const size_t nc = baseCorners.size();
    anchor.assign(nc, 0.0);
    std::vector<double> nonzero;
    for (size_t idx = 0; idx < nc; ++idx) {
        nonzero.clear();
        for (int k = 0; k < K; ++k) {
            const double c = keyUsage400[static_cast<size_t>(k) * nc + idx];
            if (c != 0.0) nonzero.push_back(c);
        }
        std::sort(nonzero.begin(), nonzero.end(), std::greater<double>());  // OrderByDescending
        if (nonzero.size() > 1) {
            double walk = 0.0;
            double maxWalk = 0.0;
            for (size_t i = 0; i + 1 < nonzero.size(); ++i) {
                const double ratio = nonzero[i + 1] / nonzero[i];
                walk += nonzero[i] * (1.0 - 4.0 * std::pow(0.5 - ratio, 2.0));
                maxWalk += nonzero[i];
            }
            anchor[idx] = walk / maxWalk;
        } else {
            anchor[idx] = 0.0;
        }
    }
    for (double& a : anchor) a = 1.0 + std::min(a - 0.18, 5.0 * std::pow(a - 0.22, 3.0));
}

void lnBodiesCountSparseRepresentation(const std::vector<SrNote>& lnSeq, int T,
                                       std::vector<int>& points, std::vector<double>& cumsum,
                                       std::vector<double>& values) {
    // Models the C# Dictionary<int,double> of time -> change.
    std::vector<std::pair<int, double>> diff;
    auto addDiff = [&diff](int t, double d) {
        auto it = std::lower_bound(diff.begin(), diff.end(), t,
                                   [](const std::pair<int, double>& p, int key) { return p.first < key; });
        if (it != diff.end() && it->first == t)
            it->second += d;
        else
            diff.insert(it, {t, d});
    };
    for (const SrNote& ln : lnSeq) {
        const int t0 = std::min(ln.headMs + static_cast<int>(kLnBodyFirstMs), ln.tailMs);
        const int t1 = std::min(ln.headMs + static_cast<int>(kLnBodySecondMs), ln.tailMs);
        addDiff(t0, kLnBodyFirstAdd);
        addDiff(t1, kLnBodySecondAdd);  // net -1.3 + 1
        addDiff(ln.tailMs, kLnBodyTailAdd);
    }

    points.clear();
    points.push_back(0);
    points.push_back(T);
    for (const auto& kv : diff) points.push_back(kv.first);
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());

    values.assign(points.size() - 1, 0.0);
    cumsum.assign(points.size(), 0.0);
    double curr = 0.0;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const int t = points[i];
        auto it = std::lower_bound(diff.begin(), diff.end(), t,
                                   [](const std::pair<int, double>& p, int key) { return p.first < key; });
        if (it != diff.end() && it->first == t) curr += it->second;
        const double v = std::min(curr, kLnBodySaturation + 0.5 * curr);
        values[i] = v;
        cumsum[i + 1] = cumsum[i] + static_cast<double>(points[i + 1] - points[i]) * v;
    }
}

double lnSum(int a, int b, const std::vector<int>& points, const std::vector<double>& cumsum,
             const std::vector<double>& values) {
    const int i = searchSortedRight(points, a) - 1;
    const int j = searchSortedRight(points, b) - 1;
    double total = 0.0;
    if (i == j) {
        total = static_cast<double>(b - a) * values[i];
    } else {
        total += static_cast<double>(points[i + 1] - a) * values[i];
        total += cumsum[j] - cumsum[i + 1];
        total += static_cast<double>(b - points[j]) * values[j];
    }
    return total;
}

void computeJbar(int K, double x,
                 const std::vector<std::vector<SrNote>>& noteSeqByColumn,
                 const std::vector<int>& baseCorners, std::vector<double>& deltaKs,
                 std::vector<double>& jBar) {
    const size_t nc = baseCorners.size();
    std::vector<double> jKs(static_cast<size_t>(K) * nc, 0.0);
    deltaKs.assign(static_cast<size_t>(K) * nc, 1e9);

    auto jackNerfer = [](double delta) {
        return 1.0 - 7e-5 * std::pow(0.15 + std::fabs(delta - 0.08), -4.0);
    };

    for (int k = 0; k < K; ++k) {
        const std::vector<SrNote>& notes = noteSeqByColumn[k];
        for (size_t i = 0; i + 1 < notes.size(); ++i) {
            const int start = notes[i].headMs;
            const int end = notes[i + 1].headMs;
            const int leftIdx = searchSortedLeft(baseCorners, start);
            const int rightIdx = searchSortedLeft(baseCorners, end);
            if (leftIdx >= rightIdx) continue;
            const double delta = 0.001 * static_cast<double>(end - start);
            const double val = (1.0 / delta) * (1.0 / (delta + 0.11 * std::pow(x, 1.0 / 4.0)));
            const double jVal = val * jackNerfer(delta);
            for (int idx = leftIdx; idx < rightIdx; ++idx) {
                jKs[static_cast<size_t>(k) * nc + idx] = jVal;
                deltaKs[static_cast<size_t>(k) * nc + idx] = delta;
            }
        }
    }

    // Per-column 500ms sum smoothing (scale 0.001), then power-mean-5 aggregation across
    // columns weighted by 1/delta.
    std::vector<double> jBarKs(static_cast<size_t>(K) * nc, 0.0);
    std::vector<double> rowData(nc);
    for (int k = 0; k < K; ++k) {
        for (size_t i = 0; i < nc; ++i) rowData[i] = jKs[static_cast<size_t>(k) * nc + i];
        const std::vector<double> smoothed = smoothOnCorners(baseCorners, rowData, 500, 0.001, false);
        for (size_t i = 0; i < nc; ++i) jBarKs[static_cast<size_t>(k) * nc + i] = smoothed[i];
    }

    jBar.assign(nc, 0.0);
    for (size_t i = 0; i < nc; ++i) {
        double num = 0.0;
        double den = 0.0;
        for (int k = 0; k < K; ++k) {
            const double v = jBarKs[static_cast<size_t>(k) * nc + i];
            const double w = 1.0 / deltaKs[static_cast<size_t>(k) * nc + i];
            num += std::pow(std::max(v, 0.0), 5.0) * w;
            den += w;
        }
        jBar[i] = std::pow(num / std::max(1e-9, den), 1.0 / 5.0);
    }
}

bool activeColumnsContains(const std::vector<int>& cols, int value) {
    return std::find(cols.begin(), cols.end(), value) != cols.end();
}

std::vector<double> computeXbar(int K, double x, const std::vector<std::vector<SrNote>>& noteSeqByColumn,
                                const std::vector<std::vector<int>>& activeColumns,
                                const std::vector<int>& baseCorners) {
    // crossMatrix[K] has K+1 entries (indices 0..K); row k indexes the (k-1, k) column pair,
    // where 0 and K are the outer virtual pairs and the centre 0.05 (odd K) is the
    // "same-column" pair. Values copied verbatim from SRCalculator.cs.
    static const double kCrossMatrix[11][12] = {
        {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0.075, 0.075, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0.125, 0.05, 0.125, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0.125, 0.125, 0.125, 0.125, 0, 0, 0, 0, 0, 0, 0, 0},
        {0.175, 0.25, 0.05, 0.25, 0.175, 0, 0, 0, 0, 0, 0, 0},
        {0.175, 0.25, 0.175, 0.175, 0.25, 0.175, 0, 0, 0, 0, 0, 0},
        {0.225, 0.35, 0.25, 0.05, 0.25, 0.35, 0.225, 0, 0, 0, 0, 0},
        {0.225, 0.35, 0.25, 0.225, 0.225, 0.25, 0.35, 0.225, 0, 0, 0, 0},
        {0.275, 0.45, 0.35, 0.25, 0.05, 0.25, 0.35, 0.45, 0.275, 0, 0, 0},
        {0.275, 0.45, 0.35, 0.25, 0.275, 0.275, 0.25, 0.35, 0.45, 0.275, 0, 0},
        {0.325, 0.55, 0.45, 0.35, 0.25, 0.05, 0.25, 0.35, 0.45, 0.55, 0.325, 0}};
    const double* crossCoeff = kCrossMatrix[K];

    const size_t nc = baseCorners.size();
    std::vector<double> xKs(static_cast<size_t>(K + 1) * nc, 0.0);
    std::vector<double> fastCross(static_cast<size_t>(K + 1) * nc, 0.0);

    for (int k = 0; k <= K; ++k) {
        std::vector<SrNote> notesInPair;
        if (k == 0) {
            notesInPair = noteSeqByColumn[0];
        } else if (k == K) {
            notesInPair = noteSeqByColumn[K - 1];
        } else {
            notesInPair = noteSeqByColumn[k - 1];
            notesInPair.insert(notesInPair.end(), noteSeqByColumn[k].begin(), noteSeqByColumn[k].end());
            std::stable_sort(notesInPair.begin(), notesInPair.end(),
                             [](const SrNote& a, const SrNote& b) { return a.headMs < b.headMs; });
        }

        for (size_t i = 1; i < notesInPair.size(); ++i) {
            const int start = notesInPair[i - 1].headMs;
            const int end = notesInPair[i].headMs;
            const int idxStart = searchSortedLeft(baseCorners, start);
            const int idxEnd = searchSortedLeft(baseCorners, end);
            if (idxStart >= idxEnd) continue;

            const double delta = 0.001 * static_cast<double>(end - start);
            double val = 0.16 * std::pow(std::max(x, delta), -2.0);

            const bool condition1 = !activeColumnsContains(activeColumns[idxStart], k - 1) &&
                                    !activeColumnsContains(activeColumns[idxEnd], k - 1);
            const bool condition2 = !activeColumnsContains(activeColumns[idxStart], k) &&
                                    !activeColumnsContains(activeColumns[idxEnd], k);
            if (condition1 || condition2) val *= (1.0 - crossCoeff[k]);

            const double fc = std::max(
                0.0, 0.4 * std::pow(std::max(delta, std::max(0.06, 0.75 * x)), -2.0) - 80.0);
            for (int idx = idxStart; idx < idxEnd; ++idx) {
                xKs[static_cast<size_t>(k) * nc + idx] = val;
                fastCross[static_cast<size_t>(k) * nc + idx] = fc;
            }
        }
    }

    std::vector<double> xBase(nc, 0.0);
    for (size_t i = 0; i < nc; ++i) {
        double sum1 = 0.0;
        for (int k = 0; k <= K; ++k) sum1 += xKs[static_cast<size_t>(k) * nc + i] * crossCoeff[k];
        double sum2 = 0.0;
        for (int k = 0; k < K; ++k)
            sum2 += std::sqrt(fastCross[static_cast<size_t>(k) * nc + i] * crossCoeff[k] *
                              fastCross[static_cast<size_t>(k + 1) * nc + i] * crossCoeff[k + 1]);
        xBase[i] = sum1 + sum2;
    }
    return smoothOnCorners(baseCorners, xBase, 500, 0.001, false);
}

std::vector<double> computePbar(int K, double x, const std::vector<SrNote>& noteSeq,
                                const std::vector<int>& points, const std::vector<double>& cumsum,
                                const std::vector<double>& values, const std::vector<double>& anchor,
                                const std::vector<int>& baseCorners) {
    const size_t nc = baseCorners.size();
    std::vector<double> pStep(nc, 0.0);

    auto streamBooster = [](double delta) {
        const double ratio = 7.5 / delta;
        return (160.0 < ratio && ratio < 360.0)
                   ? 1.0 + 1.7e-7 * (ratio - 160.0) * std::pow(ratio - 360.0, 2.0)
                   : 1.0;
    };

    for (size_t i = 0; i + 1 < noteSeq.size(); ++i) {
        const int hL = noteSeq[i].headMs;
        const int hR = noteSeq[i + 1].headMs;
        const int deltaTime = hR - hL;

        if (deltaTime < 1e-9) {
            // Chord (Dirac delta): spike at the exact head position.
            const double spike = 1000.0 * std::pow(0.02 * (4.0 / x - 24.0), 1.0 / 4.0);
            const int leftIdx = searchSortedLeft(baseCorners, hL);
            const int rightIdx = searchSortedRight(baseCorners, hL);
            for (int idx = leftIdx; idx < rightIdx; ++idx) pStep[idx] += spike;
        } else {
            const int leftIdx = searchSortedLeft(baseCorners, hL);
            const int rightIdx = searchSortedLeft(baseCorners, hR);
            if (leftIdx >= rightIdx) continue;

            const double delta = 0.001 * static_cast<double>(deltaTime);
            const double v = 1.0 + 6.0 * 0.001 * lnSum(hL, hR, points, cumsum, values);
            const double bVal = streamBooster(delta);

            double inc;
            if (delta < 2.0 * x / 3.0) {
                inc = (1.0 / delta) *
                      std::pow(0.08 / x * (1.0 - 24.0 / x * std::pow(delta - x / 2.0, 2.0)), 1.0 / 4.0) *
                      std::max(bVal, v);
            } else {
                inc = (1.0 / delta) *
                      std::pow(0.08 / x * (1.0 - 24.0 / x * std::pow(x / 6.0, 2.0)), 1.0 / 4.0) *
                      std::max(bVal, v);
            }
            for (int idx = leftIdx; idx < rightIdx; ++idx)
                pStep[idx] += std::min(inc * anchor[idx], std::max(inc, inc * 2.0 - 10.0));
        }
    }
    return smoothOnCorners(baseCorners, pStep, 500, 0.001, false);
}

std::vector<double> computeAbar(int K, const std::vector<std::vector<SrNote>>&,
                                const std::vector<std::vector<int>>& activeColumns,
                                const std::vector<double>& deltaKs, const std::vector<int>& aCorners,
                                const std::vector<int>& baseCorners) {
    const size_t nc = baseCorners.size();
    std::vector<double> dks(static_cast<size_t>(K > 0 ? K - 1 : 0) * nc, 0.0);
    for (size_t i = 0; i < nc; ++i) {
        const std::vector<int>& cols = activeColumns[i];
        for (size_t j = 0; j + 1 < cols.size(); ++j) {
            const int k0 = cols[j];
            const int k1 = cols[j + 1];
            const double d0 = deltaKs[static_cast<size_t>(k0) * nc + i];
            const double d1 = deltaKs[static_cast<size_t>(k1) * nc + i];
            dks[static_cast<size_t>(k0) * nc + i] =
                std::fabs(d0 - d1) + 0.4 * std::max(0.0, std::max(d0, d1) - 0.11);
        }
    }

    std::vector<double> aStep(aCorners.size(), 1.0);
    for (size_t i = 0; i < aCorners.size(); ++i) {
        int idx = searchSortedLeft(baseCorners, aCorners[i]);
        if (idx >= static_cast<int>(baseCorners.size())) idx = static_cast<int>(baseCorners.size()) - 1;
        const std::vector<int>& cols = activeColumns[idx];
        for (size_t j = 0; j + 1 < cols.size(); ++j) {
            const int k0 = cols[j];
            const int k1 = cols[j + 1];
            const double dVal = dks[static_cast<size_t>(k0) * nc + idx];
            const double maxDelta = std::max(deltaKs[static_cast<size_t>(k0) * nc + idx],
                                             deltaKs[static_cast<size_t>(k1) * nc + idx]);
            if (dVal < 0.02) {
                aStep[i] *= std::min(0.75 + 0.5 * maxDelta, 1.0);
            } else if (dVal < 0.07) {
                aStep[i] *= std::min(0.65 + 5.0 * dVal + 0.5 * maxDelta, 1.0);
            }
        }
    }
    return smoothOnCorners(aCorners, aStep, 250, 1.0, true);
}

std::vector<double> computeRbar(int K, double x, const std::vector<std::vector<SrNote>>& noteSeqByColumn,
                                const std::vector<SrNote>& tailSeq, const std::vector<int>& baseCorners) {
    const size_t nc = baseCorners.size();
    std::vector<double> rStep(nc, 0.0);

    std::vector<std::vector<int>> timesByColumn(noteSeqByColumn.size());
    for (size_t i = 0; i < noteSeqByColumn.size(); ++i) {
        timesByColumn[i].reserve(noteSeqByColumn[i].size());
        for (const SrNote& n : noteSeqByColumn[i]) timesByColumn[i].push_back(n.headMs);
    }

    // Release index per LN (in tailSeq order). NOTE: iArr from the C# source is dead code
    // (assigned, never read) and is intentionally not ported.
    std::vector<double> iList;
    iList.reserve(tailSeq.size());
    for (const SrNote& ln : tailSeq) {
        const SrNote nextNote = findNextNoteInColumn(ln, timesByColumn[ln.column], noteSeqByColumn);
        const double iH = 0.001 * std::fabs(static_cast<double>(ln.tailMs - ln.headMs - 80)) / x;
        const double iT = 0.001 * std::fabs(static_cast<double>(nextNote.headMs - ln.tailMs - 80)) / x;
        iList.push_back(2.0 / (2.0 + std::exp(-5.0 * (iH - 0.75)) + std::exp(-5.0 * (iT - 0.75))));
    }

    for (size_t i = 0; i + 1 < tailSeq.size(); ++i) {
        const int tStart = tailSeq[i].tailMs;
        const int tEnd = tailSeq[i + 1].tailMs;
        const int leftIdx = searchSortedLeft(baseCorners, tStart);
        const int rightIdx = searchSortedLeft(baseCorners, tEnd);
        if (leftIdx >= rightIdx) continue;
        const double deltaR = 0.001 * static_cast<double>(tailSeq[i + 1].tailMs - tailSeq[i].tailMs);
        const double rVal = 0.08 * std::pow(deltaR, -0.5) * (1.0 / x) *
                            (1.0 + 0.8 * (iList[i] + iList[i + 1]));
        for (int idx = leftIdx; idx < rightIdx; ++idx) rStep[idx] = rVal;  // assignment, not +=
    }
    return smoothOnCorners(baseCorners, rStep, 500, 0.001, false);
}

void computeCAndKs(int K, const std::vector<SrNote>& noteSeq, const std::vector<unsigned char>& keyUsage,
                   const std::vector<int>& baseCorners, std::vector<double>& cStep,
                   std::vector<double>& ksStep) {
    const size_t nc = baseCorners.size();
    std::vector<int> noteHitTimes;
    noteHitTimes.reserve(noteSeq.size());
    for (const SrNote& n : noteSeq) noteHitTimes.push_back(n.headMs);
    std::sort(noteHitTimes.begin(), noteHitTimes.end());

    cStep.assign(nc, 0.0);
    for (size_t i = 0; i < nc; ++i) {
        const int s = baseCorners[i];
        const int highIdx = searchSortedLeft(noteHitTimes, s + 500);
        const int lowIdx = searchSortedLeft(noteHitTimes, s - 500);
        cStep[i] = static_cast<double>(highIdx - lowIdx);
    }

    ksStep.assign(nc, 0.0);
    for (size_t i = 0; i < nc; ++i) {
        int count = 0;
        for (int k = 0; k < K; ++k)
            if (keyUsage[static_cast<size_t>(k) * nc + i]) ++count;
        ksStep[i] = static_cast<double>(std::max(count, 1));
    }
}

double calculateFromInternals(const SrrInternals& it) {
    const size_t n = it.allCorners.size();
    std::vector<double> sAll(n), tAll(n), dAll(n);
    for (size_t i = 0; i < n; ++i) {
        const double term1 = std::pow(it.aBarAll[i], 3.0 / it.ksArr[i]) *
                             std::min(it.jBarAll[i], 8.0 + 0.85 * it.jBarAll[i]);
        const double term2 = std::pow(it.aBarAll[i], 2.0 / 3.0) *
                             (0.8 * it.pBarAll[i] + it.rBarAll[i] * 35.0 / (it.cArr[i] + 8.0));
        sAll[i] = std::pow(0.4 * std::pow(term1, 1.5) + (1.0 - 0.4) * std::pow(term2, 1.5), 2.0 / 3.0);
        tAll[i] = std::pow(it.aBarAll[i], 3.0 / it.ksArr[i]) * it.xBarAll[i] /
                  (it.xBarAll[i] + sAll[i] + 1.0);
        dAll[i] = 2.7 * std::pow(sAll[i], 0.5) * std::pow(tAll[i], 1.5) + sAll[i] * 0.27;
    }

    // Effective weight per corner: C times half the distance to the neighbouring corners.
    std::vector<double> gaps(n, 0.0);
    gaps.front() = static_cast<double>(it.allCorners[1] - it.allCorners[0]) / 2.0;
    gaps.back() = static_cast<double>(it.allCorners[n - 1] - it.allCorners[n - 2]) / 2.0;
    for (size_t i = 1; i + 1 < n; ++i)
        gaps[i] = static_cast<double>(it.allCorners[i + 1] - it.allCorners[i - 1]) / 2.0;

    std::vector<double> effectiveWeights(n, 0.0);
    for (size_t i = 0; i < n; ++i) effectiveWeights[i] = it.cArr[i] * gaps[i];

    std::vector<size_t> sortedIndices(n);
    for (size_t i = 0; i < n; ++i) sortedIndices[i] = i;
    std::stable_sort(sortedIndices.begin(), sortedIndices.end(),
                     [&](size_t a, size_t b) { return dAll[a] < dAll[b]; });

    std::vector<double> cumWeights(n, 0.0);
    cumWeights[0] = effectiveWeights[sortedIndices[0]];
    for (size_t i = 1; i < n; ++i)
        cumWeights[i] = cumWeights[i - 1] + effectiveWeights[sortedIndices[i]];
    const double totalWeight = cumWeights.back();

    // Target percentiles {0.945, 0.935, 0.925, 0.915, 0.845, 0.835, 0.825, 0.815}; first
    // normalised cumulative weight >= target, last element as fallback.
    static const double kTargetPercentiles[8] = {0.945, 0.935, 0.925, 0.915, 0.845, 0.835, 0.825, 0.815};
    size_t indices[8];
    for (int t = 0; t < 8; ++t) {
        indices[t] = n - 1;
        for (size_t i = 0; i < n; ++i) {
            if (cumWeights[i] / totalWeight >= kTargetPercentiles[t]) {
                indices[t] = i;
                break;
            }
        }
    }
    const double percentile93 = (dAll[sortedIndices[indices[0]]] + dAll[sortedIndices[indices[1]]] +
                                 dAll[sortedIndices[indices[2]]] + dAll[sortedIndices[indices[3]]]) /
                                4.0;
    const double percentile83 = (dAll[sortedIndices[indices[4]]] + dAll[sortedIndices[indices[5]]] +
                                 dAll[sortedIndices[indices[6]]] + dAll[sortedIndices[indices[7]]]) /
                                4.0;

    double weightedSum = 0.0;
    double weightSum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        weightedSum += std::pow(dAll[sortedIndices[i]], 5.0) * effectiveWeights[sortedIndices[i]];
        weightSum += effectiveWeights[sortedIndices[i]];
    }
    const double weightedMean = std::pow(weightedSum / weightSum, 1.0 / 5.0);

    double sr = (0.88 * percentile93) * 0.25 + (0.94 * percentile83) * 0.2 + weightedMean * 0.55;
    // C# line "sr = Math.Pow(sr, 1.0) / Math.Pow(8, 1.0) * 8" is the identity and is skipped.

    double totalNotes = static_cast<double>(it.noteSeq.size());
    for (const SrNote& ln : it.lnSeq)
        totalNotes += 0.5 * std::min(static_cast<double>(ln.tailMs - ln.headMs), 1000.0) / 200.0;
    sr *= totalNotes / (totalNotes + 60.0);

    sr = rescaleHigh(sr);
    sr *= 0.975;
    return sr;
}

bool computeInternals(int keyCount, double od, const std::vector<SrNote>& notes, SrrInternals* out) {
    if (keyCount < 1 || keyCount > kMaxKeys) return false;
    if (static_cast<int>(notes.size()) < kMinNotes) return false;
    for (const SrNote& n : notes)
        if (n.column < 0 || n.column >= keyCount) return false;

    SrrInternals& it = *out;
    it.K = keyCount;

    // ---- PreProcess ----
    it.x = kXScale * std::pow((kXWindowBase - std::ceil(od * 3.0)) / kXWindowDiv, 0.5);
    it.x = std::min(it.x, kXCapSlope * (it.x - kXCapOffset) + kXCapOffset);

    it.noteSeq = notes;
    std::stable_sort(it.noteSeq.begin(), it.noteSeq.end(), [](const SrNote& a, const SrNote& b) {
        if (a.headMs != b.headMs) return a.headMs < b.headMs;
        return a.column < b.column;
    });

    it.noteSeqByCol.assign(keyCount, {});
    it.lnSeqByCol.assign(keyCount, {});
    for (const SrNote& note : it.noteSeq) {
        if (note.column < 0 || note.column >= keyCount) continue;  // guarded above; kept for parity
        it.noteSeqByCol[note.column].push_back(note);
        if (note.tailMs != -1) {
            it.lnSeq.push_back(note);
            it.lnSeqByCol[note.column].push_back(note);
        }
    }
    it.tailSeq = it.lnSeq;
    std::stable_sort(it.tailSeq.begin(), it.tailSeq.end(),
                     [](const SrNote& a, const SrNote& b) { return a.tailMs < b.tailMs; });

    if (it.tailSeq.empty())
        it.T = it.noteSeq.back().headMs + 1;
    else
        it.T = std::max(it.noteSeq.back().headMs, it.tailSeq.back().tailMs) + 1;

    // ---- Stages ----
    getCorners(it.noteSeq, it.T, it.baseCorners, it.aCorners, it.allCorners);
    getKeyUsage(it.noteSeq, it.K, it.T, it.baseCorners, it.keyUsage);

    const size_t nc = it.baseCorners.size();
    it.activeColumns.assign(nc, {});
    for (size_t i = 0; i < nc; ++i)
        for (int k = 0; k < it.K; ++k)
            if (it.keyUsage[static_cast<size_t>(k) * nc + i]) it.activeColumns[i].push_back(k);

    getKeyUsage400(it.noteSeq, it.K, it.T, it.baseCorners, it.keyUsage400);
    computeAnchor(it.K, it.keyUsage400, it.baseCorners, it.anchor);

    computeJbar(it.K, it.x, it.noteSeqByCol, it.baseCorners, it.deltaKs, it.jBar);
    it.jBarAll = interpValues(it.allCorners, it.baseCorners, it.jBar);

    it.xBar = computeXbar(it.K, it.x, it.noteSeqByCol, it.activeColumns, it.baseCorners);
    it.xBarAll = interpValues(it.allCorners, it.baseCorners, it.xBar);

    lnBodiesCountSparseRepresentation(it.lnSeq, it.T, it.lnPoints, it.lnCumsum, it.lnValues);

    it.pBar = computePbar(it.K, it.x, it.noteSeq, it.lnPoints, it.lnCumsum, it.lnValues, it.anchor,
                          it.baseCorners);
    it.pBarAll = interpValues(it.allCorners, it.baseCorners, it.pBar);

    it.aBar = computeAbar(it.K, it.noteSeqByCol, it.activeColumns, it.deltaKs, it.aCorners,
                          it.baseCorners);
    it.aBarAll = interpValues(it.allCorners, it.aCorners, it.aBar);

    it.rBar = computeRbar(it.K, it.x, it.noteSeqByCol, it.tailSeq, it.baseCorners);
    it.rBarAll = interpValues(it.allCorners, it.baseCorners, it.rBar);

    std::vector<double> cStep;
    std::vector<double> ksStep;
    computeCAndKs(it.K, it.noteSeq, it.keyUsage, it.baseCorners, cStep, ksStep);
    it.cArr = stepInterp(it.allCorners, it.baseCorners, cStep);
    it.ksArr = stepInterp(it.allCorners, it.baseCorners, ksStep);
    return true;
}

}  // namespace

bool SrrComputeInternals(int keyCount, double od, const std::vector<SrNote>& notes, SrrInternals* out) {
    return computeInternals(keyCount, od, notes, out);
}

double SrrStarRating(int keyCount, double od, const std::vector<SrNote>& notes) {
    SrrInternals it;
    if (!computeInternals(keyCount, od, notes, &it)) return -1.0;
    const double sr = calculateFromInternals(it);
    return std::isfinite(sr) ? sr : -1.0;
}

double SrrStarRatingLocal(int keyCount, double od, const std::vector<SrNote>& notes, int startMs,
                          int endMs) {
    if (keyCount < 1 || keyCount > kMaxKeys) return -1.0;
    std::vector<SrNote> selected;
    selected.reserve(notes.size());
    for (const SrNote& n : notes)
        if (n.headMs >= startMs - 500 && n.headMs <= endMs + 500) selected.push_back(n);
    if (static_cast<int>(selected.size()) < kMinNotes) return -1.0;  // same guard as global
    SrrInternals it;
    if (!computeInternals(keyCount, od, selected, &it)) return -1.0;
    const double sr = calculateFromInternals(it);
    return std::isfinite(sr) ? sr : -1.0;
}

}  // namespace mra
