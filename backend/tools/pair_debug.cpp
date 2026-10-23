// pair_debug.cpp — step-by-step reproducer: run every phase of
// loadReplaySession with breadcrumb prints to localize the 0xC0000005 crash
// on a specific replay+beatmap pair.
#include "beatmap/osu_parser.h"
#include "http/api_server.h"
#include "io/md5.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
#include "replay/osr_reader.h"
#include "replay/replay_decoder.h"
#include "replay/replay_frames.h"
#include "session/session_store.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace mra;

#define STEP(msg)                         \
    do {                                  \
        std::printf("[step] %s\n", msg);  \
        std::fflush(stdout);              \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: pair_debug <replay.osr> <beatmap.osu>\n");
        return 2;
    }
    const char* replayPath = argv[1];
    const char* beatmapPath = argv[2];

    STEP("read .osr");
    std::ifstream rf(replayPath, std::ios::binary);
    std::vector<uint8_t> osrBytes((std::istreambuf_iterator<char>(rf)),
                                  std::istreambuf_iterator<char>());
    OsrParseResult osr = parseOsr(osrBytes);
    if (!osr.error.empty()) {
        std::printf("osr parse error: %s\n", osr.error.c_str());
        return 1;
    }
    std::printf("  mods=0x%08x  counts=%lld/%lld/%lld/%lld/%lld/%lld\n",
                osr.header.mods, (long long)osr.header.count300,
                (long long)osr.header.count100, (long long)osr.header.count50,
                (long long)osr.header.countGeki, (long long)osr.header.countKatu,
                (long long)osr.header.countMiss);
    std::fflush(stdout);

    STEP("lzma decode");
    LzmaDecodeResult decoded = lzmaDecode(osr.header.compressedData.data(),
                                          osr.header.compressedData.size());
    if (!decoded.ok) {
        std::printf("lzma error: %s\n", decoded.error.c_str());
        return 1;
    }

    STEP("decode frames");
    ReplayFrameDecodeResult df = decodeReplayFrames(decoded.output);
    if (!df.ok) {
        std::printf("frames error: %s\n", df.error.c_str());
        return 1;
    }
    std::vector<ReplayFrame> frames = std::move(df.frames);
    int seedFrames = 0;
    for (auto& f : frames)
        if (f.isSeedFrame) ++seedFrames;
    std::printf("  frames=%zu seedFrames=%d\n", frames.size(), seedFrames);
    std::fflush(stdout);

    STEP("parse .osu");
    std::ifstream bf(beatmapPath, std::ios::binary);
    std::string osuText((std::istreambuf_iterator<char>(bf)),
                        std::istreambuf_iterator<char>());
    OsuParseResult parsed = parseOsu(osuText);
    if (!parsed.ok) {
        std::printf("osu parse error: %s\n", parsed.error.c_str());
        return 1;
    }
    OsuBeatmap beatmap = std::move(parsed.beatmap);
    std::printf("  mode=%d keyCount=%d OD=%.4f objects=%zu\n",
                (int)beatmap.mode, beatmap.keyCount,
                beatmap.overallDifficulty, beatmap.objects.size());
    std::fflush(stdout);

    STEP("build options");
    int keyCount = keyCountFromMods(osr.header.mods);
    if (keyCount <= 0) keyCount = beatmap.keyCount;
    MatchOptions o;
    o.windows = ManiaWindows::fromOD(beatmap.overallDifficulty);
    o.od = beatmap.overallDifficulty;
    o.nativeManiaBeatmap = beatmap.isMania();
    o.mirrorColumns = (osr.header.mods & kModMirror) != 0;
    o.mods = osr.header.mods;
    if (osr.header.mods & (kModDoubleTime | kModNightcore))
        o.rate = 1.5;
    else if (osr.header.mods & kModHalfTime)
        o.rate = 0.75;
    if (osr.header.mods & kModRandom) {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            if (it->isSeedFrame) {
                o.randomMod = true;
                o.randomSeed = it->seed;
                break;
            }
    }
    std::printf("  keyCount=%d mirror=%d random=%d(seed=%u) rate=%.2f od=%.3f\n",
                keyCount, (int)o.mirrorColumns, (int)o.randomMod, o.randomSeed,
                o.rate, o.od);
    std::fflush(stdout);

    STEP("V1 matchReplayToObjects");
    MatchResult match = matchReplayToObjects(beatmap.objects, keyCount, frames, o);
    std::printf("  events=%zu holds=%zu unmatchedInputs=%zu unmatchedObjects=%d\n",
                match.events.size(), match.holds.size(),
                match.unmatchedInputs.size(), match.unmatchedObjects);
    std::fflush(stdout);

    STEP("V2 matchReplayToObjects");
    MatchOptions v2 = o;
    v2.scoreV2 = true;
    MatchResult matchV2 = matchReplayToObjects(beatmap.objects, keyCount, frames, v2);
    std::printf("  events=%zu holds=%zu\n", matchV2.events.size(),
                matchV2.holds.size());
    std::fflush(stdout);

    STEP("counts-model detection + analyzeMatch");
    int64_t declaredTotal = (int64_t)osr.header.count300 +
                            (int64_t)osr.header.count100 +
                            (int64_t)osr.header.count50 +
                            (int64_t)osr.header.countGeki +
                            (int64_t)osr.header.countKatu +
                            (int64_t)osr.header.countMiss;
    int64_t noteCount = (int64_t)beatmap.objects.size(), lnCount = 0;
    for (const ManiaHitObject& ob : beatmap.objects)
        if (ob.type == ManiaObjectType::Hold) ++lnCount;
    const bool countsV2 = lnCount > 0 && declaredTotal == noteCount + lnCount;
    const bool countsV1 = lnCount > 0 && declaredTotal == noteCount;
    const bool modsScoreV2 = (osr.header.mods & (1u << 29)) != 0;
    const bool scoreV2Replay = countsV2 || (!countsV1 && modsScoreV2);
    GlobalAnalysis analysis =
        analyzeMatch(scoreV2Replay ? matchV2 : match, keyCount);
    std::printf("  declared=%lld notes=%lld lns=%lld -> v2replay=%d\n",
                (long long)declaredTotal, (long long)noteCount,
                (long long)lnCount, (int)scoreV2Replay);
    std::fflush(stdout);
    const bool stableCountMode =
        !scoreV2Replay &&
        (static_cast<int64_t>(analysis.stableCounts.judged()) == declaredTotal ||
         static_cast<int64_t>(analysis.counts.judged()) != declaredTotal);

    STEP("ReplaySession member moves");
    ReplaySession session;
    session.replayPath = replayPath;
    session.beatmapPath = beatmapPath;
    session.beatmapMd5 = "0";
    session.replay = std::move(osr);
    session.frames = std::move(frames);
    session.keyCount = keyCount;
    session.beatmap = std::move(beatmap);
    session.match = std::move(match);
    session.matchV2 = std::move(matchV2);
    session.analysis = std::move(analysis);
    session.stableCountMode = stableCountMode;
    session.rulesScoreV2 = scoreV2Replay;
    session.ready = true;
    std::printf("  moves done\n");
    std::fflush(stdout);

    STEP("SessionStore::create(std::move(session))");
    {
        std::string id = SessionStore::instance().create(std::move(session));
        std::printf("  id=%s\n", id.c_str());
        std::fflush(stdout);
    }

    STEP("real loadReplaySession(\"\")");
    {
        LoadOutcome out = loadReplaySession(replayPath, beatmapPath, "", "");
        std::printf("  ok=%d err=%s/%s\n", (int)out.ok, out.errorCode.c_str(),
                    out.message.c_str());
        std::fflush(stdout);
    }

    STEP("ALL OK");
    return 0;
}
