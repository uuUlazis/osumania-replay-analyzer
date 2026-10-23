// 偶数键 + 右特殊键 lane bit 顺序 (I-6) 的单元测试。
//
// 单独一个 console target (mra_tests_replay_lanes): test_mania_core.cpp 持有
// mra_tests 的 main, 沿用 mra_tests_star_rating 的既有做法, 不去改公共测试文件。
//
// 权威语义见 replay/replay_frames.h 的注释块与
// build/tmp_accupdate/re_evenkey_findings.md:
//   编码 GetManiaKeyMask      : 按住列 i → 写列 (i+1) mod N 的 bit
//   回放 InputAndReplayPlayback: bit i → 作用到列 (i-1) mod N
//   物件侧 RemapManiaColumn   : Random ? RandomColumnMap[c] : (right ? c-1 : c),
//                               然后 Mirror: N-1-该值
//   闸门 ManiaStage.Obf020598 = 阶段列数 > 4 且为偶数 且 SpecialStyle == Right
//
// 本文件原有断言逐条保留 (它们是 archive 反推的编码语义)。新增的最后一段把该
// 语义接到**源码忠实引擎**上 (mania/source_engine.h): 引擎的 applyReplayFrame 已经
// 实现了与 GetManiaKeyMask 互逆的位序, 且 remapManiaColumn 与客户端同源 ⇒
// 原始 .osr 帧 + 引擎本来就是正确的, 本模块的 ρ 表 (Mirror=2 / RD=1) 只是旧
// 分析管线 ("bit c == 解析列 c") 的补偿 —— 对引擎开着 ρ 会把按下搬到错误的列。
#include "mania/source_engine.h"
#include "replay/replay_frames.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace se = mra::mania_src;


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

using namespace mra;

namespace {

// ---- 真代码的两侧映射 (纯函数, 便于交叉验证) --------------------------------

// ManiaHitObjectManager.RemapManiaColumn (IL 0x060049CA) 的忠实翻译。
// specialRight = 触发条件第二半 (ManiaStage.Obf020598: 列数>4 且偶数 且
// SpecialStyle==Right)。注意 Random 分支优先, 右特殊键的 -1 位移只在
// 不带 RD 时生效 (真代码是 else if)。
int remapManiaColumn(int column, int keyCount, bool randomMod, bool mirrorMod,
                     const std::vector<int>& randomColumnMap,
                     bool specialRight = true) {
    int n = column;
    if (randomMod) {
        n = randomColumnMap[static_cast<size_t>(column)];
    } else if (specialRight && keyCount > 4 && (keyCount % 2) == 0) {
        n = (column != 0) ? column - 1 : keyCount - 1;
    }
    if (mirrorMod) n = keyCount - 1 - n;
    return n;
}

// 一个物件 (解析列 c) 在真代码里最终被哪个 bit 表示。
int officialBitForNote(int column, int keyCount, bool randomMod, bool mirrorMod,
                       const std::vector<int>& randomColumnMap,
                       bool specialRight = true) {
    const int stageCol = remapManiaColumn(column, keyCount, randomMod, mirrorMod,
                                          randomColumnMap, specialRight);
    // GetManiaKeyMask: 按住 stage 列 s → 记 bit (s+1) mod N (仅右特殊键偶数布局)。
    const bool evenKeyRight =
        specialRight && keyCount > 4 && (keyCount % 2) == 0;
    return evenKeyRight ? ((stageCol + 1) % keyCount) : stageCol;
}

ReplayFrame makeFrame(int64_t t, uint32_t keys, uint32_t down, uint32_t up) {
    ReplayFrame f;
    f.timeMs = t;
    f.rawKeys = keys;
    f.keyDownMask = down;
    f.keyUpMask = up;
    return f;
}

// 由 rawKeys 序列重建 keyDown/keyUp, 与 decodeReplayFrames 同口径。
std::vector<ReplayFrame> buildFrames(const std::vector<uint32_t>& masks,
                                     int64_t t0 = 1000, int64_t step = 20) {
    std::vector<ReplayFrame> frames;
    uint32_t prev = 0;
    int64_t t = t0;
    for (uint32_t m : masks) {
        frames.push_back(makeFrame(t, m, m & ~prev, ~m & prev));
        prev = m;
        t += step;
    }
    return frames;
}

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value == nullptr ? "" : value);
#else
    if (value == nullptr)
        unsetenv(name);
    else
        setenv(name, value, 1);
#endif
}

}  // namespace

