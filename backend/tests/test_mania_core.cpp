// Minimal assert-based unit tests for the mania analysis core.
#include "beatmap/osu_parser.h"
#include "mania/accuracy.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
#include "replay/replay_frames.h"
#include "scores/scores_db.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

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

static void testStablePressSemantics() {
    // Stable V1 press rules, faithful to the reference OsuManiaJudger:
    //  * rice accept window is [-miss, +ok); a press past OK kills the note,
    //  * a grey press (between the 50 and miss windows) CONSUMES the note
    //    as a Miss — reproduces the endemize 12 labelled phantom misses,
    //  * the scan continues past too-late notes onto the next candidate.
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);

    // Grey early press eats note 1; the next press still hits note 2.
    std::vector<ManiaHitObject> objs = {note(0, 1000), note(0, 1200)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(853, 1u << 0, 0),    // -147ms grey press: consumes note 1
        frame(1020, 0, 1u << 0),   // release
        frame(1220, 1u << 0, 0),   // +20 vs note 2 -> Great
        frame(1400, 0, 1u << 0),   // release
    };
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK(r.events.size() == 2);
    CHECK(r.events[0].judgement == HitResult::Miss);
    CHECK(r.events[0].offsetMs && *r.events[0].offsetMs == -147.0);
    CHECK(r.events[1].judgement == HitResult::Great);
    CHECK(r.unmatchedInputs.empty());
    CHECK(r.unmatchedObjects == 0);

    // Isolated late press past the OK window: the note dies (too-late
    // deletion, sweep-style Miss without offset) and the press is unmatched.
    std::vector<ManiaHitObject> solo = {note(1, 1000)};
    std::vector<ReplayFrame> soloFrames = {
        frame(0, 0, 0),
        frame(1140, 1u << 1, 0),   // +140ms: past OK (103)
        frame(2000, 0, 1u << 1),
    };
    MatchResult r2 = matchReplayToObjects(solo, 4, soloFrames, opt);
    CHECK(r2.events.size() == 1);
    CHECK(r2.events[0].judgement == HitResult::Miss);
    CHECK(!r2.events[0].offsetMs.has_value());
    CHECK(r2.unmatchedInputs.size() == 1);

    // Scan-past: a too-late front note dies and the same press hits the
    // next note in the column.
    std::vector<ManiaHitObject> pair = {note(3, 1000), note(3, 1150)};
    std::vector<ReplayFrame> pairFrames = {
        frame(0, 0, 0),
        frame(1160, 1u << 3, 0),   // +160 vs note 1: too late; +10 vs note 2
        frame(2000, 0, 1u << 3),
    };
    MatchResult r4 = matchReplayToObjects(pair, 4, pairFrames, opt);
    CHECK(r4.events.size() == 2);
    CHECK(r4.events[0].judgement == HitResult::Miss);
    CHECK(r4.events[1].judgement == HitResult::Perfect);
    CHECK(r4.events[1].offsetMs && *r4.events[1].offsetMs == 10.0);

    // Isolated grey early press inside the miss window: stable CONSUMES the
    // note as a labelled Miss at the press time (endemize video evidence).
    std::vector<ManiaHitObject> early = {note(2, 2000)};
    std::vector<ReplayFrame> earlyFrames = {
        frame(0, 0, 0),
        frame(1860, 1u << 2, 0),   // -140ms: grey, no roll run -> consume
        frame(3000, 0, 1u << 2),
    };
    MatchResult r3 = matchReplayToObjects(early, 4, earlyFrames, opt);
    CHECK(r3.events.size() == 1);
    CHECK(r3.events[0].judgement == HitResult::Miss);
    CHECK(r3.events[0].offsetMs && *r3.events[0].offsetMs == -140.0);
    CHECK(r3.unmatchedInputs.empty());

    // Grey early press beyond the boundary ((50+miss)/2 = 145.5 at od8):
    // consumes the note as a Miss and is not unmatched.
    std::vector<ManiaHitObject> deep = {note(2, 2000)};
    std::vector<ReplayFrame> deepFrames = {
        frame(0, 0, 0),
        frame(1850, 1u << 2, 0),   // -150ms: beyond the boundary
        frame(3000, 0, 1u << 2),
    };
    MatchResult r7 = matchReplayToObjects(deep, 4, deepFrames, opt);
    CHECK(r7.events.size() == 1);
    CHECK(r7.events[0].judgement == HitResult::Miss);
    CHECK(r7.events[0].offsetMs && *r7.events[0].offsetMs == -150.0);
    CHECK(r7.unmatchedInputs.empty());

    // Rolling roll chain (Ice Angel col5): >=4 consecutive same-column grey
    // presses, each landing in (meh, miss) ahead of the next note with
    // <=1.6*OK spacing, are IGNORED; every following press then hits the
    // previous note normally and the chain self-heals.
    std::vector<ManiaHitObject> roll = {note(0, 1000), note(0, 1150), note(0, 1300),
                                        note(0, 1450), note(0, 1600)};
    std::vector<ReplayFrame> rollFrames = {
        frame(0, 0, 0),
        frame(860, 1u << 0, 0),    // -140 vs note 1: grey (run of 5) -> ignored
        frame(1000, 0, 1u << 0),
        frame(1010, 1u << 0, 0),   // +10 vs note 1 -> Perfect
        frame(1100, 0, 1u << 0),
        frame(1160, 1u << 0, 0),   // +10 vs note 2
        frame(1250, 0, 1u << 0),
        frame(1310, 1u << 0, 0),   // +10 vs note 3
        frame(1400, 0, 1u << 0),
        frame(1460, 1u << 0, 0),   // +10 vs note 4
        frame(1550, 0, 1u << 0),
        frame(1610, 1u << 0, 0),   // +10 vs note 5
        frame(1700, 0, 1u << 0),
    };
    MatchResult r8 = matchReplayToObjects(roll, 4, rollFrames, opt);
    CHECK(r8.events.size() == 5);
    for (const auto& ev : r8.events) {
        CHECK(ev.judgement == HitResult::Perfect);
        CHECK(ev.offsetMs && *ev.offsetMs == 10.0);
    }
    CHECK(r8.unmatchedObjects == 0);

    // Late press past the OK window but inside the 50 window does not hit:
    // the rice accept window ends at OK.
    std::vector<ManiaHitObject> late = {note(0, 1000)};
    std::vector<ReplayFrame> lateFrames = {
        frame(0, 0, 0),
        frame(1105, 1u << 0, 0),   // +105ms: past OK (103)
        frame(1300, 0, 1u << 0),
    };
    MatchResult r5 = matchReplayToObjects(late, 4, lateFrames, opt);
    CHECK(r5.events.size() == 1);
    CHECK(r5.events[0].judgement == HitResult::Miss);
    CHECK(!r5.events[0].offsetMs.has_value());
    CHECK(r5.unmatchedInputs.size() == 1);

    // LN deep break (release beyond tail−breakBound 185): the unit awaits a
    // re-grip instead of dying (深夜 XII; azimuth truth: 63 deep breaks all
    // credited). The re-press re-binds it (new head event) and the settle
    // happens at that cycle's release — the merge inputs come from the
    // LAST press (最后一次按键覆盖): startDiff 600 → Meh floor.
    std::vector<ManiaHitObject> ln = {hold(0, 1000, 2000)};
    std::vector<ReplayFrame> lnFrames = {
        frame(0, 0, 0),
        frame(1000, 1u << 0, 0),   // head press: Perfect
        frame(1500, 0, 1u << 0),   // release 500ms early: deep break (deferred)
        frame(1600, 1u << 0, 0),   // re-grip press re-binds the LN
        frame(2200, 0, 1u << 0),   // held through the tail -> settle
    };
    MatchResult r6 = matchReplayToObjects(ln, 4, lnFrames, opt);
    CHECK(r6.holds.size() == 1);  // exactly one stable unit for the LN
    CHECK(r6.events.size() == 3);
    CHECK(r6.holds[0].tail.v1Judgement.value_or(HitResult::Meh) ==
          HitResult::Meh);
    CHECK(r6.unmatchedInputs.empty());

    // Playback-stepped sweep (深夜 VII refactor): the frame clock MISSes an
    // unhit head at start+50w — before any later press. A press landing
    // past the LN head's 50 window finds the head gone (head+tail MISS
    // pair, no press offset) and flows on; it cannot bind the dead head.
    std::vector<ManiaHitObject> swept = {hold(2, 1000, 1300), note(2, 1500)};
    std::vector<ReplayFrame> sweptFrames = {
        frame(0, 0, 0),
        frame(1140, 1u << 2, 0),   // +140 vs LN head: past 50w (127) -> swept
        frame(1500, 0, 1u << 2),   // release
        frame(1510, 1u << 2, 0),   // +10 vs the rice -> Perfect
        frame(1700, 0, 1u << 2),
    };
    MatchResult r9 = matchReplayToObjects(swept, 4, sweptFrames, opt);
    CHECK(r9.events.size() == 3);
    CHECK(r9.events[0].phase == HitPhase::HoldHead);
    CHECK(r9.events[0].judgement == HitResult::Miss);
    CHECK(!r9.events[0].offsetMs.has_value());
    CHECK(r9.events[1].phase == HitPhase::HoldTail);
    CHECK(r9.events[1].judgement == HitResult::Miss);
    CHECK(r9.events[2].phase == HitPhase::NoteHead);
    CHECK(r9.events[2].judgement == HitResult::Perfect);
    CHECK(r9.events[2].offsetMs && *r9.events[2].offsetMs == 10.0);
    CHECK(r9.unmatchedInputs.size() == 1);  // the press past the dead head

    // 深夜 XIII: deep-break re-grip consistency — repeated deep re-breaks keep
    // deferring under the SAME bound; a shallow re-break settles the break
    // Miss (184 < bound 185 → dead), and the re-grip heads feed the merge.
    std::vector<ManiaHitObject> rb = {hold(0, 1000, 2000)};
    std::vector<ReplayFrame> rbFrames = {
        frame(0, 0, 0),
        frame(1000, 1u << 0, 0),   // head press
        frame(1400, 0, 1u << 0),   // break −600 (deep) -> defer
        frame(1500, 1u << 0, 0),   // re-grip #1
        frame(1550, 0, 1u << 0),   // re-break −450 (deep) -> defer
        frame(1600, 1u << 0, 0),   // re-grip #2
        frame(1700, 0, 1u << 0),   // re-break −300 (deep) -> defer
        frame(1750, 1u << 0, 0),   // re-grip #3
        frame(1850, 0, 1u << 0),   // re-break −150 (SHALLOW, <185) -> break Miss
    };
    MatchResult r10 = matchReplayToObjects(rb, 4, rbFrames, opt);
    CHECK(r10.holds.size() == 1);
    CHECK(r10.events.size() == 5);  // 4 re-bind heads + 1 tail
    CHECK(r10.holds[0].tail.v1Judgement.value_or(HitResult::Meh) ==
          HitResult::Miss);
    CHECK(r10.unmatchedInputs.empty());
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
    // Regression: the combined V1 LN judgement must survive into the stored
    // hold copies. A previous ordering bug assigned tail.v1Judgement AFTER
    // hold.tail/events were copied, so the field was always empty and the
    // stable tally silently fell back to the raw tail judgement.
    // head 0ms + tail 20ms early @ od8: perfect head window 19.2, sum 38.4.
    CHECK(r.holds[0].tail.v1Judgement.value_or(HitResult::Meh) ==
          HitResult::Perfect);
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
    // Stable per-object view: one judgement per object (an LN is a single
    // unit tallied from its tail), so 2 notes + 1 hold -> 3 judgements.
    CHECK(g.stableCounts.judged() == 3);
    // Held-through tail released 50ms past the end merges to Great under the
    // reference rules (endDiff enters the sum); V1 ACC is unchanged (300 both).
    CHECK(g.stableCounts.perfect == 2);
    CHECK(g.stableCounts.great == 1);
    CHECK(std::abs(g.stableAccuracyV1 - 100.0) < 1e-9);
    CHECK(segAll.stableCounts.judged() == g.stableCounts.judged());
    // Segment covering only the hold: head + tail events count 2, the
    // stable view tallies the LN once.
    SegmentAnalysis segHold = analyzeSegment(r, 4, 2500, 4000);
    CHECK(segHold.counts.judged() == 2);
    CHECK(segHold.stableCounts.judged() == 1);
    CHECK(std::abs(segHold.stableAccuracyV1 - 100.0) < 1e-9);
}

