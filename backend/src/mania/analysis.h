#pragma once
// Statistical analysis over matched HitEvents: offset distribution, per-column
// stats, early/late split, judgement counts, consistency report, segmentation.
#include "mania/accuracy.h"
#include "mania/hit_matcher.h"

#include <string>
#include <vector>

namespace mra {

struct Histogram {
    double bucketMs = 5.0;         // fixed 5 ms buckets
    int64_t minMs = -100;          // buckets span [-100, +100)
    int64_t maxMs = 100;
    std::vector<int64_t> buckets;  // 40 buckets
    int64_t below = 0;             // offsets < minMs
    int64_t above = 0;             // offsets >= maxMs

    Histogram() : buckets(40, 0) {}
};

struct OffsetStats {
    int64_t count = 0;
    double mean = 0.0;      // signed mean
    double meanAbs = 0.0;   // mean |offset|
    double median = 0.0;
    double sd = 0.0;
    double p95 = 0.0;       // 95th percentile of |offset|
    int64_t early = 0;      // offset < 0
    int64_t late = 0;       // offset > 0
    double earlyMean = 0.0;
    double lateMean = 0.0;
    double min = 0.0;
    double max = 0.0;
};

struct ColumnStats {
    int column = 0;
    JudgementCounts counts;
    OffsetStats offsets;    // over note heads + hold heads
};

struct JudgementBreakdownItem {
    HitResult result;
    int64_t count = 0;
    double share = 0.0;  // of judged events
};

struct ConsistencyReport {
    OffsetStats rolling20Sd;   // SD of per-20-note rolling mean offsets
    double chordOffsetSpread = 0.0;  // mean intra-chord spread of offsets
    int chordCount = 0;
    double tapStability = 0.0;       // 1 - (sd / 50), clamped [0,1], heuristic
    std::vector<double> rollingMeanSeries;  // per-20-note rolling mean offsets
};

struct SegmentAnalysis {
    int64_t startTimeMs = 0;
    int64_t endTimeMs = 0;
    JudgementCounts counts;      // heads + hold tails in range (matches global)
    JudgementCounts headCounts;  // heads only (notes + hold heads)
    OffsetStats offsets;
    double accuracyV1 = 0.0;
    double accuracyV2 = 0.0;
    // Stable per-object view (see GlobalAnalysis::stableCounts).
    JudgementCounts stableCounts;
    double stableAccuracyV1 = 0.0;
    double stableAccuracyV2 = 0.0;
    std::vector<ColumnStats> perColumn;
};

struct GlobalAnalysis {
    JudgementCounts counts;         // note heads + hold heads + hold tails
    JudgementCounts headCounts;     // note heads + hold heads only
    OffsetStats offsets;            // over non-miss note-head/hold-head events
    OffsetStats tailOffsets;        // hold-tail offsets (non-miss, early only)
    double accuracyV1 = 0.0;
    double accuracyV2 = 0.0;
    // Stable scoring view: ONE judgement per object (an LN is a single unit —
    // the combined V1 judgement). osu!stable .osr counters use this model;
    // lazer records LN heads and tails separately (= counts above).
    JudgementCounts stableCounts;
    double stableAccuracyV1 = 0.0;
    double stableAccuracyV2 = 0.0;
    Histogram histogram;
    std::vector<ColumnStats> perColumn;
    std::vector<JudgementBreakdownItem> breakdown;
    ConsistencyReport consistency;
    int64_t firstEventTimeMs = -1;
    int64_t lastEventTimeMs = -1;
    int unmatchedInputCount = 0;
};

// Computes full analysis for a match result. keyCount drives per-column split.
GlobalAnalysis analyzeMatch(const MatchResult& match, int keyCount);

// Sub-range analysis over events whose objectTimeMs falls in [start, end).
SegmentAnalysis analyzeSegment(const MatchResult& match, int keyCount,
                               int64_t startMs, int64_t endMs);

OffsetStats computeOffsetStats(const std::vector<double>& offsets);

}  // namespace mra
