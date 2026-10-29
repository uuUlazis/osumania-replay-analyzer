#include "http/api_server.h"

#include "io/paths.h"

#include "app/dialogs.h"
#include "app/embedded_assets.h"
#include "beatmap/osu_parser.h"
#include "io/md5.h"
#include "lazer/lazer_scan.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
#include "mania/lazer_matcher.h"
// SR 算法模块 (officialSR / SrrStarRating, 独立并行产出)。__has_include 让
// 本接线在其落地前也可编译: 缺头文件时全部 SR 存 -1, pp/局部SR 显示 "—"。
#if __has_include("mania/official_sr.h")
#include "mania/official_sr.h"
#define MRA_HAS_OFFICIAL_SR 1
#else
#define MRA_HAS_OFFICIAL_SR 0
#endif
#if __has_include("mania/srr_sr.h")
#include "mania/srr_sr.h"
#define MRA_HAS_SRR_SR 1
#else
#define MRA_HAS_SRR_SR 0
#endif
#include "util/debug_log.h"

#include <chrono>

#include "replay/osr_reader.h"
#include "replay/replay_decoder.h"
#include "replay/replay_frames.h"
#include "scores/scores_db.h"
#include "session/session_store.h"
#include "skin/skin_loader.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <objbase.h>  // CoInitializeEx (native dialogs)
#include <shlobj.h>   // SHGetFolderPathW (last-state location)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <utility>

namespace mra {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Restart-persistent "last session" state. localStorage cannot be used:
// the server binds a new port on every launch, so the page origin changes
// and its storage resets. All app data (last_state / index caches / webview
// state) lives in <exe dir>\_data\ (用户约定: 不进用户目录, _data 不进 git);
// exe 目录不可写时回退 %TEMP%\ManiaReplayAnalyzer。首次运行会把旧的用户目录
// 数据 (%LOCALAPPDATA%\ManiaReplayAnalyzer 下的 last_state + 三个索引缓存)
// 一次性迁移过来; WebView2 大档案不迁移, 全新开始。

// 应用数据目录 (带一次性迁移与进程内缓存)。
std::filesystem::path dataDir() {
    static std::filesystem::path dir;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        wchar_t exeBuf[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exeBuf, MAX_PATH) != 0) {
            std::filesystem::path candidate =
                std::filesystem::path(exeBuf).parent_path() / L"_data";
            std::error_code ec;
            std::filesystem::create_directories(candidate, ec);
            if (!ec) dir = candidate;
        }
        if (dir.empty()) {
            // 回退: exe 目录不可写 → %TEMP%\ManiaReplayAnalyzer
            wchar_t temp[MAX_PATH]{};
            if (GetTempPathW(MAX_PATH, temp) != 0) {
                std::filesystem::path candidate =
                    std::filesystem::path(temp) / L"ManiaReplayAnalyzer";
                std::error_code ec;
                std::filesystem::create_directories(candidate, ec);
                if (!ec) dir = candidate;
            }
        }
        // 一次性迁移: 旧用户目录里的小状态文件 → _data (保住全量扫描成果);
        // WebView2 大档案不迁移。
        if (!dir.empty()) {
            wchar_t buf[MAX_PATH]{};
            if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                 SHGFP_TYPE_CURRENT, buf) == S_OK) {
                std::filesystem::path legacy =
                    std::filesystem::path(buf) / L"ManiaReplayAnalyzer";
                for (const wchar_t* name :
                     {L"last_state.json", L"songs_index_cache.json",
                      L"replay_index_cache.json", L"browse_cache.json"}) {
                    std::error_code ec;
                    std::filesystem::path dst = dir / name;
                    std::filesystem::path src = legacy / name;
                    if (!std::filesystem::exists(dst, ec) &&
                        std::filesystem::exists(src, ec)) {
                        std::filesystem::copy_file(
                            src, dst,
                            std::filesystem::copy_options::overwrite_existing,
                            ec);
                    }
                }
            }
        }
    }
    return dir;
}

std::filesystem::path lastStatePath() {
    auto dir = dataDir();
    if (dir.empty()) return {};
    return dir / L"last_state.json";
}

// Disk (L2) sidecar for the /api/scores/browse response cache: the last
// fully built response lives next to last_state.json so a process restart
// can serve an unchanged scores.db without rebuilding the indexes.
std::filesystem::path browseCacheSidecarPath() {
    auto path = lastStatePath();
    if (path.empty()) return {};
    return path.parent_path() / L"browse_cache.json";
}

// Disk (L2) sidecars for the per-file index caches built by
// refreshScoreStoresLocked: they live next to last_state.json so a process
// restart can seed the Data\r replay index and the Songs index in
// milliseconds instead of rescanning the whole library (tens of seconds).
std::filesystem::path replayIndexCachePath() {
    auto path = lastStatePath();
    if (path.empty()) return {};
    return path.parent_path() / L"replay_index_cache.json";
}

std::filesystem::path songsIndexCachePath() {
    auto path = lastStatePath();
    if (path.empty()) return {};
    return path.parent_path() / L"songs_index_cache.json";
}

// Best-effort silent sidecar write: persistence must never produce a new
// failure path, and a missing/stale file only costs a rescan.
void writeJsonSidecar(const std::filesystem::path& path, const json& j) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return;
    try {
        // replace: odd bytes in .osu metadata must not abort the write.
        o << j.dump(-1, ' ', false, json::error_handler_t::replace);
    } catch (...) {
    }
}

// Best-effort silent sidecar read: a non-object result means "unusable"
// and the caller falls back to the normal full rebuild.
json readJsonSidecar(const std::filesystem::path& path) {
    if (!path.empty()) {
        std::ifstream f(path);
        if (f) {
            try {
                json in;
                f >> in;
                if (in.is_object()) return in;
            } catch (...) {
            }
        }
    }
    return json();
}

// Merge-patch the restart-persistent last-session file. Best-effort:
// failures are silently ignored (the UI still works without memory).
void mergeLastState(const json& patch) {
    auto path = lastStatePath();
    if (path.empty()) return;
    json existing = json::object();
    {
        std::ifstream f(path);
        if (f) {
            try {
                json in;
                f >> in;
                if (in.is_object()) existing = std::move(in);
            } catch (...) {
            }
        }
    }
    for (auto it = patch.begin(); it != patch.end(); ++it)
        existing[it.key()] = it.value();
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (o) o << existing.dump();
}

std::optional<std::string> readFileBytes(const std::filesystem::path& p,
                                         std::vector<uint8_t>& out) {
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec))
        return "file not found: " + mra::pathToUtf8(p);
    std::ifstream f(p, std::ios::binary);
    if (!f) return "cannot open: " + mra::pathToUtf8(p);
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size > 512ull * 1024 * 1024) return "file too large: " + mra::pathToUtf8(p);
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), size);
    if (!f && size > 0) return "short read: " + mra::pathToUtf8(p);
    return std::nullopt;
}

std::optional<std::string> readFileBytes(const std::string& path,
                                         std::vector<uint8_t>& out) {
    return readFileBytes(mra::pathFromUtf8(path), out);
}

std::optional<std::string> readTextFile(const std::string& path,
                                        std::string& out,
                                        std::string* md5Hex = nullptr) {
    std::vector<uint8_t> bytes;
    auto err = readFileBytes(path, bytes);
    if (err) return err;
    if (md5Hex) {
        Md5 md5;
        md5.update(bytes.data(), bytes.size());
        *md5Hex = Md5::toHex(md5.digest());
    }
    // Strip UTF-8 BOM if present; osu files may include it.
    size_t start = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
        bytes[2] == 0xBF)
        start = 3;
    out.assign(reinterpret_cast<const char*>(bytes.data() + start),
               bytes.size() - start);
    return std::nullopt;
}

json okResponse(const json& data) {
    return json{{"ok", true}, {"data", data}};
}

json errorResponse(const std::string& code, const std::string& message,
                   const json& details = json::object()) {
    return json{{"ok", false},
                {"error", json{{"code", code}, {"message", message},
                               {"details", details}}}};
}

void sendError(const httplib::Request& req, httplib::Response& res,
               int httpStatus, const std::string& code,
               const std::string& message,
               const json& details = json::object()) {
    debuglog::log("api error " + std::to_string(httpStatus) + " " + code +
                  " " + req.method + " " + req.path + ": " + message);
    res.status = httpStatus;
    res.set_content(errorResponse(code, message, details).dump(), "application/json");
}

const char* hitResultNameJson(HitResult r) {
    switch (r) {
        case HitResult::Perfect: return "perfect";
        case HitResult::Great: return "great";
        case HitResult::Good: return "good";
        case HitResult::Ok: return "ok";
        case HitResult::Meh: return "meh";
        case HitResult::Miss: return "miss";
    }
    return "?";
}

const char* hitPhaseNameJson(HitPhase p) {
    switch (p) {
        case HitPhase::NoteHead: return "note_head";
        case HitPhase::HoldHead: return "hold_head";
        case HitPhase::HoldTail: return "hold_tail";
    }
    return "?";
}

json countsJson(const JudgementCounts& c) {
    return json{{"perfect", c.perfect}, {"great", c.great}, {"good", c.good},
                {"ok", c.ok}, {"meh", c.meh}, {"miss", c.miss}};
}

// 判定窗两套取值 (matcher 实际上报的 w / w2), 用于核对 mod 因子是否生效。
json windowsJson(const ManiaWindows& w) {
    return json{{"perfect", w.perfect}, {"great", w.great}, {"good", w.good},
                {"ok", w.ok}, {"meh", w.meh}, {"miss", w.miss}};
}

// acc 保留 5 位小数 (既有 accuracy 字段的既有精度, 保持输出逐位不变)。
double round5(double v) { return std::round(v * 100000.0) / 100000.0; }
// ---- Hands aggregation (POST /api/analysis/{sid}/hands) ----
struct HandBucket {
    int64_t max = 0, c300 = 0, c200 = 0, c100 = 0, c50 = 0;
    // miss 只作展示计数 (用户 2026-10-25 追加), 进 acc 分母, 权重 0; 不进 n/偏差 — miss 无偏移样本) [2026-10-27 修正]
    int64_t miss = 0;
};

struct HandStats {
    HandBucket counts;
    int64_t n = 0;                     // 命中判定数 (miss 计入 acc 分母, 不计于此)
    double acc = 0.0;                  // percent, follows the match's scoring profile
    // 打早/打晚平均 (符号保留, 打早为负): 各侧只对各自样本求均值, offset=0 不计
    // (用户定案 1A/2A/3B); 无样本保持 0, 由 hasEarly/hasLate 区分前端显示 -
    double earlyMeanMs = 0.0, lateMeanMs = 0.0;
    bool hasEarly = false, hasLate = false;
};

// Hit grade -> per-grade bucket. miss 进 acc 分母 (权重 0); 命中档才进 n/偏差
// (未匹配输入仍由调用方排除, 不进任何统计)。
void handBucketAdd(HandBucket& b, HitResult r) {
    switch (r) {
        case HitResult::Perfect: ++b.max; break;
        case HitResult::Great: ++b.c300; break;
        case HitResult::Good: ++b.c200; break;
        case HitResult::Ok: ++b.c100; break;
        case HitResult::Meh: ++b.c50; break;
        case HitResult::Miss: ++b.miss; break;
        default: break;
    }
}

json handStatsJson(const HandStats& h) {
    return json{
        {"counts", json{{"max", h.counts.max},
                        {"c300", h.counts.c300},
                        {"c200", h.counts.c200},
                        {"c100", h.counts.c100},
                        {"c50", h.counts.c50},
                        {"miss", h.counts.miss}}},
        {"n", h.n},
        {"acc", std::round(h.acc * 100.0) / 100.0},
        {"earlyMeanMs", std::round(h.earlyMeanMs * 1000.0) / 1000.0},
        {"lateMeanMs", std::round(h.lateMeanMs * 1000.0) / 1000.0},
        {"hasEarly", h.hasEarly},
        {"hasLate", h.hasLate}};
}

// One hand's aggregation over the match pass of the session's scoring
// profile (rulesScoreV2 ? matchV2 : match — same source as /segment).
// Code fact: match.events already carries LN head AND tail judgement events
// (phase == HoldHead / HoldTail) and holds[].head/.tail duplicate them.
// V1 (stable): each judged object contributes exactly ONE hit —
//   * rice / note head -> NoteHead event (time axis: objectTimeMs)
//   * hold note        -> holds[].tail combined judgement (time axis:
//                         tail.objectTimeMs), deduplicated by objectIndex —
//                         the same "one unit per LN" tally analyzeSegment
//                         uses for stableCounts; the events' HoldHead /
//                         HoldTail copies are always skipped.
// V2 (ScoreV2/lazer): an LN is TWO independent judgements (head + tail;
//   same tally analyzeSegment/总览 uses for counts: NoteHead + HoldHead +
//   holds[].tail), so HoldHead events are counted too. Skipping them
//   undercounted acc — LN heads are near-always MAX (2026-10-27 fix).
// Effective judgement = v1Judgement.value_or(judgement): V1 (stable) LNs
// carry the combined head+tail result in v1Judgement, rice and V2 events
// fall back to their own judgement. Misses enter counts + acc (weight 0) but no n/offset.
HandStats analyzeHand(const MatchResult& match, int keyCount, bool leftHand,
                      bool centerToLeft, bool hasRange, int64_t startMs,
                      int64_t endMs, bool v2Profile) {
    HandStats out;
    std::vector<double> offsets;
    std::set<int> seenHolds;  // dedupe re-judged hold entries

    auto isLeft = [&](int col) {
        if (keyCount % 2 == 0) return col < keyCount / 2;
        const int center = (keyCount - 1) / 2;
        if (col < center) return true;
        if (col > center) return false;
        return centerToLeft;  // odd key count: center column is assignable
    };

    auto consider = [&](const HitEvent& ev, int64_t timeMs, bool withOffset) {
        if (ev.column < 0 || ev.column >= keyCount) return;
        if (isLeft(ev.column) != leftHand) return;  // 只统计本手 (leftHand 由调用方给出)
        if (hasRange && (timeMs < startMs || timeMs >= endMs)) return;
        const HitResult r = ev.v1Judgement.value_or(ev.judgement);
        handBucketAdd(out.counts, r);  // miss 进 acc 分母 (权重 0)
        if (r == HitResult::Miss) return;
        ++out.n;
        if (withOffset && ev.offsetMs) offsets.push_back(*ev.offsetMs);
    };

    for (const HitEvent& ev : match.events) {
        if (ev.phase == HitPhase::NoteHead) consider(ev, ev.objectTimeMs, true);
        // V2: LN 头是独立判定 (与总览/选段 counts 同口径); 不采头判偏移样本
        else if (v2Profile && ev.phase == HitPhase::HoldHead)
            consider(ev, ev.objectTimeMs, false);
    }
    for (const HoldJudgement& h : match.holds) {
        if (!seenHolds.insert(h.tail.objectIndex).second) continue;
        consider(h.tail, h.tail.objectTimeMs, true);
    }

    if (out.n + out.counts.miss > 0) {
        // Reuse accuracy.cpp formulas: miss 计入分母 (judged() 含 miss,
        // 分子权重 0) — 与总览 acc 同口径, 左右手可对总 acc 对账.
        JudgementCounts c;
        c.perfect = out.counts.max;
        c.great = out.counts.c300;
        c.good = out.counts.c200;
        c.ok = out.counts.c100;
        c.meh = out.counts.c50;
        c.miss = out.counts.miss;
        out.acc = accuracyPercent(
            c, v2Profile ? AccuracyProfile::LegacyScoreV2
                         : AccuracyProfile::LegacyScoreV1);
        double earlySum = 0.0, lateSum = 0.0;
        int64_t earlyN = 0, lateN = 0;
        for (double v : offsets) {
            if (v < 0.0) { earlySum += v; ++earlyN; }
            else if (v > 0.0) { lateSum += v; ++lateN; }
        }
        if (earlyN > 0) {
            out.hasEarly = true;
            out.earlyMeanMs = earlySum / static_cast<double>(earlyN);
        }
        if (lateN > 0) {
            out.hasLate = true;
            out.lateMeanMs = lateSum / static_cast<double>(lateN);
        }
    }
    return out;
}

json offsetStatsJson(const OffsetStats& s, int digits = 2) {
    auto r = [](double v, int d) {
        double f = std::pow(10.0, d);
        return std::round(v * f) / f;
    };
    return json{{"count", s.count},
                {"mean", r(s.mean, digits)},
                {"meanAbs", r(s.meanAbs, digits)},
                {"median", r(s.median, digits)},
                {"sd", r(s.sd, digits)},
                {"p95", r(s.p95, digits)},
                {"early", s.early},
                {"late", s.late},
                {"earlyMean", r(s.earlyMean, digits)},
                {"lateMean", r(s.lateMean, digits)},
                {"min", r(s.min, digits)},
                {"max", r(s.max, digits)}};
}

json columnStatsJson(const std::vector<ColumnStats>& cols) {
    json arr = json::array();
    for (const auto& c : cols) {
        arr.push_back(json{{"column", c.column},
                           {"counts", countsJson(c.counts)},
                           {"offsets", offsetStatsJson(c.offsets)}});
    }
    return arr;
}

// ---- scores.db auxiliary helpers (Data\r replay index, Songs index) ----

bool isMd5Hex(const std::string& s) {
    if (s.size() != 32) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c)) &&
            !(c >= 'a' && c <= 'f'))
            return false;
    return true;
}

// Reads one osu!string (0x00 empty / 0x0b ULEB-length UTF-8) from buf.
// Returns false when the header is malformed or the string runs past the end.
bool readOsuStringAt(const std::vector<uint8_t>& buf, size_t& pos,
                     std::string& out) {
    if (pos >= buf.size()) return false;
    uint8_t flag = buf[pos++];
    if (flag == 0x00) { out.clear(); return true; }
    if (flag != 0x0b) return false;
    int64_t len = 0;
    int shift = 0;
    for (;;) {
        if (pos >= buf.size()) return false;
        uint8_t b = buf[pos++];
        len |= static_cast<int64_t>(b & 0x7f) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
        if (shift > 35) return false;
    }
    if (len < 0 || pos + static_cast<size_t>(len) > buf.size()) return false;
    out.assign(reinterpret_cast<const char*>(buf.data() + pos),
               static_cast<size_t>(len));
    pos += static_cast<size_t>(len);
    return true;
}

