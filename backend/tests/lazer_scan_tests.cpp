// L1 lazer 数据通道: lazer_scan.h 目录扫描器单元测试。
//
// 用合成 .osr (按官方序列亲手拼字节) 验证:
//   - lazer files/ 风格的 <h>/<h2>/<hash> 深层目录 + 平铺文件夹;
//   - 扩展名粗筛 + parseOsr 探测 (垃圾 .osr / 非 .osr 跳过并计数);
//   - stable 小版本无尾块 vs lazer 版本带 LZMA 尾块 (真实 blob);
//   - refresh 增量: 只重 parse 新增/变化文件 (reParsed 计数断言);
//   - entries() 按 playedMs 降序稳定排序。
//
// 单独 console target (mra_tests_lazer_scan), 沿用仓库既有测试写法。
// 全部数据为合成 (玩家名 TesterX, 计数随意), 不含真实玩家数据 (红线)。
#include "lazer/lazer_scan.h"
#include "lazer_tail_blob.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace mra;
namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        const long long _va = (long long)(a);                                \
        const long long _vb = (long long)(b);                                \
        if (_va != _vb) {                                                    \
            std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__,   \
                        __LINE__, #a, #b, _va, _vb);                         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

namespace {

// ---- 合成 .osr 构造 ----------------------------------------------------------

void pushU8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }

void pushU16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xff));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xff));
}

void pushU32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xff));
}

void pushU64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i)
        v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xff));
}

void pushUleb(std::vector<uint8_t>& v, uint64_t x) {
    do {
        uint8_t b = static_cast<uint8_t>(x & 0x7f);
        x >>= 7;
        if (x) b |= 0x80;
        v.push_back(b);
    } while (x);
}

void pushOsuString(std::vector<uint8_t>& v, const std::string& s) {
    if (s.empty()) {
        v.push_back(0x00);
        return;
    }
    v.push_back(0x0b);
    pushUleb(v, s.size());
    v.insert(v.end(), s.begin(), s.end());
}

// tail 附加在 onlineId (+ target practice) 之后: i32 压缩长度 + LZMA 流。
std::vector<uint8_t> framedLzmaTail(const unsigned char* blob, size_t size) {
    std::vector<uint8_t> out;
    pushU32(out, static_cast<uint32_t>(size));
    out.insert(out.end(), blob, blob + size);
    return out;
}

struct OsrSpec {
    uint8_t mode = 3;
    uint32_t gameVersion = 20251203;      // stable 日期型版本号
    std::string beatmapMd5 = "0123456789abcdef0123456789abcdef";
    std::string playerName = "Tester";
    std::string replayMd5 = "fedcba9876543210fedcba9876543210";
    uint16_t n300 = 0, n100 = 0, n50 = 0, geki = 0, katu = 0, miss = 0;
    uint32_t score = 0;
    uint16_t maxCombo = 0;
    uint8_t perfect = 0;
    uint32_t mods = 0;
    int64_t timestamp = 0;                // .NET ticks
    std::vector<uint8_t> compressedData{0xaa, 0xaa, 0xaa, 0xaa,
                                        0xaa, 0xaa, 0xaa, 0xaa};
    bool withOnlineId = false;
    uint64_t onlineId = 0;
    std::vector<uint8_t> tail;            // appended after onlineId
};

std::vector<uint8_t> buildOsr(const OsrSpec& s) {
    std::vector<uint8_t> out;
    pushU8(out, s.mode);
    pushU32(out, s.gameVersion);
    pushOsuString(out, s.beatmapMd5);
    pushOsuString(out, s.playerName);
    pushOsuString(out, s.replayMd5);
    pushU16(out, s.n300);
    pushU16(out, s.n100);
    pushU16(out, s.n50);
    pushU16(out, s.geki);
    pushU16(out, s.katu);
    pushU16(out, s.miss);
    pushU32(out, s.score);
    pushU16(out, s.maxCombo);
    pushU8(out, s.perfect);
    pushU32(out, s.mods);
    pushOsuString(out, "");  // life graph: empty
    pushU64(out, static_cast<uint64_t>(s.timestamp));
    pushU32(out, static_cast<uint32_t>(s.compressedData.size()));
    out.insert(out.end(), s.compressedData.begin(), s.compressedData.end());
    if (s.withOnlineId || !s.tail.empty()) {
        pushU64(out, s.onlineId);
        out.insert(out.end(), s.tail.begin(), s.tail.end());
    }
    return out;
}

