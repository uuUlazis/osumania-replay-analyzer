// crash_sweep.cpp — diagnostic: run the full loadReplaySession analysis chain
// over real library replays, one per process invocation, to reproduce the
// 0xC0000005 crash seen at MatchResult move-assignment.
//
// Modes:
//   crash_sweep <replayPath> <beatmapPath> <audioPath> <skinFolder>   direct pair
//   crash_sweep <scoresDb> <dataR> <songs> [from=N] [limit=N]         sweep mode
//
// Sweep prints "TRY <replay>" before each call (flushed); after a hard crash
// the last TRY line without an OK/ERR outcome names the culprit. Continue
// past it with from=<index>.
#include "http/api_server.h"
#include "scores/scores_db.h"
#include "io/md5.h"

#include <chrono>
#include <cstdio>
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

static void runPair(const std::string& replay, const std::string& beatmap,
                    const std::string& audio, const std::string& skin) {
    std::printf("TRY %s\n    bm %s\n", replay.c_str(), beatmap.c_str());
    std::fflush(stdout);
    auto t0 = std::chrono::steady_clock::now();
    LoadOutcome out =
        loadReplaySession(replay, beatmap, audio, skin);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    if (out.ok)
        std::printf("OK  %s (%lld ms)\n", replay.c_str(), (long long)ms);
    else
        std::printf("ERR %s %s/%s\n", replay.c_str(), out.errorCode.c_str(),
                    out.message.c_str());
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc == 5 && argv[1][0] != '-') {
        runPair(argv[1], argv[2], argv[3], argv[4]);
        return 0;
    }
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: crash_sweep <replay> <beatmap> <audio> <skin>\n"
                     "       crash_sweep <scoresDb> <dataR> <songs> "
                     "[from=N] [limit=N]\n");
        return 2;
    }
    const std::string scoresDb = argv[1], dataR = argv[2], songs = argv[3];
    long long from = 0, limit = -1;
    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("from=", 0) == 0) from = atoll(a.c_str() + 5);
        if (a.rfind("limit=", 0) == 0) limit = atoll(a.c_str() + 6);
    }

    ScoresDb db;
    std::string err;
    if (!parseScoresDb(scoresDb, db, err)) {
        std::fprintf(stderr, "scores.db parse failed: %s\n", err.c_str());
        return 2;
    }

    // replay key: "<beatmapMd5 lowercase>-<playedTicks ticks>" -> .osr path
    // (osu pairs a score with its replay through the file name, api_server.cpp)
    std::unordered_map<std::string, std::string> replays;
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
    std::printf("replay index: %zu files\n", replays.size());
    std::fflush(stdout);

    // beatmapMd5 -> .osu path (Songs walk, cached to beatmap_index.tsv)
    std::unordered_map<std::string, std::string> beatmaps;
    const fs::path indexCache = "beatmap_index.tsv";
    bool loadedCache = false;
    if (fs::exists(indexCache)) {
        std::ifstream in(indexCache);
        std::string line;
        while (std::getline(in, line)) {
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            beatmaps.emplace(line.substr(0, tab), line.substr(tab + 1));
        }
        loadedCache = !beatmaps.empty();
        std::printf("beatmap cache: %zu entries\n", beatmaps.size());
        std::fflush(stdout);
    }
    if (!loadedCache) {
    std::vector<fs::path> osuFiles;
    for (auto& e : fs::recursive_directory_iterator(songs))
        if (e.is_regular_file() && e.path().extension() == ".osu")
            osuFiles.push_back(e.path());
    std::printf("songs walk: %zu .osu files, hashing...\n", osuFiles.size());
    std::fflush(stdout);
    long long hashed = 0, skipped = 0;
    for (auto& p : osuFiles) {
        try {
            std::string h = md5File(p);
            if (!h.empty())
                beatmaps.emplace(std::move(h), p.string());
            else
                ++skipped;
        } catch (...) {
            ++skipped;  // unconvertible name / unreadable file — not a target
            std::printf("SKIP #%lld (unreadable/unconvertible)\n", hashed);
            std::fflush(stdout);
        }
        if (++hashed % 2000 == 0) {
            std::printf("hashed %lld/%zu\n", hashed, osuFiles.size());
            std::fflush(stdout);
        }
    }
    std::ofstream out(indexCache);
    for (auto& kv : beatmaps) out << kv.first << '\t' << kv.second << '\n';
    std::printf("beatmap index: %zu (skipped %lld)\n", beatmaps.size(),
                skipped);
    std::fflush(stdout);
    }

    long long idx = 0, done = 0;
    for (auto& g : db.groups) {
        for (auto& s : g.scores) {
            if (s.mode != 3) continue;  // mania only
            std::string rk = g.beatmapMd5;
            for (char& c : rk)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            // .NET ticks -> FILETIME ticks (api_server kDotNetToFileTimeTicks),
            // with the app's ±1 ms slack variants.
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
            if (idx++ < from) continue;
            if (limit >= 0 && done >= limit) { std::printf("limit reached\n"); return 0; }
            ++done;
            runPair(r->second, b->second, "", "");
        }
    }
    std::printf("SWEEP DONE (%lld mania-with-replay pairs)\n", idx);
    return 0;
}
