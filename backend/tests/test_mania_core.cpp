// Unit tests for the source-faithful mania judgement / scoring core.
//
// EVERY expectation below is derived from the port specs, never from the
// implementation (otherwise the tests are just an echo of the code):
//   docs/port-specs/01-input-replay-state.md  — 回放帧 → 按键状态 → 判定调用
//   docs/port-specs/02-judgement-core.md      — 判定窗 / note+LN 状态机 / 仲裁
//   docs/port-specs/03-scoring-acc.md         — counts / acc / grade / score
// Section references in the comments ("spec 02 §4.5") point at the conclusion
// the CHECK pins. Float expectations are written as the spec's own arithmetic
// (`(float)num / (float)den`), so exact `==` comparisons are meaningful.
#include "beatmap/osu_parser.h"
#include "mania/accuracy.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
#include "mania/score_record.h"
#include "mania/source_engine.h"
#include "replay/osr_reader.h"
#include "replay/replay_decoder.h"
#include "replay/replay_frames.h"
#include "scores/scores_db.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        ++g_checks;                                                          \
        const long long _va = (long long)(a);                                \
        const long long _vb = (long long)(b);                                \
        if (_va != _vb) {                                                    \
            std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__,   \
                        __LINE__, #a, #b, _va, _vb);                         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

// Exact single-precision comparison (the source's acc is a float; the expected
// value is computed with the spec's own int->float->float-div path).
#define CHECK_FEQ(a, b)                                                      \
    do {                                                                     \
        ++g_checks;                                                          \
        const float _va = (float)(a);                                        \
        const float _vb = (float)(b);                                        \
        if (!(_va == _vb)) {                                                 \
            std::printf("FAIL %s:%d: %s == %s (%.9g vs %.9g)\n", __FILE__,   \
                        __LINE__, #a, #b, (double)_va, (double)_vb);         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

using namespace mra;
namespace se = mra::mania_src;

// JudgmentFlags (0x0200079D) — spec 02 §4.12.
constexpr int32_t kFMax = judgment_flag::kMax;      // 268435456 (320)
constexpr int32_t kF300 = judgment_flag::k300;      // 134217728
constexpr int32_t kF200 = judgment_flag::k200;      // 67108864
constexpr int32_t kF100 = judgment_flag::k100;      // 33554432
constexpr int32_t kF50 = judgment_flag::k50;        // 16777216
constexpr int32_t kFTick = judgment_flag::kTick;    // 7
constexpr int32_t kFMiss = judgment_flag::kMiss;    // -65536
constexpr int32_t kFBreak = judgment_flag::kBreak;  // -1048576

// Obf001147 grade values (declaration order = value), spec 03 §0.2.
constexpr int kXH = 0, kSH = 1, kX = 2, kS = 3, kA = 4, kB = 5, kC = 6,
              kD = 7, kF = 8;

// Mods bits (spec 03 §0.2).
constexpr uint32_t kNoFail = 0x1u;
constexpr uint32_t kEasy = 0x2u;
constexpr uint32_t kHidden = 0x8u;
constexpr uint32_t kHardRock = 0x10u;
constexpr uint32_t kDoubleTime = 0x40u;
constexpr uint32_t kHalfTime = 0x100u;
constexpr uint32_t kFlashlight = 0x400u;
constexpr uint32_t kSpunOut = 0x1000u;
constexpr uint32_t kPerfectMod = 0x4000u;
constexpr uint32_t kFadeIn = 0x100000u;
constexpr uint32_t kRandomMod = 0x200000u;
constexpr uint32_t kScoreV2Mod = 0x20000000u;
constexpr uint32_t kMirrorMod = 0x40000000u;

// ---------------------------------------------------------------------------
// matcher-level helpers
// ---------------------------------------------------------------------------
static ReplayFrame frame(int64_t t, uint32_t down, uint32_t up) {
    ReplayFrame f;
    f.timeMs = t;
    f.keyDownMask = down;
    f.keyUpMask = up;
    return f;
}

static ManiaHitObject note(int col, int64_t t) {
    ManiaHitObject o;
    o.type = ManiaObjectType::Note;
    o.column = col;
    o.startTimeMs = t;
    return o;
}

static ManiaHitObject hold(int col, int64_t s, int64_t e) {
    ManiaHitObject o;
    o.type = ManiaObjectType::Hold;
    o.column = col;
    o.startTimeMs = s;
    o.endTimeMs = e;
    return o;
}

// ---------------------------------------------------------------------------
// engine rig: drives ManiaHitObjectManager directly (one replay frame == one
// judgement tick, spec 01 §2.3.8 / §2.4.6; the offline host clock is PORTed to
// "now = frame time" in hit_matcher).
// ---------------------------------------------------------------------------
struct Engine {
    se::ManiaStage stage;
    std::unique_ptr<se::ManiaScoreRecord> record;
    std::unique_ptr<se::ManiaHitObjectManager> manager;
    std::vector<std::unique_ptr<se::ManiaNote>> owned;
    std::vector<std::pair<int32_t, se::ManiaNote*>> judgments;
    std::vector<std::pair<int32_t, int>> unmatchedPresses;  // (now, column)

    explicit Engine(int columns, uint32_t mods = 0, bool v2 = false)
        : record(std::make_unique<se::ManiaScoreRecord>(mods, columns, v2)) {
        stage.columns.resize(static_cast<size_t>(columns));
        for (int i = 0; i < columns; ++i) stage.columns[i].index = i;
        manager = std::make_unique<se::ManiaHitObjectManager>(
            stage, record.get(), static_cast<int32_t>(mods), 0u);
        manager->initializeJudgementArrays(0u);
        manager->judgmentCallback = [this](int32_t flags, se::ManiaNote* n) {
            judgments.emplace_back(flags, n);
        };
        manager->unmatchedPressCallback = [this](int32_t when, int column) {
            unmatchedPresses.emplace_back(when, column);
        };
    }

    se::ManiaNote* addRice(int column, int start, int objectType = 1) {
        auto n = se::createManiaNote(*manager, column, start, objectType,
                                     static_cast<int>(owned.size()));
        manager->addObject(n.get());
        owned.push_back(std::move(n));
        return owned.back().get();
    }

    se::ManiaLongNote* addLn(int column, int start, int end) {
        auto n = se::createManiaLongNote(*manager, column, start, end, 128,
                                         static_cast<int>(owned.size()));
        manager->addObject(n.get());
        owned.push_back(std::move(n));
        return static_cast<se::ManiaLongNote*>(owned.back().get());
    }

    // OD8 原生 mania 表的整数窗口（spec 02 §2.6 路径 B）。
    void od8Windows() { setWindows(16, 40, 73, 103, 127, 164); }

    void setWindows(int maxW, int w300, int w200, int w100, int w50,
                    int early) {
        manager->windowMax = maxW;
        manager->window300 = w300;
        manager->window200 = w200;
        manager->window100 = w100;
        manager->window50 = w50;
        manager->earlyInputWindow = early;
    }

    // Obf014317 (g) → Virtual0177 → UpdateInputJudgments
    void tick(int32_t now, int32_t keyMask) {
        se::applyReplayFrame(*manager, now, keyMask);
        manager->virtual0177();
        manager->updateInputJudgments();
    }

    // Obf014317 (h) branch: no frame consumed ⇒ `WasHeld = IsHeld` resync.
    void tickNoFrame(int32_t now) {
        manager->now = now;
        for (se::ManiaColumn& c : stage.columns) c.wasHeld = c.isHeld;
        manager->virtual0177();
        manager->updateInputJudgments();
    }

    bool held(int column) const { return stage.columns[column].isHeld; }
    bool wasHeld(int column) const { return stage.columns[column].wasHeld; }
};

// ===========================================================================
// 1. 判定窗四路径 (spec 02 §2.1 / §2.2 / §2.3 / §2.6)
// ===========================================================================

// 路径 A: ScoreV2 = DifficultyRange(OD, min/mid/max) → AdjustManiaHitWindow。
// 锚点：OD0 ⇒ 22.4/64/97/127/151（conv.i4 后 22/64/97/127/151）；
//       OD5 ⇒ mid；（OD 越高窗口越小）。EarlyInputWindow 裸 conv.i4、不过 mod。
static void testWindowPathA() {
    struct Case {
        double od;
        int max_, w300, w200, w100, w50, early;
    };
    const Case cases[] = {
        {0.0, 22, 64, 97, 127, 151, 188},
        {5.0, 19, 49, 82, 112, 136, 173},
        {8.0, 16, 40, 73, 103, 127, 164},
        {9.0, 15, 37, 70, 100, 124, 161},
        {10.0, 13, 34, 67, 97, 121, 158},
        {7.5, 16, 41, 74, 104, 128, 165},
    };
    for (const Case& c : cases) {
        const se::ManiaWindowSet s = se::computeManiaWindowSet(c.od, 0, true, true);
        CHECK_EQ(s.windowMax, c.max_);
        CHECK_EQ(s.window300, c.w300);
        CHECK_EQ(s.window200, c.w200);
        CHECK_EQ(s.window100, c.w100);
        CHECK_EQ(s.window50, c.w50);
        CHECK_EQ(s.earlyInputWindow, c.early);
    }

    // spec 02 §2.2: `if (!(difficulty <= 5.0))` is the negation of ble.un, so NaN
    // takes the FIRST branch (mid + (max-mid)*(d-5)/5) ⇒ NaN out.
    CHECK(std::isnan(se::difficultyRangeFor(std::nan(""), 22.4, 19.4, 13.9)));
    // spec 02 §2.1: a single conv.i4 at the end; NaN ⇒ cvttsd2si "integer
    // indefinite" (0x80000000, pinned by score_record.h; the spec leaves the
    // conversion itself unspecified but forbids inventing a clamp).
    const se::ManiaWindowSet nanSet =
        se::computeManiaWindowSet(std::nan(""), 0, true, true);
    CHECK_EQ(nanSet.windowMax, INT32_MIN);
    CHECK_EQ(nanSet.window300, INT32_MIN);
    CHECK_EQ(nanSet.window50, INT32_MIN);
    CHECK_EQ(nanSet.earlyInputWindow, INT32_MIN);
    CHECK_EQ(se::adjustManiaHitWindowFor(0, std::nan("")), INT32_MIN);
    // 窗口是 int32（conv.i4 向零截断），不是 double 载体。
    CHECK_EQ(se::dotnetConvI4(16.1), 16);

    // 路径 A 的 EarlyInputWindow 不经过 mod 调整（spec 02 §2.3 关键细节）。
    const se::ManiaWindowSet dtA =
        se::computeManiaWindowSet(0.0, kDoubleTime, true, true);
    CHECK_EQ(dtA.windowMax, 33);       // (int)(22.4 * 1.5)
    CHECK_EQ(dtA.earlyInputWindow, 188);  // 裸 conv.i4(188.0)，未乘 1.5
}

// 路径 B: 原生 mania 谱，全 float。d = Min(10f, Max(0f, 10f - OD))；34f+3f*d …
// 每个窗口再经 AdjustManiaHitWindow；OD 先 conv.r4。
static void testWindowPathB() {
    struct Case {
        double od;
        int max_, w300, w200, w100, w50, early;
    };
    const Case cases[] = {
        {0.0, 16, 64, 97, 127, 151, 188},
        {6.0, 16, 46, 79, 109, 133, 170},
        // 41.5 / 74.5 / 104.5 / 128.5 / 165.5 —— conv.i4 向零截断（四舍五入会 +1）
        {7.5, 16, 41, 74, 104, 128, 165},
        {8.0, 16, 40, 73, 103, 127, 164},
        // float 的 d：10f - 8.4f = 1.6000003814697266 ⇒ 38.80000305175781 → 38
        {8.4, 16, 38, 71, 101, 125, 162},
        {10.0, 16, 34, 67, 97, 121, 158},
        // d 被 clamp 到 [0,10]
        {-5.0, 16, 64, 97, 127, 151, 188},
        {12.0, 16, 34, 67, 97, 121, 158},
    };
    for (const Case& c : cases) {
        const se::ManiaWindowSet s = se::computeManiaWindowSet(c.od, 0, false, true);
        CHECK_EQ(s.windowMax, c.max_);
        CHECK_EQ(s.window300, c.w300);
        CHECK_EQ(s.window200, c.w200);
        CHECK_EQ(s.window100, c.w100);
        CHECK_EQ(s.window50, c.w50);
        CHECK_EQ(s.earlyInputWindow, c.early);
    }
    // OD 先 conv.r4：8.0000001 变成 8.0f ⇒ d = 2.0 ⇒ 34f+6f = 40（不经过 float
    // 舍入时的 double 路径会给 34+3*1.9999999 = 39.9999997 → 39）。
    const se::ManiaWindowSet near8 =
        se::computeManiaWindowSet(8.0000001, 0, false, true);
    CHECK_EQ(near8.window300, 40);
    // 路径 B 的 EarlyInputWindow **也**过 AdjustManiaHitWindow（与路径 A 不同）。
    const se::ManiaWindowSet hrB =
        se::computeManiaWindowSet(8.0, kHardRock, false, true);
    CHECK_EQ(hrB.windowMax, 11);         // (int)(16 / 1.4)
    CHECK_EQ(hrB.window300, 28);         // (int)(40 / 1.4)
    CHECK_EQ(hrB.earlyInputWindow, 117); // (int)(164 / 1.4) = 117.14…
    // OD9：路径 A（ScoreV2）与路径 B（原生 mania）只在 WindowMax 上不同 ——
    // 任务书点名的 "v1 16 vs v2 15" 位级差异最小样本（3f*d = 3 恰好为整数，
    // 300/200/100/50 四档重合）。
    {
        const se::ManiaWindowSet a9 = se::computeManiaWindowSet(9.0, 0, true, true);
        const se::ManiaWindowSet b9 = se::computeManiaWindowSet(9.0, 0, false, true);
        CHECK_EQ(a9.windowMax, 15);
        CHECK_EQ(b9.windowMax, 16);
        CHECK_EQ(a9.window300, b9.window300);
        CHECK_EQ(a9.window200, b9.window200);
        CHECK_EQ(a9.window100, b9.window100);
        CHECK_EQ(a9.window50, b9.window50);
    }
    // 未开 mod 时路径 B 的表与 legacy OD 载体 ManiaWindows::fromOD 同源
    // (16 / 64-3·OD / 97-3·OD / 127-3·OD / 151-3·OD / 188-3·OD)。
    for (double od : {0.0, 4.0, 8.0, 10.0}) {
        const ManiaWindows w = ManiaWindows::fromOD(od);
        const se::ManiaWindowSet s =
            se::computeManiaWindowSet(od, 0, false, true);
        CHECK_EQ(static_cast<int>(w.perfect), s.windowMax);
        CHECK_EQ(static_cast<int>(w.great), s.window300);
        CHECK_EQ(static_cast<int>(w.good), s.window200);
        CHECK_EQ(static_cast<int>(w.ok), s.window100);
        CHECK_EQ(static_cast<int>(w.meh), s.window50);
        CHECK_EQ(static_cast<int>(w.miss), s.earlyInputWindow);
    }
}

// 路径 C/D: 转换谱。Math.Round 单参 = 中点取偶（ToEven）；
// `Round(OD) <= 4`（ble.un，NaN 也走这里）⇒ D，否则 C。
static void testWindowPathCD() {
    struct Case {
        double od;
        bool high;  // Round(OD) > 4
    };
    const Case cases[] = {
        {3.0, false}, {4.0, false}, {4.4, false},
        {4.5, false},  // ToEven: Round(4.5) = 4 ⇒ D（四舍五入会走 C）
        {4.6, true},  {5.0, true},
        {5.5, true},   // ToEven: Round(5.5) = 6 ⇒ C
        {std::nan(""), false},  // NaN 不 > 4 ⇒ D
    };
    for (const Case& c : cases) {
        const se::ManiaWindowSet s =
            se::computeManiaWindowSet(c.od, 0, false, false);
        CHECK_EQ(s.windowMax, 16);
        CHECK_EQ(s.window300, c.high ? 34 : 47);
        CHECK_EQ(s.window200, c.high ? 67 : 77);
        CHECK_EQ(s.window100, 97);
        CHECK_EQ(s.window50, 121);
        CHECK_EQ(s.earlyInputWindow, 158);
    }
    // C/D 的常数同为 double 且同样过 mod 调整。
    const se::ManiaWindowSet hrC =
        se::computeManiaWindowSet(5.0, kHardRock, false, false);
    CHECK_EQ(hrC.window300, 24);  // (int)(34 / 1.4) = 24.28…
}

// AdjustManiaHitWindow 的因子序列（spec 02 §2.1）：
//   (mods & 16)>0 ⇒ /=1.4 ; else (mods & 2)>0 ⇒ *=1.4
//   (mods & 64)>0 ⇒ *=1.5 ; else (mods & 256)>0 ⇒ *=0.75
// 全 double，末尾唯一一次 conv.i4（向零截断）。
static void testWindowModFactors() {
    CHECK_EQ(se::adjustManiaHitWindowFor(0, 100.0), 100);
    CHECK_EQ(se::adjustManiaHitWindowFor(kHardRock, 100.0), 71);   // 71.428…
    CHECK_EQ(se::adjustManiaHitWindowFor(kEasy, 100.0), 140);
    CHECK_EQ(se::adjustManiaHitWindowFor(kDoubleTime, 100.0), 150);
    CHECK_EQ(se::adjustManiaHitWindowFor(kHalfTime, 100.0), 75);
    // HR / EZ 是 else-if：两者同置时 HR 生效。
    CHECK_EQ(se::adjustManiaHitWindowFor(kHardRock | kEasy, 100.0), 71);
    // DT / HT 同样 else-if：两者同置时 DT 生效（NC 带 DT 位即走这支）。
    CHECK_EQ(se::adjustManiaHitWindowFor(kDoubleTime | kHalfTime, 100.0), 150);
    // 两段串联（先 HR/EZ 再 DT/HT），不是择一。
    CHECK_EQ(se::adjustManiaHitWindowFor(kHardRock | kDoubleTime, 100.0), 107);
    CHECK_EQ(se::adjustManiaHitWindowFor(kEasy | kHalfTime, 100.0), 105);
    // conv.i4 只在最后：97*1.4 = 135.79999999999998 ⇒ 135（不是 136）。
    CHECK_EQ(se::adjustManiaHitWindowFor(kEasy, 97.0), 135);
    CHECK_EQ(se::adjustManiaHitWindowFor(kHardRock, 46.0), 32);  // 46/1.4 = 32.857
    CHECK_EQ(se::adjustManiaHitWindowFor(kDoubleTime, 22.4), 33);  // 33.6
    CHECK_EQ(se::adjustManiaHitWindowFor(kHalfTime, 22.4), 16);    // 16.8
    CHECK_EQ(se::adjustManiaHitWindowFor(0, 22.4), 22);
    CHECK_EQ(se::adjustManiaHitWindowFor(0, 41.5), 41);  // 截断，不是四舍五入
    // 负窗口也按向零截断（源里没有 clamp）。
    CHECK_EQ(se::adjustManiaHitWindowFor(0, -41.5), -41);
}

// ===========================================================================
// 2. 输入侧 (spec 01 §2.6.2 / §2.6.3 / §2.3.8(g) / §4.3)
// ===========================================================================

// Obf005399: `WasHeld = IsHeld` 无条件先搬，值相等则早退 ⇒ 边沿只在
// "值确实改变的那次调用之后" 存活，直到下一次写列（或显式 WasHeld = IsHeld）。
static void testColumnEdgeLifetime() {
    se::ManiaColumn col;
    col.index = 0;
    CHECK(!col.isHeld && !col.wasHeld);
    col.setHeld(true);  // WasHeld=false（先搬）; false != true ⇒ IsHeld=true
    CHECK(col.isHeld && !col.wasHeld);  // 新按下边沿
    col.setHeld(true);  // WasHeld=true（先搬）; 值相等 ⇒ 早退
    CHECK(col.isHeld && col.wasHeld);  // [怪癖] 同值重写抹掉边沿
    col.setHeld(false);
    CHECK(!col.isHeld && col.wasHeld);  // 松开边沿
    col.setHeld(false);                 // WasHeld=false 后早退
    CHECK(!col.isHeld && !col.wasHeld);

    // 早退发生在任何图形/状态更新之前，但 WasHeld 已经被写过。
    se::ManiaColumn c2;
    c2.isHeld = true;
    c2.wasHeld = false;
    c2.setHeld(false);
    CHECK(!c2.isHeld && c2.wasHeld);
    // HoldLightActive 有同形 early-out（视觉字段，判定从不读它）。
    CHECK(!c2.holdLightActive);
    c2.setHoldLightActive(true);
    CHECK(c2.holdLightActive);
}

// GetReplayBit = (int)Math.Pow(2.0, Index) —— double 幂 + conv.i4，
// 不是 `1 << Index`（spec 01 §2.6.3）。
static void testReplayBit() {
    se::ManiaColumn col;
    for (int i = 0; i <= 30; ++i) {
        col.index = i;
        CHECK_EQ(col.replayBit(), 1LL << i);
    }
    // Index 31 ⇒ Pow = 2^31 超出 int32 ⇒ cvttsd2si "integer indefinite"。
    col.index = 31;
    CHECK_EQ(col.replayBit(), INT32_MIN);
}

// 特殊键在右：掩码位整体右旋一位（bit i 写给列 i-1，bit 0 写给列 N-1）。
// 闸门 = 列数 > 4 且偶数 且 SpecialStyle == Right 且 Skin != null。
static void testSpecialKeyRightInputRotation() {
    // 6K + Right：bit0 → 列5，bit1 → 列0，bit5 → 列4。
    {
        Engine e(6);
        e.stage.specialStyleRight = true;
        CHECK(e.stage.evenKeysOverFour());
        CHECK(e.stage.specialKeyOnRight());
        se::applyReplayFrame(*e.manager, 1000, 0x1);  // bit0
        CHECK(e.held(5));
        for (int i = 0; i < 5; ++i) CHECK(!e.held(i));
        se::applyReplayFrame(*e.manager, 1010, 0x2);  // bit1
        CHECK(e.held(0) && !e.held(5));
        se::applyReplayFrame(*e.manager, 1020, 0x20);  // bit5
        CHECK(e.held(4));
        se::applyReplayFrame(*e.manager, 1030, 0x21);  // bit0+bit5 → 列5 + 列4
        CHECK(e.held(5) && e.held(4) && !e.held(0));
        se::applyReplayFrame(*e.manager, 1040, 0);  // 全松
        for (int i = 0; i < 6; ++i) CHECK(!e.held(i));
    }
    // 4K / 5K / 7K：永不移位（`n <= 4` 或奇数）。
    for (int keys : {4, 5, 7, 9}) {
        Engine e(keys);
        e.stage.specialStyleRight = true;
        CHECK(!e.stage.specialKeyOnRight());
        se::applyReplayFrame(*e.manager, 1000, 0x1);
        CHECK(e.held(0));
        CHECK(!e.held(keys - 1));
    }
    // 8K 偶数 > 4 且 Right ⇒ 移位；SpecialStyle 不是 Right ⇒ 不移位。
    {
        Engine e(8);
        e.stage.specialStyleRight = true;
        CHECK(e.stage.specialKeyOnRight());
        se::applyReplayFrame(*e.manager, 1000, 0x1);
        CHECK(e.held(7));
    }
    {
        Engine e(6);
        e.stage.specialStyleRight = false;
        CHECK(!e.stage.specialKeyOnRight());
        se::applyReplayFrame(*e.manager, 1000, 0x1);
        CHECK(e.held(0));
    }
    {
        // Skin == null ⇒ Obf020598 的第二条守卫失败。
        Engine e(6);
        e.stage.specialStyleRight = true;
        e.stage.hasSkin = false;
        CHECK(!e.stage.specialKeyOnRight());
        se::applyReplayFrame(*e.manager, 1000, 0x1);
        CHECK(e.held(0));
    }
}

// 物件侧的 RemapManiaColumn（0x060049CA）与输入写入用同一个条件、同一个方向：
// 特殊键在右 ⇒ column==0 → N-1，否则 column-1；Random 优先（else if）；
// Mirror 最后 `N-1-该值`。RandomColumnMap 由 .NET System.Random 抽成置换，
// 且偶数键 > 4 时 map[0] 被钉在 N-1（special right）或 0（否则）——spec 02 §3.3。
static void testRemapManiaColumn() {
    // 6K + 特殊键在右（无 mod）
    {
        Engine e(6);
        e.stage.specialStyleRight = true;
        CHECK_EQ(e.manager->remapManiaColumn(0), 5);
        CHECK_EQ(e.manager->remapManiaColumn(1), 0);
        CHECK_EQ(e.manager->remapManiaColumn(5), 4);
        // 物件构造时就落位（ManiaNote::.ctor 内部走 remap）
        se::ManiaNote* n0 = e.addRice(0, 1000);
        se::ManiaNote* n1 = e.addRice(1, 1000);
        CHECK_EQ(n0->column->index, 5);
        CHECK_EQ(n0->originalColumnIndex, 0);  // 原始列号保留
        CHECK_EQ(n1->column->index, 0);
    }
    // 6K + 特殊键在右 + Mirror：`N-1-remap` 叠加
    {
        Engine e(6, kMirrorMod);
        e.stage.specialStyleRight = true;
        CHECK_EQ(e.manager->remapManiaColumn(0), 0);  // 5-5
        CHECK_EQ(e.manager->remapManiaColumn(1), 5);  // 5-0
        CHECK_EQ(e.manager->remapManiaColumn(2), 4);  // 5-1
    }
    // 4K / 奇数键：不做特殊键位移，只有 Mirror。
    {
        Engine e(4, kMirrorMod);
        e.stage.specialStyleRight = true;
        CHECK_EQ(e.manager->remapManiaColumn(0), 3);
        CHECK_EQ(e.manager->remapManiaColumn(1), 2);
    }
    // Random：使用 RandomColumnMap（优先于特殊键位移），Mirror 可再叠加。
    {
        Engine e(6);
        e.manager->activeMods = static_cast<int32_t>(kRandomMod);
        e.manager->randomColumnMap = {5, 0, 3, 1, 4, 2};
        for (int c = 0; c < 6; ++c)
            CHECK_EQ(e.manager->remapManiaColumn(c),
                     e.manager->randomColumnMap[static_cast<size_t>(c)]);
        e.manager->activeMods = static_cast<int32_t>(kRandomMod | kMirrorMod);
        CHECK_EQ(e.manager->remapManiaColumn(0), 0);  // 5-5
        CHECK_EQ(e.manager->remapManiaColumn(2), 2);  // 5-3
    }
    // Virtual0063 的随机列映射：置换 + 偶数键首列特例。
    {
        Engine e(6);
        e.stage.specialStyleRight = true;
        e.manager->initializeJudgementArrays(424242u);
        const std::vector<int>& map = e.manager->randomColumnMap;
        CHECK_EQ(map.size(), 6u);
        CHECK_EQ(map[0], 5);  // 特殊键在右 ⇒ 首列钉在 N-1，不抽随机数
        std::vector<int> seen(6, 0);
        for (int v : map) {
            CHECK(v >= 0 && v < 6);
            if (v >= 0 && v < 6) ++seen[static_cast<size_t>(v)];
        }
        for (int v : seen) CHECK_EQ(v, 1);  // 是一个置换
    }
    {
        Engine e(8);  // 偶数键 > 4 但 SpecialStyle 不是 Right ⇒ map[0] = 0
        e.manager->initializeJudgementArrays(7u);
        CHECK_EQ(e.manager->randomColumnMap[0], 0);
        std::vector<int> seen(8, 0);
        for (int v : e.manager->randomColumnMap)
            if (v >= 0 && v < 8) ++seen[static_cast<size_t>(v)];
        for (int v : seen) CHECK_EQ(v, 1);
    }
}

// 边沿的单 tick 生存期：没有新帧的 tick 必须 `WasHeld = IsHeld`（(h) 分支），
// 否则同一个边沿会被连续消费（一行按键判掉两个物件）。
static void testEdgeLifetimeAcrossTicks() {
    // (a) 有 resync：一次按下只判一个物件。
    {
        Engine e(4);
        e.od8Windows();
        e.manager->approachTime = 200;
        se::ManiaNote* a = e.addRice(0, 1000);
        se::ManiaNote* b = e.addRice(0, 1050);
        e.tick(1000, 0x1);
        CHECK(a->isJudged);
        CHECK_EQ(a->judgmentResult, kFMax);
        CHECK(!b->isJudged);
        e.tickNoFrame(1000);  // (h): WasHeld = IsHeld
        CHECK(e.held(0) && e.wasHeld(0));
        CHECK(!b->isJudged);  // 边沿已消失 ⇒ 不会被二次消费
        // 再消费一个新帧（值相同）同样早退 ⇒ 无新边沿。
        e.tick(1010, 0x1);
        CHECK(!b->isJudged);
        // 松手 + 再按下 ⇒ 新边沿判掉 b。
        e.tick(1040, 0x0);
        e.tick(1080, 0x1);
        CHECK(b->isJudged);
        CHECK_EQ(b->judgmentResult, kF300);  // |1080-1050| = 30 ≤ Window300 40
    }
    // (b) 跳过 resync：同一个边沿在一个 tick 之后仍然可见 ⇒ 连判两个物件。
    //     这正是 PlaySession.Obf003440 无条件调用 UpdateInputJudgments（且 (h)
    //     分支显式重同步）的原因。
    {
        Engine e(4);
        e.od8Windows();
        e.manager->approachTime = 200;
        se::ManiaNote* a = e.addRice(0, 1000);
        se::ManiaNote* b = e.addRice(0, 1050);
        e.tick(1000, 0x1);
        CHECK(a->isJudged && !b->isJudged);
        // 不重同步，直接再跑一次判定：边沿仍在。
        e.manager->now = 1000;
        e.manager->virtual0177();
        e.manager->updateInputJudgments();
        CHECK(b->isJudged);  // [怪癖] 单边沿双消费（host 未 resync 的后果）
    }
    // (c) 漏掉一个 tick 的 UpdateInputJudgments ⇒ 边沿永久丢失（spec 01 §4.3：
    //     "一个边沿只在一个 tick 内可见"）。
    {
        Engine e(4);
        e.od8Windows();
        e.manager->approachTime = 200;
        se::ManiaNote* n = e.addRice(0, 1000);
        // tick 1000：只写输入，**不**跑判定派发（模拟漏 tick）。
        se::applyReplayFrame(*e.manager, 1000, 0x1);
        CHECK(e.held(0) && !e.wasHeld(0));  // 边沿此刻存在
        // tick 1050 仍有帧：值未变 ⇒ WasHeld 追上 IsHeld，边沿消失。
        e.tick(1050, 0x1);
        CHECK(e.held(0) && e.wasHeld(0));
        CHECK(!n->isJudged);  // 这次按下再也不会被判到
        // 松手/再按才会产生新边沿；否则物件只会到期 miss。
        e.tick(1100, 0x0);
        e.tick(1200, 0x1);
        CHECK(n->isJudged);  // 新的按下边沿（此时已过 Window50 ⇒ miss 档）
        CHECK_EQ(n->judgmentResult, kFMiss);
    }
}

// ===========================================================================
// 3. 绑定 / 仲裁 (spec 02 §1.4 / §3.10 / §3.8 / §7.10.1)
// ===========================================================================

// consumed[] 是每 pass 的局部数组 ⇒ 一个列的一次边沿最多消费同列 1 个物件。
static void testConsumedOneObjectPerColumn() {
    Engine e(4);
    e.od8Windows();
    e.manager->approachTime = 200;
    se::ManiaNote* a = e.addRice(0, 1000);
    se::ManiaNote* b = e.addRice(0, 1100);
    e.tick(1000, 0x1);
    CHECK(a->isJudged);
    CHECK_EQ(a->judgmentResult, kFMax);
    CHECK_EQ(a->pressTime, 1000);
    CHECK(!b->isJudged);  // consumed[0] 挡住
    CHECK_EQ(e.judgments.size(), 1u);
    // 新边沿（松开 → 再按）才能推进到同列下一个物件。
    e.tick(1050, 0x0);
    e.tick(1130, 0x1);
    CHECK(b->isJudged);
    CHECK_EQ(b->judgmentResult, kF300);  // |1130-1100| = 30 ≤ Window300 40
}

// ExpireManiaNote 在边沿判定之前（IL_0212）：到期物件先被判掉并 `continue`，
// 因此它**不**消费该列的边沿，但同列的下一个物件仍可消费同一个边沿。
static void testExpireBeforeEdge() {
    Engine e(4);
    e.od8Windows();
    e.manager->approachTime = 200;
    se::ManiaNote* expired = e.addRice(0, 1000);
    se::ManiaNote* next = e.addRice(0, 1150);
    // rice 的到期时刻 = EndTime + (float)Window100 = 1000 + 103 = 1103。
    e.tick(1103, 0x1);
    CHECK(expired->isJudged);
    CHECK_EQ(expired->judgmentResult, kFMiss);  // 未按下 ⇒ miss
    CHECK(!expired->isHeld);
    CHECK(next->isJudged);  // 同一个边沿被下一个物件消费
    CHECK_EQ(next->pressTime, 1103);
    CHECK_EQ(next->judgmentResult, kF200);  // |1103-1150| = 47 > 40, ≤ 73
    CHECK_EQ(e.judgments.size(), 2u);
    // 到期但已判 ⇒ ExpireManiaNote 返回 false（调用方继续）。
    CHECK(!e.manager->expireManiaNote(expired));
}

// "未来时间的已判定物件封锁该列"（IL_01D4 怪癖）：IsJudged && StartTime > now
// ⇒ consumed[col] = true，同列更晚的物件本 pass 内不被判定。
static void testFutureJudgedObjectLocksColumn() {
    Engine e(4);
    e.od8Windows();
    e.manager->approachTime = 0;  // 让两个未来物件都进 ActiveObjects
    se::ManiaNote* future = e.addRice(0, 2000);
    se::ManiaNote* later = e.addRice(0, 2100);
    // 人为制造"未来却已判定"（源里该状态的产生路径见 spec 02 Q8）。
    e.manager->now = 1800;
    CHECK_EQ(e.manager->judgeObject(future), kFMiss);
    CHECK(future->isJudged);
    CHECK_EQ(e.judgments.size(), 1u);
    // 1800 时按下列 0：future 已判定且 StartTime(2000) > now(1800) ⇒ 封锁该列。
    e.tick(1800, 0x1);
    CHECK(!later->isJudged);  // 被封锁，press 没有被这个物件消费
    CHECK_EQ(e.judgments.size(), 1u);  // 没有新判定
    CHECK_EQ(later->pressTime, 0);
    // 时间越过 future.StartTime 之后该封锁解除，later 正常到期 miss。
    e.tick(2203, 0x1);  // later: EndTime 2100 + Window100 103 = 2203
    CHECK(later->isJudged);
    CHECK_EQ(later->judgmentResult, kFMiss);
}

// Virtual0177 的切片上界是闭区间且多含 1 个越界物件（spec 02 §7.10.1(f)）：
// 循环停在"第一个 StartTime > now+300"的元素上，GetRange(i, 1+j-i) 把它计入。
static void testActiveObjectsWindow() {
    // 4 个物件：1000 / 1300 / 2000 / 2600；now = 1000 ⇒ now+300 = 1300。
    Engine e(4);
    e.manager->approachTime = 1000;  // 下界 key = 1000-1000-200 = -200 ⇒ i=0
    e.addRice(0, 1000);
    e.addRice(1, 1300);
    e.addRice(2, 2000);
    e.addRice(3, 2600);
    e.manager->now = 1000;
    e.manager->virtual0177();
    CHECK_EQ(e.manager->activeObjects.size(), 3u);  // 1000,1300,2000（多 1 个）
    CHECK_EQ(e.manager->activeObjects[0]->startTime, 1000);
    CHECK_EQ(e.manager->activeObjects[1]->startTime, 1300);
    CHECK_EQ(e.manager->activeObjects[2]->startTime, 2000);  // > now+300 却仍在切片里
    // 末元素永远被计入（`j < ObjectCount-1` 把最后一个元素留在切片内）。
    {
        Engine f(2);
        f.manager->approachTime = 1000;
        f.addRice(0, 1000);
        f.addRice(1, 5000);
        f.manager->now = 1000;
        f.manager->virtual0177();
        CHECK_EQ(f.manager->activeObjects.size(), 2u);
        CHECK_EQ(f.manager->activeObjects[1]->startTime, 5000);
    }
    // i == ObjectCount ⇒ ActiveObjects.Clear()。
    {
        Engine g(2);
        g.manager->approachTime = 0;
        g.addRice(0, 1000);
        g.addRice(1, 1300);
        g.manager->now = 10000;  // key = 9800 > 所有 EndTime
        g.manager->virtual0177();
        CHECK_EQ(g.manager->activeObjects.size(), 0u);
    }
}

// 含 LN 时 Virtual0177 的 BinarySearch 按 **EndTime** 三路比较（spec 02 §7.10.1(e)
// 的反例）：LN(1000..5000) + rice(2000) + rice(3000), key = 1500 ⇒ 切片起点 0。
static void testActiveObjectsEndTimeBinarySearch() {
    {
        Engine e(3);
        e.manager->approachTime = 1000;
        e.addLn(0, 1000, 5000);
        e.addRice(1, 2000);
        e.addRice(2, 3000);
        e.manager->now = 2700;  // key = 2700 - 1000 - 200 = 1500
        e.manager->virtual0177();
        CHECK_EQ(e.manager->activeObjects.size(), 3u);
        CHECK_EQ(e.manager->activeObjects[0]->startTime, 1000);  // LN 在切片内
        // "第一个 StartTime >= 1500" 会给 2000 ⇒ 差一个物件。
        CHECK(e.manager->activeObjects[0]->noteKind == 1);
    }
    // 纯 rice 段（StartTime == EndTime）退化为 StartTime 下界：起点是 2000。
    {
        Engine e(3);
        e.manager->approachTime = 1000;
        e.addRice(0, 1000);
        e.addRice(1, 2000);
        e.addRice(2, 3000);
        e.manager->now = 2700;  // 同一个 key = 1500
        e.manager->virtual0177();
        CHECK_EQ(e.manager->activeObjects.size(), 2u);
        CHECK_EQ(e.manager->activeObjects[0]->startTime, 2000);
    }
    // 同一个按下在 V1 / V2 下绑到**不同**物件（ScoreV2 每列预选，IL_023F）。
    //   物件 1000 / 1080 同列，按下在 1085：
    //   V1: 列表顺序 ⇒ 绑 1000（|85| → 100 档）
    //   V2: selected[col] = 1080（now > Min(start+W50, later-1) 的 limit）
    //       ⇒ 1000 被过滤，绑 1080（|5| → MAX）
    {
        Engine v1(2);
        v1.od8Windows();
        v1.manager->approachTime = 0;
        se::ManiaNote* a = v1.addRice(0, 1000);
        se::ManiaNote* b = v1.addRice(0, 1080);
        v1.tick(1085, 0x1);
        CHECK(a->isJudged);
        CHECK_EQ(a->judgmentResult, kF100);
        CHECK(!b->isJudged);

        Engine v2(2, 0u, true);
        v2.od8Windows();
        v2.manager->approachTime = 0;
        se::ManiaNote* c = v2.addRice(0, 1000);
        se::ManiaNote* d = v2.addRice(0, 1080);
        v2.tick(1085, 0x1);
        CHECK(!c->isJudged);  // 预选把它过滤掉了（且未标 consumed）
        CHECK(d->isJudged);
        CHECK_EQ(d->judgmentResult, kFMax);
        CHECK_EQ(d->pressTime, 1085);
    }
}

// ===========================================================================
// 4. note / LN 状态机 (spec 02 §4.3–§4.8)
// ===========================================================================

// ManiaNote::Judge：全 int 窗口链 |PressTime-StartTime| ≤ Max/300/200/100/50，
// 边界是 `<=`（bgt.s 取反），IsJudged 无条件写入。
static void testRiceJudgeWindowChain() {
    struct Case {
        int err;
        int32_t expected;
    };
    const Case cases[] = {
        {0, kFMax},   {16, kFMax},  {17, kF300}, {40, kF300}, {41, kF200},
        {73, kF200},  {74, kF100},  {103, kF100}, {104, kF50},
        {127, kF50},  {128, kFMiss},
    };
    for (const Case& c : cases) {
        Engine e(1);
        e.od8Windows();
        se::ManiaNote* n = e.addRice(0, 1000);
        e.manager->now = 1000 + c.err;
        n->isHeld = true;  // "已被按下"：Judge 只看这个
        n->pressTime = 1000 + c.err;
        const int32_t r = e.manager->judgeObject(n);
        CHECK_EQ(r, c.expected);
        CHECK(n->isJudged);
        CHECK_EQ(n->judgmentResult, c.expected);
    }
    // 未按下 ⇒ miss（仍然写 IsJudged）。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaNote* n = e.addRice(0, 1000);
        e.manager->now = 1000;
        CHECK_EQ(e.manager->judgeObject(n), kFMiss);
        CHECK(n->isJudged);
    }
    // 已判定物件再判 ⇒ 原样返回，不改字段（IL_0000-IL_000E）。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaNote* n = e.addRice(0, 1000);
        n->isHeld = true;
        n->pressTime = 1000;
        e.manager->now = 1000;
        CHECK_EQ(e.manager->judgeObject(n), kFMax);
        CHECK_EQ(e.manager->judgeObject(n), 0);  // JudgeObject 的 IsJudged 守卫
        CHECK_EQ(n->judgmentResult, kFMax);
        CHECK_EQ(e.judgments.size(), 1u);
    }
    // 视觉状态字段（判定会读/写，spec 02 §4.9）：
    //   Result > 50 ⇒ Virtual0110（IsHiddenAfterHit）；否则 Virtual0112(true)（IsDimmed）。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaNote* hit = e.addRice(0, 1000);
        hit->isHeld = true;
        hit->pressTime = 1000;
        e.manager->now = 1000;
        e.manager->judgeObject(hit);
        CHECK(hit->isHiddenAfterHit);
        CHECK(!hit->isDimmed);
        se::ManiaNote* missed = e.addRice(1, 1000);
        e.manager->now = 1000;
        e.manager->judgeObject(missed);  // 未按下 ⇒ miss
        CHECK(missed->isDimmed);
        CHECK(!missed->isHiddenAfterHit);
        // 50 档也走变暗分支（`> 16777216` 才隐藏）。
        se::ManiaNote* meh = e.addRice(2, 1000);
        meh->isHeld = true;
        meh->pressTime = 1110;  // |110| > 103, ≤ 127 ⇒ 50 档
        e.manager->now = 1110;
        CHECK_EQ(e.manager->judgeObject(meh), kF50);
        CHECK(meh->isDimmed);
        CHECK(!meh->isHiddenAfterHit);
        // virtual0112 的 early-out：同值重复调用不改状态。
        meh->virtual0112(true);
        CHECK(meh->isDimmed);
        meh->virtual0112(false);
        CHECK(!meh->isDimmed);
    }
}

// 第二趟统计（spec 02 §3.10.3）：KeyPressTimes 只在"新按下"时写；松开时按
// delta < 20 / < 38 / < 100 累计 RapidPressWeight / ShortPressCount；
// 且整趟只在 PlaySession.0x04000A51 == 2（离线端 clockPlaying）时执行。
static void testRapidPressStats() {
    {
        Engine e(4);
        e.od8Windows();
        e.manager->approachTime = 200;
        // 没有物件的空谱：所有按下都是"未命中边界"的边沿。
        e.tick(1000, 0x1);   // 新按下 ⇒ KeyPressTimes[0] = 1000
        CHECK_EQ(e.manager->keyPressTimes[0], 1000);
        e.tick(1010, 0x0);   // 松开，delta = 10 < 20 ⇒ weight += 2, short += 1
        CHECK_EQ(e.manager->rapidPressWeight, 2);
        CHECK_EQ(e.manager->shortPressCount, 1);
        e.tick(1020, 0x1);
        e.tick(1050, 0x0);   // delta = 30 → < 38 ⇒ weight += 1；< 100 ⇒ short += 1
        CHECK_EQ(e.manager->rapidPressWeight, 3);
        CHECK_EQ(e.manager->shortPressCount, 2);
        e.tick(1060, 0x1);
        e.tick(1200, 0x0);   // delta = 140 ⇒ 都不加
        CHECK_EQ(e.manager->rapidPressWeight, 3);
        CHECK_EQ(e.manager->shortPressCount, 2);
        // 比例：3/2 = 1.5 > 0.6；ShortPressCount == 0 时短路 false。
        CHECK(e.manager->obf021373());
        e.manager->shortPressCount = 0;
        CHECK(!e.manager->obf021373());
    }
    {
        // clockPlaying == false（源里 Obf006571 != 2）⇒ 第二趟整体跳过；
        // 第一趟（物件判定）不受影响。
        Engine e(4);
        e.od8Windows();
        e.manager->approachTime = 200;
        se::ManiaNote* n = e.addRice(0, 1000);
        e.manager->clockPlaying = false;
        e.tick(1000, 0x1);
        e.tick(1010, 0x0);
        CHECK(n->isJudged);
        CHECK_EQ(n->judgmentResult, kFMax);
        CHECK_EQ(e.manager->rapidPressWeight, 0);
        CHECK_EQ(e.manager->shortPressCount, 0);
        CHECK_EQ(e.manager->keyPressTimes[0], 0);  // 连时间戳都不写
        CHECK(e.unmatchedPresses.empty());
    }
}

// ManiaLongNote::Judge 的三个 `return 0` 守卫**不写 IsJudged**；
// 提前松手返回 -1048576 也不写 IsJudged，且可重复触发（spec 02 §4.5 / Q5）。
static void testLongNoteJudgeGuards() {
    // 守卫 1：now < StartTime + Window50 且 PressTime == 0。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        e.manager->now = 1000;  // < 1000+127
        CHECK_EQ(ln->judge(*e.manager), 0);
        CHECK(!ln->isJudged);
    }
    // 守卫 3：还按着且 now < EndTime + Window50。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->pressTime = 1000;
        ln->isHeld = true;
        e.manager->now = 2000;  // < 2000+127
        CHECK_EQ(ln->judge(*e.manager), 0);
        CHECK(!ln->isJudged);
    }
    // 守卫 2：提前松手 ⇒ -1048576（break），不写 IsJudged，可重复触发。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->pressTime = 1000;
        ln->isHeld = false;
        e.manager->now = 1500;  // < 2000-127
        CHECK_EQ(ln->judge(*e.manager), kFBreak);
        CHECK(ln->wasBroken);
        CHECK(!ln->isJudged);
        CHECK_EQ(ln->judge(*e.manager), kFBreak);  // 再进一次仍然 break
        CHECK(!ln->isJudged);
        // 经 JudgeObject 走同样的路径：返回值照样是 break。
        CHECK_EQ(e.manager->judgeObject(ln), kFBreak);
        CHECK(!ln->isJudged);
    }
    // 守卫之外：最终判定会写 IsJudged 并给出正档位。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->pressTime = 1000;
        ln->releaseTime = 2000;
        ln->isHeld = false;
        e.manager->now = 2100;
        CHECK_EQ(ln->judge(*e.manager), kFMax);
        CHECK(ln->isJudged);
    }
}