// ---- 临时目录 ----------------------------------------------------------------

fs::path makeTempRoot(const char* tag) {
    const auto tick =
        std::chrono::steady_clock::now().time_since_epoch().count();
    // 临时根放在执行目录 (仓库内) 而不是全局 %TEMP%: 沙箱化的子进程对
    // 仓库外路径写入会被拒 (实测 create_directories -> Access is denied)。
    // 每个用例结束 remove_all 清理; 残留目录也全在仓库内, 便于清扫。
    fs::path root = fs::current_path() /
                    ("lazer_scan_test_tmp_" + std::string(tag) + "_" +
                     std::to_string(tick));
    std::error_code ec;
    fs::remove_all(root, ec);  // 清掉上次崩溃可能的残留 (ignore ec)
    fs::create_directories(root, ec);
    if (ec) {
        std::printf("  [temp] create failed at %s: %s\n",
                    root.string().c_str(), ec.message().c_str());
        std::fflush(stdout);
    }
    return root;
}

void writeFile(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    f.close();
    CHECK(static_cast<bool>(f));
}

// ---- 合成数据常量 --------------------------------------------------------------

// .NET ticks -> playedMs: 638000000000000000/10000 - 62135596800000 = 1664403200000
constexpr int64_t kTicksNewer = 638000000000000000LL;
constexpr int64_t kTicksOlder = 637000000000000000LL;
constexpr int64_t kPlayedMsNewer = 1664403200000LL;
constexpr int64_t kPlayedMsOlder = 1564403200000LL;

OsrSpec lazerSpec(int64_t ticks, const char* player) {
    OsrSpec s;
    s.gameVersion = 30000019;
    s.playerName = player;
    s.timestamp = ticks;
    s.withOnlineId = true;
    s.onlineId = 42;
    s.tail = framedLzmaTail(mra_lazer_test::kTailBlob.data(),
                            mra_lazer_test::kTailBlobSize);
    return s;
}

OsrSpec stableSpec(int64_t ticks, const char* player) {
    OsrSpec s;
    s.gameVersion = 20251203;
    s.playerName = player;
    s.timestamp = ticks;
    return s;
}

const LazerScanEntry* findEntry(const std::vector<LazerScanEntry>& entries,
                                const std::string& relPath) {
    for (const LazerScanEntry& e : entries)
        if (e.relativePath == relPath) return &e;
    return nullptr;
}

// ---- 测试 ---------------------------------------------------------------------