// ── 1. 闸门: 只有“偶数且 > 4”的键数才可能旋转 ────────────────────────────
static void testGate() {
    const int never[] = {1, 2, 3, 4, 5, 7, 9, 11};
    for (int keys : never) {
        for (int m = 0; m < 4; ++m) {
            const bool rd = (m & 1) != 0;
            const bool mir = (m & 2) != 0;
            CHECK_EQ(evenKeyRightLaneBitRotation(keys, rd, mir, -1), 0);
        }
    }
    for (int keys : {6, 8, 10}) {
        CHECK_EQ(evenKeyRightLaneBitRotation(keys, false, false, -1), 0);  // NoMod
        CHECK_EQ(evenKeyRightLaneBitRotation(keys, false, true, -1), 2);   // Mirror
        CHECK_EQ(evenKeyRightLaneBitRotation(keys, true, false, -1), 1);   // RD
        CHECK_EQ(evenKeyRightLaneBitRotation(keys, true, true, -1), 1);    // RD+Mirror
    }
    // 强制值覆盖自动表, 并对 keyCount 取模。
    CHECK_EQ(evenKeyRightLaneBitRotation(6, false, true, 0), 0);
    CHECK_EQ(evenKeyRightLaneBitRotation(6, false, true, 1), 1);
    CHECK_EQ(evenKeyRightLaneBitRotation(6, false, true, 3), 3);
    CHECK_EQ(evenKeyRightLaneBitRotation(6, false, false, 6), 0);
    CHECK_EQ(evenKeyRightLaneBitRotation(6, false, false, 7), 1);
    // 强制也不能把 4K/7K 拉进来 (红线: 4K/7K 零影响)。
    CHECK_EQ(evenKeyRightLaneBitRotation(7, false, false, 2), 0);
    CHECK_EQ(evenKeyRightLaneBitRotation(4, true, true, 3), 0);
}

// ── 2. 旋转原语: 低 N 位循环平移, 高位杂位原样保留 ────────────────────────
static void testRotatePrimitive() {
    auto rotated = [](uint32_t mask, int keys, int rotation) {
        std::vector<ReplayFrame> frames = buildFrames({mask});
        applyEvenKeyLaneBitOrder(frames, keys, false, false,
                                 EvenKeyLaneOrder{true, rotation, 0});
        return frames[0].rawKeys;
    };
    CHECK_EQ(rotated(0x1u, 6, 1), 0x20u);   // 列 0 的 bit 平移到 bit 5
    CHECK_EQ(rotated(0x20u, 6, 0), 0x20u);  // ρ=0 不动
    CHECK_EQ(rotated(0x3Fu, 6, 2), 0x3Fu);  // 全 1 旋转后仍是全 1
    CHECK_EQ(rotated(0b101010u, 6, 1), 0b010101u);
    CHECK_EQ(rotated(0x3Fu, 8, 3), 0xE7u);  // 0b00111111 循环右移 3 → 0b11100111
    // 高位杂位 (bit 20) 不动, 只旋转低 6 位。
    CHECK_EQ(rotated((1u << 20) | 0x1u, 6, 1), (1u << 20) | 0x20u);
    // ρ 与 N-ρ 互为逆。
    std::vector<ReplayFrame> fwd = buildFrames({0x2Du});
    applyEvenKeyLaneBitOrder(fwd, 6, false, false, EvenKeyLaneOrder{true, 1, 0});
    applyEvenKeyLaneBitOrder(fwd, 6, false, false, EvenKeyLaneOrder{true, 5, 0});
    CHECK_EQ(fwd[0].rawKeys, 0x2Du);
    // 掩码同步: keyDown/keyUp 与 rawKeys 的位序一致。
    std::vector<ReplayFrame> edge = buildFrames({0x1u, 0x3u, 0x0u});
    applyEvenKeyLaneBitOrder(edge, 6, false, false, EvenKeyLaneOrder{true, 1, 0});
    CHECK_EQ(edge[0].rawKeys, 0x20u);
    CHECK_EQ(edge[0].keyDownMask, 0x20u);
    CHECK_EQ(edge[1].keyDownMask, 0x1u);   // 列 1 新按下 -> bit 0 (原 bit 1 右移 1)
    CHECK_EQ(edge[2].keyUpMask, 0x21u);    // 释放列 0+1 -> bit 5 + bit 0
}

// ── 3. 官方 bit 顺序 vs 旋转后 (Mirror, 无 RD) ───────────────────────────
static void testMirrorOfficialVsRotated() {
    const int N = 6;
    std::vector<int> noMap(static_cast<size_t>(N), 0);
    for (int c = 0; c < N; ++c) {
        const int officialBit =
            officialBitForNote(c, N, /*randomMod=*/false, /*mirrorMod=*/true,
                               noMap);
        // 官方: bit = (1 - c) mod 6 → 1,0,5,4,3,2
        CHECK_EQ(officialBit, ((1 - c) % N + N) % N);
        std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
        const int rot = applyEvenKeyLaneBitOrder(
            frames, N, /*randomMod=*/false, /*mirrorMod=*/true,
            EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight});
        CHECK_EQ(rot, 2);
        // 旋转后 bit 必须等于我方物件侧列号 N-1-c (mirrorColumns)。
        const uint32_t expect = 1u << (N - 1 - c);
        CHECK_EQ(frames[0].rawKeys, expect);
        CHECK_EQ(frames[0].keyDownMask, expect);
    }
}

// ── 4. 官方 bit 顺序 vs 旋转后 (RD) ───────────────────────────────────────
static void testRandomOfficialVsRotated() {
    const int N = 6;
    const std::vector<int> stableMap = {5, 0, 3, 1, 4, 2};  // map[0] = N-1
    bool seen[6] = {false, false, false, false, false, false};
    for (int v : stableMap) {
        CHECK(v >= 0 && v < N && !seen[v]);
        if (v >= 0 && v < N) seen[v] = true;
    }
    for (int c = 0; c < N; ++c) {
        const int officialBit =
            officialBitForNote(c, N, /*randomMod=*/true, /*mirrorMod=*/false,
                               stableMap);
        CHECK_EQ(officialBit, (stableMap[c] + 1) % N);
        std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
        const int rot = applyEvenKeyLaneBitOrder(
            frames, N, /*randomMod=*/true, /*mirrorMod=*/false,
            EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight});
        CHECK_EQ(rot, 1);
        CHECK_EQ(frames[0].rawKeys, 1u << stableMap[c]);
    }
    // RD + Mirror: bit = (-map[c]) mod N, ρ 仍为 1, 旋转后 = N-1-map[c]。
    for (int c = 0; c < N; ++c) {
        const int officialBit =
            officialBitForNote(c, N, true, true, stableMap);
        CHECK_EQ(officialBit, ((-stableMap[c]) % N + N) % N);
        std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
        applyEvenKeyLaneBitOrder(frames, N, true, true, EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight});
        CHECK_EQ(frames[0].rawKeys, 1u << (N - 1 - stableMap[c]));
    }
}