// .NET ticks (0001-01-01 epoch, scores.db playedTicks) minus this equals the
// Win32 FILETIME ticks (1601-01-01 epoch) used in Data\\r .osr file names.
constexpr int64_t kDotNetToFileTimeTicks = 504911232000000000LL;

// Browse response cache schema. Bumped when the row schema changes so the
// persisted (L2) and in-memory (L1) browse caches built by older builds are
// invalidated on the first request after an upgrade (the scores.db MD5 key
// alone would keep serving rows without the new fields).
constexpr int kBrowseResponseCacheVersion = 5;

// Extracts the value of the first "Key: value" line with an exact key match.
std::string iniValue(const std::string& text, const std::string& key) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon != std::string::npos &&
            line.substr(0, colon) == key) {
            std::string v = line.substr(colon + 1);
            if (!v.empty() && v.front() == ' ') v.erase(0, 1);
            return v;
        }
        pos = eol + 1;
    }
    return "";
}

// ---- SR / pp wiring (docs/pp-sr-notes.md §2 公式, §3.8 mods 档位) ----
// 不可算一律 -1 (前端显示 "—")。
constexpr double kSrNotComputable = -1.0;

double saneSr(double v) {
    return (std::isfinite(v) && v >= 0.0) ? v : kSrNotComputable;
}

// 官方 pp (lazer 移植, 常数见 pp-sr-notes §2.2/§2.3):
//   pp = 8.0 * max(SR-0.15, 0.05)^2.2 * max(0, 5*acc-4)
//        * (1 + 0.1*min(1, hits/1500)) * mult          mult: NF ×0.75, EZ ×0.50
// acc 用官方判定权重 Perfect 320 / Great 300 / Good 200 / Ok 100 / Meh 50
// (stable .osr 映射: Geki→Perfect, 300→Great, Katu→Good, 100→Ok, 50→Meh),
// hits 含 Miss。SR < 0 (无谱面/不可算) → -1。
double officialPpFromCounts(double sr, int64_t perfect, int64_t great,
                            int64_t good, int64_t ok, int64_t meh,
                            int64_t miss, int64_t mods) {
    if (sr < 0) return -1.0;
    const double totalHits = static_cast<double>(perfect) + great + good +
                             ok + meh + miss;
    if (totalHits <= 0) return -1.0;
    const double acc =
        (320.0 * perfect + 300.0 * great + 200.0 * good + 100.0 * ok +
         50.0 * meh) / (totalHits * 320.0);
    double mult = 1.0;
    if (mods & kModNoFail) mult *= 0.75;
    if (mods & kModEasy) mult *= 0.50;
    return 8.0 * std::pow(std::max(sr - 0.15, 0.05), 2.2) *
           std::max(0.0, 5.0 * acc - 4.0) *
           (1.0 + 0.1 * std::min(1.0, totalHits / 1500.0)) * mult;
}

// 与 loadReplaySession 的判定 rate 同源 (§3.8: 只有 DT/NC/HT 改 rate):
// DT/NC → 1.5, HT → 0.75, 其余 → 1.0。summary 的 rate/windows 上报也用它,
// 未编译 SR 模块时同样需要有定义。
double rateForMods(int64_t mods) {
    if (mods & (kModDoubleTime | kModNightcore)) return 1.5;
    if (mods & kModHalfTime) return 0.75;
    return 1.0;
}

// lazer 尾块 mods 的实际 rate (特殊 mod 适配): DT/NC/HT 取 speed_change
// (小数倍率, 如 DT 1.25×), 缺省 1.5/1.5/0.75; 其余 mod 不改 rate; 无相关
// mod = 1.0。与 loadReplaySession 的 lazer 匹配 rate 同源; 仅 lazer 局使用
// (stable 局维持 legacy 位映射, 判定红线不动)。
double lazerRateFromHeader(const OsrHeader& h) {
    double rate = 1.0;
    for (const LazerMod& m : h.lazerExtra.mods) {
        if (m.acronym == "HT") {
            rate = 0.75;
            auto it = m.settings.find("speed_change");
            if (it != m.settings.end()) {
                try { rate = std::stod(it->second); } catch (...) {}
            }
        } else if (m.acronym == "DT" || m.acronym == "NC") {
            double sc = 1.5;
            auto it = m.settings.find("speed_change");
            if (it != m.settings.end()) {
                try { sc = std::stod(it->second); } catch (...) {}
            }
            rate = sc;
        }
    }
    return rate;
}

// lazer 扫描索引持久化路径 (上次位置迟滞修复): bind/refresh 的全量 .osr
// 重解析是秒级; 把 (size, mtimeMs) + browse 字段缓存到 _data, 下次 bind
// 同目录时 warm 预热 → refresh 仅 stat 行走, 毫秒级应用。
fs::path lazerIndexCachePath() {
    auto dir = dataDir();
    if (dir.empty()) return {};
    return dir / L"lazer_index_cache.json";
}

#if MRA_HAS_OFFICIAL_SR || MRA_HAS_SRR_SR
// Beatmap 物件 → SR 模块的 note 列表 (column, headMs, tailMs);
// rice (非 LN) 约定 tailMs < 0。
std::vector<SrNote> srNotesFromBeatmap(const OsuBeatmap& beatmap) {
    std::vector<SrNote> notes;
    notes.reserve(beatmap.objects.size());
    for (const ManiaHitObject& o : beatmap.objects) {
        SrNote n;
        n.column = o.column;
        n.headMs = static_cast<int>(o.startTimeMs);
        n.tailMs = (o.type == ManiaObjectType::Hold && o.endTimeMs)
                       ? static_cast<int>(*o.endTimeMs)
                       : -1;
        notes.push_back(n);
    }
    return notes;
}
#endif

#if MRA_HAS_OFFICIAL_SR
// Songs 索引行的 official SR 档位选择 (只读缓存字段, 不重算 SR)。
double officialSrForMods(const SongsBeatmapMeta& meta, int64_t mods) {
    if (mods & (kModDoubleTime | kModNightcore)) return meta.officialSrDt;
    if (mods & kModHalfTime) return meta.officialSrHt;
    return meta.officialSrBase;
}
#endif

// Songs 索引扫描的 SR 计算: 完整解析 .osu 后算 officialSR base/DT(1.5)/
// HT(0.75) 与 SRR 全局 SR (OD 用谱面 OD)。只在扫描到新增/变更文件时运行;
// 未变更文件由 per-file 缓存与 sidecar 直接回放这些字段。非 mania /
// K>10 / notes<20 / 解析失败一律存 -1。
void computeSrMeta(const std::string& text, SongsBeatmapMeta& meta) {
    meta.officialSrBase = kSrNotComputable;
    meta.officialSrDt = kSrNotComputable;
    meta.officialSrHt = kSrNotComputable;
    meta.srrSr = kSrNotComputable;
    meta.bpm = kSrNotComputable;
    meta.lengthMs = -1;
    OsuParseResult parsed = parseOsu(text);
    if (!parsed.ok || !parsed.beatmap.isMania()) return;
    const OsuBeatmap& beatmap = parsed.beatmap;
    if (beatmap.keyCount <= 0 || beatmap.keyCount > 10) return;
    if (beatmap.objects.size() < 20) return;
#if MRA_HAS_OFFICIAL_SR || MRA_HAS_SRR_SR
    const std::vector<SrNote> notes = srNotesFromBeatmap(beatmap);
#endif
    try {
#if MRA_HAS_OFFICIAL_SR
        meta.officialSrBase =
            saneSr(OfficialStarRating(beatmap.keyCount, notes, 1.0));
        meta.officialSrDt =
            saneSr(OfficialStarRating(beatmap.keyCount, notes, 1.5));
        meta.officialSrHt =
            saneSr(OfficialStarRating(beatmap.keyCount, notes, 0.75));
#endif
#if MRA_HAS_SRR_SR
        meta.srrSr = saneSr(SrrStarRating(beatmap.keyCount,
                                          beatmap.overallDifficulty, notes));
#endif
    } catch (...) {
        // sunny SR / official SR 在极端谱面上抛错: 按 不可算 处理, 不让
        // 扫描线程带异常逃逸。
        meta.officialSrBase = kSrNotComputable;
        meta.officialSrDt = kSrNotComputable;
        meta.officialSrHt = kSrNotComputable;
        meta.srrSr = kSrNotComputable;
        meta.bpm = kSrNotComputable;
        meta.lengthMs = -1;
    }
    // BPM: 驻留时长最长的无继承 timing 段; Length: 物件跨度 (首头→末尾,
    // LN 尾计入)。多 BPM 图取主体段, 单值可排序可筛选。
    {
        double minHead = 0, maxEnd = 0;
        for (const auto& o : beatmap.objects) {
            const double end = (o.type == ManiaObjectType::Hold && o.endTimeMs)
                                   ? static_cast<double>(*o.endTimeMs)
                                   : static_cast<double>(o.startTimeMs);
            if (minHead == 0 || o.startTimeMs < minHead)
                minHead = static_cast<double>(o.startTimeMs);
            maxEnd = std::max(maxEnd, end);
        }
        if (!beatmap.objects.empty() && maxEnd > minHead)
            meta.lengthMs = static_cast<int64_t>(maxEnd - minHead);
        const auto& tps = beatmap.timingPoints;
        std::map<double, double> dwell;  // bpm -> dwell ms
        for (size_t i = 0; i < tps.size(); ++i) {
            if (!tps[i].uninherited || tps[i].beatLength <= 0) continue;
            double segEnd = (i + 1 < tps.size())
                                ? tps[i + 1].timeMs
                                : std::max(maxEnd, tps[i].timeMs + 1);
            if (segEnd <= tps[i].timeMs) segEnd = tps[i].timeMs + 1;
            dwell[60000.0 / tps[i].beatLength] += segEnd - tps[i].timeMs;
        }
        double bestDwell = -1;
        for (const auto& [bpmVal, d] : dwell)
            if (d > bestDwell) { bestDwell = d; meta.bpm = saneSr(bpmVal); }
    }
    if (debuglog::enabled())
        debuglog::log("sr computed " + meta.path + " official=" +
                      std::to_string(meta.officialSrBase) + "/" +
                      std::to_string(meta.officialSrDt) + "/" +
                      std::to_string(meta.officialSrHt) + " srr=" +
                      std::to_string(meta.srrSr) + " bpm=" +
                      std::to_string(meta.bpm) + " lenMs=" +
                      std::to_string(meta.lengthMs));
}

}  // namespace

// ---- Fresh scores.db / replay / songs data for the score browser ----
//
// The app can stay open for days while osu! keeps writing new plays into
// scores.db and Data\r. Every browse therefore re-checks the sources on
// disk; unchanged files are only stat-ed (their parsed data is cached), so
// a refresh over a full library costs a couple of seconds at most.
// Callers must hold scoresMutex_.
void ApiServer::refreshScoreStoresLocked(std::string& err) {
    err.clear();

    // 1. scores.db: re-parse when size/mtime changed. A transient failure
    // (osu! mid-write) keeps the previous snapshot; the next browse retries.
    if (scoresLoaded_ && !scoresDbPath_.empty()) {
        std::error_code ec;
        fs::path dbp = mra::pathFromUtf8(scoresDbPath_);
        auto stamp = fs::last_write_time(dbp, ec);
        uintmax_t sz = fs::file_size(dbp, ec);
        if (!ec && (stamp != scoresDbStamp_ || sz != scoresDbSize_)) {
            ScoresDb db;
            std::string perr;
            if (parseScoresDb(scoresDbPath_, db, perr)) {
                scores_ = std::make_unique<ScoresDb>(std::move(db));
                scoresDbStamp_ = stamp;
                scoresDbSize_ = sz;
            }
        }
    }

    // 2. Data\r replay index: rescan when the directory mtime changed.
    if (dataRFolder_.empty()) {
        err = "Data\\r folder is not selected";
        return;
    }
    {
        std::error_code ec;
        fs::path dir = mra::pathFromUtf8(dataRFolder_);
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
            err = "Data\\r folder not found: " + dataRFolder_;
            return;
        }
        auto dstamp = fs::last_write_time(dir, ec);  // ec -> treat as changed
        bool dirty = !replayIndexBuilt_ || !dataRStampValid_ || ec ||
                     dstamp != dataRStamp_;
        // Per-file cache <-> sidecar entry converters. mtime values
        // round-trip as integer counts of file_time_type's own clock, so a
        // restored stamp compares exactly equal to later stat()s.
        auto replayEntryToJson = [](const fs::path& p,
                                    const ReplayFileEntry& e) {
            return json{{"p", mra::pathToUtf8(p)},
                        {"size", e.size},
                        {"mtime",
                         static_cast<int64_t>(
                             e.mtime.time_since_epoch().count())},
                        {"ticks", e.ticks},
                        {"md5", e.md5}};
        };
        auto replayEntryFromJson = [](const json& item, ReplayFileEntry& e,
                                      std::string& pUtf8) {
            try {
                if (!item.is_object() || !item.at("p").is_string() ||
                    !item.at("size").is_number_unsigned() ||
                    !item.at("mtime").is_number_integer() ||
                    !item.at("ticks").is_string() ||
                    !item.at("md5").is_string())
                    return false;
                pUtf8 = item.at("p").get<std::string>();
                if (pUtf8.empty()) return false;
                e.size = item.at("size").get<std::uintmax_t>();
                e.mtime = fs::file_time_type{
                    fs::file_time_type::duration{
                        item.at("mtime").get<int64_t>()}};
                e.ticks = item.at("ticks").get<std::string>();
                e.md5 = item.at("md5").get<std::string>();
                return true;
            } catch (...) {
                return false;
            }
        };
        // Restart fast path: seed the per-file cache from the sidecar the
        // last full scan wrote (milliseconds instead of a full rescan) and
        // rebuild the lookup indexes exactly like the scan tail below. Any
        // version/folder/schema mismatch or IO/parse failure silently falls
        // through to the full scan without touching the caches. Drift gate:
        // the seed may only adopt the saved directory stamp and skip the
        // scan when that stamp still equals the live directory mtime
        // (dstamp read above). When the library drifted since the sidecar
        // was written (e.g. new .osr files), the sidecar entries merely
        // warm the per-file cache so the full scan below only stat-s
        // unchanged files and re-reads new/changed ones — the index must
        // not freeze at the stale on-disk snapshot.
        bool seeded = false;
        if (dirty && !replayIndexBuilt_) {
            json env = readJsonSidecar(replayIndexCachePath());
            try {
                if (env.is_object() && env.value("version", 0) == 1 &&
                    env.value("folder", std::string{}) == dataRFolder_ &&
                    env.at("dirStamp").is_number_integer() &&
                    env.at("entries").is_array()) {
                    const auto savedStamp = fs::file_time_type{
                        fs::file_time_type::duration{
                            env.at("dirStamp").get<int64_t>()}};
                    // stat failure -> no trustworthy live stamp -> rescan.
                    bool driftFree = !ec && savedStamp == dstamp;
                    std::map<fs::path, ReplayFileEntry> seededCache;
                    bool ok = true;
                    for (const auto& item : env.at("entries")) {
                        ReplayFileEntry e;
                        std::string pUtf8;
                        if (!replayEntryFromJson(item, e, pUtf8)) {
                            ok = false;
                            break;
                        }
                        seededCache[mra::pathFromUtf8(pUtf8)] = std::move(e);
                    }
                    if (ok) {
                        replayFileCache_ = std::move(seededCache);
                        if (driftFree) {
                            replayIndex_.clear();
                            replayTickIndex_.clear();
                            for (const auto& kv : replayFileCache_) {
                                std::string p = mra::pathToUtf8(kv.first);
                                if (isMd5Hex(kv.second.md5))
                                    replayIndex_[kv.second.md5] = p;
                                if (!kv.second.ticks.empty())
                                    replayTickIndex_[kv.second.ticks] = p;
                            }
                            dataRStamp_ = savedStamp;
                            dataRStampValid_ = true;
                            replayIndexBuilt_ = true;
                            seeded = true;
                        }
                        // Drifted: keep the warmed per-file cache but leave
                        // Built/Valid/stamp untouched, so the full scan
                        // branch below runs and picks up the changes.
                    }
                }
            } catch (...) {
                seeded = false;
            }
        }
        if (dirty && !seeded) {
            std::map<fs::path, ReplayFileEntry> fresh;
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code ec2;
                if (!it->is_regular_file(ec2)) continue;
                std::string ext = it->path().extension().string();
                for (char& c : ext)
                    c = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(c)));
                if (ext != ".osr") continue;
                auto sz = it->file_size(ec2);
                auto mt = it->last_write_time(ec2);
                if (ec2) continue;
                auto cit = replayFileCache_.find(it->path());
                if (cit != replayFileCache_.end() &&
                    cit->second.size == sz && cit->second.mtime == mt) {
                    fresh.emplace(it->path(), cit->second);
                    continue;
                }
                ReplayFileEntry e;
                e.size = sz;
                e.mtime = mt;
                // osu pairs a score with its replay through the file name
                // <beatmapMd5>-<FILETIME ticks>.
                std::string stem = it->path().stem().string();
                size_t dash = stem.rfind('-');
                if (dash != std::string::npos && dash > 0) {
                    std::string bm = stem.substr(0, dash);
                    std::string ticks = stem.substr(dash + 1);
                    bool digits = !ticks.empty();
                    for (char c : ticks)
                        if (c < '0' || c > '9') { digits = false; break; }
                    if (digits) {
                        for (char& c : bm)
                            c = static_cast<char>(std::tolower(
                                static_cast<unsigned char>(c)));
                        e.ticks = bm + "-" + ticks;
                    }
                }
                // embedded replay md5 from the fixed-size header
                std::ifstream f(it->path(), std::ios::binary);
                if (f) {
                    std::vector<uint8_t> head(512);
                    f.read(reinterpret_cast<char*>(head.data()),
                           static_cast<std::streamsize>(head.size()));
                    head.resize(static_cast<size_t>(f.gcount()));
                    size_t pos = 5;  // mode(1) + version(4)
                    std::string bm, player, rmd5;
                    if (readOsuStringAt(head, pos, bm) &&
                        readOsuStringAt(head, pos, player) &&
                        readOsuStringAt(head, pos, rmd5) && isMd5Hex(rmd5))
                        e.md5 = rmd5;
                }
                fresh.emplace(it->path(), std::move(e));
            }
            if (!ec) {
                replayFileCache_ = std::move(fresh);
                replayIndex_.clear();
                replayTickIndex_.clear();
                for (const auto& kv : replayFileCache_) {
                    std::string p = mra::pathToUtf8(kv.first);
                    if (isMd5Hex(kv.second.md5))
                        replayIndex_[kv.second.md5] = p;
                    if (!kv.second.ticks.empty())
                        replayTickIndex_[kv.second.ticks] = p;
                }
                dataRStamp_ = dstamp;
                dataRStampValid_ = true;
                replayIndexBuilt_ = true;
                // Persist the freshly built cache so the next process start
                // seeds from disk instead of rescanning (best-effort,
                // silent; dataRFolder_ is non-empty here).
                json entries = json::array();
                for (const auto& kv : replayFileCache_)
                    entries.push_back(replayEntryToJson(kv.first, kv.second));
                writeJsonSidecar(replayIndexCachePath(),
                                 json{{"version", 1},
                                      {"folder", dataRFolder_},
                                      {"dirStamp",
                                       static_cast<int64_t>(
                                           dstamp.time_since_epoch()
                                               .count())},
                                      {"entries", std::move(entries)}});
            }
        }
    }

    // 3. Songs index: same pattern, recursive over the Songs tree.
    // (Extracted into its own member so the lazer data channel — which has
    // no scores.db / Data\r and must not run sections 1-2 — can reuse it.)
    refreshSongsIndexLocked(err);
}