// 破 LN 封顶：传统（V1）路径 `Result > 200 && WasBroken ⇒ 200`；
// ScoreV2 路径 `Result > 50 && (WasBroken || !HeadJudged) ⇒ 50`（spec 02 §4.5）。
static void testLongNoteCaps() {
    // (a) V1：未破的完美尾判 = 320；破过之后同样的误差被压到 200。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->pressTime = 1000;
        ln->releaseTime = 2000;
        ln->isHeld = false;
        e.manager->now = 2100;
        CHECK_EQ(ln->judge(*e.manager), kFMax);
    }
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->pressTime = 1000;
        ln->isHeld = false;
        e.manager->now = 1500;
        CHECK_EQ(ln->judge(*e.manager), kFBreak);  // 先破
        ln->releaseTime = 2000;
        e.manager->now = 2100;
        CHECK_EQ(ln->judge(*e.manager), kF200);  // 封顶 200
        CHECK(ln->isJudged);
    }
    // (b) V2：头从未判过（HeadJudged == false）⇒ 完美尾判被封顶到 50。
    {
        Engine e(1, 0u, true);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->pressTime = 1000;
        ln->releaseTime = 2000;
        ln->isHeld = false;
        CHECK(!ln->headJudged);
        e.manager->now = 2100;
        CHECK_EQ(ln->judge(*e.manager), kF50);  // |0|/1.5 = 0 → 320 → 封顶 50
        CHECK(ln->isJudged);
    }
    // (c) V2 + 头已判过 ⇒ 不再封顶（|ReleaseTime-EndTime|/1.5 决定档位）。
    {
        Engine e(1, 0u, true);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->pressTime = 1000;
        ln->isHeld = true;
        e.manager->now = 1000;
        CHECK_EQ(e.manager->judgeLongNoteHead(ln), kFMax);
        CHECK(ln->headJudged);
        // 尾：先松手（release 2000，误差 0）再判。
        ln->isHeld = false;
        ln->releaseTime = 2000;
        e.manager->now = 2100;
        CHECK_EQ(ln->judge(*e.manager), kFMax);
    }
    // (d) V2 尾误差是 (float)|ReleaseTime-EndTime| / 1.5f：150/1.5 = 100 → 100 档。
    {
        Engine e(1, 0u, true);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->pressTime = 1000;
        ln->isHeld = true;
        e.manager->now = 1000;
        e.manager->judgeLongNoteHead(ln);
        ln->isHeld = false;
        ln->releaseTime = 2150;  // |150| / 1.5 = 100 → ≤ Window100(103)
        e.manager->now = 2200;
        CHECK_EQ(ln->judge(*e.manager), kF100);
    }
}

