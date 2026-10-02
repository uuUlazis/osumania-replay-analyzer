// Minimal assert-based unit tests for the mania analysis core.
#include "beatmap/osu_parser.h"
#include "mania/accuracy.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
#include "replay/replay_frames.h"

#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

using namespace mra;

static ReplayFrame frame(int64_t t, uint32_t down, uint32_t up) {
    ReplayFrame f;
    f.timeMs = t;
    f.keyDownMask = down;
    f.keyUpMask = up;
    return f;
}

static void testWindows() {
    ManiaWindows w = ManiaWindows::fromOD(8.0);
    CHECK(w.perfect == 16.0);
    CHECK(std::abs(w.great - 40.0) < 1e-9);
    CHECK(std::abs(w.miss - 164.0) < 1e-9);
    ManiaWindows w10 = ManiaWindows::fromOD(10.0);
    CHECK(std::abs(w10.meh - 121.0) < 1e-9);
}

static ManiaHitObject note(int col, int64_t t) {
    ManiaHitObject o;
    o.type = ManiaObjectType::Note;
    o.column = col;
    o.startTimeMs = t;
    return o;
}

static ManiaHitObject hold(int col, int64_t s, int64_t e) {
    ManiaHitObject o;
    o.type = ManiaObjectType::Hold;
    o.column = col;
    o.startTimeMs = s;
    o.endTimeMs = e;
    return o;
}

