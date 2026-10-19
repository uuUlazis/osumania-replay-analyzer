#include "mania/analysis.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mra {
namespace {

double percentileAbs(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double idx = p * static_cast<double>(v.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(idx));
    size_t hi = static_cast<size_t>(std::ceil(idx));
    double frac = idx - static_cast<double>(lo);
    return std::abs(v[lo]) * (1.0 - frac) + std::abs(v[hi]) * frac;
}

}  // namespace

OffsetStats computeOffsetStats(const std::vector<double>& offsets) {
    OffsetStats s;
    s.count = static_cast<int64_t>(offsets.size());
    if (offsets.empty()) return s;
    double sum = 0.0, sumAbs = 0.0;
    s.min = offsets[0];
    s.max = offsets[0];
    for (double v : offsets) {
        sum += v;
        sumAbs += std::abs(v);
        s.min = std::min(s.min, v);
        s.max = std::max(s.max, v);
        if (v < 0) ++s.early;
        else if (v > 0) ++s.late;
    }
    s.mean = sum / static_cast<double>(s.count);
    s.meanAbs = sumAbs / static_cast<double>(s.count);
    double var = 0.0;
    for (double v : offsets) var += (v - s.mean) * (v - s.mean);
    s.sd = std::sqrt(var / static_cast<double>(s.count));
    std::vector<double> sorted = offsets;
    std::sort(sorted.begin(), sorted.end());
    double mid = static_cast<double>(s.count) / 2.0;
    s.median = (s.count % 2 == 1)
                   ? sorted[static_cast<size_t>(mid)]
                   : (sorted[static_cast<size_t>(mid) - 1] +
                      sorted[static_cast<size_t>(mid)]) / 2.0;
    s.p95 = percentileAbs(offsets, 0.95);
    std::vector<double> earlyV, lateV;
    for (double v : offsets) {
        if (v < 0) earlyV.push_back(v);
        else if (v > 0) lateV.push_back(v);
    }
    // Early/late means computed inline (recursing here would loop forever:
    // a negative-only subset always yields a negative-only subset).
    if (!earlyV.empty()) {
        double sum = 0.0;
        for (double v : earlyV) sum += v;
        s.earlyMean = sum / static_cast<double>(earlyV.size());
    }
    if (!lateV.empty()) {
        double sum = 0.0;
        for (double v : lateV) sum += v;
        s.lateMean = sum / static_cast<double>(lateV.size());
    }
    return s;
}

