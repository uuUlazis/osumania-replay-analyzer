// L2 lazer 判定重建引擎 E2E 对拍 (真实 .osr 样例, Phase L2 验收门)。
//
// 流程:
//   1. 读 build/tmp_lazer/lazer_samples.json (L1 探针产物: folder/file/beatmapPath/jsonCounts);
//   2. 排除 folder=="3" (该局带 IN (Invert) mod, 物件被变换, 本期不支持 → SKIP+原因);
//   3. 其余每局: readFile → parseOsr → lzmaDecode(header.compressedData) →
//      decodeReplayFrames (谱面时基帧); rate/HR/EZ/MR 从尾块 JSON mods 推导
//      (HT→0.75, DT/NC→settings.speed_change 或 1.5, HR/EZ/MR acronym);
//      parseOsu 读 beatmapPath (非 mania → SKIP 报告);
//   4. matchLazer → 六档 vs jsonCounts 打表 (逐档差 + L1 距离 + acc305 重建 vs 声明);
//   5. 收敛门: 每局每档差全 0 → 退出码 0, 否则 1。
// 附加交叉验证 (不进门, 只报告): 重建 N vs maximum_statistics[perfect];
//   重建「尾 Miss 数」vs statistics[ignore_miss] (lazer 里父物件 IgnoreMiss ⇔ 尾判 Miss)。
//
// 判定/acc 语义来源见 backend/src/mania/lazer_matcher.cpp 文件头。
#include "beatmap/osu_parser.h"
#include "lazer/lazer_format.h"
#include "mania/lazer_matcher.h"
#include "replay/osr_reader.h"
#include "replay/replay_decoder.h"
#include "replay/replay_frames.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef MRA_LAZER_SAMPLES_JSON
#define MRA_LAZER_SAMPLES_JSON "build/tmp_lazer/lazer_samples.json"
#endif
#ifndef MRA_LAZER_SAMPLE_ROOT
#define MRA_LAZER_SAMPLE_ROOT "test/sample/lazer"
#endif

using namespace mra;
namespace fs = std::filesystem;

namespace {

bool readFileBytes(const fs::path& p, std::vector<uint8_t>& out, std::string* err) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(p, ec);
    if (ec) {
        if (err) *err = "file_size: " + ec.message();
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) {
        if (err) *err = "open failed (errno " + std::to_string(errno) + ")";
        return false;
    }
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    const bool ok = static_cast<size_t>(f.gcount()) == out.size();
    if (!ok && err) *err = "short read";
    return ok;
}

bool readFileText(const fs::path& p, std::string& out, std::string* err) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(p, bytes, err)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

// ---- 极简 JSON 提取 (只为 build/tmp_lazer/lazer_samples.json 的固定形状服务) ----

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
}

// 解析 key 后的 JSON string 值 (处理 \" 与 \\ 转义)。
bool jsonStringAfter(const std::string& s, size_t from, const char* key, size_t* next,
                     std::string& out) {
    const size_t k = s.find(key, from);
    if (k == std::string::npos) return false;
    size_t i = k + std::strlen(key);
    skipWs(s, i);
    if (i >= s.size() || s[i] != ':') return false;
    ++i;
    skipWs(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            const char n = s[i + 1];
            if (n == '\\' || n == '/' || n == '"') out.push_back(n);
            else if (n == 'n') out.push_back('\n');
            else if (n == 't') out.push_back('\t');
            else out.push_back(n);
            i += 2;
            continue;
        }
        if (c == '"') {
            if (next) *next = i + 1;
            return true;
        }
        out.push_back(c);
        ++i;
    }
    return false;
}

// 解析 key 后的对象里的 6 个整数计数 (键 = perfect/great/good/ok/meh/miss)。
bool jsonCountsAfter(const std::string& s, size_t from, size_t* next, int64_t counts[6]) {
    const size_t k = s.find("\"jsonCounts\"", from);
    if (k == std::string::npos) return false;
    size_t i = s.find('{', k);
    if (i == std::string::npos) return false;
    const size_t end = s.find('}', i);
    if (end == std::string::npos) return false;
    static const char* kKeys[6] = {"\"perfect\"", "\"great\"", "\"good\"",
                                   "\"ok\"",       "\"meh\"",    "\"miss\""};
    for (int t = 0; t < 6; ++t) counts[t] = -1;
    for (int t = 0; t < 6; ++t) {
        size_t p = s.find(kKeys[t], i);
        if (p == std::string::npos || p > end) continue;
        // p 指向带引号的键; 键名后应为 ':'
        size_t q = p + std::strlen(kKeys[t]);
        skipWs(s, q);
        if (q >= s.size() || s[q] != ':') continue;
        ++q;
        skipWs(s, q);
        counts[t] = std::strtoll(s.c_str() + q, nullptr, 10);
    }
    if (next) *next = end + 1;
    return true;
}