// ProcessLongNoteTick：100 ms 一拍、返回 7、LastTickTime 只前进一格（会追赶）；
// LastTickTime 在头发判按下时才被设成 now（初值 -1 ⇒ 永远返回 0）。
static void testLongNoteTick() {
    Engine e(1);
    e.od8Windows();
    se::ManiaLongNote* ln = e.addLn(0, 1000, 5000);
    CHECK_EQ(ln->processLongNoteTick(1000), 0);  // LastTickTime == -1
    CHECK_EQ(ln->processLongNoteTick(2000), 0);
    // 按下（JudgeLongNoteHead 的 IsHeld 分支把 LastTickTime 设成 now）。
    ln->isHeld = true;
    ln->pressTime = 1000;
    e.manager->now = 1000;
    e.manager->judgeLongNoteHead(ln);
    CHECK_EQ(ln->lastTickTime, 1000);
    CHECK_EQ(ln->tickCount, 0);
    CHECK_EQ(ln->processLongNoteTick(1050), 0);
    CHECK_EQ(ln->processLongNoteTick(1099), 0);
    CHECK_EQ(ln->processLongNoteTick(1100), 7);  // 恰满 100 ms
    CHECK_EQ(ln->processLongNoteTick(1150), 0);
    CHECK_EQ(ln->processLongNoteTick(1200), 7);
    CHECK_EQ(ln->processLongNoteTick(1350), 7);  // 追赶：LastTickTime → 1300
    CHECK_EQ(ln->lastTickTime, 1300);
    CHECK_EQ(ln->processLongNoteTick(1400), 7);  // 1400 - 1300 == 100
    CHECK_EQ(ln->lastTickTime, 1400);
    CHECK_EQ(ln->processLongNoteTick(1450), 0);
    // now > EndTime ⇒ 0；now < StartTime ⇒ 0。
    CHECK_EQ(ln->processLongNoteTick(5001), 0);
    CHECK_EQ(ln->processLongNoteTick(999), 0);
    // 管理器的薄包装：回调收到 flags 7，tickCount 计入报告。
    e.manager->now = 1500;
    CHECK_EQ(e.manager->processLongNoteTick(ln), 7);
    CHECK_EQ(ln->tickCount, 1);
    CHECK_EQ(e.judgments.size(), 1u);
    CHECK_EQ(e.judgments.back().first, kFTick);
}