// ---- scores.db parser (synthetic byte stream) ----

static void put8(std::string& b, uint8_t v) { b.push_back(static_cast<char>(v)); }
static void put16(std::string& b, uint16_t v) {
    for (int i = 0; i < 2; ++i)
        b.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
static void put32(std::string& b, int32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
static void put64(std::string& b, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        b.push_back(static_cast<char>((u >> (8 * i)) & 0xff));
}
static void putOsuString(std::string& b, const std::string& s) {
    put8(b, 0x0b);
    size_t len = s.size();
    do {
        uint8_t byte = static_cast<uint8_t>(len & 0x7f);
        len >>= 7;
        if (len) byte |= 0x80;
        put8(b, byte);
    } while (len);
    b.append(s);
}
static void putEmptyString(std::string& b) { put8(b, 0x00); }

static void putScore(std::string& b, const std::string& md5,
                     const std::string& player, const std::string& replayMd5,
                     int32_t mods, bool writeHpString, int64_t onlineId) {
    put8(b, 3);                    // mania
    put32(b, 20260924);            // client version
    putOsuString(b, md5);
    putOsuString(b, player);
    putOsuString(b, replayMd5);
    put16(b, 400); put16(b, 30); put16(b, 2);   // 300 / 100 / 50
    put16(b, 120); put16(b, 8); put16(b, 1);    // geki / katu / miss
    put32(b, 987654);
    put16(b, 321);
    put8(b, 1);                    // full combo
    put32(b, mods);
    if (writeHpString) putEmptyString(b);
    put64(b, 637800000000000000LL + onlineId);  // .NET ticks
    put32(b, -1);                  // sentinel
    put64(b, onlineId);
    if (mods & (1 << 23)) put64(b, 0);          // TargetPractice accumulator
}

static void testScoresDb() {
    const std::string md5A(32, 'a');
    const std::string md5B(32, 'b');
    const std::string md5C(32, 'c');
    std::string b;
    put32(b, 20260924);            // db version
    put32(b, 2);                   // group count
    putOsuString(b, md5A);
    put32(b, 1);
    putScore(b, md5A, "player1", md5B, 0, true, 44068);
    putOsuString(b, md5C);
    put32(b, 2);
    putScore(b, md5C, "player2", md5A, 1 << 15, true, 12345);   // 4K
    putScore(b, md5C, "player3", md5B, 1 << 23, false, 6789);   // TP, no hp string

    std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "mra_test_scores.db";
    {
        // Sandboxed environments may deny writes to %TEMP%; fall back to CWD.
        std::ofstream probe(tmp, std::ios::binary | std::ios::trunc);
        if (!probe)
            tmp = std::filesystem::current_path() / "mra_test_scores.db";
    }
    auto writeTmp = [&tmp](const std::string& bytes) {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::printf("  tmp open FAILED: %s errno=%d\n",
                        tmp.string().c_str(), errno);
            ++g_failures;
            return;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            std::printf("  tmp write FAILED: %s errno=%d\n",
                        tmp.string().c_str(), errno);
            ++g_failures;
        }
    };
    writeTmp(b);

    ScoresDb db;
    std::string err;
    bool ok = parseScoresDb(tmp.string(), db, err);
    CHECK(ok);
    if (!ok) {
        std::printf("  scores.db parse err: %s\n", err.c_str());
        std::filesystem::remove(tmp);
        return;
    }
    CHECK(db.version == 20260924);
    CHECK(db.groups.size() == 2);
    CHECK(db.groups[0].beatmapMd5 == md5A);
    CHECK(db.groups[0].scores.size() == 1);
    const ScoreEntry& s1 = db.groups[0].scores[0];
    CHECK(s1.mode == 3);
    CHECK(s1.clientVersion == 20260924);
    CHECK(s1.beatmapMd5 == md5A);
    CHECK(s1.playerName == "player1");
    CHECK(s1.replayMd5 == md5B);
    CHECK(s1.c300 == 400 && s1.c100 == 30 && s1.c50 == 2);
    CHECK(s1.geki == 120 && s1.katu == 8 && s1.miss == 1);
    CHECK(s1.totalScore == 987654);
    CHECK(s1.maxCombo == 321);
    CHECK(s1.fullCombo);
    CHECK(s1.mods == 0);
    CHECK(s1.onlineId == 44068);
    CHECK(s1.playedTicks == 637800000000000000LL + 44068);
    CHECK(s1.playedMs ==
          (637800000000000000LL + 44068) / 10000 - 62135596800000LL);
    CHECK(db.groups[1].scores.size() == 2);
    CHECK(db.groups[1].scores[0].mods == (1 << 15));
    CHECK(db.groups[1].scores[0].onlineId == 12345);
    CHECK(db.groups[1].scores[1].hpGraph.empty());   // tolerated missing string
    CHECK(db.groups[1].scores[1].onlineId == 6789);

    // Corruption: break the -1 sentinel of the last score -> strict failure.
    std::string bad = b;
    bad[bad.size() - 20] = 0x2a;   // tail: sentinel(4)+onlineId(8)+TP f64(8)
    writeTmp(bad);
    ScoresDb db2;
    CHECK(!parseScoresDb(tmp.string(), db2, err));
    std::filesystem::remove(tmp);
}

static void testRateWindows() {
    // acc-semantics §0.2: windows scale by rate; frames stay real-time.
    std::vector<ManiaHitObject> objs = {note(0, 1000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1020, 1u << 0, 0),  // +20ms
        frame(1100, 0, 1u << 0),
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);  // perfect 16, great 40
    MatchResult r1 = matchReplayToObjects(objs, 4, frames, opt);
    CHECK(r1.events.size() == 1);
    CHECK(r1.events[0].judgement == HitResult::Great);  // 20 > 16
    opt.rate = 1.5;  // DT: perfect 24 -> +20 becomes Perfect
    MatchResult r2 = matchReplayToObjects(objs, 4, frames, opt);
    CHECK(r2.events.size() == 1);
    CHECK(r2.events[0].judgement == HitResult::Perfect);
    CHECK(r2.events[0].offsetMs && *r2.events[0].offsetMs == 20.0);
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
    run("stablePress", testStablePressSemantics);
    run("missNoInput", testMissNoInput);
    run("chord", testChordDifferentColumns);
    run("holdEarlyRelease", testHoldTailEarlyRelease);
    run("holdHeldThrough", testHoldTailHeldThrough);
    run("accuracy", testAccuracyV1V2);
    run("stats", testStats);
    run("analysis", testAnalysis);
    run("scoresDb", testScoresDb);
    run("rateWindows", testRateWindows);
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