void ApiServer::refreshSongsIndexLocked(std::string& err) {
    err.clear();
    if (songsFolder_.empty()) {
        err = "Songs folder is not selected";
        return;
    }
    {
        std::error_code ec;
        fs::path dir = mra::pathFromUtf8(songsFolder_);
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
            err = "Songs folder not found: " + songsFolder_;
            return;
        }
        auto sstamp = fs::last_write_time(dir, ec);
        bool dirty = !songsIndexBuilt_ || !songsStampValid_ || ec ||
                     sstamp != songsStamp_;
        // Per-file cache <-> sidecar entry converters; mtime values
        // round-trip as integer counts of file_time_type's own clock (see
        // the replay section above).
        auto songsEntryToJson = [](const fs::path& p,
                                   const SongsFileEntry& e) {
            return json{{"p", mra::pathToUtf8(p)},
                        {"size", e.size},
                        {"mtime",
                         static_cast<int64_t>(
                             e.mtime.time_since_epoch().count())},
                        {"md5", e.md5},
                        {"meta", json{{"path", e.meta.path},
                                      {"title", e.meta.title},
                                      {"artist", e.meta.artist},
                                      {"creator", e.meta.creator},
                                      {"audioFilename", e.meta.audioFilename},
                                      {"version", e.meta.version},
                                      {"keyCount", e.meta.keyCount},
                                      {"officialSrBase", e.meta.officialSrBase},
                                      {"officialSrDt", e.meta.officialSrDt},
                                      {"officialSrHt", e.meta.officialSrHt},
                                      {"srrSr", e.meta.srrSr},
                                      {"bpm", e.meta.bpm},
                                      {"lengthMs", e.meta.lengthMs}}}};
        };
        auto songsEntryFromJson = [](const json& item, SongsFileEntry& e,
                                     std::string& pUtf8) {
            try {
                if (!item.is_object() || !item.at("p").is_string() ||
                    !item.at("size").is_number_unsigned() ||
                    !item.at("mtime").is_number_integer() ||
                    !item.at("md5").is_string() ||
                    !item.at("meta").is_object())
                    return false;
                const json& meta = item.at("meta");
                if (!meta.at("path").is_string() ||
                    !meta.at("title").is_string() ||
                    !meta.at("artist").is_string() ||
                    !meta.at("creator").is_string() ||
                    !meta.at("audioFilename").is_string() ||
                    !meta.at("version").is_string() ||
                    !meta.at("keyCount").is_string() ||
                    !meta.at("officialSrBase").is_number() ||
                    !meta.at("officialSrDt").is_number() ||
                    !meta.at("officialSrHt").is_number() ||
                    !meta.at("srrSr").is_number() ||
                    !meta.at("bpm").is_number() ||
                    !meta.at("lengthMs").is_number())
                    return false;
                pUtf8 = item.at("p").get<std::string>();
                if (pUtf8.empty()) return false;
                e.size = item.at("size").get<std::uintmax_t>();
                e.mtime = fs::file_time_type{
                    fs::file_time_type::duration{
                        item.at("mtime").get<int64_t>()}};
                e.md5 = item.at("md5").get<std::string>();
                e.meta.path = meta.at("path").get<std::string>();
                e.meta.title = meta.at("title").get<std::string>();
                e.meta.artist = meta.at("artist").get<std::string>();
                e.meta.creator = meta.at("creator").get<std::string>();
                e.meta.audioFilename =
                    meta.at("audioFilename").get<std::string>();
                e.meta.version = meta.at("version").get<std::string>();
                e.meta.keyCount = meta.at("keyCount").get<std::string>();
                e.meta.officialSrBase =
                    meta.at("officialSrBase").get<double>();
                e.meta.officialSrDt = meta.at("officialSrDt").get<double>();
                e.meta.officialSrHt = meta.at("officialSrHt").get<double>();
                e.meta.srrSr = meta.at("srrSr").get<double>();
                e.meta.bpm = meta.at("bpm").get<double>();
                e.meta.lengthMs = meta.at("lengthMs").get<int64_t>();
                return true;
            } catch (...) {
                return false;
            }
        };
        // Restart fast path: seed the per-file cache from the sidecar the
        // last full scan wrote and rebuild the lookup index exactly like
        // the scan tail below. Any version/folder/schema mismatch or
        // IO/parse failure silently falls through to the full scan without
        // touching the caches. Drift gate (same as the replay section
        // above): the seed may only adopt the saved directory stamp and
        // skip the scan when that stamp still equals the live directory
        // mtime (sstamp read above). On drift the sidecar entries merely
        // warm the per-file cache so the full scan below only stat-s
        // unchanged files and re-reads new/changed ones.
        bool seeded = false;
        if (dirty && !songsIndexBuilt_) {
            json env = readJsonSidecar(songsIndexCachePath());
            try {
                if (env.is_object() && env.value("version", 0) == 3 &&
                    env.value("folder", std::string{}) == songsFolder_ &&
                    env.at("dirStamp").is_number_integer() &&
                    env.at("entries").is_array()) {
                    const auto savedStamp = fs::file_time_type{
                        fs::file_time_type::duration{
                            env.at("dirStamp").get<int64_t>()}};
                    // stat failure -> no trustworthy live stamp -> rescan.
                    bool driftFree = !ec && savedStamp == sstamp;
                    std::map<fs::path, SongsFileEntry> seededCache;
                    bool ok = true;
                    for (const auto& item : env.at("entries")) {
                        SongsFileEntry e;
                        std::string pUtf8;
                        if (!songsEntryFromJson(item, e, pUtf8)) {
                            ok = false;
                            break;
                        }
                        seededCache[mra::pathFromUtf8(pUtf8)] = std::move(e);
                    }
                    if (ok) {
                        songsFileCache_ = std::move(seededCache);
                        if (driftFree) {
                            songsIndex_.clear();
                            for (const auto& kv : songsFileCache_)
                                if (isMd5Hex(kv.second.md5))
                                    songsIndex_[kv.second.md5] =
                                        kv.second.meta;
                            songsStamp_ = savedStamp;
                            songsStampValid_ = true;
                            songsIndexBuilt_ = true;
                            seeded = true;
                        }
                        // Drifted: keep the warmed per-file cache but leave
                        // Built/Valid/stamp untouched, so the full scan
                        // branch below runs and picks up the changes.
                    }
                }
            } catch (...) {
                seeded = false;
            }
        }
        if (dirty && !seeded) {
            std::map<fs::path, SongsFileEntry> fresh;
            for (fs::recursive_directory_iterator it(
                     dir, fs::directory_options::skip_permission_denied, ec),
                 end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code ec2;
                if (!it->is_regular_file(ec2)) continue;
                std::string ext = it->path().extension().string();
                for (char& c : ext)
                    c = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(c)));
                if (ext != ".osu") continue;
                auto sz = it->file_size(ec2);
                auto mt = it->last_write_time(ec2);
                if (ec2) continue;
                auto cit = songsFileCache_.find(it->path());
                if (cit != songsFileCache_.end() &&
                    cit->second.size == sz && cit->second.mtime == mt) {
                    fresh.emplace(it->path(), cit->second);
                    continue;
                }
                SongsFileEntry e;
                e.size = sz;
                e.mtime = mt;
                std::string text;
                std::string md5;
                // readTextFile yields an error message on failure.
                if (!readTextFile(mra::pathToUtf8(it->path()), text, &md5) &&
                    isMd5Hex(md5)) {
                    e.md5 = md5;
                    e.meta.path = mra::pathToUtf8(it->path());
                    e.meta.title = iniValue(text, "Title");
                    e.meta.artist = iniValue(text, "Artist");
                    e.meta.creator = iniValue(text, "Creator");
                    e.meta.audioFilename = iniValue(text, "AudioFilename");
                    e.meta.version = iniValue(text, "Version");
                    e.meta.keyCount = iniValue(text, "CircleSize");
                    // SR 索引 (sidecar v2): officialSR base/DT/HT + SRR 全局
                    // SR。只对新增/变更文件运行; 不可算存 -1。
                    computeSrMeta(text, e.meta);
                }
                fresh.emplace(it->path(), std::move(e));
            }
            if (!ec) {
                songsFileCache_ = std::move(fresh);
                songsIndex_.clear();
                for (const auto& kv : songsFileCache_)
                    if (isMd5Hex(kv.second.md5))
                        songsIndex_[kv.second.md5] = kv.second.meta;
                songsStamp_ = sstamp;
                songsStampValid_ = true;
                songsIndexBuilt_ = true;
                // Persist the freshly built cache so the next process start
                // seeds from disk instead of rescanning (best-effort,
                // silent; songsFolder_ is non-empty here).
                json entries = json::array();
                for (const auto& kv : songsFileCache_)
                    entries.push_back(songsEntryToJson(kv.first, kv.second));
                writeJsonSidecar(songsIndexCachePath(),
                                 json{{"version", 3},
                                      {"folder", songsFolder_},
                                      {"dirStamp",
                                       static_cast<int64_t>(
                                           sstamp.time_since_epoch()
                                               .count())},
                                      {"entries", std::move(entries)}});
            }
        }
    }
}