static void testSimpleNote() {
    std::vector<ManiaHitObject> objs = {note(0, 1000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1005, 1u << 0, 0),   // press col0, +5ms -> Perfect
        frame(1100, 0, 1u << 0),   // release
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK(r.events.size() == 1);
    CHECK(r.events[0].judgement == HitResult::Perfect);
    CHECK(r.events[0].offsetMs && *r.events[0].offsetMs == 5.0);
    CHECK(r.unmatchedObjects == 0);
    CHECK(r.unmatchedInputs.empty());
}

static void testMissNoInput() {
    std::vector<ManiaHitObject> objs = {note(1, 1000)};
    std::vector<ReplayFrame> frames = {frame(0, 0, 0), frame(3000, 0, 0)};
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK(r.events.size() == 1);
    CHECK(r.events[0].judgement == HitResult::Miss);
}

static void testChordDifferentColumns() {
    // Chord at t=1000 cols 0,1; hits at +3ms and +9ms.
    std::vector<ManiaHitObject> objs = {note(0, 1000), note(1, 1000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1003, (1u << 0), 0),
        frame(1009, (1u << 1), (1u << 0)),
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK(r.events.size() == 2);
    CHECK(r.events[0].judgement == HitResult::Perfect);
    CHECK(r.events[1].judgement == HitResult::Perfect);
}

static void testHoldTailEarlyRelease() {
    // Hold 1000..2000, head hit at 1000, released at 1980 (20ms early -> tail Great).
    std::vector<ManiaHitObject> objs = {hold(2, 1000, 2000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1000, 1u << 2, 0),
        frame(1980, 0, 1u << 2),
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK(r.holds.size() == 1);
    CHECK(r.holds[0].head.judgement == HitResult::Perfect);
    CHECK(r.holds[0].tail.judgement == HitResult::Great);
    CHECK(r.holds[0].tail.offsetMs && *r.holds[0].tail.offsetMs == -20.0);
    CHECK(r.holds[0].releasedEarly);
}

static void testHoldTailHeldThrough() {
    std::vector<ManiaHitObject> objs = {hold(2, 1000, 2000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(998, 1u << 2, 0),
        frame(2200, 0, 1u << 2),  // released well after end
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK(r.holds.size() == 1);
    CHECK(r.holds[0].tail.judgement == HitResult::Perfect);
    CHECK(r.holds[0].heldThroughTail);
    CHECK(!r.holds[0].releasedEarly);
}

static void testAccuracyV1V2() {
    JudgementCounts c;
    c.perfect = 95;
    c.great = 3;
    c.good = 1;
    c.ok = 0;
    c.meh = 0;
    c.miss = 1;
    double a1 = accuracyPercent(c, AccuracyProfile::LegacyScoreV1);
    // (300*98 + 200*1) / (300*100) * 100
    CHECK(std::abs(a1 - ((300.0 * 98 + 200.0) / 30000.0 * 100.0)) < 1e-9);
    double a2 = accuracyPercent(c, AccuracyProfile::LegacyScoreV2);
    // (305*95 + 300*3 + 200*1) / (305*100) * 100 — MAX counts as 305 under V2;
    // mixed judgements make V2 land slightly below V1 (305 ceiling > 300).
    CHECK(std::abs(a2 - ((305.0 * 95 + 300.0 * 3 + 200.0) / 30500.0 * 100.0)) < 1e-9);
    // All-MAX is exactly 100% under every profile.
    JudgementCounts allMax;
    allMax.perfect = 10;
    CHECK(std::abs(accuracyPercent(allMax, AccuracyProfile::LegacyScoreV1) - 100.0) < 1e-9);
    CHECK(std::abs(accuracyPercent(allMax, AccuracyProfile::LegacyScoreV2) - 100.0) < 1e-9);
    CHECK(std::abs(accuracyPercent(allMax, AccuracyProfile::ModernRuleset) - 100.0) < 1e-9);
}

static void testStats() {
    std::vector<double> v = {5, -3, 10, -8, 2};
    std::printf("[stats] computing\n"); std::fflush(stdout);
    OffsetStats s = computeOffsetStats(v);
    std::printf("[stats] computed\n"); std::fflush(stdout);
    CHECK(s.count == 5);
    CHECK(s.early == 2 && s.late == 3);
    CHECK(std::abs(s.mean - 1.2) < 1e-9);
    CHECK(s.median == 2.0);
    CHECK(s.p95 >= 8.0);
}

static void testAnalysis() {
    std::vector<ManiaHitObject> objs = {note(0, 1000), note(1, 2000),
                                        hold(2, 3000, 3200)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1002, 1u << 0, 0),
        frame(1100, 0, 1u << 0),
        frame(2010, 1u << 1, 0),
        frame(2100, 0, 1u << 1),
        frame(2999, 1u << 2, 0),
        frame(3250, 0, 1u << 2),
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    std::printf("[analysis] matching\n"); std::fflush(stdout);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    std::printf("[analysis] matched, events=%zu holds=%zu unmatched=%zu\n",
                r.events.size(), r.holds.size(), r.unmatchedInputs.size());
    std::fflush(stdout);
    GlobalAnalysis g = analyzeMatch(r, 4);
    std::printf("[analysis] analyzed\n"); std::fflush(stdout);
    CHECK(g.counts.judged() == 4);  // 2 note heads + hold head + tail
    CHECK(g.headCounts.judged() == 3);
    CHECK(g.perColumn.size() == 4);
    CHECK(g.perColumn[2].counts.judged() == 2);
    SegmentAnalysis seg = analyzeSegment(r, 4, 0, 1500);
    CHECK(seg.counts.judged() == 1);
    // A full-range segment must equal the global analysis exactly
    // (heads + hold tails), and must not crash on the headCounts pass.
    SegmentAnalysis segAll =
        analyzeSegment(r, 4, 0, g.lastEventTimeMs + 1);
    CHECK(segAll.counts.judged() == g.counts.judged());
    CHECK(segAll.headCounts.judged() == g.headCounts.judged());
}

#include <exception>

static void run(const char* name, void (*fn)()) {
    std::printf("[test] %s\n", name);
    std::fflush(stdout);
    try { fn(); } catch (const std::exception& e) {
        std::printf("EXCEPTION in %s: %s\n", name, e.what());
        std::fflush(stdout);
        ++g_failures;
    }
}

int main() {
    run("windows", testWindows);
    run("simpleNote", testSimpleNote);
    run("missNoInput", testMissNoInput);
    run("chord", testChordDifferentColumns);
    run("holdEarlyRelease", testHoldTailEarlyRelease);
    run("holdHeldThrough", testHoldTailHeldThrough);
    run("accuracy", testAccuracyV1V2);
    run("stats", testStats);
    run("analysis", testAnalysis);
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
