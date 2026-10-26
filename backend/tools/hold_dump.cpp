// hold_dump.cpp — per-hold judgement dump for one replay+beatmap pair:
// find tails the engine never judged (the V2 dMiss deficit pattern).
#include "beatmap/osu_parser.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
#include "replay/osr_reader.h"
#include "replay/replay_decoder.h"
#include "replay/replay_frames.h"

#include <cstdio>
#include <fstream>
#include <vector>

using namespace mra;

static const char* res(HitResult r) {
    switch (r) {
        case HitResult::Perfect: return "MAX";
        case HitResult::Great: return "300";
        case HitResult::Good: return "200";
        case HitResult::Ok: return "100";
        case HitResult::Meh: return "50";
        default: return "MISS";
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: hold_dump <replay.osr> <beatmap.osu>\n");
        return 2;
    }
    std::ifstream rf(argv[1], std::ios::binary);
    std::vector<uint8_t> osrBytes((std::istreambuf_iterator<char>(rf)),
                                  std::istreambuf_iterator<char>());
    OsrParseResult osr = parseOsr(osrBytes);
    LzmaDecodeResult decoded = lzmaDecode(osr.header.compressedData.data(),
                                          osr.header.compressedData.size());
    ReplayFrameDecodeResult df = decodeReplayFrames(decoded.output);
    std::vector<ReplayFrame> frames = std::move(df.frames);
    std::ifstream bf(argv[2], std::ios::binary);
    std::string osuText((std::istreambuf_iterator<char>(bf)),
                        std::istreambuf_iterator<char>());
    OsuBeatmap beatmap = std::move(parseOsu(osuText).beatmap);

    int keyCount = keyCountFromMods(osr.header.mods);
    if (keyCount <= 0) keyCount = beatmap.keyCount;
    MatchOptions o;
    o.od = beatmap.overallDifficulty;
    o.nativeManiaBeatmap = beatmap.isMania();
    o.mods = osr.header.mods;
    if (osr.header.mods & kModRandom)
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            if (it->isSeedFrame) { o.randomMod = true; o.randomSeed = it->seed; break; }
    MatchOptions v2 = o;
    v2.scoreV2 = true;
    MatchResult m = matchReplayToObjects(beatmap.objects, keyCount, frames, v2);

    const int64_t lastFrame =
        frames.empty() ? 0 : frames.back().timeMs;
    std::printf("declaredMiss=%d engineMiss=%lld lastFrame=%lld objects=%zu\n",
                osr.header.countMiss, (long long)m.sourceScore.countMiss,
                (long long)lastFrame, beatmap.objects.size());

    // count per-object tail outcomes from events (HoldTail phase)
    std::vector<const HitEvent*> tail(beatmap.objects.size(), nullptr);
    std::vector<const HitEvent*> head(beatmap.objects.size(), nullptr);
    for (auto& e : m.events) {
        if (e.objectIndex < 0 ||
            e.objectIndex >= (int)beatmap.objects.size()) continue;
        if (e.phase == HitPhase::HoldTail) tail[e.objectIndex] = &e;
        if (e.phase == HitPhase::HoldHead) head[e.objectIndex] = &e;
    }
    int missing = 0, missingAfterLastFrame = 0, missingHeld = 0;
    int reCatch = 0, noRelease = 0;
    for (size_t i = 0; i < beatmap.objects.size(); ++i) {
        const ManiaHitObject& ob = beatmap.objects[i];
        if (ob.type != ManiaObjectType::Hold) continue;
        if (tail[i] != nullptr) continue;
        ++missing;
        const int64_t end = ob.endTimeMs.value_or(ob.startTimeMs);
        bool afterLast = end > lastFrame;
        if (afterLast) ++missingAfterLastFrame;
        if (head[i] != nullptr) ++missingHeld;
        // count down-edges / up-edges on this column in [start, lastFrame]
        const uint32_t bit = 1u << ob.column;
        int downs = 0, ups = 0;
        for (auto& f : frames) {
            if (f.timeMs < ob.startTimeMs) continue;
            downs += (f.keyDownMask & bit) != 0;
            ups += (f.keyUpMask & bit) != 0;
        }
        const bool recaught = downs >= 2;  // head press + re-press
        if (recaught) ++reCatch;
        else if (downs == 1 && ups == 0) ++noRelease;
        std::printf("NO-TAIL obj=%zu col=%d start=%lld end=%lld "
                    "head=%s afterLast=%d downs=%d ups=%d%s\n",
                    i, ob.column, (long long)ob.startTimeMs, (long long)end,
                    head[i] ? res(head[i]->judgement) : "-",
                    (int)afterLast, downs, ups,
                    recaught ? "  <-- RE-CATCH" : (downs == 1 && ups == 0
                                                       ? "  held-no-release"
                                                       : ""));
    }
    std::printf("MISSING-TAIL total=%d afterLastFrame=%d withHead=%d "
                "reCatch(>=2press)=%d heldNoRelease=%d\n",
                missing, missingAfterLastFrame, missingHeld, reCatch,
                noRelease);

    // tail grade distribution + source counts vs event tally
    std::printf("--- tail grades ---\n");
    int tailGrade[6] = {0, 0, 0, 0, 0, 0};
    for (auto& e : m.events)
        if (e.phase == HitPhase::HoldTail) {
            int g = (int)e.judgement;  // Perfect=0..Miss=5
            if (g >= 0 && g < 6) ++tailGrade[g];
        }
    std::printf("tails: MAX=%d 300=%d 200=%d 100=%d 50=%d MISS=%d\n",
                tailGrade[0], tailGrade[1], tailGrade[2], tailGrade[3],
                tailGrade[4], tailGrade[5]);
    const auto& sc = m.sourceScore;
    std::printf("sourceScore: max=%lld 300=%lld 200=%lld 100=%lld 50=%lld "
                "miss=%lld\n",
                (long long)sc.countMax, (long long)sc.count300,
                (long long)sc.count200, (long long)sc.count100,
                (long long)sc.count50, (long long)sc.countMiss);
    std::printf("declared:    max=%d 300=%d 200=%d 100=%d 50=%d miss=%d\n",
                osr.header.countGeki, osr.header.count300, osr.header.countKatu,
                osr.header.count100, osr.header.count50, osr.header.countMiss);
    // LN head grade distribution
    int headGrade[6] = {0, 0, 0, 0, 0, 0};
    for (auto& e : m.events)
        if (e.phase == HitPhase::HoldHead) {
            int g = (int)e.judgement;
            if (g >= 0 && g < 6) ++headGrade[g];
        }
    std::printf("heads: MAX=%d 300=%d 200=%d 100=%d 50=%d MISS=%d\n",
                headGrade[0], headGrade[1], headGrade[2], headGrade[3],
                headGrade[4], headGrade[5]);
    return 0;
}