LoadOutcome loadReplaySession(const std::string& replayPath,
                              const std::string& beatmapPath,
                              const std::string& audioPath,
                              const std::string& skinFolder) {
    // 1. .osr
    std::vector<uint8_t> osrBytes;
    if (auto err = readFileBytes(replayPath, osrBytes))
        return LoadOutcome{false, "", "REPLAY_FILE_NOT_FOUND", *err, ""};
    OsrParseResult osr = parseOsr(osrBytes);
    if (!osr.error.empty())
        return LoadOutcome{false, "", "REPLAY_PARSE_FAILED", osr.error, ""};

    // 2. LZMA decode
    LzmaDecodeResult decoded = lzmaDecode(osr.header.compressedData.data(),
                                          osr.header.compressedData.size());
    if (!decoded.ok)
        return LoadOutcome{false, "", "REPLAY_DECODE_FAILED",
                           decoded.error.empty() ? "unknown LZMA error"
                                                 : decoded.error,
                           ""};

    // 3. frames
    ReplayFrameDecodeResult decodedFrames = decodeReplayFrames(decoded.output);
    if (!decodedFrames.ok)
        return LoadOutcome{false, "", "REPLAY_FRAMES_INVALID",
                           decodedFrames.error, ""};
    std::vector<ReplayFrame> frames = std::move(decodedFrames.frames);

    // 4. .osu
    std::string osuText;
    std::string osuMd5;
    if (auto err = readTextFile(beatmapPath, osuText, &osuMd5))
        return LoadOutcome{false, "", "BEATMAP_FILE_NOT_FOUND", *err, ""};
    OsuParseResult parsedBeatmap = parseOsu(osuText);
    if (!parsedBeatmap.ok)
        return LoadOutcome{false, "", "BEATMAP_PARSE_FAILED",
                           parsedBeatmap.error.empty()
                               ? "unable to parse beatmap"
                               : parsedBeatmap.error,
                           ""};
    OsuBeatmap beatmap = std::move(parsedBeatmap.beatmap);
    if (!beatmap.isMania())
        return LoadOutcome{false, "", "BEATMAP_NOT_MANIA",
                           "beatmap mode is not osu!mania", ""};

    // 5. match + analyze
    int keyCount = keyCountFromMods(osr.header.mods);
    if (keyCount <= 0) keyCount = beatmap.keyCount;
    MatchResult match;
    MatchResult matchV2;
    bool scoreV2Replay = false;
    bool stableCountMode = true;
    GlobalAnalysis analysis;
    // L3: lazer 转换 mod 的复现备注 (如 RD 缺 seed), 透出到 /summary lazer 块。
    std::vector<std::string> lazerModNotes;

    if (osr.header.isLazer) {
        // ---- L2: lazer 判定 profile 重建 (mania/lazer_matcher, 与 stable 引擎并列) ----
        // 帧已是谱面时基 (L1 定案): 窗 = lazer 基础窗(OD) × rate / HR·EZ 倍率,
        // floor+0.5, 闭区间; 尾窗 ×1.5、不松键 end+1.5×MeH窗 强制 Miss、断带封顶 MeH。
        // mods 一律取尾块 JSON acronym (legacy 位对 IN 有损); IN/RD 物件变换本期
        // 不支持 (残差在 lazer_e2e folder 3 SKIP 记录)。
        LazerMatchOptions lo;
        lo.od = beatmap.overallDifficulty;
        lo.keyCount = keyCount;
        // L3 mod 适配: 转换类物件变换 (IN/HO/RD) 按尾块 mods 列表顺序直接作用在
        // session.beatmap.objects 上 (内存中原地修改, 不落盘) —— 判定重建、
        // /objects 回放渲染与判定显示共用同一份变换后物件, 与 lazer 游戏内
        // (IApplicableAfterBeatmapConversion 改写转换后谱面) 一致; SR/SRR 等
        // 谱面级派生量也因此按转换后谱面计算。rate/window/判定语义类
        // (DC/DT/NC/HT/HR/EZ/MR/NR/DA/CL) 走 options。
        for (const LazerMod& m : osr.header.lazerExtra.mods) {
            if (m.acronym == "HT") {
                lo.rate = 0.75;
                auto it = m.settings.find("speed_change");
                if (it != m.settings.end()) {
                    try { lo.rate = std::stod(it->second); } catch (...) {}
                }
            } else if (m.acronym == "DC") {
                // ManiaModDaycore : ModDaycore, IManiaRateAdjustmentMod — 空壳类,
                // 与 HT 同一 rate 路径 (ModDaycore 默认 speed_change = 0.75)。
                lo.rate = 0.75;
                auto it = m.settings.find("speed_change");
                if (it != m.settings.end()) {
                    try { lo.rate = std::stod(it->second); } catch (...) {}
                }
            } else if (m.acronym == "DT" || m.acronym == "NC") {
                double sc = 1.5;
                auto it = m.settings.find("speed_change");
                if (it != m.settings.end()) {
                    try { sc = std::stod(it->second); } catch (...) {}
                }
                lo.rate = sc;
            } else if (m.acronym == "HR") {
                lo.hardRock = true;
            } else if (m.acronym == "EZ") {
                lo.easy = true;
            } else if (m.acronym == "MR") {
                lo.mirror = true;
            } else if (m.acronym == "NR") {
                // ManiaModNoRelease: 判定语义 (HoldNote → NoReleaseHoldNote),
                // 不是物件变换 — 尾时刻仍按住 ⇒ GetCappedResult(Perfect)。
                lo.noRelease = true;
            } else if (m.acronym == "DA") {
                // ManiaModDifficultyAdjust: 唯一影响判定的设置键是
                // overall_difficulty (绝对值; mania 扩展限 −15..15, 源码
                // ExtendedMinValue/ExtendedMaxValue)。drain_rate 只影响 HP,
                // 不进判定窗; extended_limits 只放宽 UI 限位。
                auto it = m.settings.find("overall_difficulty");
                if (it != m.settings.end()) {
                    try { lo.od = std::stod(it->second); } catch (...) {}
                }
            } else if (m.acronym == "CL") {
                // ManiaModClassic: ManiaHitWindows.ClassicModActive = true
                // (native 谱 IsConvert=false) ⇒ stable 式经典窗口。
                lo.classic = true;
            } else if (m.acronym == "SV2") {
                // ManiaModScoreV2: ApplyToBeatmap 给每个 Note / LN 头尾的
                // ManiaHitWindows 置 ScoreV2Active=true; 对窗口的唯一效果是压制 CL
                // 的 stable 分支 (ManiaHitWindows.cs L129 `ClassicModActive &&
                // !ScoreV2Active`) — SV2 单开不改窗口, 折算在 matchLazer 入口
                // (classic && !scoreV2)。 [docs/lazer-sv2-window-findings.md]
                lo.scoreV2 = true;
            } else if (m.acronym == "IN") {
                // ManiaModInvert: IApplicableAfterBeatmapConversion, 无 settings,
                // 确定性变换 (尾物件时刻 beatLength, 末物件不产 LN)。
                applyManiaInvert(beatmap.objects, beatmap.timingPoints);
            } else if (m.acronym == "HO") {
                // ManiaModHoldOff: LN → 头时刻 Note; 滚速补偿只影响视觉。
                applyManiaHoldOff(beatmap.objects);
            } else if (m.acronym == "RD") {
                // ManiaModRandom (ModRandom, IHasSeed): settings 唯一键 seed
                // (int?, null = 当时随机)。缺 seed 时列置换不可复现 — 不猜,
                // 标注后跳过变换 (物件保持原列; 判定仍可重建)。
                auto it = m.settings.find("seed");
                if (it != m.settings.end() && it->second != "null") {
                    try {
                        const int seed = std::stoi(it->second);
                        applyManiaRandom(beatmap.objects, keyCount, seed);
                    } catch (...) {
                        lazerModNotes.push_back(
                            "RD: seed 解析失败, 列置换未复现 (缺 seed 无法核对)");
                    }
                } else {
                    lazerModNotes.push_back(
                        "RD: 尾块无 seed, 列置换不可复现 (缺 seed 无法核对)");
                }
            }
        }
        LazerMatchResult lm = matchLazer(beatmap.objects, frames, lo);
        match = lm.match;
        matchV2 = lm.match;   // 下游 segment/holds/summary 全走 lazer 重建 (V2=305 权重)
        scoreV2Replay = true; // rulesScoreV2=true ⇒ analyzeSegment 端点吃 lazer 重建
        stableCountMode = false; // lazer 计数天然头尾分计 (pairwise), 无 stable 单位口径
        analysis = analyzeMatch(match, keyCount);
    } else {
        // ---- stable 引擎 (源码忠实, 红线区: 以下判定逻辑不随 lazer 改动) ----
        MatchOptions matchOptions;
        matchOptions.windows = ManiaWindows::fromOD(beatmap.overallDifficulty);
        // 源码忠实引擎的窗口由 ApplyDifficultySettings(OD, ActiveMods) 现算：
        // 传原始 OD（double，源里先 conv.r4 成 float）与原生 mania 谱标志。
        matchOptions.od = beatmap.overallDifficulty;
        matchOptions.nativeManiaBeatmap = beatmap.isMania();
        matchOptions.mirrorColumns = (osr.header.mods & kModMirror) != 0;
        // .osr mod 位原样进 matcher: MRA_RE_WIN 的窗口因子 (HR÷1.4 / EZ×1.4 再
        // DT/NC×1.5 / HT×0.75) 需要 HR/EZ/DT/HT 位才完整; rate 只承载 DT/HT。
        // 无 mod ⇒ 因子 1.0, 判定与改动前逐位一致。
        matchOptions.mods = osr.header.mods;
        // 判定窗 × 速率 (acc-semantics §0.2): DT/NC 1.5, HT 0.75 (NC 优先级与 DT
        // 同值). 只乘窗, 不动帧时基.
        if (osr.header.mods & (kModDoubleTime | kModNightcore))
            matchOptions.rate = 1.5;
        else if (osr.header.mods & kModHalfTime)
            matchOptions.rate = 0.75;
        // RD (Random): the .osr trailing RNG seed frame carries the seed the
        // client drew its column shuffle from; take the LAST seed frame. With no
        // seed frame the permutation is unrecoverable, so the mod silently stays
        // off (visible as unmatched objects in the response).
        if (osr.header.mods & kModRandom) {
            for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
                if (it->isSeedFrame) {
                    matchOptions.randomMod = true;
                    matchOptions.randomSeed = it->seed;
                    break;
                }
            }
        }
        // I-6 (MRA_RE_EVENKEY) 已在源码忠实重写后钉死为 OFF (不再作为可选补偿)。
        // 该帧位旋转是旧分析管线 ("bit c == 解析列 c") 的补偿; 源码忠实引擎
        // (mania/source_engine.cpp: applyReplayFrame 的 GetManiaKeyMask 逆向 +
        // remapManiaColumn 的物件侧同源位移) 已内建同一语义, 外部旋转 ρ 必须为 0 ——
        // 再叠加会把 6K/8K 的 Mirror/RD 局转到错列。
        // 回归网: test_replay_lanes engineCompositionIdentity / engineGateMatchesModule。
        // 旋转原语保留在 replay/replay_frames.cpp 仅供测试与考古。
        match =
            matchReplayToObjects(beatmap.objects, keyCount, frames, matchOptions);

        // Second pass under ScoreV2 rules for the renderer's V2 judgement view
        // (LN head late window ok*1.5, tail judged independently on |endDiff|/1.5).
        MatchOptions v2Options = matchOptions;
        v2Options.scoreV2 = true;
        matchV2 =
            matchReplayToObjects(beatmap.objects, keyCount, frames, v2Options);

        // Which counting model do this replay's .osr counters use? osu!stable
        // records ONE judgement per object (an LN is a single unit); lazer
        // records LN heads and tails separately. Pick whichever total matches
        // the declared one; stable wins ties and no-matches (install base).
        const int64_t declaredTotal =
            static_cast<int64_t>(osr.header.count300) +
            static_cast<int64_t>(osr.header.count100) +
            static_cast<int64_t>(osr.header.count50) +
            static_cast<int64_t>(osr.header.countGeki) +
            static_cast<int64_t>(osr.header.countKatu) +
            static_cast<int64_t>(osr.header.countMiss);

        // ScoreV2 局判据 (acc-semantics §2, 经验): 六计数和 == notes 数 + LN 数
        // (pairwise: V2 把 LN 头尾分开计票) → V2; mods 位不可靠 (段位课程强制
        // V2 常不带位), 仅在计数判据不定 (无 LN 或两头都不吻合) 时作后备。
        const int64_t noteCount = static_cast<int64_t>(beatmap.objects.size());
        int64_t lnCount = 0;
        for (const ManiaHitObject& o : beatmap.objects)
            if (o.type == ManiaObjectType::Hold) ++lnCount;
        const bool countsV2 =
            lnCount > 0 && declaredTotal == noteCount + lnCount;
        const bool countsV1 = lnCount > 0 && declaredTotal == noteCount;
        const bool modsScoreV2 = (osr.header.mods & kModScoreV2) != 0;
        scoreV2Replay = countsV2 || (!countsV1 && modsScoreV2);
        analysis =
            analyzeMatch(scoreV2Replay ? matchV2 : match, keyCount);

        stableCountMode =
            !scoreV2Replay &&
            (static_cast<int64_t>(analysis.stableCounts.judged()) == declaredTotal ||
             static_cast<int64_t>(analysis.counts.judged()) != declaredTotal);
    }

    ReplaySession session;
    session.replayPath = replayPath;
    session.beatmapPath = beatmapPath;
    session.beatmapMd5 = osuMd5;
    session.skinFolder = skinFolder.empty() ? std::nullopt
                                            : std::optional<std::string>(skinFolder);
    // lazer 数据通道 (Phase L1): 声明值层。尾块 JSON 的六档 statistics 为准
    // (IgnoreHit/ComboBreak 已由 lazerManiaCountsFromStatistics 过滤);
    // 无尾块时 declared 仍用头 6 个 ushort (summary 侧标注 countsSource)。
    session.isLazer = osr.header.isLazer;
    if (osr.header.lazerExtra.present) {
        session.lazerCounts =
            lazerManiaCountsFromStatistics(osr.header.lazerExtra.statistics);
        session.lazerCountsFromJson = session.lazerCounts.valid;
        session.lazerRank = osr.header.lazerExtra.rank;
    }
    session.replay = std::move(osr);
    session.frames = std::move(frames);
    session.keyCount = keyCount;
    session.beatmap = std::move(beatmap);
    session.match = std::move(match);
    session.matchV2 = std::move(matchV2);
    session.analysis = std::move(analysis);
    session.stableCountMode = stableCountMode;
    session.rulesScoreV2 = scoreV2Replay;
    session.lazerModNotes = std::move(lazerModNotes);
    if (!audioPath.empty()) {
        std::error_code ec;
        // audioPath is UTF-8: convert explicitly (a bare std::string would be
        // interpreted in the ANSI code page and break non-ASCII song folders).
        const fs::path audio = mra::pathFromUtf8(audioPath);
        if (fs::exists(audio, ec) && fs::is_regular_file(audio, ec))
            session.audioPath = audioPath;
    }
    session.ready = true;

    std::string id = SessionStore::instance().create(std::move(session));
    return LoadOutcome{true, id, "", "", ""};
}

// 构建扫描索引 (上次位置迟滞修复的后续: 解析时机延后到回放库打开):
// warmFromDisk=true 时先用持久化缓存预热 (dir 匹配才灬入, 未变更文件零
// 重解析); false 丢弃缓存全量重扫 (强制刷新)。lazerMutex_ 必须持有;
// 构建后持久化, 供下次启动预热。
void ApiServer::buildLazerIndexLocked(bool warmFromDisk) {
    if (!lazerIndex_ || lazerDir_.empty()) return;
    if (warmFromDisk) {
        std::error_code ec;
        const fs::path cachePath = lazerIndexCachePath();
        if (fs::exists(cachePath, ec)) {
            std::ifstream in(cachePath, std::ios::binary);
            if (in) {
                try {
                    json cache = json::parse(in);
                    if (cache.value("dir", "") == lazerDir_) {
                        std::vector<LazerScanWarmEntry> warmFiles;
                        for (const auto& je :
                             cache.value("entries", json::array())) {
                            LazerScanWarmEntry wf;
                            wf.relativePath = je.value("relativePath", "");
                            wf.size = je.value("size", 0ULL);
                            wf.mtimeMs = je.value("mtimeMs", 0LL);
                            LazerScanEntry& e = wf.entry;
                            e.relativePath = wf.relativePath;
                            e.size = wf.size;
                            e.mtimeMs = wf.mtimeMs;
                            e.osr.header.mode = je.value("mode", 0);
                            e.osr.header.count300 = je.value("count300", 0);
                            e.osr.header.count100 = je.value("count100", 0);
                            e.osr.header.count50 = je.value("count50", 0);
                            e.osr.header.countGeki = je.value("countGeki", 0);
                            e.osr.header.countKatu = je.value("countKatu", 0);
                            e.osr.header.countMiss = je.value("countMiss", 0);
                            e.osr.header.score = je.value("score", 0);
                            e.osr.header.maxCombo = je.value("maxCombo", 0);
                            e.osr.header.perfect = je.value("perfect", false);
                            e.osr.header.onlineId = je.value("onlineId", 0ULL);
                            e.osr.header.mods = je.value("mods", 0);
                            e.beatmapMd5 = je.value("beatmapMd5", "");
                            e.playerName = je.value("playerName", "");
                            e.playedMs = je.value("playedMs", -1LL);
                            e.extra.present = je.value("tailPresent", false);
                            e.extra.rank = je.value("rank", "");
                            if (je.contains("statistics") &&
                                je["statistics"].is_object())
                                for (auto sit = je["statistics"].begin();
                                     sit != je["statistics"].end(); ++sit)
                                    if (sit.value().is_number_integer())
                                        e.extra.statistics[sit.key()] =
                                            sit.value().get<int64_t>();
                            if (je.contains("extraMods") &&
                                je["extraMods"].is_array())
                                for (const auto& jm : je["extraMods"]) {
                                    LazerMod m;
                                    if (jm.contains("acronym") &&
                                        jm["acronym"].is_string())
                                        m.acronym =
                                            jm["acronym"].get<std::string>();
                                    if (jm.contains("settings") &&
                                        jm["settings"].is_object())
                                        for (auto sit = jm["settings"].begin();
                                             sit != jm["settings"].end(); ++sit)
                                            m.settings[sit.key()] =
                                                sit.value().dump();
                                    e.extra.mods.push_back(std::move(m));
                                }
                            if (e.extra.present)
                                e.counts = lazerManiaCountsFromStatistics(
                                    e.extra.statistics);
                            warmFiles.push_back(std::move(wf));
                        }
                        lazerIndex_->warm(
                            mra::pathFromUtf8(lazerDir_), warmFiles);
                    }
                } catch (...) {
                    // 缓存损坏/结构不符 → 忽略, 走全量重扫
                }
            }
        }
    } else {
        lazerIndex_->bind(mra::pathFromUtf8(lazerDir_));  // 丢弃缓存, 全量重扫
    }
    lazerIndex_->refresh();
    lazerIndexBuilt_ = true;
    persistLazerIndexLocked();
}

// 将当前索引持久化到 _data/lazer_index_cache.json (下次启动预热)。损坏无害。
void ApiServer::persistLazerIndexLocked() {
    const fs::path cachePath = lazerIndexCachePath();
    if (cachePath.empty()) return;
    json entries = json::array();
    for (const LazerScanEntry& e : lazerIndex_->entries()) {
        json extraMods = json::array();
        for (const LazerMod& m : e.extra.mods)
            extraMods.push_back(
                json{{"acronym", m.acronym}, {"settings", m.settings}});
        entries.push_back(json{
            {"relativePath", e.relativePath},
            {"size", e.size},
            {"mtimeMs", e.mtimeMs},
            {"beatmapMd5", e.beatmapMd5},
            {"playerName", e.playerName},
            {"playedMs", e.playedMs},
            {"mode", e.osr.header.mode},
            {"count300", e.osr.header.count300},
            {"count100", e.osr.header.count100},
            {"count50", e.osr.header.count50},
            {"countGeki", e.osr.header.countGeki},
            {"countKatu", e.osr.header.countKatu},
            {"countMiss", e.osr.header.countMiss},
            {"score", e.osr.header.score},
            {"maxCombo", e.osr.header.maxCombo},
            {"perfect", e.osr.header.perfect},
            {"onlineId", e.osr.header.onlineId},
            {"mods", e.osr.header.mods},
            {"tailPresent", e.extra.present},
            {"rank", e.extra.rank},
            {"statistics", e.extra.statistics},
            {"extraMods", std::move(extraMods)}});
    }
    std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
    if (out)
        out << okResponse(json{{"dir", lazerDir_},
                               {"entries", std::move(entries)}})
               .dump();
}