// ── 5. NoMod: 真代码两次位移相消 → ρ=0, 帧一位不改 ───────────────────────
static void testNoModIsIdentity() {
    const int N = 6;
    std::vector<int> noMap(static_cast<size_t>(N), 0);
    for (int c = 0; c < N; ++c) {
        const int officialBit = officialBitForNote(c, N, false, false, noMap);
        CHECK_EQ(officialBit, c);  // 官方 bit == 解析列 → 我方现状本来就对
        std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
        const int rot = applyEvenKeyLaneBitOrder(frames, N, false, false,
                                                 EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight});
        CHECK_EQ(rot, 0);
        CHECK_EQ(frames[0].rawKeys, 1u << c);
    }
}

// ── 6. 4K/7K 零影响: 同样的帧集合逐位不变 ─────────────────────────────────
static void testNoChangeFor4K7K() {
    const std::vector<uint32_t> masks = {0x1, 0x3, 0x2, 0x6, 0x0, 0x40, 0x60, 0x20,
                                         0x0, 0x0F, 0x08, 0x0, 0x7F, 0x01, 0x0};
    for (int keys : {4, 5, 7, 9}) {
        for (int m = 0; m < 4; ++m) {
            const bool rd = (m & 1) != 0;
            const bool mir = (m & 2) != 0;
            std::vector<ReplayFrame> frames = buildFrames(masks);
            const std::vector<ReplayFrame> before = frames;
            EvenKeyLaneOrder cfg;
            cfg.enabled = true;
            const int rot = applyEvenKeyLaneBitOrder(frames, keys, rd, mir, cfg);
            CHECK_EQ(rot, 0);
            CHECK_EQ(frames.size(), before.size());
            for (size_t i = 0; i < frames.size(); ++i) {
                CHECK_EQ(frames[i].rawKeys, before[i].rawKeys);
                CHECK_EQ(frames[i].keyDownMask, before[i].keyDownMask);
                CHECK_EQ(frames[i].keyUpMask, before[i].keyUpMask);
                CHECK_EQ(frames[i].timeMs, before[i].timeMs);
            }
        }
    }
}

// ── 7. 默认 OFF: env 未设时不改任何帧 ────────────────────────────────────
static void testOffByDefault() {
    setEnv("MRA_RE_EVENKEY", nullptr);
    setEnv("MRA_RE_EVENKEY_KEYS", nullptr);
    setEnv("MRA_RE_SPECIAL_RIGHT", nullptr);
    setEnv("MRA_RE_SPECIAL_STYLE", nullptr);
    setEnv("MRA_RE_SKIN_INI", nullptr);
    const EvenKeyLaneOrder cfg = evenKeyLaneOrderFromEnv();
    CHECK(!cfg.enabled);
    const std::vector<uint32_t> masks = {0x2A, 0x15, 0x3F, 0x00};
    std::vector<ReplayFrame> frames = buildFrames(masks);
    EvenKeyLaneOrder off;  // 默认构造 = OFF
    CHECK_EQ(applyEvenKeyLaneBitOrder(frames, 6, true, true, off), 0);
    for (size_t i = 0; i < frames.size(); ++i)
        CHECK_EQ(frames[i].rawKeys, masks[i]);
    CHECK_EQ(applyEvenKeyLaneBitOrder(frames, 6, false, true, off), 0);
    CHECK_EQ(frames[0].rawKeys, masks[0]);
}