// ManiaLongNote::JudgeLongNoteHead：V1（无 ScoreV2 处理器）恒返回 0 但**仍置**
// HeadJudged；V2 走窗口链，IsHeld == false ⇒ 0，超出窗口 ⇒ -65536。
static void testLongNoteHead() {
    // V1：恒 0，HeadJudged 置位，不产生判定回调。
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->isHeld = true;
        ln->pressTime = 1010;
        e.manager->now = 1010;
        CHECK(!ln->headJudged);
        CHECK_EQ(e.manager->judgeLongNoteHead(ln), 0);
        CHECK(ln->headJudged);
        CHECK_EQ(ln->judgmentResult, 0);
        CHECK_EQ(e.judgments.size(), 0u);
    }
    // V2 + IsHeld：窗口链（<=，MAX→300→200→100→50），越界 ⇒ miss。
    struct Case {
        int err;
        int32_t expected;
    };
    const Case cases[] = {
        {0, kFMax},  {16, kFMax}, {17, kF300}, {41, kF200},
        {74, kF100}, {104, kF50}, {128, kFMiss},
    };
    for (const Case& c : cases) {
        Engine e(1, 0u, true);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->isHeld = true;
        ln->pressTime = 1000 + c.err;
        e.manager->now = 1000 + c.err;
        CHECK_EQ(e.manager->judgeLongNoteHead(ln), c.expected);
        CHECK(ln->headJudged);
        CHECK_EQ(ln->headJudgment, c.expected);
    }
    // V2 + 未按下 ⇒ MISS(-65536)（IL_0073 默认值保留：IL_007E brfalse.s
    // IL_011D 跳过 IL_0116 的清零），仍置 HeadJudged。
    // （2026-10-23 经 stable 声明账本裁决纠正：旧断言 0 系规格 §4.4 伪代码
    //   内外层 else 混淆所致——未按 LN 头在 V2 是一票 miss。）
    {
        Engine e(1, 0u, true);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->isHeld = false;
        e.manager->now = 1000;
        CHECK_EQ(e.manager->judgeLongNoteHead(ln), (int32_t)kFMiss);
        CHECK(ln->headJudged);
        CHECK_EQ(ln->judgmentResult, (int32_t)kFMiss);
    }
    // V2 + 已破 ⇒ 0（else 分支），HeadJudged 仍置位。
    {
        Engine e(1, 0u, true);
        e.od8Windows();
        se::ManiaLongNote* ln = e.addLn(0, 1000, 2000);
        ln->wasBroken = true;
        ln->isHeld = true;
        ln->pressTime = 1000;
        e.manager->now = 1000;
        CHECK_EQ(e.manager->judgeLongNoteHead(ln), 0);
        CHECK(ln->headJudged);
    }
}