void testScanMixedTree() {
    const fs::path root = makeTempRoot("mixed");

    // lazer files/ 风格三层目录
    writeFile(root / "files/a/b/lazer.osr",
              buildOsr(lazerSpec(kTicksNewer, "TesterAlpha")));
    writeFile(root / "files/c/d/stable.osr",
              buildOsr(stableSpec(kTicksOlder, "TesterBeta")));
    // 垃圾 .osr: 扩展名命中但 parseOsr 失败
    writeFile(root / "files/e/f/garbage.osr",
              std::vector<uint8_t>(100, 'A'));
    // 非 .osr 混放 (谱面/说明文件等)
    writeFile(root / "files/readme.txt",
              {'o', 's', 'u', '!', ' ', 'd', 'a', 't', 'a'});
    writeFile(root / "files/0123abcd", {'x'});  // 无扩展名内容寻址文件

    LazerScanIndex idx;
    idx.refresh();  // 未绑定目录: 不崩溃
    CHECK(idx.entries().empty());
    CHECK(!idx.diagnostics().lastError.empty());

    idx.bind(root);
    idx.refresh();
    CHECK(idx.diagnostics().lastError.empty());
    CHECK_EQ(idx.entries().size(), 2);
    CHECK_EQ(idx.diagnostics().filesSeen, 5);
    CHECK_EQ(idx.diagnostics().osrCandidates, 3);
    CHECK_EQ(idx.diagnostics().parsedOk, 2);
    CHECK_EQ(idx.diagnostics().parsedFailed, 1);
    CHECK_EQ(idx.diagnostics().tailPresent, 1);
    CHECK_EQ(idx.diagnostics().tailErrors, 0);

    // playedMs 降序: lazer (最新) 在前
    CHECK(!idx.entries().empty());
    if (idx.entries().size() == 2) {
        CHECK_EQ(idx.entries()[0].relativePath.compare("files/a/b/lazer.osr"),
                 0);
        CHECK_EQ(idx.entries()[1].relativePath.compare("files/c/d/stable.osr"),
                 0);
    }

    const LazerScanEntry* lazer =
        findEntry(idx.entries(), "files/a/b/lazer.osr");
    CHECK(lazer);
    if (lazer) {
        // relativePath 用正斜杠, 无反斜杠
        CHECK(lazer->relativePath.find('\\') == std::string::npos);
        CHECK(lazer->osr.error.empty());
        CHECK_EQ(lazer->osr.header.gameVersion, 30000019u);
        // trailingBytes 是 onlineId 之后的尾块 (长度帧 + LZMA 流)
        CHECK_EQ(lazer->osr.header.trailingBytes,
                 static_cast<size_t>(4 + mra_lazer_test::kTailBlobSize));
        CHECK(lazer->extra.present);
        CHECK(lazer->extra.error.empty());
        CHECK_EQ(lazer->extra.rank.compare("S"), 0);
        CHECK(lazer->counts.valid);
        CHECK_EQ(lazer->counts.perfect, 850);
        CHECK_EQ(lazer->counts.miss, 1);
        CHECK_EQ(lazer->beatmapMd5.compare("0123456789abcdef0123456789abcdef"),
                 0);
        CHECK_EQ(lazer->playerName.compare("TesterAlpha"), 0);
        CHECK_EQ(lazer->playedMs, kPlayedMsNewer);
        CHECK(lazer->size > 0);
        CHECK(lazer->mtimeMs > 0);
    }

    const LazerScanEntry* stable =
        findEntry(idx.entries(), "files/c/d/stable.osr");
    CHECK(stable);
    if (stable) {
        CHECK(!stable->extra.present);
        CHECK(stable->extra.error.empty());  // 无尾块不算错误
        CHECK(!stable->counts.valid);
        CHECK_EQ(stable->playedMs, kPlayedMsOlder);
        CHECK_EQ(stable->playerName.compare("TesterBeta"), 0);
    }

    fs::remove_all(root);
}

void testStableJunkTailCounted() {
    const fs::path root = makeTempRoot("junktail");

    // stable 版本 + onlineId + 16 字节垃圾尾巴: 入口保留, 尾块解析计错
    OsrSpec s = stableSpec(636000000000000000LL, "TesterGamma");
    s.withOnlineId = true;
    s.onlineId = 7;
    s.tail.assign(16, 0x41);
    writeFile(root / "junktail.osr", buildOsr(s));

    LazerScanIndex idx;
    idx.bind(root);
    idx.refresh();
    CHECK(idx.diagnostics().lastError.empty());
    CHECK_EQ(idx.entries().size(), 1);
    CHECK_EQ(idx.diagnostics().parsedOk, 1);
    CHECK_EQ(idx.diagnostics().tailPresent, 0);
    CHECK_EQ(idx.diagnostics().tailErrors, 1);
    if (idx.entries().size() == 1) {
        CHECK(!idx.entries()[0].extra.present);
        CHECK(!idx.entries()[0].extra.error.empty());
        CHECK(!idx.entries()[0].counts.valid);
    }
    fs::remove_all(root);
}

void testRefreshIncremental() {
    const fs::path root = makeTempRoot("incremental");
    LazerScanIndex idx;
    idx.bind(root);

    idx.refresh();
    CHECK_EQ(idx.entries().size(), 0);
    CHECK_EQ(idx.diagnostics().reParsed, 0);

    writeFile(root / "a/lazer.osr",
              buildOsr(lazerSpec(kTicksNewer, "TesterAlpha")));
    idx.refresh();
    CHECK_EQ(idx.entries().size(), 1);
    CHECK_EQ(idx.diagnostics().reParsed, 1);

    idx.refresh();  // 无变化: 全部缓存命中
    CHECK_EQ(idx.entries().size(), 1);
    CHECK_EQ(idx.diagnostics().reParsed, 0);
    CHECK_EQ(idx.diagnostics().parsedOk, 1);

    writeFile(root / "b/c/stable.osr",
              buildOsr(stableSpec(kTicksOlder, "TesterBeta")));
    idx.refresh();
    CHECK_EQ(idx.entries().size(), 2);
    CHECK_EQ(idx.diagnostics().reParsed, 1);
    // 排序: 新的在前
    CHECK_EQ(idx.entries()[0].relativePath.compare("a/lazer.osr"), 0);
    CHECK_EQ(idx.entries()[1].relativePath.compare("b/c/stable.osr"), 0);

    // 变化的文件 (尺寸不同) 重新 parse, 未变的走缓存
    OsrSpec changed = stableSpec(kTicksOlder, "TesterBeta");
    changed.compressedData.assign(40, 0xbb);
    writeFile(root / "b/c/stable.osr", buildOsr(changed));
    idx.refresh();
    CHECK_EQ(idx.entries().size(), 2);
    CHECK_EQ(idx.diagnostics().reParsed, 1);

    // 删除后条目消失, 且不产生新 parse
    std::error_code ec;
    fs::remove(root / "a/lazer.osr", ec);
    idx.refresh();
    CHECK_EQ(idx.entries().size(), 1);
    CHECK_EQ(idx.entries()[0].relativePath.compare("b/c/stable.osr"), 0);
    CHECK_EQ(idx.diagnostics().reParsed, 0);

    fs::remove_all(root);
}