// ── 8. env 解析 ──────────────────────────────────────────────────────────
static void testEnvParsing() {
    setEnv("MRA_RE_EVENKEY_KEYS", nullptr);
    setEnv("MRA_RE_SPECIAL_RIGHT", nullptr);
    setEnv("MRA_RE_SPECIAL_STYLE", nullptr);
    setEnv("MRA_RE_SKIN_INI", nullptr);
    setEnv("MRA_RE_EVENKEY", "0");
    CHECK(!evenKeyLaneOrderFromEnv().enabled);
    setEnv("MRA_RE_EVENKEY", "off");
    CHECK(!evenKeyLaneOrderFromEnv().enabled);
    setEnv("MRA_RE_EVENKEY", "");
    CHECK(!evenKeyLaneOrderFromEnv().enabled);
    setEnv("MRA_RE_EVENKEY", "1");
    {
        const EvenKeyLaneOrder c = evenKeyLaneOrderFromEnv();
        CHECK(c.enabled);
        CHECK_EQ(c.forcedRotation, -1);
    }
    setEnv("MRA_RE_EVENKEY", "auto");
    CHECK_EQ(evenKeyLaneOrderFromEnv().forcedRotation, -1);
    setEnv("MRA_RE_EVENKEY", "stage");
    CHECK_EQ(evenKeyLaneOrderFromEnv().forcedRotation, 1);
    setEnv("MRA_RE_EVENKEY", "mirror");
    CHECK_EQ(evenKeyLaneOrderFromEnv().forcedRotation, 2);
    setEnv("MRA_RE_EVENKEY", "3");
    CHECK_EQ(evenKeyLaneOrderFromEnv().forcedRotation, 3);
    setEnv("MRA_RE_EVENKEY", "garbage");
    CHECK(evenKeyLaneOrderFromEnv().enabled);
    CHECK_EQ(evenKeyLaneOrderFromEnv().forcedRotation, -1);

    // SpecialStyle (触发条件第二半): 默认 unknown → auto 查表不旋转。
    setEnv("MRA_RE_EVENKEY", "1");
    {
        const EvenKeyLaneOrder c = evenKeyLaneOrderFromEnv();
        CHECK_EQ(c.specialStyle, kManiaSpecialStyleUnknown);
        CHECK(c.specialStyleSource.empty());
        std::vector<ReplayFrame> frames = buildFrames({0x1});
        CHECK_EQ(applyEvenKeyLaneBitOrder(frames, 6, false, true, c), 0);
        CHECK_EQ(frames[0].rawKeys, 0x1u);
    }
    setEnv("MRA_RE_SPECIAL_RIGHT", "1");
    {
        const EvenKeyLaneOrder c = evenKeyLaneOrderFromEnv();
        CHECK_EQ(c.specialStyle, kManiaSpecialStyleRight);
        CHECK(c.specialStyleSource == "env");
        std::vector<ReplayFrame> frames = buildFrames({0x1});
        CHECK_EQ(applyEvenKeyLaneBitOrder(frames, 6, false, true, c), 2);
    }
    setEnv("MRA_RE_SPECIAL_RIGHT", "0");
    CHECK_EQ(evenKeyLaneOrderFromEnv().specialStyle, kManiaSpecialStyleNone);
    setEnv("MRA_RE_SPECIAL_RIGHT", "left");
    CHECK_EQ(evenKeyLaneOrderFromEnv().specialStyle, kManiaSpecialStyleNone);
    setEnv("MRA_RE_SPECIAL_RIGHT", nullptr);
    setEnv("MRA_RE_SPECIAL_STYLE", "left");
    CHECK_EQ(evenKeyLaneOrderFromEnv().specialStyle, kManiaSpecialStyleLeft);
    setEnv("MRA_RE_SPECIAL_STYLE", "2");  // 数字 2 = Right (skin.ini 口径)
    CHECK_EQ(evenKeyLaneOrderFromEnv().specialStyle, kManiaSpecialStyleRight);
    setEnv("MRA_RE_SPECIAL_STYLE", "none");
    CHECK_EQ(evenKeyLaneOrderFromEnv().specialStyle, kManiaSpecialStyleNone);
    setEnv("MRA_RE_SPECIAL_STYLE", "garbage");
    CHECK_EQ(evenKeyLaneOrderFromEnv().specialStyle, kManiaSpecialStyleUnknown);
    // MRA_RE_SPECIAL_STYLE 优先于 MRA_RE_SPECIAL_RIGHT。
    setEnv("MRA_RE_SPECIAL_RIGHT", "1");
    setEnv("MRA_RE_SPECIAL_STYLE", "left");
    CHECK_EQ(evenKeyLaneOrderFromEnv().specialStyle, kManiaSpecialStyleLeft);
    setEnv("MRA_RE_SPECIAL_STYLE", nullptr);
    setEnv("MRA_RE_SPECIAL_RIGHT", nullptr);

    // 键数过滤: 只对 6 生效 (SpecialStyle 必须确证在右, 否则 auto 不动)。
    setEnv("MRA_RE_EVENKEY", "1");
    setEnv("MRA_RE_EVENKEY_KEYS", "6");
    setEnv("MRA_RE_SPECIAL_RIGHT", "1");
    const EvenKeyLaneOrder cfg = evenKeyLaneOrderFromEnv();
    CHECK_EQ(cfg.keyCountFilter, 6);
    std::vector<ReplayFrame> six = buildFrames({0x1});
    CHECK_EQ(applyEvenKeyLaneBitOrder(six, 6, false, true, cfg), 2);
    std::vector<ReplayFrame> eight = buildFrames({0x1});
    CHECK_EQ(applyEvenKeyLaneBitOrder(eight, 8, false, true, cfg), 0);
    CHECK_EQ(eight[0].rawKeys, 0x1u);
    setEnv("MRA_RE_EVENKEY", nullptr);
    setEnv("MRA_RE_EVENKEY_KEYS", nullptr);
    setEnv("MRA_RE_SPECIAL_RIGHT", nullptr);
    setEnv("MRA_RE_SPECIAL_STYLE", nullptr);
    setEnv("MRA_RE_SKIN_INI", nullptr);
}