// PerfectRequires300 只改 JudgeObject 的**局部返回值**，不动 o.JudgmentResult
// （spec 02 §3.14 / IL_0039–IL_004E；条件是 `r < 134217728`，所以 300 本身保留）。
static void testPerfectRequires300() {
    {
        Engine e(1);
        e.od8Windows();
        se::ManiaNote* n = e.addRice(0, 1000);
        e.manager->perfectRequires300 = true;
        n->isHeld = true;
        n->pressTime = 1070;  // 200 档（|70| > 40, ≤ 73）
        e.manager->now = 1070;
        CHECK_EQ(e.manager->judgeObject(n), kFMiss);  // 局部被改写成 miss
        CHECK_EQ(n->judgmentResult, kF200);           // 物件字段保持 200
        CHECK_EQ(n->countedResult, kFMiss);
        CHECK_EQ(e.judgments.size(), 1u);
        CHECK_EQ(e.judgments.back().first, kFMiss);
    }
    {
        // 临界：`r < 134217728` 是严格小于 ⇒ 300 档**不**被改写。
        Engine e(1);
        e.od8Windows();
        se::ManiaNote* n = e.addRice(0, 1000);
        e.manager->perfectRequires300 = true;
        n->isHeld = true;
        n->pressTime = 1030;
        e.manager->now = 1030;
        CHECK_EQ(e.manager->judgeObject(n), kF300);
        CHECK_EQ(n->judgmentResult, kF300);
    }
    {
        // 320 档 ≥ 134217728 ⇒ 不改写。
        Engine e(1);
        e.od8Windows();
        se::ManiaNote* n = e.addRice(0, 1000);
        e.manager->perfectRequires300 = true;
        n->isHeld = true;
        n->pressTime = 1010;
        e.manager->now = 1010;
        CHECK_EQ(e.manager->judgeObject(n), kFMax);
        CHECK_EQ(n->judgmentResult, kFMax);
    }
    {
        // 50 档与 miss 档同样被改写成 miss（-65536 本身也 < 134217728）。
        Engine e(1);
        e.od8Windows();
        se::ManiaNote* n = e.addRice(0, 1000);
        e.manager->perfectRequires300 = true;
        n->isHeld = true;
        n->pressTime = 1100;  // |100| > 73, ≤ 103 → 100 档
        e.manager->now = 1100;
        CHECK_EQ(e.manager->judgeObject(n), kFMiss);
        CHECK_EQ(n->judgmentResult, kF100);
    }
    // Perfect mod（0x4000）经 onLoadComplete 置位（ActiveMods & 16384）。
    {
        Engine e(1, kPerfectMod);
        e.od8Windows();
        e.addRice(0, 1000);
        CHECK(!e.manager->perfectRequires300);
        e.manager->onLoadComplete(1000.0);
        CHECK(e.manager->perfectRequires300);
    }
}

// ===========================================================================
// 5. 计分 (spec 03 §0.1 / §2 / §3)
// ===========================================================================

// counts 全是 ushort：`add; conv.u2; stfld` ⇒ 65535 + 1 = 0。
static void testCountsUshortWrap() {
    ScoreCounts c;
    c.count300 = 65535;
    c.add300();
    CHECK_EQ(c.count300, 0);
    c.countMax = 65535;
    c.addMax();
    CHECK_EQ(c.countMax, 0);
    c.countMiss = 65535;
    c.addMiss();
    CHECK_EQ(c.countMiss, 0);
    // 总数是 6 项 int32 加法（mania 覆盖版），非 4 项。
    ScoreCounts t;
    t.count300 = 65535;
    t.countMiss = 65535;
    CHECK_EQ(t.total(), 131070);
    CHECK_EQ(t.nonMissTotal(), 65535);
    // 经提交器同样回绕。
    se::ManiaScoreRecord rec(0, 4, false);
    se::ManiaScoreSubmission sub(&rec, 1, 4, true);
    sub.setupMultiplier();
    se::JudgeTargetState target;
    rec.counts.count300 = 65535;
    sub.onJudgment(kF300, target);
    CHECK_EQ(rec.counts.count300, 0);
    // MaxCombo 是 int32（不回绕），CurrentCombo 是 ushort。
    CHECK_EQ(sizeof(rec.counts.maxCombo), sizeof(int32_t));
    CHECK_EQ(sizeof(rec.counts.currentCombo), sizeof(uint16_t));
}

// acc = int32 分子/分母 → conv.r4 → 单精度 div（spec 03 §2.1 / §3.1）。
static void testAccuracyV1V2() {
    ScoreCounts c;  // 95 MAX + 3×300 + 1×200 + 1 miss
    c.count300 = 3;
    c.count200 = 1;
    c.countMax = 95;
    c.countMiss = 1;
    CHECK_EQ(c.total(), 100);
    // V1: (50*C50 + 100*C100 + 200*C200 + (C300+CMax)*300) / (total*300)
    // 本组：C300=3, C200=1, CMax=95, CMiss=1 ⇒ 200 + 98*300 = 29600
    const float v1 = static_cast<float>(50 * 0 + 100 * 0 + 200 * 1 +
                                        (3 + 95) * 300) /
                     static_cast<float>(100 * 300);
    CHECK_FEQ(c.accuracy(false), v1);
    // V2: ... + C300*300 + CMax*305，分母 total*305 ⇒ 200 + 900 + 28975 = 30075
    const float v2 = static_cast<float>(50 * 0 + 100 * 0 + 200 * 1 + 300 * 3 +
                                        305 * 95) /
                     static_cast<float>(100 * 305);
    CHECK_FEQ(c.accuracy(true), v2);
    CHECK(v2 < v1);  // MAX 权重 305 > 300 ⇒ 同一份 counts 在 V2 下 acc 更低

    // 全 300（CMax = 0）：V1 = 1.0，V2 = 300/305 = 0.9836…（表观差异必须保留）。
    ScoreCounts all300;
    all300.count300 = 10;
    CHECK_FEQ(all300.accuracy(false), 1.0f);
    CHECK_FEQ(all300.accuracy(true),
              static_cast<float>(10 * 300) / static_cast<float>(10 * 305));
    CHECK(all300.accuracy(true) < 1.0f);
    // 全 MAX：V2 也是 1.0。
    ScoreCounts allMax;
    allMax.countMax = 10;
    CHECK_FEQ(allMax.accuracy(false), 1.0f);
    CHECK_FEQ(allMax.accuracy(true),
              static_cast<float>(10 * 305) / static_cast<float>(10 * 305));
    // 空 counts ⇒ 恰好 1.0f（入口判断是 `== 0`）。
    ScoreCounts empty;
    CHECK_FEQ(empty.accuracy(false), 1.0f);
    CHECK_FEQ(empty.accuracy(true), 1.0f);
    // 大数：整数乘加先算完再 conv.r4（不是逐项转 float）。
    ScoreCounts big;
    big.count300 = 60000;
    big.count100 = 5000;
    const float bigV1 = static_cast<float>(60000 * 300 + 5000 * 100) /
                        static_cast<float>(65000 * 300);
    CHECK_FEQ(big.accuracy(false), bigV1);
    const float bigV2 = static_cast<float>(60000 * 300 + 5000 * 100) /
                        static_cast<float>(65000 * 305);
    CHECK_FEQ(big.accuracy(true), bigV2);
    // 记录口径：带处理器 或 mods 带 ScoreV2 位 ⇒ V2 公式。
    se::ManiaScoreRecord recMods(kScoreV2Mod, 4, false);
    recMods.counts = all300;
    CHECK_FEQ(recMods.accuracy(), all300.accuracy(true));
    se::ManiaScoreRecord recProc(0, 4, true);
    recProc.counts = all300;
    CHECK_FEQ(recProc.accuracy(), all300.accuracy(true));
    se::ManiaScoreRecord recV1(0, 4, false);
    recV1.counts = all300;
    CHECK_FEQ(recV1.accuracy(), all300.accuracy(false));
}

// 等级表（spec 03 §2.3）：`acc == 1.0f` 是精确单精度比较；0.95/0.9/0.8/0.7 是
// conv.r8 后的 double **严格大于**；HD/FL 只影响 X/XH 与 S/SH；NaN ⇒ D；!Passed ⇒ F。
static void testGradeTable() {
    CHECK_EQ(maniaGrade(1.0f, 0, true), kX);
    CHECK_EQ(maniaGrade(1.0f, kHidden, true), kXH);        // 0x100408
    CHECK_EQ(maniaGrade(1.0f, kFlashlight, true), kXH);
    CHECK_EQ(maniaGrade(1.0f, kFadeIn, true), kXH);
    CHECK_EQ(maniaGrade(1.0f, kHardRock, true), kX);        // HR 不在掩码里
    CHECK_EQ(maniaGrade(0.9999f, 0, true), kS);
    CHECK_EQ(maniaGrade(0.9999f, kHidden, true), kSH);
    // 精确边界：(double)0.95f = 0.9499999880… **不** > 0.95
    CHECK_EQ(maniaGrade(0.95f, 0, true), kA);
    CHECK_EQ(maniaGrade(0.95f, kHidden, true), kA);         // HD 不影响 A
    CHECK_EQ(maniaGrade(std::nextafter(0.95f, 1.0f), 0, true), kS);
    CHECK_EQ(maniaGrade(std::nextafter(0.95f, 1.0f), kHidden, true), kSH);
    CHECK_EQ(maniaGrade(0.9f, 0, true), kB);   // 0.9f 的 double 化 < 0.9
    CHECK_EQ(maniaGrade(0.91f, 0, true), kA);
    // 0.8f 的 double 化 = 0.800000011920929 **大于** 0.8 ⇒ 仍是 B；
    // 真正掉到 C 的是 0.8f 下面的那个 float。
    CHECK_EQ(maniaGrade(0.8f, 0, true), kB);
    CHECK_EQ(maniaGrade(std::nextafter(0.8f, 0.0f), 0, true), kC);
    CHECK_EQ(maniaGrade(0.81f, 0, true), kB);
    CHECK_EQ(maniaGrade(0.7f, 0, true), kD);
    CHECK_EQ(maniaGrade(0.71f, 0, true), kC);
    CHECK_EQ(maniaGrade(0.0f, 0, true), kD);
    // NaN 落空所有比较 ⇒ D。
    CHECK_EQ(maniaGrade(std::nanf(""), 0, true), kD);
    // 未 Passed ⇒ F，且优先于其它一切。
    CHECK_EQ(maniaGrade(1.0f, 0, false), kF);
    CHECK_EQ(maniaGrade(std::nanf(""), kHidden, false), kF);
    // 记录口径。
    se::ManiaScoreRecord rec(0, 4, false);
    rec.counts.countMax = 4;
    CHECK_EQ(rec.grade(), kX);
}

