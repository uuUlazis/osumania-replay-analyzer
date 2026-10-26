#pragma once
// Localhost-only HTTP API server (httplib). Stable versioned JSON DTOs.
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "app/embedded_assets.h"

namespace httplib { class Server; }

namespace mra {

struct ScoresDb;
class LazerScanIndex;

// Lightweight metadata for a beatmap file found in the Songs folder.
struct SongsBeatmapMeta {
    std::string path;
    std::string title;
    std::string artist;
    std::string creator;
    std::string audioFilename;
    // [Metadata]Version (difficulty name) for the browse list.
    std::string version;
    // [Metadata]CircleSize (mania key count) for the browse list.
    std::string keyCount;
    // ---- SR index (songs sidecar v2; -1 = not computable / not scanned) ----
    // Official star rating at clock rate 1.0 / 1.5 (DT+NC) / 0.75 (HT)
    // (docs/pp-sr-notes.md §3.8: only rate mods change mania SR), and the
    // SRR global star rating at the beatmap's own OD.
    double officialSrBase = -1.0;
    double officialSrDt = -1.0;
    double officialSrHt = -1.0;
    double srrSr = -1.0;
    // BPM (驻留时长最长的无继承 timing 段) 与物件跨度长度 ms; -1 = 不可算
    double bpm = -1.0;
    int64_t lengthMs = -1;
};

// Loads the full analysis pipeline for a replay+beatmap pair and registers a
// session. Returns (ok, sessionIdOrError).
struct LoadOutcome {
    bool ok = false;
    std::string sessionId;
    std::string errorCode;    // e.g. REPLAY_DECODE_FAILED
    std::string message;
    std::string details;
};

LoadOutcome loadReplaySession(const std::string& replayPath,
                              const std::string& beatmapPath,
                              const std::string& audioPath,
                              const std::string& skinFolder);

class ApiServer {
  public:
    ApiServer();   // out-of-line: httplib::Server is incomplete in this header
    ~ApiServer();
    // Binds 127.0.0.1 on the requested port; on success port() is the actual
    // bound port (when requestedPort == 0 an ephemeral port is allocated).
    bool start(int requestedPort);
    void stop();
    int port() const { return port_; }

    // Serves non-/api GET paths (embedded frontend / dev dist dir).
    void setAssetProvider(AssetProvider provider) {
        assetProvider_ = std::move(provider);
    }

  private:
    void registerRoutes(httplib::Server& svr);
    int port_ = 0;
    std::unique_ptr<httplib::Server> svr_;
    std::unique_ptr<std::thread> listenThread_;
    AssetProvider assetProvider_;

    // Loaded scores.db (app-level store, independent of replay sessions).
    // unique_ptr keeps ScoresDb incomplete in this header.
    std::mutex scoresMutex_;
    std::unique_ptr<ScoresDb> scores_;
    bool scoresLoaded_ = false;
    // Where scores.db was loaded from (used to auto-derive Data\r), the
    // Data\r folder (auto-derived or user-picked), and the Songs folder.
    std::string scoresDbPath_;
    std::string dataRFolder_;
    bool dataRAuto_ = false;
    std::string songsFolder_;
    // Lazily built indexes: replayMd5 -> .osr path inside Data\r, and
    // beatmapMd5 -> beatmap metadata inside Songs. Rebuilt when folders change.
    std::map<std::string, std::string> replayIndex_;
    // "<beatmapMd5 lowercase>-<filetime ticks>" -> .osr path. This mirrors how
    // osu!stable itself pairs a score with its saved replay: the .osr file
    // name is <beatmapMd5>-<ticks> where ticks is the score time as a Win32
    // FILETIME (100 ns since 1601-01-01), i.e. scores.db's .NET playedTicks
    // minus 50491123200000000. replayMd5 matching alone misses scores whose
    // stored replay md5 is stale/foreign even though the replay file exists.
    std::map<std::string, std::string> replayTickIndex_;
    std::map<std::string, SongsBeatmapMeta> songsIndex_;
    bool replayIndexBuilt_ = false;
    bool songsIndexBuilt_ = false;

    // Freshness tracking: the app can stay open for days while osu! keeps
    // writing new plays, so every browse re-checks the sources on disk.
    // Per-file caches keep incremental rescans cheap (stat-only for
    // unchanged files, re-parse only new/changed ones).
    struct ReplayFileEntry {
        std::uintmax_t size = 0;
        std::filesystem::file_time_type mtime{};
        std::string md5;    // embedded replay md5 ("" when unreadable)
        std::string ticks;  // "<beatmapMd5>-<filetime ticks>" key ("" when the
                            // file name does not follow osu's pairing scheme)
    };
    struct SongsFileEntry {
        std::uintmax_t size = 0;
        std::filesystem::file_time_type mtime{};
        std::string md5;
        SongsBeatmapMeta meta;
    };
    std::filesystem::file_time_type scoresDbStamp_{};
    std::uintmax_t scoresDbSize_ = 0;
    std::map<std::filesystem::path, ReplayFileEntry> replayFileCache_;
    std::map<std::filesystem::path, SongsFileEntry> songsFileCache_;
    std::filesystem::file_time_type dataRStamp_{};
    std::filesystem::file_time_type songsStamp_{};
    bool dataRStampValid_ = false;
    bool songsStampValid_ = false;

    // Response-level cache for /api/scores/browse: the full okResponse dump
    // keyed by the scores.db content MD5 plus the requested mode filter.
    // The MD5 is the single invalidation criterion (replay/songs folder
    // changes deliberately keep serving the cache); /api/scores/load clears
    // it. An empty browseCacheResponse_ means "no cache". A disk (L2)
    // copy of the last response lives next to last_state.json so a
    // restart can also serve an unchanged scores.db without rebuilding.
    std::string browseCacheMd5_;
    int browseCacheMode_ = -1;
    // Row-schema version of browseCacheResponse_ (kBrowseResponseCacheVersion
    // in api_server.cpp): bumped when the row schema changes so caches built
    // by older builds fail the key check and are rebuilt once.
    int browseCacheVersion_ = 0;
    std::string browseCacheResponse_;

    // Re-reads scores.db and refreshes both indexes when the disk changed
    // (scoresMutex_ must be held). Best-effort: a transient parse failure
    // (osu! mid-write) keeps the previous snapshot silently.
    void refreshScoreStoresLocked(std::string& err);
    // Songs-index-only refresh for callers without scores.db/Data\r (the
    // lazer data channel). Same semantics as section 3 of
    // refreshScoreStoresLocked; scoresMutex_ must be held.
    void refreshSongsIndexLocked(std::string& err);

    // ---- lazer 数据通道 (Phase L1): 绑定目录 + .osr 扫描索引 ----
    // 锁序约定: scoresMutex_ → lazerMutex_ (browse 的谱面 join 需要两者)。
    std::mutex lazerMutex_;
    std::unique_ptr<LazerScanIndex> lazerIndex_;
    std::string lazerDir_;
    // .osr 解析时机 (L3.1 用户约定): bind (目录选择/上次位置) 只登记目录,
    // 不解析; 首次打开回放库 (browse/载入) 才 warm+refresh 全量解析。
    bool lazerIndexBuilt_ = false;
    // 构建扫描索引 (warmFromDisk=true 先灌持久化缓存预热; false 丢弃缓存全
    // 量重扫 = 强制刷新), 构建后持久化。lazerMutex_ 必须持有。
    void buildLazerIndexLocked(bool warmFromDisk);
    void persistLazerIndexLocked();
};

}  // namespace mra