void testFlatFolder() {
    const fs::path root = makeTempRoot("flat");
    writeFile(root / "one.osr", buildOsr(lazerSpec(kTicksNewer, "TesterAlpha")));
    writeFile(root / "TWO.OSR", buildOsr(stableSpec(kTicksOlder, "TesterBeta")));

    LazerScanIndex idx;
    idx.bind(root);
    idx.refresh();
    CHECK(idx.diagnostics().lastError.empty());
    CHECK_EQ(idx.entries().size(), 2);
    CHECK_EQ(idx.diagnostics().osrCandidates, 2);
    // 扩展名大小写不敏感, relativePath 就是文件名
    CHECK_EQ(idx.entries()[0].relativePath.compare("one.osr"), 0);
    CHECK_EQ(idx.entries()[1].relativePath.compare("TWO.OSR"), 0);
    CHECK(idx.entries()[0].extra.present);
    CHECK(!idx.entries()[1].extra.present);
    fs::remove_all(root);
}

void testMissingDir() {
    LazerScanIndex idx;
    idx.bind(fs::temp_directory_path() /
             "mra_lazer_scan_tests_definitely_missing_dir");
    idx.refresh();
    CHECK(idx.entries().empty());
    CHECK(!idx.diagnostics().lastError.empty());
}

void testExtensionlessHashTree() {
    // 真实 lazer files/ 树: 一切文件以内容寻址无扩展名存储, 回放与谱面/
    // 皮肤/音频同池; 靠体积+首字节魔数预筛 + parseOsr 深度验证区分。
    const fs::path root = makeTempRoot("extless");
    const auto lazerBytes = buildOsr(lazerSpec(kTicksNewer, "TesterExtless"));
    const auto stableBytes = buildOsr(stableSpec(kTicksOlder, "TesterStable"));
    writeFile(root / "files/1/ab/a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6"
                     "e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2", lazerBytes);
    writeFile(root / "files/3/de/0f0e0d0c0b0a09080706050403020100"
                     "ffeeddccbbaa99887766554433221100",
              {'o', 's', 'u', ' ', 'f', 'i', 'l', 'e', ' ', 'f', 'o', 'r',
               'm', 'a', 't', ' ', 'v', '1', '4'});
    writeFile(root / "files/9/ff/exported.osr", stableBytes);

    LazerScanIndex idx;
    idx.bind(root);
    idx.refresh();
    CHECK(idx.diagnostics().lastError.empty());
    CHECK_EQ(idx.entries().size(), 2);
    bool sawExtlessLazer = false;
    bool sawStableOsr = false;
    for (const auto& e : idx.entries()) {
        if (e.extra.present) sawExtlessLazer = true;
        else sawStableOsr = true;
    }
    CHECK(sawExtlessLazer);
    CHECK(sawStableOsr);
    // 文本诱饵 (首字节 'o' > 3) 被预筛跳过, 不计候选
    CHECK_EQ(idx.diagnostics().osrCandidates, 2);
    fs::remove_all(root);
}

}  // namespace

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    } cases[] = {
        {"scanMixedTree", testScanMixedTree},
        {"stableJunkTailCounted", testStableJunkTailCounted},
        {"refreshIncremental", testRefreshIncremental},
        {"flatFolder", testFlatFolder},
        {"extensionlessHashTree", testExtensionlessHashTree},
        {"missingDir", testMissingDir},
    };
    for (const Case& c : cases) {
        const int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? "ok  " : "FAIL", c.name);
    }
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED (lazer scan)\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