// V1 分数：`intScore := (int)doubleAcc` 再 `+= num`（双重截断的混合），
// 结束时 ManiaGameplay::Obf007620 用 (int)Math.Round(doubleAcc) 覆盖整型分，
// 再把 999999 特判成 1000000（spec 03 §4.4 / §5.5）。
static void testV1ScoreTruncation() {
    // scoringObjectCount = 4 ⇒ 倍率 500000/4 = 125000；hp 封顶 100。
    // 3×MAX + 1×300，每一步 num2 = 125000（300 档为 117187.5 + 125000）。
    {
        se::ManiaScoreRecord rec(0, 4, false);
        se::ManiaScoreSubmission sub(&rec, 4, 4, true);
        sub.setupMultiplier();
        CHECK(sub.multiplier() == 125000.0);
        sub.resetComboAndHealth();
        se::JudgeTargetState target;
        sub.onJudgment(kFMax, target);
        sub.onJudgment(kFMax, target);
        sub.onJudgment(kFMax, target);
        sub.onJudgment(kF300, target);
        CHECK_EQ(rec.counts.countMax, 3);
        CHECK_EQ(rec.counts.count300, 1);
        // 无损累计器（double）
        CHECK(rec.doubleAcc == 992187.5);
        // 截断-再加的混合结果（比 round(doubleAcc) 小 1）
        CHECK_EQ(rec.intScore, 992187);
        sub.finish();
        // (int)Math.Round(992187.5) —— 中点取偶 ⇒ 992188。
        CHECK_EQ(rec.intScore, 992188);
    }
    // 混合口径可观察：scoringObjectCount = 7（倍率 71428.571…），7 次 MAX 之后
    // intScore = 999999 而 (int)doubleAcc = 1000000（二者不同）。
    {
        se::ManiaScoreRecord rec(0, 4, false);
        se::ManiaScoreSubmission sub(&rec, 7, 4, true);
        sub.setupMultiplier();
        sub.resetComboAndHealth();
        se::JudgeTargetState target;
        for (int i = 0; i < 7; ++i) sub.onJudgment(kFMax, target);
        CHECK(rec.doubleAcc == 1000000.0);
        CHECK_EQ(rec.intScore, 999999);
        CHECK_EQ(static_cast<int32_t>(rec.doubleAcc), 1000000);
        CHECK(rec.intScore != static_cast<int32_t>(rec.doubleAcc));
        sub.finish();
        CHECK_EQ(rec.intScore, 1000000);
    }
    // 999999 特判：doubleAcc 四舍五入成 999999 时被写成 1000000。
    {
        se::ManiaScoreRecord rec(0, 4, false);
        se::ManiaScoreSubmission sub(&rec, 4, 4, true);
        sub.setupMultiplier();
        rec.doubleAcc = 999999.4;
        CHECK_EQ(rec.score(), 0);
        sub.finish();
        CHECK_EQ(rec.intScore, 1000000);
    }
    {
        // 对照：991234.x 不会触发特判。
        se::ManiaScoreRecord rec(0, 4, false);
        se::ManiaScoreSubmission sub(&rec, 4, 4, true);
        sub.setupMultiplier();
        rec.doubleAcc = 991234.5;  // ToEven ⇒ 991234
        sub.finish();
        CHECK_EQ(rec.intScore, 991234);
    }
}

// ScoreV2 总分：combo 池 0/0 = NaN ⇒ Math.Round(NaN) = NaN ⇒ conv.i4 = 0x80000000
// （spec 03 §3.3 / Q1）。根因是 Obf011812 对 mania 的所有 flag 都返回 0。
static void testV2ScoreNanPool() {
    CHECK_EQ(se::dotnetConvI4(std::nan("")), INT32_MIN);
    CHECK_EQ(se::dotnetConvI4(1e300), INT32_MIN);
    CHECK_EQ(se::dotnetConvI4(16.9), 16);   // 向零截断
    CHECK_EQ(se::dotnetConvI4(-16.9), -16);
    // 根因：mania 的每个 JudgmentFlags 都落 default ⇒ 0。
    for (int32_t flags : {kFMax, kF300, kF200, kF100, kF50, kFTick, kFMiss,
                          kFBreak, 0}) {
        se::JudgmentRecord j;
        j.flags = flags;
        CHECK_EQ(j.scoreValue(), 0);
    }
    se::JudgmentRecord generic;
    generic.flags = 1024;   // 非 mania 的 300 flag：Obf011812 给出 300
    CHECK_EQ(generic.scoreValue(), 300);
    generic.flags = 16384;  // 500
    CHECK_EQ(generic.scoreValue(), 500);

    // 开局三步（spec 03 §5.5 / Q6）：清 → 模拟判定 → 快照。
    se::ManiaScoreRecord rec(0, 4, true);
    CHECK(rec.hasV2());
    se::ManiaScoreV2Processor* proc = rec.alternateScoreProcessor.get();
    se::ManiaScoreSubmission sub(&rec, 4, 4, true);
    sub.setupMultiplier();
    rec.resetScore(false);
    se::JudgeTargetState target;
    sub.onJudgment(kFMax, target);
    sub.onJudgment(kFMax, target);
    CHECK_EQ(proc->judgmentCount(), 2u);
    CHECK_EQ(proc->pool(), 0.0);  // scoreValue() == 0 ⇒ 池恒 0
    rec.resetScore(true);
    CHECK_EQ(proc->snapshotCount(), 2);
    CHECK_EQ(proc->snapshotPool(), 0.0);
    CHECK(std::isnan(proc->comboPart()));  // 200000.0 * 0 / 0
    CHECK_EQ(proc->totalScore(), INT32_MIN);
    CHECK_EQ(rec.score(), INT32_MIN);
    // 唯一的除零守卫是 Obf009372（快照条数），不是 Obf009373。
    se::ManiaScoreRecord guard(0, 4, true);
    guard.resetScore(true);
    CHECK_EQ(guard.alternateScoreProcessor->snapshotCount(), 0);
    CHECK_EQ(guard.alternateScoreProcessor->totalScore(), 0);

    // acc 部分（spec 03 §3.3 Obf007869）：
    //   800000.0 * acc^(2f + 2f*acc) * (total / Obf009372)，指数是单精度运算。
    {
        se::ManiaScoreRecord r(0, 4, true);
        auto* p = r.alternateScoreProcessor.get();
        se::ManiaScoreSubmission s(&r, 4, 4, true);
        s.setupMultiplier();
        se::JudgeTargetState t;
        r.resetScore(false);
        s.onJudgment(kFMax, t);   // 让快照条数为 1
        r.resetScore(true);
        CHECK_EQ(p->snapshotCount(), 1);
        r.counts.count300 = 1;    // total = 2（V2 acc = 605/610）
        r.counts.countMax = 1;
        const float acc = r.accuracy();
        CHECK_FEQ(acc, static_cast<float>(300 + 305) / static_cast<float>(2 * 305));
        const float exponentF = 2.0f + 2.0f * acc;  // 单精度
        const double expected = 800000.0 *
                                std::pow(static_cast<double>(acc),
                                         static_cast<double>(exponentF)) *
                                (static_cast<double>(r.counts.total()) / 1.0);
        const double got = p->accPart();
        CHECK(std::abs(got - expected) <= std::abs(expected) * 1e-12);
    }
}

// counts / combo 提交路径（spec 03 §1.1 / §4.2 / §4.3 / §4.5）：
// 320 ⇒ CountMax，300 ⇒ Count300 …；7 与 -1048576 不加任何 count，
// 但 7 是正数 ⇒ combo+1，-1048576 为负 ⇒ 断连。
static void testComboAndCounts() {
    se::ManiaScoreRecord rec(0, 4, false);
    se::ManiaScoreSubmission sub(&rec, 4, 4, true);
    sub.setupMultiplier();
    sub.resetComboAndHealth();
    se::JudgeTargetState target;
    sub.onJudgment(kFMax, target);
    CHECK_EQ(rec.counts.countMax, 1);
    CHECK_EQ(rec.counts.count300, 0);
    CHECK_EQ(rec.counts.currentCombo, 1);
    CHECK_EQ(rec.counts.maxCombo, 1);
    sub.onJudgment(kF300, target);
    CHECK_EQ(rec.counts.count300, 1);
    CHECK_EQ(rec.counts.currentCombo, 2);
    sub.onJudgment(kF200, target);
    CHECK_EQ(rec.counts.count200, 1);
    sub.onJudgment(kF100, target);
    CHECK_EQ(rec.counts.count100, 1);
    sub.onJudgment(kF50, target);
    CHECK_EQ(rec.counts.count50, 1);
    CHECK_EQ(rec.counts.maxCombo, 5);
    sub.onJudgment(kFMiss, target);
    CHECK_EQ(rec.counts.countMiss, 1);
    CHECK_EQ(rec.counts.currentCombo, 0);  // 断连
    CHECK_EQ(rec.counts.maxCombo, 5);      // MaxCombo 从不在断连时下降
    // 7（LN tick）：不加 count，但 flags > 0 ⇒ combo +1。
    sub.onJudgment(kFTick, target);
    CHECK_EQ(rec.counts.countMiss, 1);
    CHECK_EQ(rec.counts.currentCombo, 1);
    // -1048576（LN break）：不加 count，断连。
    sub.onJudgment(kFBreak, target);
    CHECK_EQ(rec.counts.currentCombo, 0);
    CHECK_EQ(rec.counts.count300, 1);
    // flags == 0 ⇒ 立即返回，什么都不动。
    const uint16_t comboBefore = rec.counts.currentCombo;
    const int32_t scoreBefore = rec.intScore;
    sub.onJudgment(0, target);
    CHECK_EQ(rec.counts.currentCombo, comboBefore);
    CHECK_EQ(rec.intScore, scoreBefore);
    // counts 的和 = 5 个判定（MAX/300/200/100/50）+ 1 miss。
    CHECK_EQ(rec.counts.total(), 6);
    // CurrentCombo 是 ushort：65536 次连击后回绕到 0（MaxCombo 保持 int32）。
    // 用 LN tick（flags 7：combo+1 但 num=0、num2=0）避免分数累加溢出。
    {
        se::ManiaScoreRecord r2(0, 4, false);
        se::ManiaScoreSubmission s2(&r2, 4, 4, true);
        s2.setupMultiplier();
        s2.resetComboAndHealth();
        se::JudgeTargetState t2;
        for (int i = 0; i < 65535; ++i) s2.onJudgment(kFTick, t2);
        CHECK_EQ(r2.counts.currentCombo, 65535);
        CHECK_EQ(r2.counts.maxCombo, 65535);
        s2.onJudgment(kFTick, t2);
        CHECK_EQ(r2.counts.currentCombo, 0);   // conv.u2 回绕
        CHECK_EQ(r2.counts.maxCombo, 65536);   // int32，不回绕
        CHECK_EQ(r2.counts.countMax, 0);       // tick 不加任何 count
        CHECK_EQ(r2.counts.total(), 0);
    }
}

// 记录级复位（spec 03 §3.6）：CountMiss 被写两次；带处理器时 doubleAcc 不清。
static void testResetScore() {
    se::ManiaScoreRecord rec(0, 4, false);
    rec.counts.count300 = 5;
    rec.counts.count100 = 4;
    rec.counts.count50 = 3;
    rec.counts.countMax = 2;
    rec.counts.count200 = 1;
    rec.counts.countMiss = 9;
    rec.counts.maxCombo = 42;
    rec.counts.currentCombo = 7;
    rec.intScore = 1234;
    rec.doubleAcc = 5678.0;
    rec.resetScore(false);
    CHECK_EQ(rec.counts.count300, 0);
    CHECK_EQ(rec.counts.count100, 0);
    CHECK_EQ(rec.counts.count50, 0);
    CHECK_EQ(rec.counts.countMax, 0);
    CHECK_EQ(rec.counts.count200, 0);
    CHECK_EQ(rec.counts.countMiss, 0);
    CHECK_EQ(rec.counts.maxCombo, 0);
    CHECK_EQ(rec.counts.currentCombo, 0);
    CHECK_EQ(rec.intScore, 0);
    CHECK(rec.doubleAcc == 0.0);
    // 带 ScoreV2 处理器：Obf019481（doubleAcc）不被清（spec 03 §3.6 括号说明）。
    se::ManiaScoreRecord v2(0, 4, true);
    v2.counts.countMax = 5;
    v2.doubleAcc = 42.0;
    v2.resetScore(false);
    CHECK_EQ(v2.counts.countMax, 0);
    CHECK(v2.doubleAcc == 42.0);
}

// ===========================================================================
// 6. 端到端锚：fixture_4k.osr + fixture_4k.osu
// ===========================================================================
//
// 权威口令来自实现阶段的人工验算（与 spec 02/03 同源）：
//   ScoreV2 pass: MAX=4 / 300=1 = .osr 声明总数 5
//   V1 pass:      intScore=992188、acc=1.0、grade=X
//   V2 pass:      acc=0.99672(1520/1525)、grade=S、score=-2147483648 (NaN 池)
// .osr 走现成的 osr_reader → lzmaDecode → decodeReplayFrames 链路（与
// api_server::loadReplaySession 相同），因此这里验证的是真实入口而非合成帧。
static std::string findFixture(const char* relative) {
    const char* prefixes[] = {"", "../", "../../", "../../../"};
    for (const char* prefix : prefixes) {
        std::string path = std::string(prefix) + relative;
        std::ifstream probe(path, std::ios::binary);
        if (probe) return path;
    }
    return std::string();
}

