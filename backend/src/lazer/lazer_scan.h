#pragma once
// Directory scanner for score .osr files written by osu!lazer (and stable).
//
// Lazer stores every uploaded/downloaded file content-addressed under
//   <data>/files/<h>/<h2>/<hash>
// with no extension, so score replays live next to beatmap/skin/audio blobs.
// Bound directories may therefore be either such a hash tree or a plain flat
// folder of exported .osr files: the walk is recursive, the ".osr" extension
// filter (case-insensitive) pre-screens candidates and parseOsr() is the
// authoritative probe (anything that fails to parse is skipped and counted).
//
// Self-contained on purpose: the per-file (size, mtime) cache mirrors the
// ReplayFileEntry pattern in api_server.cpp but does not touch it
// (api_server is owned by the integration step).
#include "lazer/lazer_format.h"
#include "replay/osr_reader.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mra {

struct LazerScanEntry {
    std::string relativePath;  // relative to the bound dir, forward slashes
    uint64_t size = 0;
    int64_t mtimeMs = 0;   // unix epoch milliseconds
    OsrParseResult osr;    // full parseOsr result (header stays available)
    LazerScoreExtra extra; // tail block; present=false for stable replays
    LazerManiaCounts counts;  // derived from extra.statistics when present
    // Convenience copies, all stable across refreshes:
    std::string beatmapMd5;   // lowercase
    std::string playerName;
    int64_t playedMs = -1;    // header.timestamp (.NET ticks) -> unix ms, -1 if unknown
};

struct LazerScanDiagnostics {
    // State as of the latest refresh():
    size_t filesSeen = 0;      // all regular files encountered by the walk
    size_t osrCandidates = 0;  // files with a ".osr" extension (case-insensitive)
    size_t parsedOk = 0;       // entries currently in the index (parseOsr ok)
    size_t parsedFailed = 0;   // ".osr" extension but corrupt/truncated -> skipped
    size_t tailPresent = 0;    // entries whose extra tail block parsed
    size_t tailErrors = 0;     // entries with trailing bytes that failed tail parsing
    // Per-refresh delta: how many files were actually re-parsed this pass
    // (0 on a fully cache-hit refresh).
    size_t reParsed = 0;
    std::string lastError;     // directory walk / IO error text, empty when none
};

// 持久化预热条目 (上次位置迟滞修复): warm() 的输入。browse 行取用面之外
// 的字段 (frames/compressedData/lifeGraph 等) 保持默认 —— 分析载入路径
// 重读 .osr 文件本身, 不消费索引条目的这些字段。
struct LazerScanWarmEntry {
    std::string relativePath;  // 与扫描相对路径同形 (forward slashes)
    uint64_t size = 0;
    int64_t mtimeMs = 0;       // unix epoch ms (与缓存的比较口径一致)
    LazerScanEntry entry;      // browse 所需字段已填; 其余可默认
};

class LazerScanIndex {
public:
    LazerScanIndex() = default;

    // Binds (or rebinds) the scan directory and drops all cached state.
    // Does not scan; call refresh() afterwards.
    void bind(const std::filesystem::path& dir);

    const std::filesystem::path& directory() const { return dir_; }

    // Walks the bound directory recursively and rebuilds entries(): a full
    // recursive enumeration + per-file (size, mtime) stat pass. The per-file
    // cache keeps unchanged files from being re-parsed; only new or modified
    // files hit parseOsr again. There is intentionally no directory-level
    // pruning: NTFS directory mtimes update with visible delay (a freshly
    // created child still reports the old stamp) and do not move at all on
    // in-place rewrites, so a pruning walk silently misses changes. Never
    // throws; problems land in diagnostics().lastError.
    void refresh();

    // 预热 (上次位置迟滞修复): 用持久化缓存灌入 per-file 缓存 (dir 不匹配则
    // 先 bind 重置)。只填缓存; 调用方随后 refresh() 以 stat-only 行走复用
    // 缓存项 (未变更文件零重解析, 毫秒级)。返回灌入的文件数。
    size_t warm(const std::filesystem::path& dir,
                const std::vector<LazerScanWarmEntry>& files);

    // Entries sorted by playedMs descending (newest first); ties broken by
    // relativePath ascending so the order is stable across refreshes.
    const std::vector<LazerScanEntry>& entries() const { return entries_; }

    const LazerScanDiagnostics& diagnostics() const { return diag_; }

private:
    struct CachedFile {
        LazerScanEntry entry;
        uint64_t size = 0;
        int64_t mtimeMs = 0;   // unix epoch ms (ms 粒度比较, 便于持久化回灌)
    };

    std::filesystem::path dir_;
    bool bound_ = false;
    std::map<std::filesystem::path, CachedFile> cache_;
    std::vector<LazerScanEntry> entries_;
    LazerScanDiagnostics diag_;
};

}  // namespace mra