namespace {

void accumulateCounts(JudgementCounts& c, const std::vector<HitEvent>& events,
                      int64_t* first, int64_t* last) {
    for (const HitEvent& ev : events) {
        // Hold tails are tallied by the caller from holds[].tail; events also
        // carry them (renderer/API convenience), so skip to avoid double-count.
        if (ev.phase == HitPhase::HoldTail) continue;
        c.add(ev.judgement);
        if (first && (*first < 0 || ev.objectTimeMs < *first))
            *first = ev.objectTimeMs;
        if (last && ev.objectTimeMs > *last) *last = ev.objectTimeMs;
    }
}

std::vector<double> headOffsets(const std::vector<HitEvent>& events) {
    std::vector<double> out;
    for (const HitEvent& ev : events) {
        if (ev.phase == HitPhase::HoldTail) continue;
        if (ev.judgement == HitResult::Miss) continue;
        if (ev.offsetMs) out.push_back(*ev.offsetMs);
    }
    return out;
}

std::vector<ColumnStats> perColumnStats(const std::vector<HitEvent>& events,
                                        int keyCount) {
    std::vector<ColumnStats> cols(static_cast<size_t>(keyCount));
    for (int i = 0; i < keyCount; ++i) cols[static_cast<size_t>(i)].column = i;
    std::vector<std::vector<double>> offs(static_cast<size_t>(keyCount));
    for (const HitEvent& ev : events) {
        if (ev.phase == HitPhase::HoldTail) continue;  // caller adds tails
        if (ev.column < 0 || ev.column >= keyCount) continue;
        ColumnStats& cs = cols[static_cast<size_t>(ev.column)];
        cs.counts.add(ev.judgement);
        if (ev.phase != HitPhase::HoldTail && ev.judgement != HitResult::Miss &&
            ev.offsetMs)
            offs[static_cast<size_t>(ev.column)].push_back(*ev.offsetMs);
    }
    for (int i = 0; i < keyCount; ++i)
        cols[static_cast<size_t>(i)].offsets =
            computeOffsetStats(offs[static_cast<size_t>(i)]);
    return cols;
}

ConsistencyReport consistencyReport(const std::vector<HitEvent>& events) {
    ConsistencyReport r;
    // Rolling mean series over note-head offsets in chronological order.
    std::vector<double> headOffs;
    for (const HitEvent& ev : events) {
        if (ev.phase != HitPhase::HoldTail && ev.judgement != HitResult::Miss &&
            ev.offsetMs)
            headOffs.push_back(*ev.offsetMs);
    }
    const size_t W = 20;
    if (headOffs.size() >= W) {
        double sum = 0.0;
        for (size_t i = 0; i < W; ++i) sum += headOffs[i];
        r.rollingMeanSeries.push_back(sum / static_cast<double>(W));
        for (size_t i = W; i < headOffs.size(); ++i) {
            sum += headOffs[i] - headOffs[i - W];
            r.rollingMeanSeries.push_back(sum / static_cast<double>(W));
        }
        r.rolling20Sd = computeOffsetStats(r.rollingMeanSeries);
    }
    // Chord spread: group events by identical objectTimeMs across columns.
    std::map<int64_t, std::map<int, double>> chord;  // time -> col -> offset
    for (const HitEvent& ev : events) {
        if (ev.phase == HitPhase::HoldTail || !ev.offsetMs) continue;
        if (ev.judgement == HitResult::Miss) continue;
        chord[ev.objectTimeMs][ev.column] = *ev.offsetMs;
    }
    double spreadSum = 0.0;
    for (auto& [t, cols] : chord) {
        (void)t;
        if (cols.size() < 2) continue;
        double mn = 1e18, mx = -1e18;
        for (auto& [c, off] : cols) {
            (void)c;
            mn = std::min(mn, off);
            mx = std::max(mx, off);
        }
        spreadSum += (mx - mn);
        ++r.chordCount;
    }
    if (r.chordCount > 0)
        r.chordOffsetSpread = spreadSum / static_cast<double>(r.chordCount);
    // Tap stability heuristic: mapping mean |offset| spread into [0,1].
    if (!headOffs.empty()) {
        OffsetStats s = computeOffsetStats(headOffs);
        double stability = 1.0 - (s.sd / 50.0);
        r.tapStability = std::max(0.0, std::min(1.0, stability));
    }
    return r;
}

Histogram buildHistogram(const std::vector<double>& offsets, double bucketMs,
                         int64_t minMs, int64_t maxMs) {
    Histogram h;
    h.bucketMs = bucketMs;
    h.minMs = minMs;
    h.maxMs = maxMs;
    h.buckets.assign(
        static_cast<size_t>((maxMs - minMs) / bucketMs), 0);
    for (double v : offsets) {
        if (v < static_cast<double>(minMs)) { ++h.below; continue; }
        if (v >= static_cast<double>(maxMs)) { ++h.above; continue; }
        size_t idx = static_cast<size_t>((v - static_cast<double>(minMs)) /
                                         bucketMs);
        if (idx < h.buckets.size()) ++h.buckets[idx];
    }
    return h;
}

}  // namespace

GlobalAnalysis analyzeMatch(const MatchResult& match, int keyCount) {
    GlobalAnalysis g;
    accumulateCounts(g.counts, match.events, &g.firstEventTimeMs,
                     &g.lastEventTimeMs);
    // Hold tails count toward total judgement counts and per-column
    // accuracy; they are tallied from holds[].tail (accumulateCounts skips
    // the HoldTail copies in events).
    // DEDUPE holds tally: re-judged LNs can hold multiple entries; only
    // the final resolution per object counts (matches osu!stable).
    std::map<int, const HoldJudgement*> holdByObj;
    for (const HoldJudgement& h : match.holds) holdByObj[h.tail.objectIndex] = &h;  // DEDUPE holds tally
    for (const auto& [oi, hp] : holdByObj) {
        const HoldJudgement& h = *hp;
        g.counts.add(h.tail.judgement);
        // Tails were skipped inside accumulateCounts, so track their times
        // here: otherwise lastEventTimeMs ends before the final LN tail and
        // a full-range segment would exclude every tail.
        if (g.firstEventTimeMs < 0 ||
            h.tail.objectTimeMs < g.firstEventTimeMs)
            g.firstEventTimeMs = h.tail.objectTimeMs;
        if (h.tail.objectTimeMs > g.lastEventTimeMs)
            g.lastEventTimeMs = h.tail.objectTimeMs;
    }
    std::vector<HitEvent> heads;
    for (const HitEvent& ev : match.events) {
        if (ev.phase == HitPhase::HoldTail) continue;
        heads.push_back(ev);
    }
    accumulateCounts(g.headCounts, heads, &g.firstEventTimeMs,
                     &g.lastEventTimeMs);
    g.offsets = computeOffsetStats(headOffsets(match.events));
    std::vector<double> tailOffs;
    for (const auto& h : match.holds) {
        if (h.tail.judgement != HitResult::Miss && h.tail.offsetMs &&
            *h.tail.offsetMs < 0)
            tailOffs.push_back(*h.tail.offsetMs);
    }
    g.tailOffsets = computeOffsetStats(tailOffs);
    g.accuracyV1 = accuracyPercent(g.counts, AccuracyProfile::LegacyScoreV1);
    g.accuracyV2 = accuracyPercent(g.counts, AccuracyProfile::LegacyScoreV2);
    // Stable per-object tally: notes keep their head judgement, each LN
    // contributes ONE combined unit judgement (the matcher's v1Judgement).
    // This mirrors what osu!stable writes into the .osr counters; lazer
    // instead tallies LN heads and tails separately (g.counts).
    for (const HitEvent& ev : match.events) {
        if (ev.phase != HitPhase::NoteHead) continue;
        g.stableCounts.add(ev.judgement);
    }
    for (const auto& [oi, hp] : holdByObj)
        g.stableCounts.add(hp->tail.v1Judgement.value_or(hp->tail.judgement));
    g.stableAccuracyV1 =
        accuracyPercent(g.stableCounts, AccuracyProfile::LegacyScoreV1);
    g.stableAccuracyV2 =
        accuracyPercent(g.stableCounts, AccuracyProfile::LegacyScoreV2);
    g.histogram = buildHistogram(headOffsets(match.events), 5.0, -100, 100);
    g.perColumn = perColumnStats(match.events, keyCount);
    for (const auto& [oi, hp] : holdByObj) {
        (void)oi;
        if (hp->tail.column < 0 || hp->tail.column >= keyCount) continue;
        g.perColumn[static_cast<size_t>(hp->tail.column)].counts.add(
            hp->tail.judgement);
    }
    std::map<HitResult, int64_t> byResult;
    for (const HitEvent& ev : match.events) ++byResult[ev.judgement];
    int64_t total = g.counts.judged();
    for (auto& [res, cnt] : byResult) {
        JudgementBreakdownItem item;
        item.result = res;
        item.count = cnt;
        item.share = total ? static_cast<double>(cnt) /
                                 static_cast<double>(total) * 100.0
                           : 0.0;
        g.breakdown.push_back(item);
    }
    g.consistency = consistencyReport(match.events);
    g.unmatchedInputCount = static_cast<int>(match.unmatchedInputs.size());
    return g;
}

