#include "http/api_server.h"

#include "io/paths.h"

#include "app/dialogs.h"
#include "beatmap/osu_parser.h"
#include "io/md5.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
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
#include <utility>

namespace mra {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Restart-persistent "last session" state. localStorage cannot be used:
// the server binds a new port on every launch, so the page origin changes
// and its storage resets. Paths live in
// %LOCALAPPDATA%\ManiaReplayAnalyzer\last_state.json instead.
std::filesystem::path lastStatePath() {
    wchar_t buf[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                         SHGFP_TYPE_CURRENT, buf) != S_OK)
        return {};
    std::filesystem::path dir =
        std::filesystem::path(buf) / L"ManiaReplayAnalyzer";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
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
    MatchResult match =
        matchReplayToObjects(beatmap.objects, keyCount, frames, matchOptions);

    // Second pass under ScoreV2 rules for the renderer's V2 judgement view
    // (LN head late window ok*1.5, tail judged independently on |endDiff|/1.5).
    MatchOptions v2Options = matchOptions;
    v2Options.scoreV2 = true;
    MatchResult matchV2 =
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
    const bool scoreV2Replay = countsV2 || (!countsV1 && modsScoreV2);
    GlobalAnalysis analysis =
        analyzeMatch(scoreV2Replay ? matchV2 : match, keyCount);

    const bool stableCountMode =
        !scoreV2Replay &&
        (static_cast<int64_t>(analysis.stableCounts.judged()) == declaredTotal ||
         static_cast<int64_t>(analysis.counts.judged()) != declaredTotal);

    ReplaySession session;
    session.replayPath = replayPath;
    session.beatmapPath = beatmapPath;
    session.beatmapMd5 = osuMd5;
    session.skinFolder = skinFolder.empty() ? std::nullopt
                                            : std::optional<std::string>(skinFolder);
    session.replay = std::move(osr);
    session.frames = std::move(frames);
    session.keyCount = keyCount;
    session.beatmap = std::move(beatmap);
    session.match = std::move(match);
    session.matchV2 = std::move(matchV2);
    session.analysis = std::move(analysis);
    session.stableCountMode = stableCountMode;
    session.rulesScoreV2 = scoreV2Replay;
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

void ApiServer::registerRoutes(httplib::Server& svr) {
    // Restore the debug-log toggle the frontend persisted (checkbox).
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
            {"replayPath", s.replayPath}}).dump(),
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
        res.set_content(okResponse(json{
            {"title", b.title}, {"artist", b.artist}, {"creator", b.creator},
            {"version", b.version}, {"beatmapId", b.beatmapId},
            {"beatmapSetId", b.beatmapSetId}, {"mode", b.mode},
            {"keyCount", b.keyCount}, {"od", b.overallDifficulty},
            {"hp", b.hpDrain},
            {"audioFilename", b.audioFilename},
            {"backgroundFile", b.backgroundFile},
            {"objectCount", b.objects.size()},
            {"beatmapPath", s.beatmapPath}}).dump(),
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
        double summarySr = kSrNotComputable;
#if MRA_HAS_OFFICIAL_SR
        if (s.beatmap.isMania() && s.beatmap.keyCount > 0 &&
            s.beatmap.keyCount <= 10 && s.beatmap.objects.size() >= 20) {
            const std::vector<SrNote> notes = srNotesFromBeatmap(s.beatmap);
            try {
                summarySr = saneSr(OfficialStarRating(
                    s.beatmap.keyCount, notes, rateForMods(sessionMods)));
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
        res.set_content(okResponse(json{
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
            {"rate", rateForMods(sessionMods)},
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
        if (sessionId.empty() || folder.empty())
            return sendError(req, res, 400, "MISSING_PATH",
                             "sessionId and folder are required");
        std::error_code ec;
        fs::path p = mra::pathFromUtf8(folder);
        if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
            return sendError(req, res, 400, "FOLDER_NOT_FOUND",
                             "not an existing directory", folder);
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
        if (!s.skinFolder)
            return sendError(req, res, 404, "SKIN_NOT_LOADED", "no skin loaded");
        SkinLoader loader = SkinLoader::loadFromFolder(*s.skinFolder);
        std::string element = req.matches[2];
        auto img = loader.findElement(element);
        if (!img) return sendError(req, res, 404, "RESOURCE_NOT_FOUND", "no such element");
        std::string lower = img->fileName;
        for (char& c : lower) c = static_cast<char>(std::tolower((unsigned char)c));
        const char* mime = lower.ends_with(".png") ? "image/png" : "image/jpeg";
        res.set_content(reinterpret_cast<const char*>(img->bytes.data()),
                        img->bytes.size(), mime);
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
        }
        mergeLastState({{"scores", path}});
        if (dataRAuto)
            mergeLastState({{"dataR",
                             [&] {
                                 std::lock_guard<std::mutex> l(scoresMutex_);
                                 return dataRFolder_;
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
                                        {"dataRHint", dataRInfo}})
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

    // Songs folder registration (must be chosen before browsing/loading).
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
        for (const char* key : {"scores", "dataR", "songs", "skin"}) {
            if (in.contains(key) && in[key].is_string())
                existing[key] = in[key].get<std::string>();
            else if (in.contains(key) && in[key].is_null())
                existing.erase(key);
        }
        if (in.contains("volume") && in["volume"].is_number())
            existing["volume"] = in["volume"].get<double>();
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
        auto picked =
            openFolderDialog(utf8ToWide(body.value("title", "Select folder")));
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