// ── 9. 解码链路端到端 (合成 6K Mirror 帧流) ──────────────────────────────
static void testDecodedStream() {
    const int officialBits[6] = {1, 0, 5, 4, 3, 2};  // 6K Mirror 的官方 bit 顺序
    std::string text;
    auto push = [&](int64_t dt, uint32_t mask) {
        text += std::to_string(dt) + "|" + std::to_string(mask) + "|0,";
    };
    for (int c = 0; c < 6; ++c) {
        push(100, 1u << officialBits[c]);
        push(50, 0);
    }
    text += "-12345|0|0|424242,";
    text += "0|0|0,";
    std::vector<uint8_t> bytes(text.begin(), text.end());
    ReplayFrameDecodeResult decoded = decodeReplayFrames(bytes);
    CHECK(decoded.ok);
    CHECK_EQ(decoded.frames.size(), 13);  // 12 play frames + 1 seed frame
    bool sawSeed = false;
    for (const ReplayFrame& f : decoded.frames)
        if (f.isSeedFrame) sawSeed = true;
    CHECK(sawSeed);

    std::vector<ReplayFrame> frames = decoded.frames;
    const int rot = applyEvenKeyLaneBitOrder(frames, 6, /*randomMod=*/false,
                                             /*mirrorMod=*/true,
                                             EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight});
    CHECK_EQ(rot, 2);
    int pressIdx = 0;
    for (const ReplayFrame& f : frames) {
        if (f.isSeedFrame) continue;
        if (f.keyDownMask != 0) {
            const uint32_t expect = 1u << (5 - pressIdx);  // N-1-c
            CHECK_EQ(f.rawKeys, expect);
            CHECK_EQ(f.keyDownMask, expect);
            ++pressIdx;
        } else {
            CHECK_EQ(f.rawKeys, 0u);
            CHECK_EQ(f.keyUpMask, 1u << (5 - (pressIdx - 1)));
        }
    }
    CHECK_EQ(pressIdx, 6);
    for (const ReplayFrame& f : frames) {
        if (!f.isSeedFrame) continue;
        CHECK_EQ(f.keyDownMask, 0u);
        CHECK_EQ(f.keyUpMask, 0u);
        CHECK_EQ(f.seed, 424242u);
    }
}

// ── 10. SpecialStyle 闸门: 只有确证在右才自动查表 ────────────────────────
static void testSpecialStyleGate() {
    const std::vector<uint32_t> masks = {0x1, 0x3, 0x2, 0x0, 0x2D, 0x0};
    const int notRight[] = {kManiaSpecialStyleUnknown, kManiaSpecialStyleNone,
                            kManiaSpecialStyleLeft};
    for (int style : notRight) {
        for (int m = 0; m < 4; ++m) {
            const bool rd = (m & 1) != 0;
            const bool mir = (m & 2) != 0;
            for (int keys : {6, 8, 10}) {
                std::vector<ReplayFrame> frames = buildFrames(masks);
                const std::vector<ReplayFrame> before = frames;
                EvenKeyLaneOrder cfg;
                cfg.enabled = true;
                cfg.specialStyle = style;
                CHECK_EQ(applyEvenKeyLaneBitOrder(frames, keys, rd, mir, cfg), 0);
                for (size_t i = 0; i < frames.size(); ++i) {
                    CHECK_EQ(frames[i].rawKeys, before[i].rawKeys);
                    CHECK_EQ(frames[i].keyDownMask, before[i].keyDownMask);
                    CHECK_EQ(frames[i].keyUpMask, before[i].keyUpMask);
                }
            }
        }
        // forced (人工探针) 模式跳过这条闸门, 照给定值旋转。
        std::vector<ReplayFrame> forced = buildFrames({0x1});
        EvenKeyLaneOrder cfg;
        cfg.enabled = true;
        cfg.specialStyle = style;
        cfg.forcedRotation = 2;
        CHECK_EQ(applyEvenKeyLaneBitOrder(forced, 6, false, false, cfg), 2);
        CHECK_EQ(forced[0].rawKeys, 1u << 4);
    }
    // 确证在右, 但 4K/5K/7K/9K 依旧不动 (红线: 4K/7K 零影响)。
    EvenKeyLaneOrder right;
    right.enabled = true;
    right.specialStyle = kManiaSpecialStyleRight;
    CHECK(evenKeySpecialStyleIsRight(right));
    for (int keys : {4, 5, 7, 9}) {
        std::vector<ReplayFrame> frames = buildFrames(masks);
        const std::vector<ReplayFrame> before = frames;
        CHECK_EQ(applyEvenKeyLaneBitOrder(frames, keys, false, true, right), 0);
        for (size_t i = 0; i < frames.size(); ++i)
            CHECK_EQ(frames[i].rawKeys, before[i].rawKeys);
    }
    CHECK(!evenKeySpecialStyleIsRight(EvenKeyLaneOrder{}));
}