struct Sample {
    std::string folder;
    std::string file;
    std::string beatmapPath;
    int64_t declared[6] = {0, 0, 0, 0, 0, 0};
};

std::vector<Sample> loadSamples(const char* manifestPath) {
    std::vector<Sample> out;
    std::string text;
    std::string err;
    if (!readFileText(fs::path(manifestPath), text, &err)) {
        std::printf("FATAL: cannot read manifest %s: %s\n", manifestPath, err.c_str());
        return out;
    }
    size_t pos = 0;
    while (true) {
        Sample s;
        size_t next = 0;
        if (!jsonStringAfter(text, pos, "\"folder\"", &next, s.folder)) break;
        if (!jsonStringAfter(text, next, "\"file\"", &next, s.file)) break;
        if (!jsonStringAfter(text, next, "\"beatmapPath\"", &next, s.beatmapPath)) break;
        if (!jsonCountsAfter(text, next, &pos, s.declared)) break;
        out.push_back(std::move(s));
    }
    return out;
}

// ---- mods 推导 ----------------------------------------------------------------

struct DerivedMods {
    double rate = 1.0;
    bool hardRock = false;
    bool easy = false;
    bool mirror = false;
    bool unsupported = false;
    std::string unsupportedWhy;
    std::string label;  // "NM" / "HT" / "DT+HR" ...

    void addLabel(const char* s) {
        if (!label.empty()) label += '+';
        label += s;
    }
};

double settingsSpeed(const LazerMod& m, double fallback) {
    const auto it = m.settings.find("speed_change");
    if (it == m.settings.end() || it->second.empty()) return fallback;
    return std::strtod(it->second.c_str(), nullptr);
}

DerivedMods deriveMods(const OsrHeader& h) {
    DerivedMods d;
    bool haveLazerMods = false;
    for (const LazerMod& m : h.lazerExtra.mods) {
        haveLazerMods = true;
        const std::string& a = m.acronym;
        if (a == "HT") {
            d.rate = settingsSpeed(m, 0.75);
            d.addLabel("HT");
        } else if (a == "DT" || a == "NC") {
            d.rate = settingsSpeed(m, 1.5);
            d.addLabel(a.c_str());
        } else if (a == "HR") {
            d.hardRock = true;
            d.addLabel("HR");
        } else if (a == "EZ") {
            d.easy = true;
            d.addLabel("EZ");
        } else if (a == "MR") {
            d.mirror = true;
            d.addLabel("MR");
        } else if (a == "IN") {
            d.unsupported = true;
            d.unsupportedWhy = "IN (Invert) mod: 物件被变换, 本期不支持";
        } else if (a == "RD") {
            d.unsupported = true;
            d.unsupportedWhy = "RD (Random) mod: 列置换, 本期不支持";
        } else {
            d.addLabel(a.c_str());  // NF/HD/FL/PF... 不影响判定窗, 透传显示
        }
    }
    if (!haveLazerMods) {
        // 尾块缺失/无 mods → 回退 legacy mod 位 (L1 实测: legacy 位对 IN 有损, 但
        // HT/DT/NC/HR/EZ/MR 都在)
        const uint32_t bits = h.mods;
        if (bits & 0x00000100u) { d.rate = 0.75; d.addLabel("HT"); }
        if (bits & 0x00000040u) { d.rate = 1.5; d.addLabel("DT"); }
        if (bits & 0x00000200u) { d.rate = 1.5; d.addLabel("NC"); }
        if (bits & 0x00000010u) { d.hardRock = true; d.addLabel("HR"); }
        if (bits & 0x00000002u) { d.easy = true; d.addLabel("EZ"); }
        if (bits & 0x40000000u) { d.mirror = true; d.addLabel("MR"); }
        if (d.label.empty()) d.label = "NM";
    }
    if (d.label.empty()) d.label = "NM";
    return d;
}

const char* kCountNames[6] = {"perfect", "great", "good", "ok", "meh", "miss"};

int64_t countField(const JudgementCounts& c, int t) {
    switch (t) {
        case 0: return c.perfect;
        case 1: return c.great;
        case 2: return c.good;
        case 3: return c.ok;
        case 4: return c.meh;
        default: return c.miss;
    }
}

struct RowResult {
    bool skipped = false;
    std::string skipReason;
    bool allZero = false;
};

}  // namespace