void ApiServer::registerRoutes(httplib::Server& svr) {
    // Restore the debug-log toggle the frontend persisted (checkbox).
    // 注意: Songs 绑定不在此处恢复 — 上次位置的记忆由前端「上次位置」按钮
    // 显式恢复 (lazerBind 携带 songsDir), 启动即自动绑定不符合产品语义。
    {
        json st = readJsonSidecar(lastStatePath());
        if (st.is_object() && st.value("debugLog", false))
            debuglog::setEnabled(true);
    }
    // ---- App ----
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(okResponse(json{{"status", "up"}}).dump(),
                        "application/json");
    });
    svr.Get("/api/app/info", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(okResponse(json{
            {"name", "osu!mania Replay Analyzer"},
            {"apiVersion", 1},
            {"platform", "windows"}}).dump(),
                        "application/json");
    });

    // ---- Replay load ----
    svr.Post("/api/replays/load",
             [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON", "request body is not valid JSON");
        }
        std::string replayPath = body.value("replayPath", "");
        std::string beatmapPath = body.value("beatmapPath", "");
        std::string audioPath = body.value("audioPath", "");
        std::string skinFolder = body.value("skinFolder", "");
        if (replayPath.empty() || beatmapPath.empty())
            return sendError(req, res, 400, "MISSING_PATH",
                             "replayPath and beatmapPath are required");
        const auto loadT0 = std::chrono::steady_clock::now();
        LoadOutcome out =
            loadReplaySession(replayPath, beatmapPath, audioPath, skinFolder);
        debuglog::log("load manual " + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loadT0).count()) +
            "ms ok=" + (out.ok ? "1" : "0"));
        if (!out.ok)
            return sendError(req, res, 422, out.errorCode, out.message);
        res.set_content(
            okResponse(json{{"sessionId", out.sessionId}}).dump(),
            "application/json");
    });

    // ---- Replay metadata ----
    svr.Get(R"(/api/replays/([^/]+)/metadata)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        const OsrHeader& h = s.replay.header;
        // 会话级 EffectiveSpeed = replay 帧 y (osr mania 约定 y = EffectiveSpeed,
        // stable 默认模式下 DT/HT 的 1.5/0.75 因子已烘焙其中; docs/REPLAY.md §3/§4 +
        // InputAndReplayPlayback IL)。取首个 y>0 帧 (整局常值)。lazer 4 字段帧的 y
        // 非 EffectiveSpeed, 不透出; 旧版 (<20151010) y 为 displayed 语义, 原样透传
        // (ReferenceBpm 不可复得, 不做反换算 — 已知差异)。
        float scrollSpeedEff = 0.f;
        if (!s.isLazer) {
            for (const ReplayFrame& f : s.frames) {
                if (f.isSeedFrame) continue;
                if (f.y > 0.f) {
                    scrollSpeedEff = f.y;
                    break;
                }
            }
        }
        res.set_content(okResponse(json{
            {"player", h.playerName},
            {"gameMode", h.mode},
            {"gameVersion", h.gameVersion},
            {"beatmapMd5", h.beatmapMd5},
            {"replayMd5", h.replayMd5},
            {"score", h.score},
            {"maxCombo", h.maxCombo},
            {"perfectCombo", h.perfect},
            {"mods", h.mods},
            {"timestamp", h.timestamp},
            {"counts", json{{"n300", h.count300}, {"n100", h.count100},
                            {"n50", h.count50}, {"gekki", h.countGeki},
                            {"katu", h.countKatu}, {"miss", h.countMiss}}},
            {"keyCount", s.keyCount},
            {"frameCount", s.frames.size()},
            {"replayPath", s.replayPath},
            {"scrollSpeedEff",
             scrollSpeedEff > 0.f ? json(static_cast<double>(scrollSpeedEff))
                                  : json(nullptr)}}).dump(),
                        "application/json");
    });

    // ---- Frames (paged by time range) ----
    svr.Get(R"(/api/replays/([^/]+)/frames)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        int64_t from = req.has_param("from") ? std::stoll(req.get_param_value("from")) : 0;
        int64_t to = req.has_param("to")
                         ? std::stoll(req.get_param_value("to"))
                         : INT64_MAX;
        size_t limit = req.has_param("limit")
                           ? static_cast<size_t>(std::stoll(req.get_param_value("limit")))
                           : 20000;
        json arr = json::array();
        size_t startIdx = 0, endIdx = s.frames.size();
        // frames are time-sorted: binary search the range
        auto itLo = std::lower_bound(
            s.frames.begin(), s.frames.end(), from,
            [](const ReplayFrame& f, int64_t t) { return f.timeMs < t; });
        if (itLo != s.frames.end()) startIdx = itLo - s.frames.begin();
        auto itHi = std::upper_bound(
            s.frames.begin(), s.frames.end(), to,
            [](int64_t t, const ReplayFrame& f) { return t < f.timeMs; });
        endIdx = itHi - s.frames.begin();
        for (size_t i = startIdx; i < endIdx && arr.size() < limit; ++i) {
            const ReplayFrame& f = s.frames[i];
            arr.push_back(json{{"timeMs", f.timeMs},
                               {"keys", f.rawKeys},
                               {"isSeed", f.isSeedFrame}});
        }
        res.set_content(okResponse(json{
            {"from", from},
            {"to", to == INT64_MAX ? json(nullptr) : json(to)},
            {"returned", arr.size()},
            {"totalInRange", endIdx - startIdx},
            {"frames", std::move(arr)}}).dump(),
                        "application/json");
    });

    // ---- Beatmap metadata ----
    svr.Get(R"(/api/beatmaps/([^/]+)/metadata)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        const OsuBeatmap& b = s.beatmap;
        // [TimingPoints] 透传 (osu_parser 已有解析): time/beatLength/uninherited。
        // 前端 stable 滚动模型按此逐段积分 pxPerMs = 21×Eff/BL_eff (SV/换段变速)。
        json timingPoints = json::array();
        for (const TimingPoint& tp : b.timingPoints) {
            timingPoints.push_back(json{{"timeMs", tp.timeMs},
                                        {"beatLength", tp.beatLength},
                                        {"uninherited", tp.uninherited}});
        }
        res.set_content(okResponse(json{
            {"title", b.title}, {"artist", b.artist}, {"creator", b.creator},
            {"version", b.version}, {"beatmapId", b.beatmapId},
            {"beatmapSetId", b.beatmapSetId}, {"mode", b.mode},
            {"keyCount", b.keyCount}, {"od", b.overallDifficulty},
            {"hp", b.hpDrain},
            {"audioFilename", b.audioFilename},
            {"backgroundFile", b.backgroundFile},
            {"objectCount", b.objects.size()},
            {"beatmapPath", s.beatmapPath},
            {"timingPoints", std::move(timingPoints)}}).dump(),
                        "application/json");
    });

    // ---- Beatmap objects (by time range) ----
    svr.Get(R"(/api/beatmaps/([^/]+)/objects)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        int64_t from = req.has_param("from") ? std::stoll(req.get_param_value("from")) : 0;
        int64_t to = req.has_param("to")
                         ? std::stoll(req.get_param_value("to"))
                         : INT64_MAX;
        json arr = json::array();
        for (const auto& o : s.beatmap.objects) {
            if (o.startTimeMs < from) continue;
            if (to != INT64_MAX && o.startTimeMs >= to) continue;
            json j = json{{"index", o.sourceIndex},
                          {"column", o.column},
                          {"startTimeMs", o.startTimeMs},
                          {"type", o.type == ManiaObjectType::Hold ? "hold" : "note"}};
            if (o.endTimeMs) j["endTimeMs"] = *o.endTimeMs;
            arr.push_back(std::move(j));
        }
        res.set_content(okResponse(json{{"from", from},
                                        {"to", to == INT64_MAX ? json(nullptr) : json(to)},
                                        {"objects", std::move(arr)}}).dump(),
                        "application/json");
    });

    // ---- Analysis summary ----
    svr.Get(R"(/api/analysis/([^/]+)/summary)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        const GlobalAnalysis& g = s.analysis;
        json hist = json::array();
        for (size_t i = 0; i < g.histogram.buckets.size(); ++i) {
            hist.push_back(json{
                {"fromMs", g.histogram.minMs + static_cast<int64_t>(i) * 5},
                {"toMs", g.histogram.minMs + static_cast<int64_t>(i + 1) * 5},
                {"count", g.histogram.buckets[i]}});
        }
        json breakdown = json::array();
        for (const auto& b : g.breakdown)
            breakdown.push_back(json{{"result", hitResultNameJson(b.result)},
                                     {"count", b.count},
                                     {"share", std::round(b.share * 100) / 100}});
        json consistency = json::array();
        // OSR declared counts vs reconstructed. stable records ONE judgement
        // per object (LN = single unit); lazer records heads+tails. The
        // session's count mode was picked at load time by matching totals.
        const OsrHeader& h = s.replay.header;
        const bool stableMode = s.stableCountMode;
        const JudgementCounts& shownCounts =
            stableMode ? g.stableCounts : g.counts;
        const int64_t declaredTotal =
            static_cast<int64_t>(h.count300) + static_cast<int64_t>(h.count100) +
            static_cast<int64_t>(h.count50) + static_cast<int64_t>(h.countGeki) +
            static_cast<int64_t>(h.countKatu) + static_cast<int64_t>(h.countMiss);
        bool countsMatch =
            declaredTotal == static_cast<int64_t>(shownCounts.judged());
        consistency.push_back(json{{"check", "beatmapMd5Match"},
                                   {"pass", s.beatmapMd5 == h.beatmapMd5}});
        consistency.push_back(json{{"check", "modeIsMania"},
                                   {"pass", s.beatmap.isMania()}});
        consistency.push_back(json{{"check", "judgementCountsMatch"},
                                   {"pass", countsMatch}});
        consistency.push_back(json{{"check", "replayEndedNormally"},
                                   {"pass", s.match.replayEndedNormally}});
        // Official pp (docs/pp-sr-notes §2): official SR 档位按会话 mods 的
        // rate 选 (与 loadReplaySession 的判定 rate 同源: DT/NC 1.5, HT
        // 0.75), counts 用上面 .osr 标定的匹配计数 (shownCounts, 即响应里
        // 的 "counts"), acc 走官方判定权重。SR 不可算 → pp = -1。
        const int64_t sessionMods = static_cast<int64_t>(s.replay.header.mods);
        // 特殊 mod 适配 (lazer): rate 取尾块 speed_change (小数倍率, 如
        // DT 1.25×), 与判定重建同源; stable 局维持 legacy 位映射。
        const double effectiveRate =
            s.isLazer ? lazerRateFromHeader(s.replay.header)
                      : rateForMods(sessionMods);
        double summarySr = kSrNotComputable;
#if MRA_HAS_OFFICIAL_SR
        if (s.beatmap.isMania() && s.beatmap.keyCount > 0 &&
            s.beatmap.keyCount <= 10 && s.beatmap.objects.size() >= 20) {
            const std::vector<SrNote> notes = srNotesFromBeatmap(s.beatmap);
            try {
                summarySr = saneSr(OfficialStarRating(
                    s.beatmap.keyCount, notes, effectiveRate));
            } catch (...) {
                summarySr = kSrNotComputable;
            }
        }
#endif
        const double summaryPp = officialPpFromCounts(
            summarySr, shownCounts.perfect, shownCounts.great,
            shownCounts.good, shownCounts.ok, shownCounts.meh,
            shownCounts.miss, sessionMods);
        // 当前 profile 实际分析的那个 match pass: 判定窗、整数化开关都取自它,
        // 便于前端/脚本核对 HR/EZ/DT/HT 是否真的进了 matcher。
        const MatchResult& rulesMatch = s.rulesScoreV2 ? s.matchV2 : s.match;
        // lazer 数据通道 (Phase L1): scoreSource + lazer 声明值块。
        // countsSource=json → 尾块 statistics 六档; ushort → 头 6 个计数
        // 回退 (geki→perfect / katu→good, 标注近似)。acc 恒 305 口径
        // (lazer 官方显示为截断 2 位小数, 截断格式化由前端负责)。
        json lazerInfo = json(nullptr);
        if (s.isLazer) {
            LazerManiaCounts eff;
            if (s.lazerCountsFromJson) {
                eff = s.lazerCounts;
            } else {
                eff.perfect = h.countGeki;
                eff.great = h.count300;
                eff.good = h.countKatu;
                eff.ok = h.count100;
                eff.meh = h.count50;
                eff.miss = h.countMiss;
                eff.valid = true;
            }
            lazerInfo = json{
                {"countsSource", s.lazerCountsFromJson ? "json" : "ushort"},
                {"acc305", round5(lazerAccuracy305Percent(eff))},
                {"rank", s.lazerRank},
                {"counts", json{{"perfect", eff.perfect},
                                {"great", eff.great},
                                {"good", eff.good},
                                {"ok", eff.ok},
                                {"meh", eff.meh},
                                 {"miss", eff.miss}}}};
            // L3: 转换 mod 复现备注 (RD 缺 seed 等); 空则不出字段 (加性, 兼容旧前端)。
            if (!s.lazerModNotes.empty())
                lazerInfo["modNotes"] = s.lazerModNotes;
        }
        // 尾块 mods 原列表 (acronym + settings 原始 JSON 值文本; 小数倍率/
        // IN/RD 等透传显示)。
        json lazerModsJson = json(nullptr);
        if (s.isLazer) {
            lazerModsJson = json::array();
            for (const LazerMod& m : s.replay.header.lazerExtra.mods) {
                json st = json::object();
                for (const auto& kv : m.settings) {
                    json parsed = json(kv.second);  // raw JSON text → string
                    try {
                        parsed = json::parse(kv.second);
                    } catch (...) {
                    }
                    st[kv.first] = std::move(parsed);
                }
                lazerModsJson.push_back(
                    json{{"acronym", m.acronym}, {"settings", std::move(st)}});
            }
        }
        res.set_content(okResponse(json{
            {"scoreSource", s.isLazer ? "lazer" : "stable"},
            {"lazer", lazerInfo},
            {"lazerMods", std::move(lazerModsJson)},
            {"countMode", stableMode ? "scorev1" : "scorev2"},
            {"rulesProfile", s.rulesScoreV2 ? "v2" : "v1"},
            {"counts", countsJson(shownCounts)},
            {"headCounts", countsJson(g.headCounts)},
            // 双计数口径 (向后兼容的新增字段): countsStable = 每物件一次
            // (LN 单计, osu!stable .osr 口径); countsPairwise = LN 头尾各计
            // (lazer/ScoreV2 口径)。上面的 counts 是二者中「本局实际口径」。
            {"countsStable", countsJson(g.stableCounts)},
            {"countsPairwise", countsJson(g.counts)},
            {"accuracy", json{{"scoreV1", round5(stableMode ? g.stableAccuracyV1
                                                            : g.accuracyV1)},
                              {"scoreV2", round5(stableMode ? g.stableAccuracyV2
                                                            : g.accuracyV2)}}},
            // 双口径 × 双权重: 前端可并列 V1(legacy 300) / V2(305) 两个 acc,
            // 并标出当前 profile 用的是哪一组。
            {"accuracyDetail", json{
                {"stable", json{{"scoreV1", round5(g.stableAccuracyV1)},
                                {"scoreV2", round5(g.stableAccuracyV2)}}},
                {"pairwise", json{{"scoreV1", round5(g.accuracyV1)},
                                  {"scoreV2", round5(g.accuracyV2)}}}}},
            // mods → 窗口链路的结果: rate 只承载 DT/HT, windows 是 MRA_RE_WIN
            // 整数化 + HR/EZ/DT/HT 因子之后的最终窗口 (判定实际使用的值)。
            {"mods", sessionMods},
            {"rate", effectiveRate},
            {"windows", json{
                {"integral", rulesMatch.integralWindows},
                {"v1", windowsJson(rulesMatch.windowsV1)},
                {"v2", windowsJson(rulesMatch.windowsV2)}}},
            {"offsets", offsetStatsJson(g.offsets)},
            {"tailOffsets", offsetStatsJson(g.tailOffsets)},
            {"histogram", hist},
            {"histogramBucketMs", 5.0},
            {"perColumn", columnStatsJson(g.perColumn)},
            {"breakdown", breakdown},
            {"consistency", consistency},
            {"consistencyReport", json{
                {"rolling20Sd", offsetStatsJson(g.consistency.rolling20Sd)},
                {"chordOffsetSpread", g.consistency.chordOffsetSpread},
                {"chordCount", g.consistency.chordCount},
                {"tapStability", std::round(g.consistency.tapStability * 1000) / 1000}}},
            {"unmatchedInputCount", g.unmatchedInputCount},
            {"unmatchedObjects", s.match.unmatchedObjects},
            {"firstEventTimeMs", g.firstEventTimeMs},
            {"lastEventTimeMs", g.lastEventTimeMs},
            {"pp", summaryPp},
            {"osrDeclared", json{
                {"counts", json{{"n300", h.count300}, {"n100", h.count100},
                                {"n50", h.count50}, {"gekki", h.countGeki},
                                {"katu", h.countKatu}, {"miss", h.countMiss}}},
                {"maxCombo", h.maxCombo},
                {"score", h.score}}}}).dump(),
                        "application/json");
    });

    // ---- Hits in range ----
    svr.Get(R"(/api/analysis/([^/]+)/hits)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        int64_t from = req.has_param("from") ? std::stoll(req.get_param_value("from")) : INT64_MIN;
        int64_t to = req.has_param("to") ? std::stoll(req.get_param_value("to")) : INT64_MAX;
        size_t limit = req.has_param("limit")
                           ? static_cast<size_t>(std::stoll(req.get_param_value("limit")))
                           : 5000;
        json arr = json::array();
        // version=v2 selects the ScoreV2 match pass (default v1/stable).
        const bool v2 = req.has_param("version") &&
                        req.get_param_value("version") == "v2";
        const std::vector<HitEvent>& events =
            v2 ? s.matchV2.events : s.match.events;
        // events are time-sorted by objectTimeMs: binary search lower bound
        auto lo = std::lower_bound(
            events.begin(), events.end(), from,
            [](const HitEvent& e, int64_t t) { return e.objectTimeMs < t; });
        for (auto it = lo; it != events.end() && arr.size() < limit; ++it) {
            if (it->objectTimeMs >= to) break;
            // objectIndex is a vector position; expose the source-stable id
            // (same value as /objects "index") so the frontend can join.
            const size_t oi = static_cast<size_t>(it->objectIndex);
            json j = json{{"objectIndex", oi < s.beatmap.objects.size()
                                         ? s.beatmap.objects[oi].sourceIndex
                                         : it->objectIndex},
                          {"column", it->column},
                          {"objectTimeMs", it->objectTimeMs},
                          {"judgement", hitResultNameJson(it->judgement)},
                          {"phase", hitPhaseNameJson(it->phase)}};
            if (it->v1Judgement)
                j["v1Judgement"] = hitResultNameJson(*it->v1Judgement);
            if (it->actualTimeMs) j["actualTimeMs"] = *it->actualTimeMs;
            if (it->offsetMs)
                j["offsetMs"] = std::round(*it->offsetMs * 100) / 100;
            arr.push_back(std::move(j));
        }
        res.set_content(okResponse(json{{"hits", std::move(arr)}}).dump(),
                        "application/json");
    });

    // ---- Optional session audio (music synced to the preview) ----
    svr.Get(R"(/api/audio/([^/]+))",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        if (!s.audioPath || s.audioPath->empty())
            return sendError(req, res, 404, "NO_AUDIO", "session has no audio");
        std::string lower = *s.audioPath;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const char* mime = "application/octet-stream";
        if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".flac") == 0)
            mime = "audio/flac";
        else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".mp3") == 0)
            mime = "audio/mpeg";
        else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".ogg") == 0)
            mime = "audio/ogg";
        else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".wav") == 0)
            mime = "audio/wav";
        else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".m4a") == 0)
            mime = "audio/mp4";
        // Media elements (<audio>/<video>) need HTTP Range support to seek
        // reliably and to know the duration. cpp-httplib slices res.body into
        // a 206 + Content-Range response itself when the client sends Range,
        // so just declare support and serve the full bytes.
        {
            std::error_code fsEc;
            const std::filesystem::path file = mra::pathFromUtf8(*s.audioPath);
            (void)std::filesystem::file_size(file, fsEc);
            if (fsEc)
                return sendError(req, res, 422, "AUDIO_READ_FAILED", fsEc.message());
            res.set_header("Accept-Ranges", "bytes");
        }
        std::vector<uint8_t> bytes;
        if (auto err = readFileBytes(*s.audioPath, bytes))
            return sendError(req, res, 422, "AUDIO_READ_FAILED", *err);
        res.set_content(std::string(bytes.begin(), bytes.end()), mime);
    });

    // ---- Hold notes (head/tail judgements) ----
    svr.Get(R"(/api/analysis/([^/]+)/holds)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        json arr = json::array();
        const MatchResult& rulesMatch = s.rulesScoreV2 ? s.matchV2 : s.match;
        for (const auto& h : rulesMatch.holds) {
            json j;
            const size_t hoi = static_cast<size_t>(h.head.objectIndex);
            j["objectIndex"] = hoi < s.beatmap.objects.size()
                                    ? s.beatmap.objects[hoi].sourceIndex
                                    : h.head.objectIndex;
            j["column"] = h.head.column;
            j["headTimeMs"] = h.head.objectTimeMs;
            j["tailTimeMs"] = h.tail.objectTimeMs;
            j["headJudgement"] = hitResultNameJson(h.head.judgement);
            j["tailJudgement"] = hitResultNameJson(h.tail.judgement);
            j["releasedEarly"] = h.releasedEarly;
            j["heldThroughTail"] = h.heldThroughTail;
            j["headOffsetMs"] = h.head.offsetMs
                                    ? json(std::round(*h.head.offsetMs * 100) / 100)
                                    : json(nullptr);
            j["tailOffsetMs"] = h.tail.offsetMs
                                    ? json(std::round(*h.tail.offsetMs * 100) / 100)
                                    : json(nullptr);
            arr.push_back(std::move(j));
        }
        res.set_content(okResponse(json{{"holds", std::move(arr)}}).dump(),
                        "application/json");
    });

    // ---- Segment analysis ----
    svr.Post(R"(/api/analysis/([^/]+)/segment)",
             [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON", "request body is not valid JSON");
        }
        if (!body.contains("startMs") || !body.contains("endMs"))
            return sendError(req, res, 400, "MISSING_RANGE",
                             "startMs and endMs are required");
        int64_t startMs = body["startMs"].get<int64_t>();
        int64_t endMs = body["endMs"].get<int64_t>();
        if (endMs <= startMs)
            return sendError(req, res, 400, "INVALID_RANGE", "endMs must be > startMs");
        const MatchResult& rulesMatch = s.rulesScoreV2 ? s.matchV2 : s.match;
        SegmentAnalysis seg =
            analyzeSegment(rulesMatch, s.keyCount, startMs, endMs);
        const bool stableMode = s.stableCountMode;
        const JudgementCounts& shownCounts =
            stableMode ? seg.stableCounts : seg.counts;
        const double accV1 = stableMode ? seg.stableAccuracyV1 : seg.accuracyV1;
        const double accV2 = stableMode ? seg.stableAccuracyV2 : seg.accuracyV2;
        // 局部 SRR SR: beatmap 物件 (column, headMs, tailMs<0 rice) 调
        // SrrStarRatingLocal(startMs, endMs); 不可算 -1。
        double segSrrSr = kSrNotComputable;
#if MRA_HAS_SRR_SR
        try {
            segSrrSr = saneSr(SrrStarRatingLocal(
                s.beatmap.keyCount, s.beatmap.overallDifficulty,
                srNotesFromBeatmap(s.beatmap), static_cast<int>(startMs),
                static_cast<int>(endMs)));
        } catch (...) {
            segSrrSr = kSrNotComputable;
        }
#endif
        // 局部 stable SR: 与局部 SRR 同一截取口径 (首击落在
        // [start-500, end+500]), rate 跟随会话 mods。
        double segOfficialSr = kSrNotComputable;
#if MRA_HAS_OFFICIAL_SR
        try {
            const std::vector<SrNote> allNotes =
                srNotesFromBeatmap(s.beatmap);
            const int lo = static_cast<int>(startMs) - 500;
            const int hi = static_cast<int>(endMs) + 500;
            std::vector<SrNote> clipped;
            clipped.reserve(allNotes.size());
            for (const SrNote& n : allNotes)
                if (n.headMs >= lo && n.headMs <= hi)
                    clipped.push_back(n);
            if (!clipped.empty())
                segOfficialSr = saneSr(OfficialStarRating(
                    s.beatmap.keyCount, clipped,
                    rateForMods(static_cast<int64_t>(s.replay.header.mods))));
        } catch (...) {
            segOfficialSr = kSrNotComputable;
        }
#endif
        res.set_content(okResponse(json{
            {"startMs", seg.startTimeMs},
            {"endMs", seg.endTimeMs},
            {"countMode", stableMode ? "scorev1" : "scorev2"},
            {"counts", countsJson(shownCounts)},
            {"headCounts", countsJson(seg.headCounts)},
            {"accuracyScoreV1", round5(accV1)},
            {"accuracyScoreV2", round5(accV2)},
            // 双计数口径 × 双权重 (与 /summary 同构, 供前端并列显示)
            {"countsStable", countsJson(seg.stableCounts)},
            {"countsPairwise", countsJson(seg.counts)},
            {"accuracyDetail", json{
                {"stable", json{{"scoreV1", round5(seg.stableAccuracyV1)},
                                {"scoreV2", round5(seg.stableAccuracyV2)}}},
                {"pairwise", json{{"scoreV1", round5(seg.accuracyV1)},
                                  {"scoreV2", round5(seg.accuracyV2)}}}}},
            {"offsets", offsetStatsJson(seg.offsets)},
            {"srrSr", segSrrSr},
            {"officialSr", segOfficialSr},
            {"perColumn", columnStatsJson(seg.perColumn)}}).dump(),
                        "application/json");
    });

    // ---- Hands analysis (left/right attribution) ----
    // body: { "centerToLeft": bool, "startMs"?: int64, "endMs"?: int64 }
    // startMs/endMs given together = range filter (object-time axis, same
    // as /segment); both absent = global.
    svr.Post(R"(/api/analysis/([^/]+)/hands)",
             [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        if (s.keyCount <= 0)
            return sendError(req, res, 422, "NO_MATCH",
                             "session has no analysed match");
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON", "request body is not valid JSON");
        }
        const bool centerToLeft = body.value("centerToLeft", false);
        const bool hasStart = body.contains("startMs");
        const bool hasEnd = body.contains("endMs");
        if (hasStart != hasEnd)
            return sendError(req, res, 400, "INVALID_RANGE",
                             "startMs and endMs must be given together");
        int64_t startMs = 0;
        int64_t endMs = 0;
        if (hasStart) {
            startMs = body["startMs"].get<int64_t>();
            endMs = body["endMs"].get<int64_t>();
            if (endMs <= startMs)
                return sendError(req, res, 400, "INVALID_RANGE", "endMs must be > startMs");
        }
        const MatchResult& rulesMatch = s.rulesScoreV2 ? s.matchV2 : s.match;
        const bool v2Profile = s.rulesScoreV2;
        const HandStats left = analyzeHand(rulesMatch, s.keyCount, true,
                                           centerToLeft, hasStart, startMs,
                                           endMs, v2Profile);
        const HandStats right = analyzeHand(rulesMatch, s.keyCount, false,
                                            centerToLeft, hasStart, startMs,
                                            endMs, v2Profile);
        res.set_content(okResponse(json{
            {"keyCount", s.keyCount},
            {"odd", s.keyCount % 2 != 0},
            {"centerToLeft", centerToLeft},
            {"profile", v2Profile ? "v2" : "v1"},
            {"left", handStatsJson(left)},
            {"right", handStatsJson(right)}}).dump(),
                        "application/json");
    });

    // ---- Skin ----
    svr.Post("/api/skins/load",
             [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON", "request body is not valid JSON");
        }
        std::string folder = body.value("folder", "");
        if (folder.empty())
            return sendError(req, res, 400, "MISSING_PATH", "folder is required");
        SkinLoader loader = SkinLoader::loadFromFolder(folder);
        json resources = json::array();
        for (auto& [name, img] : loader.definition().images)
            resources.push_back(name);
        res.set_content(okResponse(json{{"name", loader.definition().name},
                                        {"folder", loader.definition().folder},
                                        {"warnings", loader.definition().warnings},
                                        {"resources", resources}}).dump(),
                        "application/json");
    });

    // Skin hot-swap: repoint the session at a new skin folder; the frontend
    // then re-fetches skin.ini + resources and re-renders (no re-analysis).
    svr.Post("/api/skins/select",
             [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        std::string sessionId = body.value("sessionId", "");
        std::string folder = body.value("folder", "");
        if (sessionId.empty())
            return sendError(req, res, 400, "MISSING_PATH",
                             "sessionId is required");
        // folder 空串 = 卸载自定义皮肤, 切回默认显示 (前端「默认皮肤」按钮);
        // SessionStore::setSkinFolder 已把空串映射为 nullopt。
        if (!folder.empty()) {
            std::error_code ec;
            fs::path p = mra::pathFromUtf8(folder);
            if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
                return sendError(req, res, 400, "FOLDER_NOT_FOUND",
                                 "not an existing directory", folder);
        }
        if (!SessionStore::instance().setSkinFolder(sessionId, folder))
            return sendError(req, res, 404, "SESSION_NOT_FOUND",
                             "unknown session id");
        mergeLastState({{"skin", folder}});
        res.set_content(okResponse(json{{"folder", folder}}).dump(),
                        "application/json");
    });

    svr.Get(R"(/api/skins/([^/]+)/resource/([a-zA-Z0-9_@/.-]+))",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        std::string element = req.matches[2];
        // Lighting 家族不回落默认皮肤: 皮肤缺 lighting 即完全无打击光
        // (用户对照游戏内实测: 部分皮肤禁灯, 2026-10-27)。
        std::string elemLower = element;
        for (char& c : elemLower) c = static_cast<char>(std::tolower((unsigned char)c));
        const bool isLighting = elemLower.rfind("lighting", 0) == 0;
        // stable 元素查找店序 (S7 Obf003683, store 位 4→2→1): 谱面目录 → 自定义皮肤 →
        // 内嵌默认皮肤 (ppy/osu-resources, MIT; 打 X-MRA-Skin-Source: default 头,
        // 前端对默认 @2x 弱化处理, 自定义 1x 优先)。元素名可带子目录 ("/")。
        auto serveFile = [&res](const fs::path& p) -> bool {
            std::error_code fec;
            if (!fs::is_regular_file(p, fec)) return false;
            std::vector<uint8_t> bytes;
            if (auto err = readFileBytes(mra::pathToUtf8(p), bytes)) return false;
            std::string lower = mra::pathToUtf8(p);
            for (char& c : lower) c = static_cast<char>(std::tolower((unsigned char)c));
            const char* mime = lower.ends_with(".png") ? "image/png" : "image/jpeg";
            res.set_content(reinterpret_cast<const char*>(bytes.data()),
                            bytes.size(), mime);
            return true;
        };
        // 名字含扩展名 → 只试原名 (R3.2: name 里已含 '.' 则不再补扩展名);
        // 否则按 .png → .jpg → .jpeg 逐个尝试。
        auto tryFolder = [&serveFile, &element](const fs::path& dir) -> bool {
            std::error_code ec;
            // 拒绝路径穿越 (与 /file/ 端点同一道闸; element 来自 URL, 可能含 "../")
            if (element.empty() || element.find("..") != std::string::npos)
                return false;
            if (dir.empty() || !fs::is_directory(dir, ec)) return false;
            std::string lower = element;
            for (char& c : lower) c = static_cast<char>(std::tolower((unsigned char)c));
            const bool hasExt = lower.size() > 4 &&
                (lower.ends_with(".png") || lower.ends_with(".jpg") ||
                 lower.ends_with(".jpeg"));
            if (hasExt) return serveFile(dir / mra::pathFromUtf8(element));
            const char* exts[] = {".png", ".jpg", ".jpeg"};
            for (const char* ext : exts)
                if (serveFile(dir / mra::pathFromUtf8(element + ext))) return true;
            return false;
        };
        // (1) 谱面目录 (store 4): stable 中谱面目录优先于皮肤 (mania store=7)
        if (!s.beatmapPath.empty()) {
            const fs::path beatmapDir = mra::pathFromUtf8(s.beatmapPath).parent_path();
            if (tryFolder(beatmapDir)) return;
        }
        // (2) 自定义皮肤 (store 2)
        if (s.skinFolder) {
            SkinLoader loader = SkinLoader::loadFromFolder(*s.skinFolder);
            auto img = loader.findElement(element);
            if (img) {
                std::string lower = img->fileName;
                for (char& c : lower) c = static_cast<char>(std::tolower((unsigned char)c));
                const char* mime = lower.ends_with(".png") ? "image/png" : "image/jpeg";
                res.set_content(reinterpret_cast<const char*>(img->bytes.data()),
                                img->bytes.size(), mime);
                return;
            }
        }
        // (3) 内嵌默认皮肤 (store 1); lighting 家族跳过 (皮肤缺 lighting = 无灯)
        if (!isLighting) {
            EmbeddedAsset asset;
            if (mra::loadEmbeddedDefaultSkin(element, asset)) {
                res.set_header("X-MRA-Skin-Source", "default");
                res.set_content(reinterpret_cast<const char*>(asset.bytes.data()),
                                asset.bytes.size(), asset.mime);
                return;
            }
        }
        // (4) 404: 资源未命中属前端候选链的正常探查, 日志降级为 debug 级
        // (不再以 "api error" 级刷屏; debug 开启时以 "resource miss" 记录)。
        debuglog::log("resource miss " + req.method + " " + req.path);
        res.status = 404;
        res.set_content(errorResponse("RESOURCE_NOT_FOUND", "no such element").dump(),
                        "application/json");
    });

    // Raw skin file (skin.ini and other whitelisted names), served as text
    // so the frontend can parse [Mania] layout/colour settings locally.
    svr.Get(R"(/api/skins/([^/]+)/file/([a-zA-Z0-9_.-]+))",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(req, res, 404, "SESSION_NOT_FOUND", "unknown session id");
        if (!s.skinFolder)
            return sendError(req, res, 404, "SKIN_NOT_LOADED", "no skin loaded");
        std::string name = req.matches[2];
        if (name.empty() || name.find("..") != std::string::npos)
            return sendError(req, res, 400, "INVALID_NAME", "bad file name");
        fs::path p = mra::pathFromUtf8(*s.skinFolder) / mra::pathFromUtf8(name);
        std::vector<uint8_t> bytes;
        if (auto err = readFileBytes(p, bytes))
            return sendError(req, res, 404, "SKIN_FILE_NOT_FOUND", *err);
        res.set_content(reinterpret_cast<const char*>(bytes.data()),
                        bytes.size(), "text/plain; charset=utf-8");
    });

    // ---- Local scores.db (osu! stable score browser). App-level store:
    // loaded independently of replay sessions, lives on the ApiServer. ----
    svr.Post("/api/scores/load",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        std::string path = body.value("path", "");
        if (path.empty())
            return sendError(req, res, 400, "MISSING_PATH", "path is required");
        ScoresDb db;
        std::string err;
        if (!parseScoresDb(path, db, err))
            return sendError(req, res, 400, "PARSE_ERROR",
                             "failed to parse scores.db", err);
        int64_t dbVersion = db.version;
        int64_t groupCount = static_cast<int64_t>(db.groups.size());
        int64_t total = 0;
        std::vector<std::pair<int, int64_t>> byMode;
        for (const auto& g : db.groups)
            for (const auto& s : g.scores) {
                ++total;
                bool found = false;
                for (auto& kv : byMode)
                    if (kv.first == s.mode) { ++kv.second; found = true; break; }
                if (!found) byMode.emplace_back(s.mode, 1);
            }
        json modes = json::array();
        for (const auto& kv : byMode)
            modes.push_back(json{{"mode", kv.first}, {"count", kv.second}});
        // Auto-derive Data\\r next to scores.db (osu! stable root layout).
        std::string dataRInfo;
        bool dataRAuto = false;
        std::string songsInfo;
        bool songsAuto = false;
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            scores_ = std::make_unique<ScoresDb>(std::move(db));
            scoresLoaded_ = true;
            scoresDbPath_ = path;
            std::error_code sec;
            fs::path dbp = mra::pathFromUtf8(path);
            scoresDbStamp_ = fs::last_write_time(dbp, sec);
            scoresDbSize_ = fs::file_size(dbp, sec);
            replayIndexBuilt_ = false;
            songsIndexBuilt_ = false;
            // A newly loaded scores.db invalidates the browse response
            // cache; the next browse re-keys it by the new file's MD5.
            // The disk (L2) copy is left in place and fails its own MD5
            // check for the new file, so it needs no clearing here.
            browseCacheMd5_.clear();
            browseCacheResponse_.clear();
            // Data\r may change together with scores.db; keep the file cache
            // so the next refresh only stat-s instead of re-reading headers.
            dataRStampValid_ = false;
            songsStampValid_ = false;
            fs::path derived = mra::pathFromUtf8(path).parent_path() /
                               "Data" / "r";
            std::error_code ec;
            if (fs::exists(derived, ec) && fs::is_directory(derived, ec)) {
                dataRFolder_ = mra::pathToUtf8(derived);
                dataRAuto_ = true;
                dataRAuto = true;
            } else {
                dataRFolder_.clear();
                dataRAuto_ = false;
                dataRInfo = "no Data\\r next to scores.db - pick it manually";
            }
            // Auto-derive Songs next to scores.db the same way (osu! stable
            // root layout: <root>/Songs holds the beatmap folders). Like
            // Data\r above, a failed derive clears a previously bound
            // folder, and a manual pick wins only until the next reload.
            fs::path derivedSongs =
                mra::pathFromUtf8(path).parent_path() / "Songs";
            if (fs::exists(derivedSongs, ec) &&
                fs::is_directory(derivedSongs, ec)) {
                songsFolder_ = mra::pathToUtf8(derivedSongs);
                songsAuto_ = true;
                songsAuto = true;
            } else {
                songsFolder_.clear();
                songsAuto_ = false;
                songsInfo = "no Songs next to scores.db - pick it manually";
            }
        }
        mergeLastState({{"scores", path}});
        if (dataRAuto)
            mergeLastState({{"dataR",
                             [&] {
                                 std::lock_guard<std::mutex> l(scoresMutex_);
                                 return dataRFolder_;
                             }()}});
        if (songsAuto)
            mergeLastState({{"songs",
                             [&] {
                                 std::lock_guard<std::mutex> l(scoresMutex_);
                                 return songsFolder_;
                             }()}});
        res.set_content(okResponse(json{{"version", dbVersion},
                                        {"groups", groupCount},
                                        {"total", total},
                                        {"byMode", std::move(modes)},
                                        {"dataRAuto", dataRAuto},
                                        {"dataRFolder",
                                         [&] {
                                             std::lock_guard<std::mutex> l(
                                                 scoresMutex_);
                                             return dataRFolder_;
                                         }()},
                                        {"dataRHint", dataRInfo},
                                        {"songsAuto", songsAuto},
                                        {"songsFolder",
                                         [&] {
                                             std::lock_guard<std::mutex> l(
                                                 scoresMutex_);
                                             return songsFolder_;
                                         }()},
                                        {"songsHint", songsInfo}})
                            .dump(),
                        "application/json");
    });

    // Manual Data\\r override (when auto-derivation fails or for custom
    // layouts). Disables auto-derivation until scores.db is reloaded.
    svr.Post("/api/scores/datar",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        std::string folder = body.value("folder", "");
        if (folder.empty())
            return sendError(req, res, 400, "MISSING_PATH", "folder is required");
        std::error_code ec;
        fs::path p = mra::pathFromUtf8(folder);
        if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
            return sendError(req, res, 400, "FOLDER_NOT_FOUND",
                             "not an existing directory", folder);
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            if (!scoresLoaded_)
                return sendError(req, res, 404, "SCORES_NOT_LOADED",
                                 "load scores.db first");
            dataRFolder_ = folder;
            dataRAuto_ = false;
            replayIndexBuilt_ = false;
            dataRStampValid_ = false;
        }
        mergeLastState({{"dataR", folder}});
        res.set_content(okResponse(json{{"dataRFolder", folder}}).dump(),
                        "application/json");
    });

    // Manual Songs registration (auto-derived from the scores.db root when
    // present; a manual pick disables the auto flag until scores.db is
    // reloaded). Must be set before browsing/loading.
    svr.Post("/api/scores/songs",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        std::string folder = body.value("folder", "");
        if (folder.empty())
            return sendError(req, res, 400, "MISSING_PATH", "folder is required");
        std::error_code ec;
        fs::path p = mra::pathFromUtf8(folder);
        if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
            return sendError(req, res, 400, "FOLDER_NOT_FOUND",
                             "not an existing directory", folder);
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            songsFolder_ = folder;
            songsAuto_ = false;
            songsIndexBuilt_ = false;
            songsStampValid_ = false;
        }
        mergeLastState({{"songs", folder}});
        res.set_content(okResponse(json{{"songsFolder", folder}}).dump(),
                        "application/json");
    });

    // Enriched score browser rows: metadata from the Songs index plus
    // hasReplay from the Data\\r replay index. Builds missing indexes on
    // first call (may take tens of seconds on large libraries).
    svr.Post("/api/scores/browse",
             [this](const httplib::Request& req, httplib::Response& res) {
        int modeFilter = -1;
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            if (!scoresLoaded_ || !scores_)
                return sendError(req, res, 404, "SCORES_NOT_LOADED",
                                 "no scores.db loaded");
            if (dataRFolder_.empty())
                return sendError(req, res, 422, "DATAR_NOT_SET",
                                 "Data\\r folder is not selected");
            if (songsFolder_.empty())
                return sendError(req, res, 422, "SONGS_NOT_SET",
                                 "Songs folder is not selected");
        }
        json body = json::object();
        try {
            body = json::parse(req.body);
        } catch (...) {}
        // force: the user explicitly asked to rescan Data\r / Songs right
        // now (pick up new .osr files / new beatmaps). Bottom-up escape
        // hatch for library drift the scores.db-md5 response-cache key
        // cannot see: bypass both cache lookups below, then refresh +
        // rebuild + overwrite the caches exactly like a miss.
        bool force = false;
        if (!body.is_null()) {
            try { modeFilter = body.value("mode", -1); } catch (...) {}
            try { force = body.value("force", false); } catch (...) {}
        }
        // Response-level cache: the scores.db content MD5 plus the mode
        // filter is the only cache key (replay/songs folder changes do not
        // invalidate it). Read/hash failures fall through to the normal
        // rebuild path instead of erroring.
        std::string dbMd5;
        {
            std::string dbPath;
            {
                std::lock_guard<std::mutex> lock(scoresMutex_);
                dbPath = scoresDbPath_;
            }
            std::vector<uint8_t> dbBytes;
            if (!dbPath.empty() && !readFileBytes(dbPath, dbBytes)) {
                Md5 md5;
                md5.update(dbBytes.data(), dbBytes.size());
                dbMd5 = Md5::toHex(md5.digest());
            }
        }
        if (!force && !dbMd5.empty()) {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            if (!browseCacheResponse_.empty() &&
                browseCacheMd5_ == dbMd5 && browseCacheMode_ == modeFilter &&
                browseCacheVersion_ == kBrowseResponseCacheVersion) {
                debuglog::log("browse L1 hit");
                res.set_content(browseCacheResponse_, "application/json");
                return;
            }
        }
        // L2 (disk) hit: after a restart the in-memory cache above is
        // empty, but the sidecar next to last_state.json still holds the
        // last fully built response. When its key matches the current
        // scores.db MD5 and mode filter, serve it and skip refresh +
        // rebuild entirely (unless force, see above). Any missing/short-
        // lived file or key mismatch silently falls through to the normal
        // rebuild path.
        if (!force && !dbMd5.empty()) {
            json state = json::object();
            {
                std::ifstream f(lastStatePath());
                if (f) {
                    try {
                        json in;
                        f >> in;
                        if (in.is_object()) state = std::move(in);
                    } catch (...) {
                        state = json::object();
                    }
                }
            }
            if (state.contains("browseCache") &&
                state["browseCache"].is_object()) {
                const json& bc = state["browseCache"];
                std::string savedMd5;
                int savedMode = -1;
                int savedVersion = 0;  // absent = pre-schema build
                try {
                    savedMd5 = bc.value("scoresDbMd5", std::string{});
                    savedMode = bc.value("mode", -1);
                    savedVersion = bc.value("cacheVersion", 0);
                } catch (...) {
                    savedMd5.clear();
                }
                auto sidecar = browseCacheSidecarPath();
                std::vector<uint8_t> bytes;
                if (savedMd5 == dbMd5 && savedMode == modeFilter &&
                    savedVersion == kBrowseResponseCacheVersion &&
                    !sidecar.empty() && !readFileBytes(sidecar, bytes)) {
                    std::string cached(bytes.begin(), bytes.end());
                    // Coarse sanity check: must look like the JSON object
                    // okResponse() produced.
                    if (!cached.empty() && cached.front() == '{') {
                        // Warm the in-memory (L1) slot so further hits skip
                        // the disk round-trip too.
                        {
                            std::lock_guard<std::mutex> lock(scoresMutex_);
                            browseCacheMd5_ = dbMd5;
                            browseCacheMode_ = modeFilter;
                            browseCacheVersion_ = kBrowseResponseCacheVersion;
                            browseCacheResponse_ = cached;
                        }
                        debuglog::log("browse L2 hit");
                        res.set_content(cached, "application/json");
                        return;
                    }
                }
            }
        }
        std::string buildErr;
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            // Refresh scores.db / replay / songs data from disk first: the
            // app may have been open while osu! recorded new plays.
            refreshScoreStoresLocked(buildErr);
        }
        if (!buildErr.empty())
            return sendError(req, res, 422, "INDEX_BUILD_FAILED", buildErr);
        // Collect + sort under the lock (entries are served by pointer).
        std::vector<const ScoreEntry*> picked;
        int64_t total = 0;
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            for (const auto& g : scores_->groups)
                for (const auto& s : g.scores) {
                    if (modeFilter >= 0 && s.mode != modeFilter) continue;
                    ++total;
                    picked.push_back(&s);
                }
            std::sort(picked.begin(), picked.end(),
                      [](const ScoreEntry* a, const ScoreEntry* b) {
                          return a->playedMs > b->playedMs;
                      });
        }
        // osu pairs a score with its replay by <beatmapMd5>-<filetime ticks>;
        // allow a ±1 ms tolerance for save-time rounding.
        auto tickKeyFor = [&](const ScoreEntry* s, int64_t adjTicks) {
            std::string bm = s->beatmapMd5;
            for (char& c : bm)
                c = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c)));
            return bm + "-" + std::to_string(
                s->playedTicks - kDotNetToFileTimeTicks + adjTicks);
        };
        json arr = json::array();
        for (const ScoreEntry* s : picked) {
            auto rit = replayIndex_.find(s->replayMd5);
            auto bit = songsIndex_.find(s->beatmapMd5);
            const SongsBeatmapMeta* meta =
                bit != songsIndex_.end() ? &bit->second : nullptr;
            // SR 列 = stable 星数 (官方 SR, 按行 mods 选档: DT/NC→srDt,
            // HT→srHt, 其余→srBase); Sunny SR 列 = SRR 全局。pp 沿用官方
            // 口径 (与 SR 列同源)。只读索引缓存字段, 命中路径不进这里。
            double rowSr = kSrNotComputable;
#if MRA_HAS_OFFICIAL_SR
            if (meta) rowSr = officialSrForMods(*meta, s->mods);
#endif
            double rowSunnySr = kSrNotComputable;
#if MRA_HAS_SRR_SR
            if (meta) rowSunnySr = saneSr(meta->srrSr);
#endif
            const double rowPp = officialPpFromCounts(
                rowSr, s->geki, s->c300, s->katu, s->c100, s->c50,
                s->miss, s->mods);
            bool hasReplay = rit != replayIndex_.end() ||
                             replayTickIndex_.count(tickKeyFor(s, 0)) > 0 ||
                             replayTickIndex_.count(tickKeyFor(s, -10000)) > 0 ||
                             replayTickIndex_.count(tickKeyFor(s, 10000)) > 0;
            arr.push_back(json{
                {"mode", s->mode},
                {"beatmapMd5", s->beatmapMd5},
                {"playerName", s->playerName},
                {"replayMd5", s->replayMd5},
                {"c300", s->c300}, {"c100", s->c100}, {"c50", s->c50},
                {"geki", s->geki}, {"katu", s->katu}, {"miss", s->miss},
                {"totalScore", s->totalScore},
                {"maxCombo", s->maxCombo},
                {"fullCombo", s->fullCombo},
                {"mods", s->mods},
                {"playedMs", s->playedMs},
                // string: 100 ns ticks exceed JS safe-integer range
                {"playedTicks", std::to_string(s->playedTicks)},
                {"onlineId", s->onlineId},
                {"title", meta ? meta->title : ""},
                {"artist", meta ? meta->artist : ""},
                {"creator", meta ? meta->creator : ""},
                {"version", meta ? meta->version : ""},
                {"keyCount", meta ? meta->keyCount : ""},
                {"sr", rowSr},
                {"sunnySr", rowSunnySr},
                {"bpm", meta ? meta->bpm : -1.0},
                {"lengthMs", meta ? meta->lengthMs : int64_t(-1)},
                {"pp", rowPp},
                {"hasReplay", hasReplay},
            });
        }
        std::string response =
            okResponse(json{{"total", total},
                            {"replays", replayIndex_.size()},
                            {"beatmaps", songsIndex_.size()},
                            {"returned", arr.size()},
                            {"scores", std::move(arr)}})
                .dump();
        debuglog::log("browse rebuild rows=" + std::to_string(total));
        // Remember the fully built response, keyed by the scores.db MD5 and
        // mode filter, so unchanged scores.db requests skip the whole
        // refresh + rebuild path (only keyed responses are cached). The
        // disk copy below lets a restarted process do the same.
        if (!dbMd5.empty()) {
            {
                std::lock_guard<std::mutex> lock(scoresMutex_);
                browseCacheMd5_ = dbMd5;
                browseCacheMode_ = modeFilter;
                browseCacheVersion_ = kBrowseResponseCacheVersion;
                browseCacheResponse_ = response;
            }
            // L2 (disk): persist the response sidecar plus its key next to
            // last_state.json. Both writes are best-effort; failures are
            // silently ignored (the next browse just rebuilds).
            auto sidecar = browseCacheSidecarPath();
            if (!sidecar.empty()) {
                std::ofstream o(sidecar, std::ios::binary | std::ios::trunc);
                if (o) o << response;
            }
            mergeLastState(json{{"browseCache",
                                 json{{"scoresDbMd5", dbMd5},
                                      {"mode", modeFilter},
                                      {"cacheVersion",
                                       kBrowseResponseCacheVersion}}}});
        }
        res.set_content(response, "application/json");
    });

    // Load a replay for a scores.db entry: resolves the .osr from the
    // Data\\r replay index, the .osu + audio from the Songs index, then runs
    // the standard load pipeline.
    svr.Post("/api/scores/load-replay",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        std::string replayMd5 = body.value("replayMd5", "");
        std::string ticksStr = body.value("playedTicks", "");
        std::string skinFolder = body.value("skinFolder", "");
        int64_t playedTicks = 0;
        bool hasTicks = false;
        if (!ticksStr.empty()) {
            try {
                playedTicks = std::stoll(ticksStr);
                hasTicks = true;
            } catch (...) {
                return sendError(req, res, 400, "INVALID_PARAM",
                                 "playedTicks must be an integer string");
            }
        }
        if (!hasTicks && !isMd5Hex(replayMd5))
            return sendError(req, res, 400, "INVALID_PARAM",
                             "need playedTicks or a 32-hex replayMd5");
        std::string replayPath, beatmapPath, audioPath;
        std::string beatmapMd5;
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            if (!scoresLoaded_ || !scores_)
                return sendError(req, res, 404, "SCORES_NOT_LOADED",
                                 "no scores.db loaded");
            {
                std::string ferr;
                refreshScoreStoresLocked(ferr);
            }
            const ScoreEntry* found = nullptr;
            for (const auto& g : scores_->groups)
                for (const auto& s : g.scores) {
                    if (hasTicks ? s.playedTicks == playedTicks
                                 : s.replayMd5 == replayMd5) {
                        found = &s;
                        break;
                    }
                }
            if (!found)
                return sendError(req, res, 404, "SCORE_NOT_FOUND",
                                 "score not in loaded scores.db");
            if (!replayIndexBuilt_ || !songsIndexBuilt_)
                return sendError(req, res, 422, "INDEX_NOT_BUILT",
                                 "browse the score list first (indexes not built)");
            // osu's own pairing: <beatmapMd5>-<filetime ticks>, ±1 ms slack;
            // fall back to the replay-md5 header index.
            std::string bmLower = found->beatmapMd5;
            for (char& c : bmLower)
                c = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c)));
            const std::string base =
                std::to_string(found->playedTicks - kDotNetToFileTimeTicks);
            auto tit = replayTickIndex_.find(bmLower + "-" + base);
            if (tit == replayTickIndex_.end())
                tit = replayTickIndex_.find(bmLower + "-" +
                                            std::to_string(
                                                found->playedTicks -
                                                kDotNetToFileTimeTicks -
                                                10000));
            if (tit == replayTickIndex_.end())
                tit = replayTickIndex_.find(bmLower + "-" +
                                            std::to_string(
                                                found->playedTicks -
                                                kDotNetToFileTimeTicks +
                                                10000));
            if (tit != replayTickIndex_.end()) {
                replayPath = tit->second;
            } else {
                auto rit = replayIndex_.find(found->replayMd5);
                if (rit != replayIndex_.end()) replayPath = rit->second;
            }
            // NOTE: deliberately no nearest-ticks fallback here — scores
            // whose .osr was never saved (failed exits) or pruned must 404
            // rather than silently analyze a different playthrough of the
            // same map.
            if (replayPath.empty())
                return sendError(req, res, 404, "REPLAY_NOT_IN_CACHE",
                                 "no .osr for this replay in Data\\r");
            auto bit = songsIndex_.find(found->beatmapMd5);
            if (bit == songsIndex_.end())
                return sendError(req, res, 404, "BEATMAP_NOT_IN_SONGS",
                                 "no .osu with this md5 in the Songs folder");
            beatmapPath = bit->second.path;
            beatmapMd5 = found->beatmapMd5;
            if (!bit->second.audioFilename.empty()) {
                fs::path audio = mra::pathFromUtf8(beatmapPath)
                                     .parent_path() /
                                 mra::pathFromUtf8(bit->second.audioFilename);
                audioPath = mra::pathToUtf8(audio);
            }
        }
        const auto loadT0 = std::chrono::steady_clock::now();
        LoadOutcome out = loadReplaySession(replayPath, beatmapPath,
                                            audioPath, skinFolder);
        debuglog::log("load browser " + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loadT0).count()) +
            "ms ok=" + (out.ok ? "1" : "0"));
        if (!out.ok)
            return sendError(req, res, 422, out.errorCode, out.message);
        res.set_content(okResponse(json{{"sessionId", out.sessionId},
                                        {"replayPath", replayPath},
                                        {"beatmapPath", beatmapPath},
                                        {"audioPath", audioPath},
                                        {"beatmapMd5", beatmapMd5}})
                            .dump(),
                        "application/json");
    });

    // ---- lazer 数据通道 (Phase L1) ----
    // 数据源: 绑定目录里的 .osr (lazer files\ 哈希树或平铺导出文件夹)。
    // 声明值层: 尾块 JSON statistics 六档为准, acc 用 lazer 305 口径。
    // 锁序: scoresMutex_ → lazerMutex_ (browse 的谱面 join 需要两者)。
    svr.Post("/api/lazer/bind",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        // null/非字符串的容错: value() 遇到类型不匹配会抛, 这里显式判型。
        std::string dir;
        if (body.contains("dir") && body["dir"].is_string())
            dir = body["dir"].get<std::string>();
        if (dir.empty())
            return sendError(req, res, 400, "MISSING_PATH", "dir is required");
        std::error_code ec;
        fs::path p = mra::pathFromUtf8(dir);
        if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
            return sendError(req, res, 400, "FOLDER_NOT_FOUND",
                             "not an existing directory", dir);
        // 可选 songsDir: lazer 模式没有 scores.db, Songs 绑定独立生效
        // (谱面 join / bpm / SR 都靠它; 不给则行为同全部未命中)。
        std::string songsDir;
        if (body.contains("songsDir") && body["songsDir"].is_string())
            songsDir = body["songsDir"].get<std::string>();
        if (!songsDir.empty()) {
            std::error_code ec2;
            fs::path sp = mra::pathFromUtf8(songsDir);
            if (!fs::exists(sp, ec2) || !fs::is_directory(sp, ec2))
                return sendError(req, res, 400, "FOLDER_NOT_FOUND",
                                 "songsDir is not an existing directory",
                                 songsDir);
        }
        bool songsBuilt = false;
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            if (!songsDir.empty()) {
                songsFolder_ = songsDir;
                songsIndexBuilt_ = false;
                songsStampValid_ = false;
                // lazer 的 Songs 记忆独立成键 (lazerSongs), 不覆写 stable
                // 上次位置的三元组 (scores/dataR/songs)。
                mergeLastState({{"lazerSongs", songsDir}});
            }
            // Songs 索引不在 bind 内同步构建 (2026-10-25 用户体感约定:
            // 目录应用必须瞬时, 与 stable 一致): 上面只做失效标记, 构建
            // 延后到 /api/lazer/browse 的按需分支。此前在这里同步刷新会
            // 阻塞 bind 响应 (26k 文件全树 stat + 17MB sidecar ≈ 0.x s),
            // 即用户报告的目录应用迟滞。扫描错误由 browse 分支上报。
            songsBuilt = songsIndexBuilt_;
        }
        {
            std::lock_guard<std::mutex> llock(lazerMutex_);
            if (!lazerIndex_) lazerIndex_ = std::make_unique<LazerScanIndex>();
            // .osr 解析时机 (L3.1 用户约定): bind 只登记目录 (重置旧索引状态),
            // 解析延后到回放库打开 (browse 内 ensure-build, warm 预热毫秒级)。
            lazerIndex_->bind(p);
            lazerIndexBuilt_ = false;
            lazerDir_ = dir;
        }
        mergeLastState({{"lazerDir", dir}});
        debuglog::log("lazer bind (deferred scan) dir=" + dir);
        res.set_content(okResponse(json{
            {"dir", dir},
            {"count", 0},
            // epoch 毫秒时间戳 (前端按 >1e12 判别显示为日期时间)
            {"lastScanMs",
             std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count()},
            {"songsIndexBuilt", songsBuilt}})
                            .dump(),
                        "application/json");
    });

    svr.Post("/api/lazer/unbind",
             [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(lazerMutex_);
        lazerIndex_.reset();
        lazerDir_.clear();
        lazerIndexBuilt_ = false;
        res.set_content(okResponse(json{{}}).dump(), "application/json");
    });

    svr.Get("/api/lazer/browse",
            [this](const httplib::Request& req, httplib::Response& res) {
        // force=1: 丢弃持久化缓存全量重扫; 否则未构建时 warm 预热后全量解析
        // (首次打开回放库), 已构建则 stat 级增量捾新回放。
        const bool force =
            req.has_param("force") && req.get_param_value("force") == "1";
        // 锁序: scoresMutex_ → lazerMutex_ (谱面 join 与增量 refresh 一并)。
        std::lock_guard<std::mutex> lock(scoresMutex_);
        std::lock_guard<std::mutex> llock(lazerMutex_);
        if (!lazerIndex_ || lazerDir_.empty())
            return sendError(req, res, 404, "LAZER_NOT_BOUND",
                             "no lazer data dir bound");
        // Songs 索引按需构建: 用户可能先绑 lazer 目录、之后才选 Songs
        // (/api/scores/songs 只标记未构建)。失败仅降级 (行内"未找到谱面")。
        if (!songsFolder_.empty() && !songsIndexBuilt_) {
            std::string ferr;
            refreshSongsIndexLocked(ferr);
        }
        if (force) lazerIndex_->bind(mra::pathFromUtf8(lazerDir_));
        if (!lazerIndexBuilt_) buildLazerIndexLocked(true);
        else lazerIndex_->refresh();
        const auto& entries = lazerIndex_->entries();
        json arr = json::array();
        int total = 0;
        for (const auto& e : entries) {
            ++total;
            const OsrHeader& h = e.osr.header;
            auto sit = songsIndex_.find(e.beatmapMd5);
            const SongsBeatmapMeta* meta =
                sit != songsIndex_.end() ? &sit->second : nullptr;
            json mods = json::array();
            if (e.extra.present) {
                for (const auto& m : e.extra.mods) {
                    json settings = json::object();
                    for (const auto& kv : m.settings) {
                        try {
                            settings[kv.first] = json::parse(kv.second);
                        } catch (...) {
                            settings[kv.first] = kv.second;
                        }
                    }
                    mods.push_back(json{{"acronym", m.acronym},
                                        {"settings", std::move(settings)}});
                }
            }
            LazerManiaCounts eff;
            const char* countsSource = "ushort";
            if (e.counts.valid) {
                eff = e.counts;
                countsSource = "json";
            } else {
                eff.perfect = h.countGeki;
                eff.great = h.count300;
                eff.good = h.countKatu;
                eff.ok = h.count100;
                eff.meh = h.count50;
                eff.miss = h.countMiss;
                eff.valid = true;
            }
            double rowSr = kSrNotComputable;
#if MRA_HAS_OFFICIAL_SR
            if (meta) rowSr = officialSrForMods(*meta, h.mods);
#endif
            double rowSunnySr = kSrNotComputable;
#if MRA_HAS_SRR_SR
            if (meta) rowSunnySr = saneSr(meta->srrSr);
#endif
            arr.push_back(json{
                {"file", e.relativePath},
                {"mode", h.mode},
                {"beatmapMd5", e.beatmapMd5},
                {"playerName", e.playerName},
                {"perfect", eff.perfect},
                {"great", eff.great},
                {"good", eff.good},
                {"ok", eff.ok},
                {"meh", eff.meh},
                {"miss", eff.miss},
                {"countsSource", countsSource},
                {"totalScore", h.score},
                {"maxCombo", h.maxCombo},
                {"fullCombo", h.perfect},
                {"mods", std::move(mods)},
                {"playedMs", e.playedMs},
                {"onlineId", h.onlineId},
                {"title", meta ? meta->title : ""},
                {"artist", meta ? meta->artist : ""},
                {"creator", meta ? meta->creator : ""},
                {"version", meta ? meta->version : ""},
                {"keyCount", meta ? meta->keyCount : ""},
                {"sr", rowSr},
                {"sunnySr", rowSunnySr},
                {"bpm", meta ? meta->bpm : -1.0},
                {"lengthMs", meta ? meta->lengthMs : int64_t(-1)},
                {"pp", nullptr},
                {"hasBeatmap", meta != nullptr},
                {"hasReplay", true}});
        }
        res.set_content(okResponse(json{{"total", total},
                                        {"returned", arr.size()},
                                        {"scores", std::move(arr)}})
                            .dump(),
                        "application/json");
    });

    svr.Post("/api/lazer/load-replay",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        std::string file;
        if (body.contains("file") && body["file"].is_string())
            file = body["file"].get<std::string>();
        std::string skinFolder = body.value("skinFolder", "");
        if (file.empty())
            return sendError(req, res, 400, "MISSING_PATH", "file is required");
        std::string norm = file;
        for (char& c : norm)
            if (c == '\\') c = '/';
        // 顺序锁 (无嵌套): lazer 索引找条目 → scores 锁做谱面 join →
        // lazer 锁还原路径。
        std::string beatmapMd5;
        {
            std::lock_guard<std::mutex> llock(lazerMutex_);
            if (!lazerIndex_ || lazerDir_.empty())
                return sendError(req, res, 404, "LAZER_NOT_BOUND",
                                 "no lazer data dir bound");
            if (!lazerIndexBuilt_) buildLazerIndexLocked(true);
            bool found = false;
            for (const auto& e : lazerIndex_->entries()) {
                if (e.relativePath == norm) {
                    beatmapMd5 = e.beatmapMd5;
                    found = true;
                    break;
                }
            }
            if (!found)
                return sendError(req, res, 404, "SCORE_NOT_FOUND",
                                 "file not in the lazer index", file);
        }
        std::string replayPath, beatmapPath, audioPath;
        {
            std::lock_guard<std::mutex> lock(scoresMutex_);
            std::string ferr;
            if (!songsFolder_.empty() && !songsIndexBuilt_)
                refreshSongsIndexLocked(ferr);  // best effort
            auto sit = songsIndex_.find(beatmapMd5);
            if (sit == songsIndex_.end())
                return sendError(req, res, 404, "BEATMAP_NOT_IN_SONGS",
                                 "no .osu with this md5 in the Songs folder",
                                 beatmapMd5);
            beatmapPath = sit->second.path;
            if (!sit->second.audioFilename.empty()) {
                fs::path audio =
                    mra::pathFromUtf8(beatmapPath).parent_path() /
                    mra::pathFromUtf8(sit->second.audioFilename);
                audioPath = mra::pathToUtf8(audio);
            }
        }
        {
            std::lock_guard<std::mutex> llock(lazerMutex_);
            fs::path rp = mra::pathFromUtf8(lazerDir_) /
                          mra::pathFromUtf8(norm);
            replayPath = mra::pathToUtf8(rp);
        }
        const auto loadT0 = std::chrono::steady_clock::now();
        LoadOutcome out = loadReplaySession(replayPath, beatmapPath,
                                            audioPath, skinFolder);
        debuglog::log("load lazer " + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loadT0).count()) +
            "ms ok=" + (out.ok ? "1" : "0"));
        if (!out.ok)
            return sendError(req, res, 422, out.errorCode, out.message);
        res.set_content(okResponse(json{
            {"sessionId", out.sessionId},
            {"replayPath", replayPath},
            {"beatmapPath", beatmapPath},
            // 无音频 → null (前端静音); 有 → UTF-8 路径
            {"audioPath", audioPath.empty() ? json(nullptr) : json(audioPath)},
            {"beatmapMd5", beatmapMd5}})
                            .dump(),
                        "application/json");
    });

    svr.Get("/api/scores",
            [this](const httplib::Request& req, httplib::Response& res) {
        int modeFilter = -1;
        if (req.has_param("mode")) {
            try { modeFilter = std::stoi(req.get_param_value("mode")); }
            catch (...) {
                return sendError(req, res, 400, "INVALID_PARAM",
                                 "mode must be an integer");
            }
        }
        int limit = 2000;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); }
            catch (...) {
                return sendError(req, res, 400, "INVALID_PARAM",
                                 "limit must be an integer");
            }
            limit = std::max(1, std::min(20000, limit));
        }
        // Hold the lock for the whole handler: entries are served by pointer,
        // so a concurrent load swapping scores_ must not dangle them.
        std::lock_guard<std::mutex> lock(scoresMutex_);
        if (!scoresLoaded_ || !scores_)
            return sendError(req, res, 404, "SCORES_NOT_LOADED",
                             "no scores.db loaded");
        std::vector<const ScoreEntry*> picked;
        int64_t total = 0;
        for (const auto& g : scores_->groups)
            for (const auto& s : g.scores) {
                if (modeFilter >= 0 && s.mode != modeFilter) continue;
                ++total;
                picked.push_back(&s);
            }
        std::sort(picked.begin(), picked.end(),
                  [](const ScoreEntry* a, const ScoreEntry* b) {
                      return a->playedMs > b->playedMs;
                  });
        if (picked.size() > static_cast<size_t>(limit))
            picked.resize(static_cast<size_t>(limit));
        json arr = json::array();
        for (const ScoreEntry* s : picked)
            arr.push_back(json{{"mode", s->mode},
                               {"clientVersion", s->clientVersion},
                               {"beatmapMd5", s->beatmapMd5},
                               {"playerName", s->playerName},
                               {"replayMd5", s->replayMd5},
                               {"c300", s->c300}, {"c100", s->c100},
                               {"c50", s->c50}, {"geki", s->geki},
                               {"katu", s->katu}, {"miss", s->miss},
                               {"totalScore", s->totalScore},
                               {"maxCombo", s->maxCombo},
                               {"fullCombo", s->fullCombo},
                               {"mods", s->mods},
                               {"playedMs", s->playedMs},
                               {"onlineId", s->onlineId}});
        res.set_content(okResponse(json{{"total", total},
                                        {"returned", arr.size()},
                                        {"scores", std::move(arr)}})
                            .dump(),
                        "application/json");
    });

    // ---- Last-session state (persists across restarts) ----
    svr.Get("/api/last-state",
            [this](const httplib::Request&, httplib::Response& res) {
        json out = json::object();
        std::ifstream f(lastStatePath());
        if (f) {
            try {
                json in;
                f >> in;
                if (in.is_object()) out = std::move(in);
            } catch (...) {
                out = json::object();
            }
        }
        res.set_content(okResponse(out).dump(), "application/json");
    });
    svr.Post("/api/last-state", [this](const httplib::Request& req,
                                       httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        if (!in.is_object())
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body must be a JSON object");
        json existing = json::object();
        {
            std::ifstream f(lastStatePath());
            if (f) {
                try {
                    json prev;
                    f >> prev;
                    if (prev.is_object()) existing = std::move(prev);
                } catch (...) {
                }
            }
        }
        for (const char* key :
             {"scores", "dataR", "songs", "skin", "scoreSourceMode",
              "lazerDir", "lazerSongs"}) {
            if (in.contains(key) && in[key].is_string())
                existing[key] = in[key].get<std::string>();
            else if (in.contains(key) && in[key].is_null())
                existing.erase(key);
        }
        if (in.contains("volume") && in["volume"].is_number())
            existing["volume"] = in["volume"].get<double>();
        // 预览落速 (lazer mania scale 1..40) — Transport 拖动时写入
        if (in.contains("scrollSpeed") && in["scrollSpeed"].is_number())
            existing["scrollSpeed"] = in["scrollSpeed"].get<double>();
        // 左右手分析: 奇数 key 中键归属 (true=归左)
        if (in.contains("handCenterToLeft") && in["handCenterToLeft"].is_boolean())
            existing["handCenterToLeft"] = in["handCenterToLeft"].get<bool>();
        if (in.contains("showInputJudges") && in["showInputJudges"].is_boolean())
            existing["showInputJudges"] = in["showInputJudges"].get<bool>();
        if (in.contains("debugLog") && in["debugLog"].is_boolean()) {
            existing["debugLog"] = in["debugLog"].get<bool>();
            debuglog::setEnabled(existing["debugLog"].get<bool>());
        }
        if (in.contains("scoreColWidths") && in["scoreColWidths"].is_array()) {
            json arr = json::array();
            for (const auto& v : in["scoreColWidths"])
                if (v.is_number()) arr.push_back(v.get<double>());
            if (!arr.empty()) existing["scoreColWidths"] = arr;
        }
        std::ofstream f(lastStatePath(), std::ios::binary | std::ios::trunc);
        if (!f)
            return sendError(req, res, 500, "STATE_WRITE_FAILED",
                             "cannot write last-state file");
        f << existing.dump();
        res.set_content(okResponse(existing).dump(), "application/json");
    });

    // ---- Native dialogs ----
    // The HTTP thread pool calls these; COM must be initialized per-thread
    // (STA). RPC_E_CHANGED_MODE means another mode is active - dialogs still
    // work in MTA on modern Windows, so tolerate it.
    auto dialogRoute = [](const httplib::Request& req, httplib::Response& res) {
        HRESULT hrInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool changedMode = (hrInit == RPC_E_CHANGED_MODE);
        struct ComGuard {
            bool changed;
            ~ComGuard() { if (!changed) ::CoUninitialize(); }
        } guard{changedMode};
        json body;
        try {
            if (!req.body.empty()) body = json::parse(req.body);
        } catch (...) {
            return sendError(req, res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        if (req.path == "/api/dialog/open-file") {
            std::vector<DialogFilter> filters;
            std::vector<std::pair<std::wstring, std::wstring>> storage;
            if (body.contains("filters") && body["filters"].is_array()) {
                for (const auto& f : body["filters"]) {
                    storage.emplace_back(utf8ToWide(f.value("name", "File")),
                                         utf8ToWide(f.value("pattern", "*.*")));
                }
            }
            if (storage.empty()) {
                storage.emplace_back(L"All Files", L"*.*");
            }
            for (const auto& s : storage)
                filters.push_back({s.first.c_str(), s.second.c_str()});
            std::wstring title =
                utf8ToWide(body.value("title", "Select file"));
            auto picked = openFileDialog(title, filters);
            if (!picked) {
                res.set_content(
                    okResponse(json{{"code", "DIALOG_CANCELLED"}}).dump(),
                    "application/json");
                return;
            }
            res.set_content(okResponse(json{{"path", *picked}}).dump(),
                            "application/json");
            return;
        }
        // /api/dialog/open-folder
        // initialDir (可选) = 上次位置: 对话框直接开在那个目录。
        auto picked = openFolderDialog(
            utf8ToWide(body.value("title", "Select folder")),
            utf8ToWide(body.value("initialDir", "")));
        if (!picked) {
            res.set_content(
                okResponse(json{{"code", "DIALOG_CANCELLED"}}).dump(),
                "application/json");
            return;
        }
        res.set_content(okResponse(json{{"path", *picked}}).dump(),
                        "application/json");
    };
    svr.Post("/api/dialog/open-file", dialogRoute);
    svr.Post("/api/dialog/open-folder", dialogRoute);

    svr.set_exception_handler(
        [](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
            std::string msg = "unknown error";
            try { if (ep) std::rethrow_exception(ep); }
            catch (const std::exception& e) { msg = e.what(); }
            sendError(req, res, 500, "INTERNAL_ERROR", msg);
        });

    // ---- Static frontend (embedded RCDATA in release, disk in dev) ----
    svr.Get("/.*", [this](const httplib::Request& req, httplib::Response& res) {
        if (!assetProvider_) return sendError(req, res, 404, "NOT_FOUND", "no route");
        std::string urlPath = req.path;
        if (urlPath == "/") urlPath = "/index.html";
        EmbeddedAsset asset;
        if (assetProvider_(urlPath, asset)) {
            // index.html must always revalidate: WebView2 otherwise serves a
            // stale cached shell (old JS bundle) after the exe is updated,
            // which silently breaks newer frontend features.
            res.set_header("Cache-Control", urlPath == "/index.html"
                                               ? "no-cache"
                                               : "public, max-age=31536000");
            res.set_content(reinterpret_cast<const char*>(asset.bytes.data()),
                            asset.bytes.size(), asset.mime.c_str());
            return;
        }
        sendError(req, res, 404, "NOT_FOUND", "asset not found: " + urlPath);
    });
}

ApiServer::ApiServer() = default;

ApiServer::~ApiServer() { stop(); }

bool ApiServer::start(int requestedPort) {
    svr_.reset(new httplib::Server());
    registerRoutes(*svr_);
    // Static frontend assets: provided via staticAssetProvider (embedded in
    // release, disk in dev). Registered by app/main.cpp if available.
    if (requestedPort > 0) {
        if (!svr_->bind_to_port("127.0.0.1", requestedPort)) {
            svr_.reset();
            return false;
        }
        port_ = requestedPort;
    } else {
        int bound = svr_->bind_to_any_port("127.0.0.1");
        if (bound <= 0) {
            svr_.reset();
            return false;
        }
        port_ = bound;
    }
    httplib::Server* raw = svr_.get();
    listenThread_.reset(new std::thread([raw] { raw->listen_after_bind(); }));
    return true;
}

void ApiServer::stop() {
    if (svr_) svr_->stop();
    if (listenThread_ && listenThread_->joinable()) listenThread_->join();
    listenThread_.reset();
    svr_.reset();
}

}  // namespace mra