// ── 11. 8K: NoMod / Mirror / RD / RD+Mirror 全表 ─────────────────────────
static void testEightKey() {
    const int N = 8;
    const std::vector<int> noMap(static_cast<size_t>(N), 0);
    for (int c = 0; c < N; ++c)
        CHECK_EQ(officialBitForNote(c, N, false, false, noMap), c);  // 相消
    // Mirror: 官方 bit = (1-c) mod 8, ρ=2 → 旋转后 = 7-c (我方 mirrorColumns)。
    for (int c = 0; c < N; ++c) {
        const int officialBit = officialBitForNote(c, N, false, true, noMap);
        CHECK_EQ(officialBit, ((1 - c) % N + N) % N);
        std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
        CHECK_EQ(applyEvenKeyLaneBitOrder(
                     frames, N, false, true,
                     EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight}),
                 2);
        CHECK_EQ(frames[0].rawKeys, 1u << (N - 1 - c));
    }
    // RD: stable 的 RandomColumnMap[0] 被钉在 N-1 (偶数键首列不抽随机数)。
    const std::vector<int> stableMap = {7, 0, 5, 2, 6, 1, 4, 3};
    {
        bool seen[N] = {false, false, false, false, false, false, false, false};
        for (int v : stableMap) {
            CHECK(v >= 0 && v < N && !seen[v]);
            if (v >= 0 && v < N) seen[v] = true;
        }
        CHECK_EQ(stableMap[0], N - 1);
    }
    for (int c = 0; c < N; ++c) {
        const int officialBit = officialBitForNote(c, N, true, false, stableMap);
        CHECK_EQ(officialBit, (stableMap[c] + 1) % N);
        std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
        CHECK_EQ(applyEvenKeyLaneBitOrder(
                     frames, N, true, false,
                     EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight}),
                 1);
        CHECK_EQ(frames[0].rawKeys, 1u << stableMap[c]);
    }
    // RD + Mirror: ρ 仍为 1, 旋转后 = N-1-map[c]。
    for (int c = 0; c < N; ++c) {
        const int officialBit = officialBitForNote(c, N, true, true, stableMap);
        CHECK_EQ(officialBit, ((-stableMap[c]) % N + N) % N);
        std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
        CHECK_EQ(applyEvenKeyLaneBitOrder(
                     frames, N, true, true,
                     EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight}),
                 1);
        CHECK_EQ(frames[0].rawKeys, 1u << (N - 1 - stableMap[c]));
    }
}

// ── 12. 特殊键在左/无: 两侧都不位移 → 官方 bit == 解析列, 不旋转 ──────────
static void testSpecialStyleNotRightIdentity() {
    for (int N : {6, 8}) {
        // 偶数键的 RD 首列也不抽随机数 (Obf020597 只看键数, 与 SpecialStyle 无关),
        // 这里用一个 map[0] == 0 的置换来复刻该点。
        std::vector<int> map(static_cast<size_t>(N), 0);
        int lo = 1, hi = N - 1;
        for (int i = 1; i < N; ++i) map[static_cast<size_t>(i)] = ((i & 1) ? hi-- : lo++);
        for (int m = 0; m < 4; ++m) {
            const bool rd = (m & 1) != 0;
            const bool mir = (m & 2) != 0;
            for (int c = 0; c < N; ++c) {
                // specialRight = false: 真代码两侧都不做 ±1 位移。
                const int officialBit = officialBitForNote(c, N, rd, mir, map,
                                                           /*specialRight=*/false);
                int expect = rd ? map[static_cast<size_t>(c)] : c;
                if (mir) expect = N - 1 - expect;
                CHECK_EQ(officialBit, expect);
                std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
                EvenKeyLaneOrder cfg;
                cfg.enabled = true;
                cfg.specialStyle = kManiaSpecialStyleLeft;
                CHECK_EQ(applyEvenKeyLaneBitOrder(frames, N, rd, mir, cfg), 0);
                CHECK_EQ(frames[0].rawKeys, 1u << expect);
            }
        }
    }
}

// ── 13. skin.ini 来源 ────────────────────────────────────────────────────
// 夹具落在 <cwd>/build/tmp_accupdate/evenkey_skin/<name>/skin.ini (可写区);
// 若拿不到 cwd 再退回系统临时目录。
static std::string writeSkinIni(const char* name, const std::string& body) {
    std::error_code ec;
    std::filesystem::path base = std::filesystem::current_path(ec);
    if (ec || base.empty()) base = std::filesystem::temp_directory_path(ec);
    std::filesystem::path dir =
        base / "build" / "tmp_accupdate" / "evenkey_skin" / name;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / "skin.ini", std::ios::binary);
    out << body;
    out.close();
    if (!out) std::printf("WARN skin.ini fixture not written: %s\n",
                          dir.string().c_str());
    return dir.string();
}