static bool readWhole(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

static void testFixtureEndToEnd() {
    const std::string osrPath = findFixture("fixtures/replays/fixture_4k.osr");
    const std::string osuPath = findFixture("fixtures/beatmaps/fixture_4k.osu");
    if (osrPath.empty() || osuPath.empty()) {
        std::printf("  [skip] fixture_4k.osr/.osu not found from cwd=%s\n",
                    std::filesystem::current_path().string().c_str());
        ++g_failures;
        return;
    }
    std::vector<uint8_t> osrBytes;
    CHECK(readWhole(osrPath, osrBytes));
    OsrParseResult osr = parseOsr(osrBytes);
    CHECK(osr.error.empty());
    if (!osr.error.empty()) return;
    CHECK_EQ(osr.header.mode, 3);      // OsuMania
    CHECK_EQ(osr.header.mods, 0u);     // NoMod
    // .osr 声明的 counts：geki(MAX)=5，其它全 0（gen_fixture.py）。
    CHECK_EQ(osr.header.countGeki, 5);
    CHECK_EQ(osr.header.count300, 0);
    CHECK_EQ(osr.header.countMiss, 0);
    const int64_t declaredTotal =
        static_cast<int64_t>(osr.header.count300) + osr.header.count100 +
        osr.header.count50 + osr.header.countGeki + osr.header.countKatu +
        osr.header.countMiss;
    CHECK_EQ(declaredTotal, 5);

    LzmaDecodeResult decoded =
        lzmaDecode(osr.header.compressedData.data(), osr.header.compressedData.size());
    CHECK(decoded.ok);
    if (!decoded.ok) return;
    ReplayFrameDecodeResult decodedFrames = decodeReplayFrames(decoded.output);
    CHECK(decodedFrames.ok);
    if (!decodedFrames.ok) return;

    std::vector<uint8_t> osuBytes;
    CHECK(readWhole(osuPath, osuBytes));
    OsuParseResult parsedOsu =
        parseOsu(std::string(osuBytes.begin(), osuBytes.end()));
    CHECK(parsedOsu.ok);
    if (!parsedOsu.ok) return;
    CHECK(parsedOsu.beatmap.isMania());
    CHECK_EQ(parsedOsu.beatmap.objects.size(), 4u);
    CHECK(parsedOsu.beatmap.overallDifficulty == 8.0);
    // mods 里没有键数 mod ⇒ 用谱面的 CircleSize。
    const int keyCountFromMod = keyCountFromMods(osr.header.mods);
    CHECK_EQ(keyCountFromMod, 0);
    const int keyCount = parsedOsu.beatmap.keyCount;
    CHECK_EQ(keyCount, 4);

    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(parsedOsu.beatmap.overallDifficulty);
    opt.od = parsedOsu.beatmap.overallDifficulty;
    opt.nativeManiaBeatmap = parsedOsu.beatmap.isMania();
    opt.mods = osr.header.mods;

    // ---- V1 pass ----
    MatchResult v1 = matchReplayToObjects(parsedOsu.beatmap.objects, keyCount,
                                          decodedFrames.frames, opt);
    CHECK_EQ(v1.unmatchedObjects, 0);
    CHECK(v1.unmatchedInputs.empty());
    // 2 rice + 1 rice + LN(头+尾) = 5 事件。
    CHECK_EQ(v1.events.size(), 5u);
    CHECK_EQ(v1.holds.size(), 1u);
    CHECK_EQ(v1.sourceScore.countMax, 3);   // 3 个 rice 全 MAX
    CHECK_EQ(v1.sourceScore.count300, 1);   // V1 的 LN 是一次尾判
    CHECK_EQ(v1.sourceScore.count100, 0);
    CHECK_EQ(v1.sourceScore.count200, 0);
    CHECK_EQ(v1.sourceScore.count50, 0);
    CHECK_EQ(v1.sourceScore.countMiss, 0);
    CHECK_EQ(v1.sourceScore.maxCombo, 4);
    // acc = (C300+CMax)*300 / (total*300) = 1200/1200 = 1.0f
    CHECK_FEQ(v1.sourceScore.accuracy, 1.0f);
    CHECK_EQ(v1.sourceScore.grade, kX);
    // V1 整型分：手算 992188（doubleAcc 992187.5 经 (int)Round 覆盖）
    CHECK(v1.sourceScore.doubleAcc == 992187.5);
    CHECK_EQ(v1.sourceScore.intScore, 992188);
    // LN 头是显示档（V1 头判恒 0），尾判 300（|3150-3200| = 50 → 300 档）。
    CHECK_EQ(v1.holds[0].head.judgement, HitResult::Perfect);
    CHECK_EQ(v1.holds[0].tail.judgement, HitResult::Great);
    CHECK_EQ(v1.holds[0].tail.offsetMs.value_or(0.0), 50.0);
    CHECK(!v1.holds[0].releasedEarly);   // 3250 > 3200（松手在尾之后）
    CHECK(v1.holds[0].heldThroughTail);
    CHECK(v1.holds[0].tail.v1Judgement.has_value());
    CHECK_EQ(*v1.holds[0].tail.v1Judgement, HitResult::Great);
    // 上报的窗口：V1 = 路径 B，V2 = 路径 A（OD8 恰好同值）。
    CHECK_EQ(static_cast<int>(v1.windowsV1.perfect), 16);
    CHECK_EQ(static_cast<int>(v1.windowsV1.great), 40);
    CHECK_EQ(static_cast<int>(v1.windowsV1.miss), 164);
    CHECK(v1.integralWindows);

    // ---- ScoreV2 pass ----
    MatchOptions optV2 = opt;
    optV2.scoreV2 = true;
    MatchResult v2 = matchReplayToObjects(parsedOsu.beatmap.objects, keyCount,
                                         decodedFrames.frames, optV2);
    CHECK_EQ(v2.unmatchedObjects, 0);
    CHECK_EQ(v2.sourceScore.countMax, 4);   // 3 rice + LN 头（V2 头判独立计票）
    CHECK_EQ(v2.sourceScore.count300, 1);   // LN 尾
    CHECK_EQ(v2.sourceScore.countMiss, 0);
    const int64_t v2Total = v2.sourceScore.count300 + v2.sourceScore.count100 +
                            v2.sourceScore.count50 + v2.sourceScore.countMax +
                            v2.sourceScore.count200 + v2.sourceScore.countMiss;
    CHECK_EQ(v2Total, declaredTotal);  // MAX=4 / 300=1 == 声明的 5
    // acc = (300*1 + 305*4) / (5*305) = 1520/1525，单精度除。
    CHECK_FEQ(v2.sourceScore.accuracy,
              static_cast<float>(300 * 1 + 305 * 4) / static_cast<float>(5 * 305));
    CHECK_EQ(v2.sourceScore.grade, kS);
    // V2 分数走 NaN 池 ⇒ conv.i4 = -2147483648。
    CHECK_EQ(v2.sourceScore.scoreV2, INT32_MIN);
    CHECK(v2.sourceScore.scoreV2NanPool);
    // V2 pass 不写 V1 整型分（带处理器时走 AlternateScoreProcessor）。
    CHECK_EQ(v2.sourceScore.intScore, 0);
    // V2 的 LN 头是真判定（不是显示档）。
    CHECK_EQ(v2.holds[0].head.judgement, HitResult::Perfect);
    CHECK_EQ(v2.holds[0].tail.judgement, HitResult::Great);
    // 两套窗口：OD9 才会露出 16 vs 15 的位级差异，这里两侧同值。
    CHECK_EQ(static_cast<int>(v2.windowsV2.perfect), 16);
    // OD9 的 v1/v2 差异（任务书点名的锚点）。
    {
        MatchOptions o9 = opt;
        o9.od = 9.0;
        MatchResult r9 = matchReplayToObjects(parsedOsu.beatmap.objects, keyCount,
                                              decodedFrames.frames, o9);
        CHECK_EQ(static_cast<int>(r9.windowsV1.perfect), 16);  // 路径 B 固定 16
        CHECK_EQ(static_cast<int>(r9.windowsV2.perfect), 15);  // 路径 A DR(9)
    }
}

// ===========================================================================
// 7. legacy 载体与既有外围测试（不得放松）
// ===========================================================================

// MatchOptions.rate 是 legacy 载体：判定窗只由 mods 位驱动（spec 01 §3.3）。
static void testRateWindows() {
    struct Case {
        uint32_t mods;
        double rate;
        int64_t pressOffset;
        HitResult expected;
    };
    const Case cases[] = {
        {0, 1.0, 20, HitResult::Great},              // 20 > WindowMax 16
        {0, 1.5, 20, HitResult::Great},              // rate 不参与判定
        {0, 1.5, 13, HitResult::Perfect},
        {kDoubleTime, 1.0, 20, HitResult::Perfect},  // (int)(16*1.5) = 24
        {kEasy, 1.0, 20, HitResult::Perfect},        // (int)(16*1.4) = 22
        {kHardRock, 1.0, 13, HitResult::Great},      // (int)(16/1.4) = 11
        {0, 1.0, 13, HitResult::Perfect},
        {kHalfTime, 1.0, 32, HitResult::Good},       // 32 > (int)(40*0.75) = 30
        {0, 1.0, 32, HitResult::Great},              // 32 ≤ Window300 40
    };
    std::vector<ManiaHitObject> objs = {note(0, 1000)};
    for (const Case& c : cases) {
        std::vector<ReplayFrame> frames = {
            frame(0, 0, 0),
            frame(1000 + c.pressOffset, 1u, 0),
            frame(1600, 0, 1u),
        };
        MatchOptions opt;
        opt.windows = ManiaWindows::fromOD(8.0);
        opt.od = 8.0;
        opt.mods = c.mods;
        opt.rate = c.rate;
        MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
        CHECK_EQ(r.events.size(), 1u);
        if (r.events.size() == 1) {
            CHECK_EQ(static_cast<int>(r.events[0].judgement),
                     static_cast<int>(c.expected));
            CHECK(r.events[0].offsetMs && *r.events[0].offsetMs ==
                                             (double)c.pressOffset);
        }
    }
}

// 基础 matcher 行为（与源码语义一致的部分，保持不变）。
static void testSimpleNote() {
    std::vector<ManiaHitObject> objs = {note(0, 1000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1005, 1u << 0, 0),   // press col0, +5ms -> 320 (MAX)
        frame(1100, 0, 1u << 0),   // release
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK_EQ(r.events.size(), 1u);
    CHECK_EQ(static_cast<int>(r.events[0].judgement),
             static_cast<int>(HitResult::Perfect));
    CHECK(r.events[0].offsetMs && *r.events[0].offsetMs == 5.0);
    CHECK_EQ(r.unmatchedObjects, 0);
    CHECK(r.unmatchedInputs.empty());
}

static void testMissNoInput() {
    std::vector<ManiaHitObject> objs = {note(1, 1000)};
    std::vector<ReplayFrame> frames = {frame(0, 0, 0), frame(3000, 0, 0)};
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK_EQ(r.events.size(), 1u);
    CHECK_EQ(static_cast<int>(r.events[0].judgement),
             static_cast<int>(HitResult::Miss));
    CHECK(!r.events[0].offsetMs.has_value());  // 从未按下 ⇒ 无 offset
}

static void testChordDifferentColumns() {
    // 同一时刻、不同列的 chord：两列各自独立消费自己的边沿。
    std::vector<ManiaHitObject> objs = {note(0, 1000), note(1, 1000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1003, (1u << 0), 0),
        frame(1009, (1u << 1), (1u << 0)),
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK_EQ(r.events.size(), 2u);
    for (const HitEvent& ev : r.events)
        CHECK_EQ(static_cast<int>(ev.judgement),
                 static_cast<int>(HitResult::Perfect));
}

// LN 尾判的 V1 合并规则（spec 02 §4.5）：effEnd 在尾之后按镜像折算
// （`effEnd = EndTime - (ReleaseTime - EndTime)`），而 ExpireManiaNote 一旦先
// 到期（now ≥ EndTime + Window100 = 2103）就用 `effEnd = now` 的"还按着"口径。
static void testHoldTailCases() {
    struct Case {
        int64_t release;   // 绝对松手时刻（尾 = 2000，Window50 = 127，Window100 = 103）
        HitResult expected;
        const char* why;
    };
    const Case cases[] = {
        {1500, HitResult::Miss, "提前松手 ⇒ Judge 守卫 2 发 -1048576（break），后续按 miss 结算"},
        {1980, HitResult::Perfect, "effEnd 1980 / tailErr 20 ⇒ combined 20 ≤ WMax*2.4"},
        {2000, HitResult::Perfect, "恰好在尾松手 ⇒ tailErr 0"},
        {2050, HitResult::Great, "effEnd 1950 / tailErr 50 ⇒ 300 档（combined ≤ W300*2.2）"},
        {2100, HitResult::Good, "effEnd 1900 / tailErr 100 ⇒ 200 档（≤ W200*2）"},
        {2200, HitResult::Ok, "已过到期点 2103 ⇒ 到期判定用 effEnd = now = 2200 ⇒ 100 档"},
        {3000, HitResult::Meh, "同上，误差更大 ⇒ 50 档"},
    };
    for (const Case& c : cases) {
        std::vector<ManiaHitObject> objs = {hold(2, 1000, 2000)};
        std::vector<ReplayFrame> frames = {
            frame(0, 0, 0),
            frame(1000, 1u << 2, 0),           // 头判（V1 恒 0，仅 HeadJudged）
            frame(c.release, 0, 1u << 2),      // 松手 → 尾判（可能已被到期接管）
        };
        MatchOptions opt;
        opt.windows = ManiaWindows::fromOD(8.0);
        MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
        CHECK_EQ(r.holds.size(), 1u);
        if (r.holds.size() != 1) continue;
        if (static_cast<int>(r.holds[0].tail.judgement) !=
            static_cast<int>(c.expected)) {
            std::printf("  case release=%lld: %s\n", (long long)c.release, c.why);
        }
        CHECK_EQ(static_cast<int>(r.holds[0].tail.judgement),
                 static_cast<int>(c.expected));
        CHECK_EQ(static_cast<int>(r.holds[0].head.judgement),
                 static_cast<int>(HitResult::Perfect));
        // V1 口径下整条 LN 用尾档着色（stable 的一次判定）。
        CHECK(r.holds[0].tail.v1Judgement.has_value());
        CHECK_EQ(static_cast<int>(*r.holds[0].tail.v1Judgement),
                 static_cast<int>(c.expected));
    }
}

// V2 把 LN 拆成头/尾两次独立判定（头误差链 + 尾误差 /1.5）。
static void testHoldTailScoreV2() {
    std::vector<ManiaHitObject> objs = {hold(2, 1000, 2000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1010, 1u << 2, 0),   // 头：err 10 ≤ 16 → MAX
        frame(2150, 0, 1u << 2),   // 尾：|150|/1.5 = 100 → ≤ Window100 103 → 100
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    opt.od = 8.0;
    opt.scoreV2 = true;
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK_EQ(r.holds.size(), 1u);
    if (r.holds.size() != 1) return;
    CHECK_EQ(static_cast<int>(r.holds[0].head.judgement),
             static_cast<int>(HitResult::Perfect));
    CHECK_EQ(static_cast<int>(r.holds[0].tail.judgement),
             static_cast<int>(HitResult::Ok));
    CHECK(!r.holds[0].tail.v1Judgement.has_value());  // V2 不做 V1 合并
    CHECK_EQ(r.sourceScore.countMax, 1);
    CHECK_EQ(r.sourceScore.count100, 1);
}

// legacy OD 载体：od 未设时从 windows.miss 反解 OD = (188-miss)/3。
static void testLegacyOdCarrier() {
    std::vector<ManiaHitObject> objs = {note(0, 1000)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1013, 1u, 0),   // +13：OD8 的 WindowMax 16 ⇒ MAX
        frame(1600, 0, 1u),
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);  // miss 槽 = 164 ⇒ od = 8
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    CHECK_EQ(r.events.size(), 1u);
    CHECK_EQ(static_cast<int>(r.events[0].judgement),
             static_cast<int>(HitResult::Perfect));
    CHECK_EQ(static_cast<int>(r.windowsV1.perfect), 16);
    CHECK_EQ(static_cast<int>(r.windowsV1.great), 40);
    CHECK_EQ(static_cast<int>(r.windowsV1.miss), 164);
}

// ---- 与 mania 判定无关的既有覆盖，保持（纯统计 / scores.db / 分析外壳）----
static void testStats() {
    std::vector<double> v = {5, -3, 10, -8, 2};
    OffsetStats s = computeOffsetStats(v);
    CHECK_EQ(s.count, 5);
    CHECK(s.early == 2 && s.late == 3);
    CHECK(std::abs(s.mean - 1.2) < 1e-9);
    CHECK(s.median == 2.0);
    CHECK(s.p95 >= 8.0);
}

static void testAnalysis() {
    std::vector<ManiaHitObject> objs = {note(0, 1000), note(1, 2000),
                                        hold(2, 3000, 3200)};
    std::vector<ReplayFrame> frames = {
        frame(0, 0, 0),
        frame(1002, 1u << 0, 0),
        frame(1100, 0, 1u << 0),
        frame(2010, 1u << 1, 0),
        frame(2100, 0, 1u << 1),
        frame(2999, 1u << 2, 0),
        frame(3250, 0, 1u << 2),
    };
    MatchOptions opt;
    opt.windows = ManiaWindows::fromOD(8.0);
    MatchResult r = matchReplayToObjects(objs, 4, frames, opt);
    GlobalAnalysis g = analyzeMatch(r, 4);
    CHECK_EQ(g.counts.judged(), 4);  // 2 note heads + hold head + tail
    CHECK_EQ(g.headCounts.judged(), 3);
    CHECK_EQ(g.perColumn.size(), 4u);
    CHECK_EQ(g.perColumn[2].counts.judged(), 2);
    SegmentAnalysis seg = analyzeSegment(r, 4, 0, 1500);
    CHECK_EQ(seg.counts.judged(), 1);
    SegmentAnalysis segAll = analyzeSegment(r, 4, 0, g.lastEventTimeMs + 1);
    CHECK_EQ(segAll.counts.judged(), g.counts.judged());
    CHECK_EQ(segAll.headCounts.judged(), g.headCounts.judged());
    // stable 口径：一个物件一次判定（LN 取尾档）⇒ 3。
    CHECK_EQ(g.stableCounts.judged(), 3);
    CHECK_EQ(g.stableCounts.perfect, 2);
    CHECK_EQ(g.stableCounts.great, 1);
    CHECK(std::abs(g.stableAccuracyV1 - 100.0) < 1e-9);
    CHECK_EQ(segAll.stableCounts.judged(), g.stableCounts.judged());
    SegmentAnalysis segHold = analyzeSegment(r, 4, 2500, 4000);
    CHECK_EQ(segHold.counts.judged(), 2);
    CHECK_EQ(segHold.stableCounts.judged(), 1);
    CHECK(std::abs(segHold.stableAccuracyV1 - 100.0) < 1e-9);
}

// ---- scores.db parser (synthetic byte stream) ----
static void put8(std::string& b, uint8_t v) { b.push_back(static_cast<char>(v)); }
static void put16(std::string& b, uint16_t v) {
    for (int i = 0; i < 2; ++i)
        b.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
static void put32(std::string& b, int32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
static void put64(std::string& b, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        b.push_back(static_cast<char>((u >> (8 * i)) & 0xff));
}
static void putOsuString(std::string& b, const std::string& s) {
    put8(b, 0x0b);
    size_t len = s.size();
    do {
        uint8_t byte = static_cast<uint8_t>(len & 0x7f);
        len >>= 7;
        if (len) byte |= 0x80;
        put8(b, byte);
    } while (len);
    b.append(s);
}
static void putEmptyString(std::string& b) { put8(b, 0x00); }

static void putScore(std::string& b, const std::string& md5,
                     const std::string& player, const std::string& replayMd5,
                     int32_t mods, bool writeHpString, int64_t onlineId) {
    put8(b, 3);                    // mania
    put32(b, 20260924);            // client version
    putOsuString(b, md5);
    putOsuString(b, player);
    putOsuString(b, replayMd5);
    put16(b, 400); put16(b, 30); put16(b, 2);   // 300 / 100 / 50
    put16(b, 120); put16(b, 8); put16(b, 1);    // geki / katu / miss
    put32(b, 987654);
    put16(b, 321);
    put8(b, 1);                    // full combo
    put32(b, mods);
    if (writeHpString) putEmptyString(b);
    put64(b, 637800000000000000LL + onlineId);  // .NET ticks
    put32(b, -1);                  // sentinel
    put64(b, onlineId);
    if (mods & (1 << 23)) put64(b, 0);          // TargetPractice accumulator
}

static void testScoresDb() {
    const std::string md5A(32, 'a');
    const std::string md5B(32, 'b');
    const std::string md5C(32, 'c');
    std::string b;
    put32(b, 20260924);            // db version
    put32(b, 2);                   // group count
    putOsuString(b, md5A);
    put32(b, 1);
    putScore(b, md5A, "player1", md5B, 0, true, 44068);
    putOsuString(b, md5C);
    put32(b, 2);
    putScore(b, md5C, "player2", md5A, 1 << 15, true, 12345);   // 4K
    putScore(b, md5C, "player3", md5B, 1 << 23, false, 6789);   // TP, no hp string

    std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "mra_test_scores.db";
    {
        std::ofstream probe(tmp, std::ios::binary | std::ios::trunc);
        if (!probe)
            tmp = std::filesystem::current_path() / "mra_test_scores.db";
    }
    auto writeTmp = [&tmp](const std::string& bytes) {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::printf("  tmp open FAILED: %s errno=%d\n",
                        tmp.string().c_str(), errno);
            ++g_failures;
            return;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            std::printf("  tmp write FAILED: %s errno=%d\n",
                        tmp.string().c_str(), errno);
            ++g_failures;
        }
    };
    writeTmp(b);

    ScoresDb db;
    std::string err;
    bool ok = parseScoresDb(tmp.string(), db, err);
    CHECK(ok);
    if (!ok) {
        std::printf("  scores.db parse err: %s\n", err.c_str());
        std::filesystem::remove(tmp);
        return;
    }
    CHECK_EQ(db.version, 20260924);
    CHECK_EQ(db.groups.size(), 2u);
    CHECK(db.groups[0].beatmapMd5 == md5A);
    CHECK_EQ(db.groups[0].scores.size(), 1u);
    const ScoreEntry& s1 = db.groups[0].scores[0];
    CHECK_EQ(s1.mode, 3);
    CHECK_EQ(s1.clientVersion, 20260924);
    CHECK(s1.beatmapMd5 == md5A);
    CHECK(s1.playerName == "player1");
    CHECK(s1.replayMd5 == md5B);
    CHECK(s1.c300 == 400 && s1.c100 == 30 && s1.c50 == 2);
    CHECK(s1.geki == 120 && s1.katu == 8 && s1.miss == 1);
    CHECK_EQ(s1.totalScore, 987654);
    CHECK_EQ(s1.maxCombo, 321);
    CHECK(s1.fullCombo);
    CHECK_EQ(s1.mods, 0u);
    CHECK_EQ(s1.onlineId, 44068);
    CHECK_EQ(s1.playedTicks, 637800000000000000LL + 44068);
    CHECK_EQ(s1.playedMs,
             (637800000000000000LL + 44068) / 10000 - 62135596800000LL);
    CHECK_EQ(db.groups[1].scores.size(), 2u);
    CHECK_EQ(db.groups[1].scores[0].mods, 1u << 15);
    CHECK_EQ(db.groups[1].scores[0].onlineId, 12345);
    CHECK(db.groups[1].scores[1].hpGraph.empty());
    CHECK_EQ(db.groups[1].scores[1].onlineId, 6789);

    // Corruption: break the -1 sentinel of the last score -> strict failure.
    std::string bad = b;
    bad[bad.size() - 20] = 0x2a;
    writeTmp(bad);
    ScoresDb db2;
    CHECK(!parseScoresDb(tmp.string(), db2, err));
    std::filesystem::remove(tmp);
}

// ===========================================================================
// main
// ===========================================================================
static void run(const char* name, void (*fn)()) {
    const int before = g_failures;
    std::printf("[%s] %s\n", "test", name);
    std::fflush(stdout);
    try {
        fn();
    } catch (const std::exception& e) {
        std::printf("EXCEPTION in %s: %s\n", name, e.what());
        std::fflush(stdout);
        ++g_failures;
    }
    if (g_failures != before) std::printf("  ^ %s FAILED\n", name);
    std::fflush(stdout);
}

int main() {
    // 1. 判定窗四路径
    run("windowPathA(ScoreV2/DifficultyRange)", testWindowPathA);
    run("windowPathB(native mania float)", testWindowPathB);
    run("windowPathCD(converted Round>4)", testWindowPathCD);
    run("windowModFactors(HR/EZ/DT/HT)", testWindowModFactors);
    // 2. 输入侧
    run("columnEdgeLifetime", testColumnEdgeLifetime);
    run("replayBit(Math.Pow)", testReplayBit);
    run("specialKeyRightInputRotation", testSpecialKeyRightInputRotation);
    run("remapManiaColumn", testRemapManiaColumn);
    run("edgeLifetimeAcrossTicks", testEdgeLifetimeAcrossTicks);
    // 3. 绑定/仲裁
    run("consumedOneObjectPerColumn", testConsumedOneObjectPerColumn);
    run("expireBeforeEdge", testExpireBeforeEdge);
    run("futureJudgedObjectLocksColumn", testFutureJudgedObjectLocksColumn);
    run("activeObjectsWindow", testActiveObjectsWindow);
    run("activeObjectsEndTimeBinarySearch", testActiveObjectsEndTimeBinarySearch);
    // 4. note/LN 状态机
    run("riceJudgeWindowChain", testRiceJudgeWindowChain);
    run("rapidPressStats", testRapidPressStats);
    run("longNoteJudgeGuards", testLongNoteJudgeGuards);
    run("longNoteCaps", testLongNoteCaps);
    run("longNoteTick", testLongNoteTick);
    run("longNoteHead", testLongNoteHead);
    run("perfectRequires300", testPerfectRequires300);
    // 5. 计分
    run("countsUshortWrap", testCountsUshortWrap);
    run("accuracyV1V2", testAccuracyV1V2);
    run("gradeTable", testGradeTable);
    run("v1ScoreTruncation", testV1ScoreTruncation);
    run("v2ScoreNanPool", testV2ScoreNanPool);
    run("comboAndCounts", testComboAndCounts);
    run("resetScore", testResetScore);
    // 6. 端到端
    run("fixtureEndToEnd", testFixtureEndToEnd);
    // 7. legacy 载体 / 外围
    run("rateWindows", testRateWindows);
    run("simpleNote", testSimpleNote);
    run("missNoInput", testMissNoInput);
    run("chordDifferentColumns", testChordDifferentColumns);
    run("holdTailCases", testHoldTailCases);
    run("holdTailScoreV2", testHoldTailScoreV2);
    run("legacyOdCarrier", testLegacyOdCarrier);
    run("stats", testStats);
    run("analysis", testAnalysis);
    run("scoresDb", testScoresDb);

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("%d FAILURES (%d checks)\n", g_failures, g_checks);
    return 1;
}