SegmentAnalysis analyzeSegment(const MatchResult& match, int keyCount,
                               int64_t startMs, int64_t endMs) {
    SegmentAnalysis seg;
    seg.startTimeMs = startMs;
    seg.endTimeMs = endMs;
    std::vector<HitEvent> inRange;
    for (const HitEvent& ev : match.events) {
        if (ev.objectTimeMs >= startMs && ev.objectTimeMs < endMs)
            inRange.push_back(ev);
    }
    accumulateCounts(seg.counts, inRange, &seg.startTimeMs, &seg.endTimeMs);
    // Hold tails live in holds[].tail, not events; a full-range segment must
    // match the global analysis exactly, so count tails inside the window.
    std::vector<HitEvent> tailsInRange;
    for (const HoldJudgement& hld : match.holds) {
        if (hld.tail.objectTimeMs >= startMs &&
            hld.tail.objectTimeMs < endMs) {
            seg.counts.add(hld.tail.judgement);
            tailsInRange.push_back(hld.tail);
        }
    }
    std::vector<HitEvent> headsOnly;
    for (const HitEvent& ev : inRange) {
        if (ev.phase == HitPhase::HoldTail) continue;
        headsOnly.push_back(ev);
    }
    accumulateCounts(seg.headCounts, headsOnly, nullptr, nullptr);
    std::vector<double> offs;
    for (const HitEvent& ev : headsOnly) {
        if (ev.judgement == HitResult::Miss) continue;
        if (ev.offsetMs) offs.push_back(*ev.offsetMs);
    }
    seg.offsets = computeOffsetStats(offs);
    seg.accuracyV1 = accuracyPercent(seg.counts, AccuracyProfile::LegacyScoreV1);
    seg.accuracyV2 = accuracyPercent(seg.counts, AccuracyProfile::LegacyScoreV2);
    // Stable per-object tally (attribute each LN at its tail time).
    for (const HitEvent& ev : inRange) {
        if (ev.phase != HitPhase::NoteHead) continue;
        seg.stableCounts.add(ev.judgement);
    }
    for (const HoldJudgement& hld : match.holds) {
        if (hld.tail.objectTimeMs >= startMs &&
            hld.tail.objectTimeMs < endMs)
            seg.stableCounts.add(
                hld.tail.v1Judgement.value_or(hld.tail.judgement));
    }
    seg.stableAccuracyV1 =
        accuracyPercent(seg.stableCounts, AccuracyProfile::LegacyScoreV1);
    seg.stableAccuracyV2 =
        accuracyPercent(seg.stableCounts, AccuracyProfile::LegacyScoreV2);
    seg.perColumn = perColumnStats(inRange, keyCount);
    for (const HoldJudgement& hld : match.holds) {
        if (hld.tail.objectTimeMs < startMs ||
            hld.tail.objectTimeMs >= endMs)
            continue;
        if (hld.tail.column < 0 || hld.tail.column >= keyCount) continue;
        seg.perColumn[static_cast<size_t>(hld.tail.column)].counts.add(
            hld.tail.judgement);
    }
    return seg;
}

}  // namespace mra