static void testSkinIni() {
    // 注意 [General] 里同名的键必须被忽略 (只认 [Mania] 段)。
    const std::string right = writeSkinIni(
        "right",
        "; fixture\n[General]\nName: t\nSpecialStyle: 0\n\n[Mania]\nKeys: 6\n"
        "SpecialStyle: 2\n");
    const std::string left = writeSkinIni("left", "[Mania]\nSpecialStyle: 1\n");
    const std::string absent = writeSkinIni("absent", "[Mania]\nKeys: 8\n");
    const std::string wrongSection =
        writeSkinIni("wrongsection", "[General]\nSpecialStyle: 2\n");

    CHECK_EQ(readManiaSpecialStyleFromSkinIni(right), kManiaSpecialStyleRight);
    CHECK_EQ(readManiaSpecialStyleFromSkinIni(left), kManiaSpecialStyleLeft);
    CHECK_EQ(readManiaSpecialStyleFromSkinIni(absent), kManiaSpecialStyleUnknown);
    CHECK_EQ(readManiaSpecialStyleFromSkinIni(wrongSection), kManiaSpecialStyleUnknown);
    CHECK_EQ(readManiaSpecialStyleFromSkinIni(""), kManiaSpecialStyleUnknown);
    CHECK_EQ(readManiaSpecialStyleFromSkinIni("/no/such/skin/folder"),
             kManiaSpecialStyleUnknown);
    // 直接给 skin.ini 文件也认。
    CHECK_EQ(readManiaSpecialStyleFromSkinIni(right + "/skin.ini"),
             kManiaSpecialStyleRight);

    // env 接线: MRA_RE_SKIN_INI
    setEnv("MRA_RE_EVENKEY", "1");
    setEnv("MRA_RE_SPECIAL_RIGHT", nullptr);
    setEnv("MRA_RE_SPECIAL_STYLE", nullptr);
    setEnv("MRA_RE_SKIN_INI", right.c_str());
    {
        const EvenKeyLaneOrder c = evenKeyLaneOrderFromEnv();
        CHECK_EQ(c.specialStyle, kManiaSpecialStyleRight);
        CHECK(c.specialStyleSource == "skin.ini");
        std::vector<ReplayFrame> frames = buildFrames({0x2});
        CHECK_EQ(applyEvenKeyLaneBitOrder(frames, 6, false, true, c), 2);
    }
    setEnv("MRA_RE_SKIN_INI", nullptr);
    // 调用方给 skinFolder (api_server 的 /api/replays/load 字段)
    {
        const EvenKeyLaneOrder c = evenKeyLaneOrderFromEnv(left);
        CHECK_EQ(c.specialStyle, kManiaSpecialStyleLeft);
        CHECK(c.specialStyleSource == "skin.ini");
        std::vector<ReplayFrame> frames = buildFrames({0x2});
        CHECK_EQ(applyEvenKeyLaneBitOrder(frames, 6, false, true, c), 0);
    }
    // env 显式声明优先于 skin.ini。
    setEnv("MRA_RE_SPECIAL_RIGHT", "1");
    CHECK_EQ(evenKeyLaneOrderFromEnv(left).specialStyle, kManiaSpecialStyleRight);
    setEnv("MRA_RE_SPECIAL_RIGHT", nullptr);
    setEnv("MRA_RE_EVENKEY", nullptr);
}

// ── 14. 接到源码忠实引擎: 客户端编码 == 引擎输入写入的逆 (ρ 必须为 0) ─────
//
// 客户端:  帧 bit = (RemapManiaColumn(c) + 1) mod N      (GetManiaKeyMask)
// 引擎:    applyReplayFrame: bit i → 列 (i-1) mod N       (Obf014317, 特殊键在右)
//          ManiaNote::.ctor: 列 = RemapManiaColumn(c)     (0x060049CA)
// ⇒ 组合恒等。本段对每个 mod 组合逐列验证, 并证明本模块的 ρ(≠0) 会破坏组合。
namespace {

struct LaneStage {
    se::ManiaStage stage;
    std::unique_ptr<se::ManiaHitObjectManager> manager;
    std::vector<int> held() const {
        std::vector<int> out;
        for (size_t i = 0; i < stage.columns.size(); ++i)
            if (stage.columns[i].isHeld) out.push_back(static_cast<int>(i));
        return out;
    }
};

// 组装一个 N 列、特殊键在右的 stage + 管理器; randomMap 非空时作为 RandomColumnMap。
void setupStage(LaneStage& s, int keyCount, bool specialRight,
                const std::vector<int>& randomMap, uint32_t mods) {
    s.stage.columns.resize(static_cast<size_t>(keyCount));
    for (int i = 0; i < keyCount; ++i) s.stage.columns[static_cast<size_t>(i)].index = i;
    s.stage.specialStyleRight = specialRight;
    s.stage.hasSkin = true;
    s.manager = std::make_unique<se::ManiaHitObjectManager>(
        s.stage, nullptr, static_cast<int32_t>(mods), 0u);
    // 客户端与本端必须用同一个置换 (同一 .osr 种子 ⇒ 同一 .NET Random 序列)。
    if (!randomMap.empty()) s.manager->randomColumnMap = randomMap;
}

}  // namespace