int main() {
    const std::vector<Sample> samples = loadSamples(MRA_LAZER_SAMPLES_JSON);
    if (samples.empty()) {
        std::printf("FATAL: no samples parsed from %s\n", MRA_LAZER_SAMPLES_JSON);
        return 2;
    }
    std::printf("manifest: %zu samples\n\n", samples.size());

    int played = 0, skipped = 0, converged = 0;
    bool gate = true;

    for (const Sample& s : samples) {
        std::printf("=== [%s] %s\n", s.folder.c_str(), s.file.c_str());

        if (s.folder == "3") {
            std::printf("  SKIP: folder==3 (IN (Invert) mod, 物件被变换, 本期不支持)\n\n");
            ++skipped;
            continue;
        }

        // ---- .osr ----
        const fs::path osrPath =
            fs::path(MRA_LAZER_SAMPLE_ROOT) / s.folder / fs::path(s.file);
        std::vector<uint8_t> osrBytes;
        std::string err;
        if (!readFileBytes(osrPath, osrBytes, &err)) {
            std::printf("  FAIL: cannot read .osr: %s (%s)\n\n", osrPath.string().c_str(),
                        err.c_str());
            gate = false;
            continue;
        }
        const OsrParseResult osr = parseOsr(osrBytes);
        if (!osr.error.empty()) {
            std::printf("  FAIL: parseOsr: %s\n\n", osr.error.c_str());
            gate = false;
            continue;
        }
        if (osr.header.mode != 3) {
            std::printf("  SKIP: mode=%d 非 mania\n\n", osr.header.mode);
            ++skipped;
            continue;
        }
        if (!osr.header.isLazer || !osr.header.lazerExtra.present) {
            std::printf("  SKIP: 非 lazer .osr (无尾块 statistics)\n\n");
            ++skipped;
            continue;
        }
        const DerivedMods mods = deriveMods(osr.header);
        if (mods.unsupported) {
            std::printf("  SKIP: %s\n\n", mods.unsupportedWhy.c_str());
            ++skipped;
            continue;
        }

        // ---- 谱面 ----
        std::string osuText;
        if (!readFileText(fs::path(s.beatmapPath), osuText, &err)) {
            std::printf("  FAIL: cannot read beatmap: %s (%s)\n\n", s.beatmapPath.c_str(),
                        err.c_str());
            gate = false;
            continue;
        }
        const OsuParseResult bm = parseOsu(osuText);
        if (!bm.ok) {
            std::printf("  FAIL: parseOsu: %s\n\n", bm.error.c_str());
            gate = false;
            continue;
        }
        if (!bm.beatmap.isMania()) {
            std::printf("  SKIP: beatmap mode=%d 非 mania\n\n", bm.beatmap.mode);
            ++skipped;
            continue;
        }

        // ---- 帧 (谱面时基) ----
        const LzmaDecodeResult lz = lzmaDecode(osr.header.compressedData.data(),
                                               osr.header.compressedData.size());
        if (!lz.ok) {
            std::printf("  FAIL: lzmaDecode: %s\n\n", lz.error.c_str());
            gate = false;
            continue;
        }
        const ReplayFrameDecodeResult fr = decodeReplayFrames(lz.output);
        if (!fr.ok || fr.frames.empty()) {
            std::printf("  FAIL: decodeReplayFrames: %s (malformed=%zu)\n\n",
                        fr.error.c_str(), fr.malformedLines);
            gate = false;
            continue;
        }

        // ---- 重建 ----
        LazerMatchOptions o;
        o.od = bm.beatmap.overallDifficulty;
        o.rate = mods.rate;
        o.hardRock = mods.hardRock;
        o.easy = mods.easy;
        o.mirror = mods.mirror;
        o.keyCount = bm.beatmap.keyCount;
        const LazerMatchResult r = matchLazer(bm.beatmap.objects, fr.frames, o);

        size_t rice = 0, lns = 0;
        for (const ManiaHitObject& ob : bm.beatmap.objects) {
            if (ob.type == ManiaObjectType::Hold && ob.endTimeMs.has_value()) ++lns;
            else ++rice;
        }
        const int64_t tailMiss =
            std::count_if(r.match.holds.begin(), r.match.holds.end(),
                          [](const HoldJudgement& h) {
                              return h.tail.judgement == HitResult::Miss;
                          });

        // ---- 打表 ----
        int64_t l1 = 0;
        std::printf(
            "  mods=%s od=%g rate=%g keys=%d objects(rice=%zu ln=%zu) frames=%zu\n",
            mods.label.c_str(), o.od, o.rate, o.keyCount, rice, lns, fr.frames.size());
        std::printf("  %-7s %9s %9s %6s\n", "tier", "rebuilt", "declared", "diff");
        bool rowZero = true;
        for (int t = 0; t < 6; ++t) {
            const int64_t got = countField(r.counts, t);
            const int64_t want = s.declared[t];
            const int64_t diff = got - want;
            l1 += diff < 0 ? -diff : diff;
            if (diff != 0) rowZero = false;
            std::printf("  %-7s %9lld %9lld %+6lld%s\n", kCountNames[t],
                        static_cast<long long>(got), static_cast<long long>(want),
                        static_cast<long long>(diff), diff == 0 ? "" : "  <-- DIFF");
        }
        const int64_t nRebuilt = r.counts.judged();
        // 尾判定舍入噪声界: lazer 写盘时 Math.Round(发布时刻), 尾档 = |d|/1.5 vs
        // x.5 窗 ⇒ 边界在 x.25/x.75, 记录值 ±0.5ms 内的尾档不可复现 (声明值来自
        // 实况判定用的未取整时钟)。统计落在任一 1.5×win 边界 ±0.34ms 内的尾数,
        // 作为「残差是否在噪声界内」的审计线 (所需翻转数 = L1/2)。
        const LazerWindows lw = lazerHitWindows(o.od, o.rate, mods.hardRock, mods.easy);
        const double winArr[6] = {lw.perfect, lw.great, lw.good, lw.ok, lw.meh, lw.miss};
        int64_t flippableTails = 0;
        for (const HitEvent& e : r.match.events) {
            if (e.phase != HitPhase::HoldTail || !e.offsetMs.has_value()) continue;
            const double d = std::fabs(*e.offsetMs);
            for (const double w : winArr) {
                if (std::fabs(d - 1.5 * w) <= 0.34) { ++flippableTails; break; }
            }
        }
        const int64_t maxPerfectIt = [&] {
            const auto it = osr.header.lazerExtra.maximumStatistics.find("perfect");
            return it == osr.header.lazerExtra.maximumStatistics.end() ? -1 : it->second;
        }();
        int64_t ignoreMissIt = -1;
        {
            const auto it = osr.header.lazerExtra.statistics.find("ignore_miss");
            if (it != osr.header.lazerExtra.statistics.end()) ignoreMissIt = it->second;
        }
        std::printf(
            "  L1=%lld  N(rebuilt)=%lld vs max_stat[perfect]=%lld  tailMiss=%lld vs "
            "stat[ignore_miss]=%lld\n",
            static_cast<long long>(l1), static_cast<long long>(nRebuilt),
            static_cast<long long>(maxPerfectIt), static_cast<long long>(tailMiss),
            static_cast<long long>(ignoreMissIt));
        std::printf(
            "  tail-noise audit: flips needed=%lld vs flippable tails=%lld (lazer 写盘取整"
            "的不可复现尾档上界)\n",
            static_cast<long long>(l1 / 2), static_cast<long long>(flippableTails));
        LazerManiaCounts declaredCounts;
        declaredCounts.perfect = s.declared[0];
        declaredCounts.great = s.declared[1];
        declaredCounts.good = s.declared[2];
        declaredCounts.ok = s.declared[3];
        declaredCounts.meh = s.declared[4];
        declaredCounts.miss = s.declared[5];
        declaredCounts.fromJson = true;
        declaredCounts.valid = true;
        std::printf("  acc305 rebuilt=%.5f declared=%.5f\n", r.acc305,
                    lazerAccuracy305Percent(declaredCounts));

        // 调试通道: MRA_L2_DUMP=<folder> 时把该局的逐物件判定导出成 CSV
        // (build/tmp_lazer/dump_<folder>.csv), 供残差归因分析。
        {
            const char* dumpEnv = std::getenv("MRA_L2_DUMP");
            if (dumpEnv != nullptr && dumpEnv == s.folder) {
                const std::string dumpPath =
                    std::string("build/tmp_lazer/dump_") + s.folder + ".csv";
                std::ofstream df(dumpPath, std::ios::binary);
                df << "objectIndex,column,kind,phase,judgement,objectTimeMs,actualTimeMs,offset\n";
                for (const HitEvent& e : r.match.events) {
                    const char* kind = e.phase == HitPhase::NoteHead ? "rice" : "ln";
                    const char* ph = e.phase == HitPhase::NoteHead ? "note"
                                     : e.phase == HitPhase::HoldHead ? "head" : "tail";
                    df << e.objectIndex << ',' << e.column << ',' << kind << ',' << ph << ','
                       << hitResultName(e.judgement) << ',' << e.objectTimeMs << ','
                       << e.actualTimeMs.value_or(-1) << ','
                       << (e.offsetMs.has_value() ? *e.offsetMs : 0.0) << '\n';
                }
                df << "# unmatched_inputs\n";
                for (const UnmatchedInput& ui : r.match.unmatchedInputs)
                    df << "ui," << ui.column << ',' << ui.timeMs << '\n';
                std::printf("  [dump] %s\n", dumpPath.c_str());
            }
        }
        if (rowZero) {
            ++converged;
            std::printf("  -> OK (all-zero)\n\n");
        } else {
            gate = false;
            std::printf("  -> RESIDUAL (见 DIFF 行)\n\n");
        }
        ++played;
    }

    std::printf("==== lazer e2e: played=%d converged=%d skipped=%d => %s\n", played,
                converged, skipped, gate ? "PASS" : "FAIL");
    return gate ? 0 : 1;
}
