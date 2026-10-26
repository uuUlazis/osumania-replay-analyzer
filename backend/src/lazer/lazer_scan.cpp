#include "lazer/lazer_scan.h"

#include "io/paths.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <system_error>

namespace mra {
namespace {

// Same conversion as scores_db.cpp for .NET ticks (0001-01-01 epoch, the .osr
// header timestamp field): ticks/10'000 - epochOffset == unix ms. Keeping one
// formula across the repo means lazer scan rows and scores.db rows land on the
// same timeline.
constexpr int64_t kTicksPerMs = 10000;
constexpr int64_t kNetEpochToUnixMs = 62135596800000LL;  // 0001-01-01 -> 1970-01-01

int64_t netTicksToUnixMs(int64_t ticks) {
    if (ticks <= 0) return -1;
    return ticks / kTicksPerMs - kNetEpochToUnixMs;
}

int64_t fileTimeToUnixMs(std::filesystem::file_time_type ft) {
    const auto sys =
        std::chrono::clock_cast<std::chrono::system_clock>(ft);
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               sys.time_since_epoch())
        .count();
}

std::string asciiLower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string pathToUtf8ForwardSlashes(const std::filesystem::path& p) {
    // generic form normalizes separators to '/', the wide string keeps the
    // Unicode name intact, pathToUtf8 converts to UTF-8 for JSON boundaries.
    return pathToUtf8(std::filesystem::path(p.generic_wstring()));
}

bool isHexMd5(const std::string& s) {
    if (s.size() != 32) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

}  // namespace

void LazerScanIndex::bind(const std::filesystem::path& dir) {
    dir_ = dir;
    bound_ = true;
    cache_.clear();
    entries_.clear();
    diag_ = {};
}

void LazerScanIndex::refresh() {
    diag_ = {};
    entries_.clear();
    if (!bound_) {
        diag_.lastError = "no directory bound";
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(dir_, ec) ||
        !std::filesystem::is_directory(dir_, ec)) {
        diag_.lastError = "bound directory is missing or not a directory";
        return;
    }

    // 全量 stat 走 (回退自「目录 mtime 剪枝增量走」, 后者已实测证伪):
    // 递归枚举全部常规文件, 逐个 (size, mtimeMs) 与缓存比对 —— 未变更文件
    // 零重解析, 只对新/变化文件做读取+parseOsr。无目录级剪枝: NTFS 目录
    // mtime 更新延迟可见 (新建子目录后立即查询仍返回旧值, 新子树会被误
    // 剪, 晚 1+ 轮才入索引); 且原地改写文件不 bump 父目录 mtime (目录
    // mtime 只反映直接子项增删改名, 平铺 .osr 目录的内容修改会永久漏
    // 检)。后人勿再重蹈。
    std::vector<LazerScanEntry> fresh;
    std::map<std::filesystem::path, CachedFile> nextCache;
    std::string walkErr;

    std::error_code itErr;
    for (std::filesystem::recursive_directory_iterator it(
             dir_, std::filesystem::directory_options::skip_permission_denied,
             itErr),
         end;
         !itErr && it != end; it.increment(itErr)) {
        std::error_code ec2;
        const std::filesystem::path path = it->path();
        if (it->is_directory(ec2)) continue;  // 文件由迭代器直接枚举, 目录不计入
        if (!it->is_regular_file(ec2)) continue;
        ++diag_.filesSeen;

        // Candidate = ".osr" 文件, 或无扩展名文件 (真实 lazer files/ 树全部
        // 以小写 sha256 无扩展名存储, 每局回放与谱面/皮肤/音频同池)。无扩展
        // 名走廉价预筛 (体积 32B..64MB) + 读取后首字节 ≤ 3 (合法 mode 字节)
        // 再交给 parseOsr 深度验证; 文本/图片/音频几乎不可能首字节 ≤ 3。
        const std::string ext = asciiLower(path.extension().string());
        const bool isOsrExt = ext == ".osr";
        const uintmax_t size = it->file_size(ec2);
        if (ec2) continue;
        if (!isOsrExt && (!ext.empty() || size < 32 || size > (64u << 20)))
            continue;
        if (isOsrExt) ++diag_.osrCandidates;

        const auto mtime = it->last_write_time(ec2);
        if (ec2) continue;
        const int64_t mtimeMs = fileTimeToUnixMs(mtime);

        // Incremental path: identical (size, mtime) -> reuse the parsed entry.
        const auto cit = cache_.find(path);
        if (cit != cache_.end() && cit->second.size == size &&
            cit->second.mtimeMs == mtimeMs) {
            nextCache.emplace(path, cit->second);
            fresh.push_back(cit->second.entry);
            continue;
        }

        ++diag_.reParsed;

        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        // 无扩展名探测门: 首字节 ≤ 3 (合法 mode) 才进入 parse; parseOsr
        // 负责其余全部结构验证, 不匹配则按 parsedFailed/skip 计数。
        if (!isOsrExt) {
            if (bytes.empty() || bytes[0] > 3) continue;
            ++diag_.osrCandidates;
        }
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) {
                ++diag_.parsedFailed;
                continue;
            }
            f.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
            bytes.resize(static_cast<size_t>(f.gcount()));
            if (f.bad()) {
                ++diag_.parsedFailed;
                continue;
            }
        }