static void testEngineCompositionIdentity() {
    const uint32_t kRd = 0x200000u;      // Random
    const uint32_t kMirror = 0x40000000u;
    for (int N : {6, 8}) {
        std::vector<int> stableMap(static_cast<size_t>(N), 0);
        // 偶数键首列不抽随机数: RandomColumnMap[0] = N-1 (特殊键在右)。
        stableMap[0] = N - 1;
        int lo = 0, hi = N - 2;
        for (int i = 1; i < N; ++i) {
            stableMap[static_cast<size_t>(i)] = ((i & 1) ? hi-- : lo++);
        }
        const struct {
            uint32_t mods;
            bool rd;
            bool mir;
        } combos[] = {{0u, false, false},
                      {kMirror, false, true},
                      {kRd, true, false},
                      {kRd | kMirror, true, true}};
        for (const auto& combo : combos) {
            for (int c = 0; c < N; ++c) {
                LaneStage s;
                setupStage(s, N, /*specialRight=*/true,
                           combo.rd ? stableMap : std::vector<int>(), combo.mods);
                // 算术侧的期望 (与被测映射函数相互独立): 客户端 bit 与引擎列。
                const int engineCol = remapManiaColumn(c, N, combo.rd, combo.mir,
                                                       stableMap, true);
                CHECK_EQ(s.manager->remapManiaColumn(c), engineCol);
                const uint32_t officialBit =
                    static_cast<uint32_t>((engineCol + 1) % N);
                // 原始 .osr 帧 (未施加任何 ρ) 必须落到物件自己的列。
                se::applyReplayFrame(*s.manager, 0,
                                     static_cast<int32_t>(1u << officialBit));
                const std::vector<int> got = s.held();
                CHECK_EQ(got.size(), 1u);
                if (got.size() == 1) CHECK_EQ(got[0], engineCol);

                // 用本模块的 ρ 表再转一次 —— 非 0 就必然落到**别的**列。
                const int rho = evenKeyRightLaneBitRotation(
                    N, combo.rd, combo.mir, -1);
                CHECK_EQ(rho, combo.rd ? 1 : (combo.mir ? 2 : 0));
                std::vector<ReplayFrame> frames = buildFrames({1u << officialBit});
                const int applied = applyEvenKeyLaneBitOrder(
                    frames, N, combo.rd, combo.mir,
                    EvenKeyLaneOrder{true, -1, 0, kManiaSpecialStyleRight});
                CHECK_EQ(applied, rho);
                se::applyReplayFrame(*s.manager, 1,
                                     static_cast<int32_t>(frames[0].rawKeys));
                const std::vector<int> gotRot = s.held();
                CHECK_EQ(gotRot.size(), 1u);
                if (gotRot.size() == 1) {
                    if (rho == 0) {
                        CHECK_EQ(gotRot[0], engineCol);       // NoMod: 相消
                    } else {
                        CHECK(gotRot[0] != engineCol);        // 有 ρ ⇒ 错列
                    }
                }
            }
        }
    }
    // 非 "偶数键 + 右特殊键" 的布局: 引擎与客户端两侧都不做 ±1 位移,
    // 官方 bit 就是解析列, 本模块也永远给 ρ = 0。
    for (int N : {4, 5, 7, 9}) {
        LaneStage s;
        setupStage(s, N, /*specialRight=*/true, std::vector<int>(), 0u);
        CHECK(!s.stage.specialKeyOnRight());
        for (int c = 0; c < N; ++c) {
            CHECK_EQ(s.manager->remapManiaColumn(c), c);
            se::applyReplayFrame(*s.manager, 0, static_cast<int32_t>(1u << c));
            const std::vector<int> got = s.held();
            CHECK_EQ(got.size(), 1u);
            if (got.size() == 1) CHECK_EQ(got[0], c);
        }
        for (int m = 0; m < 4; ++m) {
            CHECK_EQ(evenKeyRightLaneBitRotation(N, (m & 1) != 0, (m & 2) != 0, -1),
                     0);
        }
    }
}

// ManiaStage.Obf020598 与 evenKeySpecialStyleIsRight 表达的是同一个闸门的
// 两半: 前者 = 键数 > 4 && 偶数 && Skin != null && SpecialStyle == Right;
// 后者只承载 "SpecialStyle == Right" 这一半 (键数闸门在
// evenKeyRightLaneBitRotation 里, 与 SpecialStyle 无关)。
static void testEngineGateMatchesModule() {
    for (int N : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}) {
        const bool evenOver4 = N > 4 && (N % 2) == 0;
        // 本模块的 ρ 表只按键数开闸 (Mirror 需要偶数键 > 4)。
        CHECK_EQ(evenKeyRightLaneBitRotation(N, false, true, -1) != 0, evenOver4);
        CHECK_EQ(evenKeyRightLaneBitRotation(N, true, false, -1) != 0, evenOver4);
        for (int style : {kManiaSpecialStyleUnknown, kManiaSpecialStyleNone,
                          kManiaSpecialStyleLeft, kManiaSpecialStyleRight}) {
            LaneStage s;
            s.stage.columns.resize(static_cast<size_t>(N));
            for (int i = 0; i < N; ++i) s.stage.columns[static_cast<size_t>(i)].index = i;
            s.stage.specialStyleRight = (style == kManiaSpecialStyleRight);
            s.stage.hasSkin = true;
            EvenKeyLaneOrder cfg;
            cfg.enabled = true;
            cfg.specialStyle = style;
            const bool moduleRight = evenKeySpecialStyleIsRight(cfg);
            CHECK_EQ(moduleRight, style == kManiaSpecialStyleRight);
            CHECK_EQ(s.stage.specialKeyOnRight(), moduleRight && evenOver4);
            // Skin == null ⇒ Obf020598 的第二条守卫失败。
            s.stage.hasSkin = false;
            CHECK(!s.stage.specialKeyOnRight());
        }
    }
}

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    } cases[] = {
        {"gate", testGate},
        {"rotatePrimitive", testRotatePrimitive},
        {"mirrorOfficialVsRotated", testMirrorOfficialVsRotated},
        {"randomOfficialVsRotated", testRandomOfficialVsRotated},
        {"noModIsIdentity", testNoModIsIdentity},
        {"noChange4K7K", testNoChangeFor4K7K},
        {"offByDefault", testOffByDefault},
        {"envParsing", testEnvParsing},
        {"specialStyleGate", testSpecialStyleGate},
        {"eightKey", testEightKey},
        {"specialStyleNotRightIdentity", testSpecialStyleNotRightIdentity},
        {"skinIni", testSkinIni},
        {"decodedStream", testDecodedStream},
        {"engineCompositionIdentity", testEngineCompositionIdentity},
        {"engineGateMatchesModule", testEngineGateMatchesModule},
    };
    for (const Case& c : cases) {
        const int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? "ok  " : "FAIL", c.name);
    }
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED (replay lane order)\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
