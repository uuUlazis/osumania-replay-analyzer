// v2diff.cpp — V2 counts audit: for every mania replay whose .osr counters
// use the ScoreV2 model (declaredTotal == notes+LN, or ScoreV2 mod), run the
// source-faithful engine's V2 pass and diff the six engine counts against the
// declared (.osr) counts, plus the accuracy delta.
//
// usage: v2diff <scoresDb> <dataR> <songs> [minAbsAccPct=0.0] [limit=N]
// output line:  DACC <dAccPct> <dMax> <d300> <d200> <d100> <d50> <dMiss>
//               keys=<k> mods=<hex> lns=<n> <replay path>
#include "beatmap/osu_parser.h"
#include "io/md5.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
#include "replay/osr_reader.h"
#include "replay/replay_decoder.h"
#include "replay/replay_frames.h"
#include "scores/scores_db.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using namespace mra;

static std::string md5File(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    Md5 h;
    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
        h.update(reinterpret_cast<const uint8_t*>(buf),
                 static_cast<size_t>(f.gcount()));
    return Md5::toHex(h.digest());
}

// faithful V2 display acc: int32 numerator -> conv.r4 -> single-precision div
static float accV2(int64_t cMax, int64_t c300, int64_t c200, int64_t c100,
                   int64_t c50, int64_t total) {
    if (total == 0) return 1.0f;
    int64_t num = 50 * c50 + 100 * c100 + 200 * c200 + 300 * c300 + 305 * cMax;
    int64_t den = total * 305;
    return static_cast<float>(num) / static_cast<float>(den);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: v2diff <scoresDb> <dataR> <songs> "
                     "[minAbsAccPct=0] [limit=N]\n");
        return 2;
    }
    double minAbsAccPct = argc > 4 ? atof(argv[4]) : 0.0;
    long long limit = argc > 5 ? atoll(argv[5]) : -1;

    ScoresDb db;
    std::string err;
    if (!parseScoresDb(argv[1], db, err)) {
        std::fprintf(stderr, "scores.db parse failed: %s\n", err.c_str());
        return 2;
    }
    const std::string dataR = argv[2], songs = argv[3];

    std::unordered_map<std::string, std::string> replays;  // tickkey -> path
    if (fs::exists(dataR))
        for (auto& e : fs::directory_iterator(dataR)) {
            if (!e.is_regular_file()) continue;
            std::string stem = e.path().stem().string();
            size_t dash = stem.rfind('-');
            if (dash == std::string::npos || dash == 0) continue;
            std::string bm = stem.substr(0, dash);
            std::string ticks = stem.substr(dash + 1);
            bool digits = !ticks.empty();
            for (char c : ticks)
                if (c < '0' || c > '9') { digits = false; break; }
            if (!digits) continue;
            for (char& c : bm)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            replays.emplace(bm + "-" + ticks, e.path().string());
        }
    std::printf("replay index: %zu\n", replays.size());
    std::fflush(stdout);

    std::unordered_map<std::string, std::string> beatmaps;
    if (fs::exists("beatmap_index.tsv")) {
        std::ifstream in("beatmap_index.tsv");
        std::string line;
        while (std::getline(in, line)) {
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            beatmaps.emplace(line.substr(0, tab), line.substr(tab + 1));
        }
    }
    if (beatmaps.empty()) {
        std::fprintf(stderr, "beatmap_index.tsv missing/empty (run from the "
                             "build dir with the cache present)\n");
        return 2;
    }
    std::printf("beatmap index: %zu\n", beatmaps.size());
    std::fflush(stdout);

    long long audited = 0, v2Found = 0, v1Found = 0;
    for (auto& g : db.groups) {
        for (auto& s : g.scores) {
            if (s.mode != 3) continue;
            std::string rk = g.beatmapMd5;
            for (char& c : rk)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            constexpr int64_t kDotNetToFileTimeTicks = 504911232000000000LL;
            const int64_t ft = s.playedTicks - kDotNetToFileTimeTicks;
            auto r = replays.find(rk + "-" + std::to_string(ft));
            if (r == replays.end())
                r = replays.find(rk + "-" + std::to_string(ft - 10000));
            if (r == replays.end())
                r = replays.find(rk + "-" + std::to_string(ft + 10000));
            if (r == replays.end()) continue;
            auto b = beatmaps.find(g.beatmapMd5);
            if (b == beatmaps.end()) continue;

            // ---- full load chain ----
            std::ifstream rf(r->second, std::ios::binary);
            std::vector<uint8_t> osrBytes((std::istreambuf_iterator<char>(rf)),
                                          std::istreambuf_iterator<char>());
            OsrParseResult osr = parseOsr(osrBytes);
            if (!osr.error.empty()) continue;
            LzmaDecodeResult decoded = lzmaDecode(
                osr.header.compressedData.data(),
                osr.header.compressedData.size());
            if (!decoded.ok) continue;
            ReplayFrameDecodeResult df = decodeReplayFrames(decoded.output);
            if (!df.ok) continue;
            std::vector<ReplayFrame> frames = std::move(df.frames);
            std::ifstream bf(b->second, std::ios::binary);
            std::string osuText((std::istreambuf_iterator<char>(bf)),
                                std::istreambuf_iterator<char>());
            OsuParseResult parsed = parseOsu(osuText);
            if (!parsed.ok) continue;
            OsuBeatmap beatmap = std::move(parsed.beatmap);
            if (!beatmap.isMania()) continue;

            int keyCount = keyCountFromMods(osr.header.mods);
            if (keyCount <= 0) keyCount = beatmap.keyCount;
            MatchOptions o;
            o.od = beatmap.overallDifficulty;
            o.nativeManiaBeatmap = beatmap.isMania();
            o.mirrorColumns = (osr.header.mods & kModMirror) != 0;
            o.mods = osr.header.mods;
            if (osr.header.mods & kModRandom) {
                for (auto it = frames.rbegin(); it != frames.rend(); ++it)
                    if (it->isSeedFrame) {
                        o.randomMod = true;
                        o.randomSeed = it->seed;
                        break;
                    }
            }
            MatchOptions v2 = o;
            v2.scoreV2 = true;

            MatchResult matchV2 =
                matchReplayToObjects(beatmap.objects, keyCount, frames, v2);
            MatchResult matchV1 =
                matchReplayToObjects(beatmap.objects, keyCount, frames, o);

            int64_t declaredTotal =
                (int64_t)osr.header.count300 + (int64_t)osr.header.count100 +
                (int64_t)osr.header.count50 + (int64_t)osr.header.countGeki +
                (int64_t)osr.header.countKatu + (int64_t)osr.header.countMiss;
            int64_t noteCount = (int64_t)beatmap.objects.size(), lnCount = 0;
            for (const ManiaHitObject& ob : beatmap.objects)
                if (ob.type == ManiaObjectType::Hold) ++lnCount;
            const bool countsV2 =
                lnCount > 0 && declaredTotal == noteCount + lnCount;
            const bool countsV1 = lnCount > 0 && declaredTotal == noteCount;
            const bool modsScoreV2 = (osr.header.mods & (1u << 29)) != 0;
            const bool scoreV2Replay =
                countsV2 || (!countsV1 && modsScoreV2);

            const auto diffCounts = [](const SourceScoreSummary& sc,
                                       const OsrHeader& h, int64_t total,
                                       bool v2, char* buf, size_t bufn) {
                int64_t dMax = sc.countMax - (int64_t)h.countGeki;
                int64_t d300 = sc.count300 - (int64_t)h.count300;
                int64_t d200 = sc.count200 - (int64_t)h.countKatu;
                int64_t d100 = sc.count100 - (int64_t)h.count100;
                int64_t d50 = sc.count50 - (int64_t)h.count50;
                int64_t dMiss = sc.countMiss - (int64_t)h.countMiss;
                double numE = v2 ? 50 * sc.count50 + 100 * sc.count100 +
                                       200 * sc.count200 + 300 * sc.count300 +
                                       305 * sc.countMax
                                 : 50 * sc.count50 + 100 * sc.count100 +
                                       200 * sc.count200 +
                                       300 * (sc.count300 + sc.countMax);
                double numD = v2 ? 50 * (int64_t)h.count50 +
                                       100 * (int64_t)h.count100 +
                                       200 * (int64_t)h.countKatu +
                                       300 * (int64_t)h.count300 +
                                       305 * (int64_t)h.countGeki
                                 : 50 * (int64_t)h.count50 +
                                       100 * (int64_t)h.count100 +
                                       200 * (int64_t)h.countKatu +
                                       300 * ((int64_t)h.count300 +
                                              (int64_t)h.countGeki);
                double den = total * (v2 ? 305 : 300);
                double dAccPct = den != 0
                                     ? ((numE - numD) / den) * 100.0
                                     : 0.0;
                std::snprintf(buf, bufn,
                              "%+.4f  dMax=%+lld d300=%+lld d200=%+lld "
                              "d100=%+lld d50=%+lld dMiss=%+lld",
                              dAccPct, (long long)dMax, (long long)d300,
                              (long long)d200, (long long)d100,
                              (long long)d50, (long long)dMiss);
            };

            char buf1[160], buf2[160];
            diffCounts(matchV2.sourceScore, osr.header, declaredTotal, true,
                       buf2, sizeof(buf2));
            diffCounts(matchV1.sourceScore, osr.header, declaredTotal, false,
                       buf1, sizeof(buf1));

            if (scoreV2Replay) {
                ++v2Found;
                std::printf("V2 %s  keys=%d mods=0x%08x lns=%lld  %s\n", buf2,
                            keyCount, osr.header.mods, (long long)lnCount,
                            r->second.c_str());
                std::fflush(stdout);
            } else if (countsV1) {
                ++v1Found;
                std::printf("V1 %s  keys=%d mods=0x%08x lns=%lld  %s\n", buf1,
                            keyCount, osr.header.mods, (long long)lnCount,
                            r->second.c_str());
                std::fflush(stdout);
            }
            if (limit > 0 && v2Found + v1Found >= limit) break;
        }
        if (limit > 0 && audited >= limit) break;
    }
    std::printf("V2DIFF DONE (v2=%lld, v1=%lld, reported=%lld)\n", v2Found,
                v1Found, audited);
    return 0;
}