        LazerScanEntry entry;
        entry.relativePath =
            pathToUtf8ForwardSlashes(path.lexically_relative(dir_));
        entry.size = size;
        entry.mtimeMs = mtimeMs;
        entry.osr = parseOsr(bytes);
        if (!entry.osr.error.empty()) {
            // Corrupt / truncated / not actually a replay: skip, count.
            ++diag_.parsedFailed;
            continue;
        }

        const OsrHeader& h = entry.osr.header;
        entry.beatmapMd5 = asciiLower(h.beatmapMd5);
        entry.playerName = h.playerName;
        entry.playedMs = netTicksToUnixMs(h.timestamp);

        // Everything after the last stable field (onlineId + optional target
        // practice double) is the candidate tail block. parseOsr exposes its
        // length as trailingBytes, so slice it straight off the file tail.
        if (h.trailingBytes > 0 && h.trailingBytes <= bytes.size()) {
            const uint8_t* tail =
                bytes.data() + (bytes.size() - h.trailingBytes);
            entry.extra = parseLazerExtra(tail, h.trailingBytes);
        }
        if (entry.extra.present) {
            entry.counts =
                lazerManiaCountsFromStatistics(entry.extra.statistics);
        } else {
            entry.counts = LazerManiaCounts{};  // valid=false, not from JSON
        }

        // Only trust an md5-looking beatmap hash for the convenience copy.
        if (!isHexMd5(entry.beatmapMd5)) entry.beatmapMd5.clear();

        CachedFile cached;
        cached.entry = entry;
        cached.size = size;
        cached.mtimeMs = mtimeMs;
        nextCache.emplace(path, std::move(cached));
        fresh.push_back(std::move(entry));
    }
    if (itErr && walkErr.empty()) walkErr = itErr.message();

    if (!walkErr.empty()) diag_.lastError = walkErr;

    // Newest first; ties resolved by path so repeated refreshes are stable.
    std::stable_sort(fresh.begin(), fresh.end(),
                     [](const LazerScanEntry& a, const LazerScanEntry& b) {
                         if (a.playedMs != b.playedMs)
                             return a.playedMs > b.playedMs;
                         return a.relativePath < b.relativePath;
                     });

    // State counters describe the index as of this refresh (so they stay
    // meaningful on cache-hit refreshes); reParsed is the per-refresh delta.
    diag_.parsedOk = fresh.size();
    for (const LazerScanEntry& e : fresh) {
        if (e.extra.present) ++diag_.tailPresent;
        if (!e.extra.error.empty()) ++diag_.tailErrors;
    }

    cache_ = std::move(nextCache);  // drops cache lines for vanished files
    entries_ = std::move(fresh);
}

size_t LazerScanIndex::warm(const std::filesystem::path& dir,
                            const std::vector<LazerScanWarmEntry>& files) {
    bind(dir);  // 重置状态; warm 只填缓存, 条目由随后的 refresh() 重建
    for (const LazerScanWarmEntry& f : files) {
        if (f.relativePath.empty()) continue;
        // 缓存键与 refresh() 的行走键同构 (绝对路径); MSVC path 比较按分量,
        // 分隔符差异不影响命中。
        CachedFile cached;
        cached.entry = f.entry;
        cached.size = f.size;
        cached.mtimeMs = f.mtimeMs;
        cache_[dir_ / mra::pathFromUtf8(f.relativePath)] = std::move(cached);
    }
    return static_cast<size_t>(cache_.size());
}

}  // namespace mra
