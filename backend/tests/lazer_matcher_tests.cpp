// L2 lazer 判定重建引擎单元测试 (合成数据, 无真实玩家信息)。
//
// 覆盖:
//   - 六档窗口公式 (OD 0/5/10/4.3, NM/HT/DT/HR/EZ/HR+HT, floor-在-乘后)
//   - rice press 窗口边界 (恰好 ±win/±1 的整数毫秒近似), (Meh, Miss] press 消耗给 Miss,
//     超 Miss 窗 press 不消耗 (记 unmatchedInput), 早按 >Miss 窗 后再按
//   - rice 强制 miss 时机 (首个 offset > Meh 窗 的 tick)
//   - hold: 头判同 rice / 头 miss ⇒ 尾封顶 MeH / 尾窗 ×1.5 边界 / 不松键强制 Miss /
//     1.5 倍窗内早松不断带 (尾拿满档) / 超窗早松断带后重按尾封顶 MeH /
//     过尾 lenience 区间内 re-press 恢复 holding
//   - 每列 note-lock (FIFO): press 落到最早可判物件; 下一物件 start 时刻起锁上一物件
//   - 跨列 chord / mirror 列像
//   - 确定性 (跑两遍逐位一致)
//   - acc305 已知值
//
// 全部时点为整数毫秒 (与 .osr 帧粒度一致); OD5 NM 窗: 19.5/49.5/82.5/112.5/136.5/173.5。
#include "lazer/lazer_format.h"
#include "mania/lazer_matcher.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mra;

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

// ---- 合成数据构造 ------------------------------------------------------------

ManiaHitObject note(int idx, int col, int64_t t) {
    ManiaHitObject o;
    o.type = ManiaObjectType::Note;
    o.column = col;
    o.startTimeMs = t;
    o.sourceIndex = idx;
    return o;
}

ManiaHitObject hold(int idx, int col, int64_t t, int64_t end) {
    ManiaHitObject o;
    o.type = ManiaObjectType::Hold;
    o.column = col;
    o.startTimeMs = t;
    o.endTimeMs = end;
    o.sourceIndex = idx;
    return o;
}

// 帧序列生成器: 维护上一帧掩码, keyDown/UpMask = 上升/下降沿 (与 decodeReplayFrames 一致)。
struct Frames {
    std::vector<ReplayFrame> v;
    uint32_t prev = 0;

    void add(int64_t t, uint32_t mask) {
        ReplayFrame f;
        f.timeMs = t;
        f.rawKeys = mask;
        f.keyDownMask = mask & ~prev;
        f.keyUpMask = ~mask & prev;
        prev = mask;
        v.push_back(f);
    }
    void press(int64_t t, uint32_t keys) { add(t, prev | keys); }
    void release(int64_t t, uint32_t keys) { add(t, prev & ~keys); }
    void tick(int64_t t) { add(t, prev); }  // 只推进时间 (强制判定检查点)
};

LazerMatchOptions opts(double od = 5.0, double rate = 1.0, bool hr = false,
                       bool ez = false, bool mirror = false, int keys = 4) {
    LazerMatchOptions o;
    o.od = od;
    o.rate = rate;
    o.hardRock = hr;
    o.easy = ez;
    o.mirror = mirror;
    o.keyCount = keys;
    return o;
}

int countOf(const LazerMatchResult& r, HitResult res) {
    switch (res) {
        case HitResult::Perfect: return static_cast<int>(r.counts.perfect);
        case HitResult::Great: return static_cast<int>(r.counts.great);
        case HitResult::Good: return static_cast<int>(r.counts.good);
        case HitResult::Ok: return static_cast<int>(r.counts.ok);
        case HitResult::Meh: return static_cast<int>(r.counts.meh);
        case HitResult::Miss: return static_cast<int>(r.counts.miss);
    }
    return -1;
}

bool nearly(double a, double b) { return std::fabs(a - b) < 1e-9; }

// ---- 窗口公式 ----------------------------------------------------------------

void testWindows() {
    // OD0 / OD5 / OD10 (NM): floor+0.5
    LazerWindows w0 = lazerHitWindows(0.0, 1.0, false, false);
    CHECK(nearly(w0.perfect, 22.5) && nearly(w0.great, 64.5) && nearly(w0.good, 97.5) &&
          nearly(w0.ok, 127.5) && nearly(w0.meh, 151.5) && nearly(w0.miss, 188.5));
    LazerWindows w5 = lazerHitWindows(5.0, 1.0, false, false);
    CHECK(nearly(w5.perfect, 19.5) && nearly(w5.great, 49.5) && nearly(w5.good, 82.5) &&
          nearly(w5.ok, 112.5) && nearly(w5.meh, 136.5) && nearly(w5.miss, 173.5));
    LazerWindows w10 = lazerHitWindows(10.0, 1.0, false, false);
    CHECK(nearly(w10.perfect, 13.5) && nearly(w10.great, 34.5) && nearly(w10.good, 67.5) &&
          nearly(w10.ok, 97.5) && nearly(w10.meh, 121.5) && nearly(w10.miss, 158.5));
    // 小数 OD (两段线性, 不取整 OD): perfect = 22.4−0.6·4.3 = 19.82 → 19.5
    LazerWindows w43 = lazerHitWindows(4.3, 1.0, false, false);
    CHECK(nearly(w43.perfect, 19.5) && nearly(w43.great, 51.5) && nearly(w43.good, 84.5) &&
          nearly(w43.ok, 114.5) && nearly(w43.meh, 138.5) && nearly(w43.miss, 175.5));
    // HT 0.75 (OD5)
    LazerWindows wht = lazerHitWindows(5.0, 0.75, false, false);
    CHECK(nearly(wht.perfect, 14.5) && nearly(wht.great, 36.5) && nearly(wht.good, 61.5) &&
          nearly(wht.ok, 84.5) && nearly(wht.meh, 102.5) && nearly(wht.miss, 129.5));
    // DT 1.5 (OD5)
    LazerWindows wdt = lazerHitWindows(5.0, 1.5, false, false);
    CHECK(nearly(wdt.perfect, 29.5) && nearly(wdt.great, 73.5) && nearly(wdt.good, 123.5) &&
          nearly(wdt.ok, 168.5) && nearly(wdt.meh, 204.5) && nearly(wdt.miss, 259.5));
    // HR ÷1.4 (OD5) — 49/1.4 的 IEEE 双精度结果与 C# `1.4`/`1/1.4` 双精度常量逐位一致
    LazerWindows whr = lazerHitWindows(5.0, 1.0, true, false);
    CHECK(nearly(whr.perfect, 13.5) && nearly(whr.great, 35.5) && nearly(whr.good, 58.5) &&
          nearly(whr.ok, 80.5) && nearly(whr.meh, 97.5) && nearly(whr.miss, 123.5));
    // EZ ×1.4 (OD5): 1/1.4 必须是 double 除法 (上游 const double = 1 / 1.4)
    LazerWindows wez = lazerHitWindows(5.0, 1.0, false, true);
    CHECK(nearly(wez.perfect, 27.5) && nearly(wez.great, 68.5) && nearly(wez.good, 114.5) &&
          nearly(wez.ok, 156.5) && nearly(wez.meh, 190.5) && nearly(wez.miss, 242.5));
    // HR+HT 叠乘: m = 0.75/1.4 (floor 在乘后)
    LazerWindows whrht = lazerHitWindows(5.0, 0.75, true, false);
    CHECK(nearly(whrht.perfect, 10.5) && nearly(whrht.great, 26.5));
    printf("[windows] ok\n");
}

// ---- rice: 窗口边界 / press 消耗 / 强制 miss ---------------------------------

void testRiceBoundaries() {
    // OD5 NM, note @1000 col0
    std::vector<ManiaHitObject> objs{note(0, 0, 1000)};

    // 恰好 ±19 (≤19.5) → Perfect; ±20 → Great
    for (int off : {19, -19}) {
        Frames f;
        f.press(1000 + off, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Perfect), 1);
    }
    for (int off : {20, -20}) {
        Frames f;
        f.press(1000 + off, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Great), 1);
    }
    // MeH 边界: 136 → MeH; 137 (>136.5, ≤173.5) → press 先于同 tick 强制 miss ⇒ Miss 消耗
    {
        Frames f;
        f.press(1000 + 136, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Meh), 1);
        CHECK_EQ(countOf(r, HitResult::Miss), 0);
    }
    {
        Frames f;
        f.press(1000 + 137, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Miss), 1);
        CHECK_EQ(r.match.events.size(), 1u);
        CHECK_EQ(static_cast<int>(r.match.events[0].phase),
                 static_cast<int>(HitPhase::NoteHead));
    }
    // 174 (>173.5): 物件在 tick 1137 已被强制 miss ⇒ press 不消耗 → unmatchedInput
    {
        Frames f;
        f.tick(1000);       // 触发 1137 前? 不: 先给一个中间 tick 触发强制 miss
        f.tick(1000 + 137); // 强制 miss 生效 tick
        f.press(1000 + 174, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Miss), 1);  // 强制 miss
        CHECK_EQ(r.match.unmatchedInputs.size(), 1u);
        CHECK_EQ(r.match.unmatchedInputs[0].timeMs, 1000 + 174);
        CHECK_EQ(r.match.events.size(), 1u);
    }
    // 早按 (>Miss 窗) 不判定不消耗 (记 unmatchedInput); 同键按住期间无新 press
    // (再按不触发上升沿) ⇒ 物件在 tick 1137 被强制 Miss
    {
        Frames f;
        f.press(1000 - 200, 1u << 0);
        f.press(1000, 1u << 0);
        f.tick(1000 + 137);  // 强制 miss 生效 tick
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Miss), 1);
        CHECK_EQ(r.match.unmatchedInputs.size(), 1u);
    }
    printf("[rice boundaries] ok\n");
}

void testRiceForceMissTiming() {
    std::vector<ManiaHitObject> objs{note(0, 1, 500)};
    {
        // 过 MeH 窗末 (136.5) 前的最后 tick 仍存活
        Frames f;
        f.tick(500 + 136);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(r.counts.judged(), 0);
        CHECK_EQ(r.match.unmatchedObjects, 1);
    }
    {
        Frames f;
        f.tick(500 + 136);
        f.tick(500 + 137);  // 137 > 136.5 ⇒ 强制 Miss
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Miss), 1);
        CHECK_EQ(r.match.unmatchedObjects, 0);
        CHECK_EQ(r.match.events[0].actualTimeMs.value_or(-1), 500 + 137);
    }
    printf("[rice force miss] ok\n");
}

// ---- hold --------------------------------------------------------------------

void testHoldHeadMissCapsTail() {
    // 头从未按下: 头在 start+137 强制 Miss; 尾在 end 附近松键 ⇒ 封顶 MeH
    std::vector<ManiaHitObject> objs{hold(0, 0, 1000, 2000)};
    {
        Frames f;
        f.tick(1000 + 137);  // 头强制 miss
        f.press(2000, 1u << 0);        // 过尾后 re-press? 不: 恰在 end 按 (恢复 holding)
        f.release(2000 + 10, 1u << 0); // 尾: (10)/1.5=6.67 → Perfect → 封顶 MeH
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Miss), 1);  // 头
        CHECK_EQ(countOf(r, HitResult::Meh), 1);   // 尾 (封顶)
        CHECK_EQ(r.match.holds.size(), 1u);
        CHECK(r.match.holds[0].tail.judgement == HitResult::Meh);
    }
    // 正点头中 + 正点松 → 满 Perfect
    {
        Frames f;
        f.press(1000, 1u << 0);
        f.release(2000, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Perfect), 2);
        CHECK(r.match.holds[0].head.judgement == HitResult::Perfect);
        CHECK(r.match.holds[0].tail.judgement == HitResult::Perfect);
    }
    printf("[hold head-miss cap] ok\n");
}

void testHoldTailBoundaries() {
    std::vector<ManiaHitObject> objs{hold(0, 0, 0, 5000)};
    // 尾窗 = 头窗 ×1.5 (松键 offset d ⇒ 判 |d/1.5|; OD5 整数毫秒边界:
    // Perfect ≤ 29.25, Great ≤ 74.25, Good ≤ 123.75, Ok ≤ 168.75, Meh ≤ 204.75,
    // Miss(消耗) ≤ 260.25)。头恒为 Perfect (press 在正点) ⇒ Perfect 计 2。
    struct Case {
        int releaseOffset;
        HitResult expect;
    };
    const Case cases[] = {
        {29, HitResult::Perfect},  {30, HitResult::Great},   {74, HitResult::Great},
        {75, HitResult::Good},     {123, HitResult::Good},   {124, HitResult::Ok},
        {168, HitResult::Ok},      {169, HitResult::Meh},    {204, HitResult::Meh},
        {260, HitResult::Miss},
    };
    for (const Case& c : cases) {
        Frames f;
        f.press(0, 1u << 0);
        f.release(5000 + c.releaseOffset, 1u << 0);
        f.tick(5000 + 400);  // 余波 tick (保证超窗 case 的强制 miss 有机会发生)
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, c.expect), c.expect == HitResult::Perfect ? 2 : 1);
        CHECK_EQ(countOf(r, HitResult::Perfect) + countOf(r, HitResult::Great) +
                    countOf(r, HitResult::Good) + countOf(r, HitResult::Ok) +
                    countOf(r, HitResult::Meh) + countOf(r, HitResult::Miss),
                2);
    }
    // +261: 松键判不了 (261/1.5=174 > 173.5) ⇒ body 立即 ComboBreak;
    // 同 tick update 强制尾 Miss (261 > 1.5×136.5 = 204.75 ✓)
    {
        Frames f;
        f.press(0, 1u << 0);
        f.release(5000 + 261, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Miss), 1);  // 只有尾 miss (头是 Perfect)
        CHECK_EQ(countOf(r, HitResult::Perfect), 1);
        CHECK_EQ(static_cast<int>(r.match.holds[0].tail.judgement),
                 static_cast<int>(HitResult::Miss));
    }
    printf("[hold tail boundaries] ok\n");
}

void testHoldNoReleaseForcedMiss() {
    std::vector<ManiaHitObject> objs{hold(0, 0, 1000, 2000)};
    {
        // 头中, 一直按住: 尾在 end+205 (1.5×136.5=204.75) 强制 Miss
        Frames f;
        f.press(1000, 1u << 0);
        f.tick(2000 + 204);
        CHECK([](const LazerMatchResult& r) {
            return r.counts.judged() == 1;  // 只有头
        }(matchLazer(objs, f.v, opts())));
        f.tick(2000 + 205);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Perfect), 1);  // 头
        CHECK_EQ(countOf(r, HitResult::Miss), 1);     // 尾强制
        CHECK_EQ(r.match.events.back().phase, HitPhase::HoldTail);
        CHECK(r.match.holds[0].heldThroughTail);
        CHECK(!r.match.holds[0].releasedEarly);
    }
    printf("[hold no-release miss] ok\n");
}

void testHoldEarlyRelease() {
    std::vector<ManiaHitObject> objs{hold(0, 0, 1000, 3000)};
    // 1.5 倍窗内早松: 不断带, 尾拿满档 (封顶判断发生在 body 判定前)
    {
        Frames f;
        f.press(1000, 1u << 0);
        f.release(3000 - 29, 1u << 0);  // |−29/1.5| = 19.33 ≤ 19.5 → Perfect
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Perfect), 2);
        CHECK(r.match.holds[0].releasedEarly);
        CHECK(!r.match.holds[0].heldThroughTail);
    }
    // 超窗早松 (−1000/1.5 = −667): 尾不判 + body 断; 重按恢复 holding,
    // 再松: 封顶判断里 body.HasHoldBreak = true ⇒ 尾最高 MeH
    {
        Frames f;
        f.press(1000, 1u << 0);
        f.release(3000 - 1000, 1u << 0);  // 断
        f.press(3000 - 100, 1u << 0);     // 重按
        f.release(3000, 1u << 0);         // 正点松 → Perfect → 封顶 MeH
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Perfect), 1);  // 头
        CHECK_EQ(countOf(r, HitResult::Meh), 1);      // 尾封顶
        CHECK_EQ(r.match.events.back().phase, HitPhase::HoldTail);
    }
    printf("[hold early release] ok\n");
}

void testHoldPastEndRepress() {
    // 过尾后 (≤ raw Meh 窗) 仍可起按: holding 恢复, 头 (远超窗) 不判;
    // 松键判尾但封顶 MeH (头未中)。之后头由 update 强制 Miss。
    std::vector<ManiaHitObject> objs{hold(0, 0, 1000, 3000)};
    Frames f;
    f.tick(1000 + 137);   // 头强制 Miss
    f.press(3000 + 100, 1u << 0);   // 过尾 ≤136.5 → 允许起按
    f.release(3000 + 101, 1u << 0); // 尾: 0.67 → Perfect → 封顶 MeH
    LazerMatchResult r = matchLazer(objs, f.v, opts());
    CHECK_EQ(countOf(r, HitResult::Miss), 1);  // 头
    CHECK_EQ(countOf(r, HitResult::Meh), 1);   // 尾封顶
    // 过尾超 raw Meh 窗起按 → 拒绝 (不恢复 holding, 松键无效果, 尾强制 Miss)
    {
        std::vector<ManiaHitObject> objs2{hold(0, 0, 1000, 3000)};
        Frames f2;
        f2.tick(1000 + 137);
        f2.press(3000 + 200, 1u << 0);  // 200 > 136.5 → 拒绝
        f2.release(3000 + 201, 1u << 0);
        f2.tick(3000 + 205);            // 尾强制 Miss
        LazerMatchResult r2 = matchLazer(objs2, f2.v, opts());
        CHECK_EQ(countOf(r2, HitResult::Miss), 2);  // 头 + 尾
        CHECK_EQ(r2.match.unmatchedInputs.size(), 1u);
    }
    printf("[hold past-end repress] ok\n");
}

// ---- note-lock / FIFO / chord / mirror ---------------------------------------

void testNoteLockFifo() {
    // 同列两个 rice: 1000 / 1100 (OD5 Miss 窗 173.5 ⇒ 窗口交叠, note-lock 决定归属)
    std::vector<ManiaHitObject> objs{note(0, 0, 1000), note(1, 0, 1100)};
    {
        // press 在 1100 (== 下一物件 start): 上一物件被锁 → 1100 的物件吃掉 press
        Frames f;
        f.press(1100, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Perfect), 1);
        CHECK_EQ(r.match.events[0].objectIndex, 1);
        CHECK_EQ(r.match.events[0].actualTimeMs.value_or(-1), 1100);
    }
    {
        // press 在 1050 (< 1100): 归 1000 的物件 (offset 50 → Good, OD5 good 窗 82.5)
        Frames f;
        f.press(1050, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(r.match.events.size(), 1u);
        CHECK_EQ(r.match.events[0].objectIndex, 0);
        CHECK_EQ(countOf(r, HitResult::Good), 1);
    }
    {
        // 两次 press: 第一次给 1000 (Perfect), 第二次 1100 给第二颗 (Perfect)
        Frames f;
        f.press(1000, 1u << 0);
        f.release(1020, 1u << 0);
        f.press(1100, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Perfect), 2);
        CHECK_EQ(r.match.events[0].objectIndex, 0);
        CHECK_EQ(r.match.events[1].objectIndex, 1);
    }
    printf("[note lock fifo] ok\n");
}

void testChordColumns() {
    std::vector<ManiaHitObject> objs{note(0, 0, 1000), note(1, 3, 1000),
                                     hold(2, 1, 1000, 1500)};
    Frames f;
    f.press(1000, (1u << 0) | (1u << 3) | (1u << 1));
    f.release(1500, 1u << 1);
    LazerMatchResult r = matchLazer(objs, f.v, opts());
    CHECK_EQ(countOf(r, HitResult::Perfect), 4);  // rice×2 + hold 头 + hold 尾
    CHECK_EQ(r.match.unmatchedInputs.size(), 0u);
    CHECK_EQ(r.match.holds.size(), 1u);
    printf("[chord] ok\n");
}

void testMirror() {
    std::vector<ManiaHitObject> objs{note(0, 0, 1000), note(1, 3, 1100)};
    {
        Frames f;
        f.press(1000, 1u << 3);  // 4K mirror: col0 → col3
        LazerMatchResult r = matchLazer(objs, f.v, opts(true, 1.0, false, false, true, 4));
        CHECK_EQ(r.match.events.size(), 1u);
        CHECK_EQ(r.match.events[0].objectIndex, 0);
        CHECK_EQ(r.match.events[0].column, 3);  // 折算后的列
    }
    {
        Frames f;  // 不开 mirror: col3 bit 打到 col3 物件
        f.press(1100, 1u << 3);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(r.match.events.size(), 1u);
        CHECK_EQ(r.match.events[0].objectIndex, 1);
    }
    printf("[mirror] ok\n");
}

void testHrWindowJudgement() {
    // OD5 HR: perfect 13.5 → +13 Perfect / +14 Great
    std::vector<ManiaHitObject> objs{note(0, 0, 0)};
    {
        Frames f;
        f.press(13, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, opts(5.0, 1.0, true)), HitResult::Perfect), 1);
    }
    {
        Frames f;
        f.press(14, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, opts(5.0, 1.0, true)), HitResult::Great), 1);
    }
    // HT 0.75: perfect 14.5 → +14 Perfect / +15 Great
    {
        Frames f;
        f.press(14, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, opts(5.0, 0.75)), HitResult::Perfect), 1);
    }
    {
        Frames f;
        f.press(15, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, opts(5.0, 0.75)), HitResult::Great), 1);
    }
    printf("[hr/ht window judgement] ok\n");
}

// ---- handleHit 级联 -----------------------------------------------------------

void testHitCascadesMiss() {
    // 同列: 未判的 1000 号 + 1200 press 命中 (1200 ≤ 1000+173.5 且 ≥ 1100 lock)
    // → 1000 号被 HandleHit 强制 Miss。事件顺序 = 上游事件流:
    // ApplyResult(命中物件) → OnNewResult → HandleHit(级联 Miss 在命中事件之后)。
    std::vector<ManiaHitObject> objs{note(0, 0, 1000), note(1, 0, 1200)};
    Frames f;
    f.press(1200, 1u << 0);
    LazerMatchResult r = matchLazer(objs, f.v, opts());
    CHECK_EQ(countOf(r, HitResult::Perfect), 1);
    CHECK_EQ(countOf(r, HitResult::Miss), 1);
    CHECK_EQ(r.match.events[0].objectIndex, 1);  // 命中事件在前
    CHECK_EQ(r.match.events[1].objectIndex, 0);  // 级联 Miss 在后
    CHECK_EQ(r.match.events[1].judgement, HitResult::Miss);
    // 未判 hold 被后续 rice 命中级联: 头/尾 Miss + body 断
    {
        std::vector<ManiaHitObject> objs2{hold(0, 0, 1000, 1500), note(1, 0, 1800)};
        Frames f2;
        f2.press(1800, 1u << 0);
        LazerMatchResult r2 = matchLazer(objs2, f2.v, opts());
        CHECK_EQ(countOf(r2, HitResult::Perfect), 1);  // rice
        CHECK_EQ(countOf(r2, HitResult::Miss), 2);     // hold 头 + 尾
        CHECK_EQ(r2.match.events[0].objectIndex, 1);   // rice 命中在前
        CHECK_EQ(r2.match.events[1].objectIndex, 0);   // hold 头 Miss
        CHECK_EQ(r2.match.events[2].objectIndex, 0);   // hold 尾 Miss
        CHECK_EQ(static_cast<int>(r2.match.events[1].phase),
                 static_cast<int>(HitPhase::HoldHead));
        CHECK_EQ(static_cast<int>(r2.match.events[2].phase),
                 static_cast<int>(HitPhase::HoldTail));
    }
    printf("[hit cascade] ok\n");
}

// ---- 确定性 + acc305 -----------------------------------------------------------

void testDeterminism() {
    std::vector<ManiaHitObject> objs{note(0, 0, 1000), hold(1, 1, 1200, 2200),
                                     note(2, 3, 1300), hold(3, 2, 1400, 2600),
                                     note(4, 1, 2700)};
    Frames f;
    f.press(1000, 1u << 0);
    f.release(1030, 1u << 0);
    f.press(1200, 1u << 1);
    f.press(1300, 1u << 3);
    f.release(1350, 1u << 3);
    f.press(1500, 1u << 2);
    f.release(2700, 1u << 2);
    f.press(2700, 1u << 1);
    f.tick(5000);

    LazerMatchResult a = matchLazer(objs, f.v, opts());
    LazerMatchResult b = matchLazer(objs, f.v, opts());
    CHECK_EQ(a.match.events.size(), b.match.events.size());
    for (size_t i = 0; i < a.match.events.size() && i < b.match.events.size(); ++i) {
        CHECK_EQ(a.match.events[i].objectIndex, b.match.events[i].objectIndex);
        CHECK_EQ(a.match.events[i].column, b.match.events[i].column);
        CHECK_EQ(a.match.events[i].objectTimeMs, b.match.events[i].objectTimeMs);
        CHECK_EQ(a.match.events[i].actualTimeMs.value_or(-1),
                 b.match.events[i].actualTimeMs.value_or(-1));
        CHECK(a.match.events[i].offsetMs == b.match.events[i].offsetMs);
        CHECK(a.match.events[i].judgement == b.match.events[i].judgement);
        CHECK(a.match.events[i].phase == b.match.events[i].phase);
    }
    CHECK(a.counts.perfect == b.counts.perfect && a.counts.great == b.counts.great &&
          a.counts.good == b.counts.good && a.counts.ok == b.counts.ok &&
          a.counts.meh == b.counts.meh && a.counts.miss == b.counts.miss);
    CHECK(a.acc305 == b.acc305);
    printf("[determinism] ok\n");
}

void testAcc305() {
    LazerManiaCounts c;
    c.perfect = 850; c.great = 120; c.good = 20; c.ok = 8; c.meh = 1; c.miss = 1;
    // 规格书 §4.5 手算例: 300100/305000 = 98.3934%
    const double acc = lazerAccuracy305Percent(c);
    CHECK(std::fabs(acc - 98.393442622950834) < 1e-6);
    LazerManiaCounts full;
    full.perfect = 10; full.valid = true;
    CHECK(nearly(lazerAccuracy305Percent(full), 100.0));
    LazerManiaCounts empty;
    CHECK(nearly(lazerAccuracy305Percent(empty), 100.0));
    printf("[acc305] ok\n");
}

void testEmptyInputs() {
    // 空物件 / 空帧: 不崩溃, acc=100, counts 全 0
    LazerMatchResult r = matchLazer({}, {}, opts());
    CHECK_EQ(r.counts.judged(), 0);
    CHECK(nearly(r.acc305, 100.0));
    CHECK_EQ(r.match.unmatchedObjects, 0);
    // keyCount=0 + 物件: 不崩溃 (keyCount 未声明时从物件列推导, press 正常判定)
    std::vector<ManiaHitObject> objs{note(0, 0, 0)};
    Frames f;
    f.press(0, 1u);
    LazerMatchResult r2 = matchLazer(objs, f.v, opts(5.0, 1.0, false, false, false, 0));
    CHECK_EQ(r2.counts.judged(), 1);
    printf("[empty inputs] ok\n");
}

// ---- L3 mod 适配: DC / IN / RD / HO / NR / DA / CL -----------------------------

TimingPoint tpAt(double t, double beatLength) {
    TimingPoint tp;
    tp.timeMs = t;
    tp.beatLength = beatLength;
    tp.uninherited = true;  // 非继承点 (beatLength > 0)
    return tp;
}

// DC (Daycore): ManiaModDaycore 空壳 : ModDaycore + IManiaRateAdjustmentMod,
// 与 HT 同一 rate 路径 (默认 0.75, speed_change 可覆盖)。
void testDaycoreRate() {
    LazerWindows w = lazerHitWindows(5.0, 0.75, false, false);
    CHECK(nearly(w.perfect, 14.5) && nearly(w.miss, 129.5));
    std::vector<ManiaHitObject> objs{note(0, 0, 0)};
    {
        Frames f;
        f.press(14, 1u << 0);  // OD5 HT/DC perfect 14.5
        CHECK_EQ(countOf(matchLazer(objs, f.v, opts(5.0, 0.75)), HitResult::Perfect), 1);
    }
    {
        Frames f;
        f.press(15, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, opts(5.0, 0.75)), HitResult::Great), 1);
    }
    printf("[DC daycore rate] ok\n");
}

// IN (Invert) 黄金用例: 双列 + timing point, 逐物件断言。
void testInvertGolden() {
    std::vector<TimingPoint> tps{tpAt(0, 400), tpAt(1150, 200)};
    std::vector<ManiaHitObject> objs{
        note(0, 0, 1000), note(1, 0, 1100), hold(2, 0, 1200, 1400),
        note(3, 1, 1050),
    };
    applyManiaInvert(objs, tps);
    // col0 locations [1000(note), 1100(note), 1200(LN 头)]:
    //   (1000,1100): d=100, bl@1100=400 → max(50, 100−100)=50 → LN 1000..1050
    //   (1100,1200): d=100, bl@1200=200 (1150 边界后) → max(50, 100−50)=50 → LN 1100..1150
    // col1 单物件 → 整列消失 (locations.Count−1 对 循环; 上游逐字)
    CHECK_EQ(objs.size(), 2u);
    CHECK(objs[0].type == ManiaObjectType::Hold);
    CHECK_EQ(objs[0].column, 0);
    CHECK_EQ(objs[0].startTimeMs, 1000);
    CHECK_EQ(objs[0].endTimeMs.value_or(-1), 1050);
    CHECK(objs[1].type == ManiaObjectType::Hold);
    CHECK_EQ(objs[1].column, 0);
    CHECK_EQ(objs[1].startTimeMs, 1100);
    CHECK_EQ(objs[1].endTimeMs.value_or(-1), 1150);
    // sourceIndex 按最终序 (OrderBy StartTime) 重编号
    CHECK_EQ(objs[0].sourceIndex, 0);
    CHECK_EQ(objs[1].sourceIndex, 1);
    printf("[IN invert golden] ok\n");
}

void testInvertBeatLengthAtEndObject() {
    // 源码: beatLength = TimingPointAt(locations[i+1].startTime) — 尾物件时刻,
    // 不是 t[i] (任务书草案写反了, 以 master 源码为准)。
    std::vector<TimingPoint> tps{tpAt(0, 400), tpAt(1150, 200)};
    std::vector<ManiaHitObject> objs{note(0, 0, 1000), note(1, 0, 1200)};
    applyManiaInvert(objs, tps);
    // d=200, bl@1200=200 → max(100, 200−50)=150 → end 1150 (bl@1000 会给 100 → 1100)
    CHECK_EQ(objs.size(), 1u);
    CHECK_EQ(objs[0].startTimeMs, 1000);
    CHECK_EQ(objs[0].endTimeMs.value_or(-1), 1150);
    printf("[IN beatLength@end] ok\n");
}

void testInvertTiesAndOrder() {
    // 双列同型: 并列起点 → 列首现序 (col0 在前); duration/2 下限 (d=500, bl=400 →
    // max(250, 500−100)=400); 空 timing → TimingPointAt fallback 首点。
    std::vector<TimingPoint> tps{tpAt(0, 400)};
    std::vector<ManiaHitObject> objs{
        note(0, 0, 1000), note(1, 0, 1500), note(2, 1, 1000), note(3, 1, 1500),
    };
    applyManiaInvert(objs, tps);
    CHECK_EQ(objs.size(), 2u);
    CHECK_EQ(objs[0].column, 0);
    CHECK_EQ(objs[0].startTimeMs, 1000);
    CHECK_EQ(objs[0].endTimeMs.value_or(-1), 1400);
    CHECK_EQ(objs[1].column, 1);
    CHECK_EQ(objs[0].sourceIndex, 0);
    CHECK_EQ(objs[1].sourceIndex, 1);
    // 并列 (Note 与 LN 头同时刻): locations 稳定序 = Note 全部在 LN 头前 (Concat 序)
    std::vector<ManiaHitObject> ties{hold(0, 0, 1000, 1200), note(1, 0, 1000)};
    applyManiaInvert(ties, tps);
    // d=0 → max(0, −100)=0 → 零长 LN
    CHECK_EQ(ties.size(), 1u);
    CHECK_EQ(ties[0].startTimeMs, 1000);
    CHECK_EQ(ties[0].endTimeMs.value_or(-1), 1000);
    // 无 timing point → DEFAULT_BEAT_LENGTH 1000 (TimingControlPoint.DEFAULT);
    std::vector<ManiaHitObject> none{note(0, 0, 1000), note(1, 0, 3000)};
    applyManiaInvert(none, {});
    // d=2000, bl=1000 → max(1000, 2000−250)=1750 → end 2750
    CHECK_EQ(none.size(), 1u);
    CHECK_EQ(none[0].endTimeMs.value_or(-1), 2750);
    printf("[IN ties/order] ok\n");
}

// RD (Random): DotNetRandom 黄金值 + 置换向量。
// 黄金值来源: dotnet/runtime main Random.CompatImpl.cs CompatPrng 的逐行参考实现
// (含 C# unchecked int32 回绕; |seed| > 161803398 时 seedArray[55] 为负, 初始化
// 第二个双循环的减法会回绕 — seed 0/42 与公开文档的 .NET 经典序列一致作外部锚点)。
void testRandomGolden() {
    {
        DotNetRandom r(0);
        CHECK_EQ(r.next(), 1559595546);
        CHECK_EQ(r.next(), 1755192844);
        CHECK_EQ(r.next(), 1649316166);
        CHECK_EQ(r.next(), 1198642031);
        CHECK_EQ(r.next(), 442452829);
    }
    {
        DotNetRandom r(42);
        CHECK_EQ(r.next(), 1434747710);
        CHECK_EQ(r.next(), 302596119);
        CHECK_EQ(r.next(), 269548474);
        CHECK_EQ(r.next(), 1122627734);
    }
    {
        // 负种子 = 绝对值 (Math.Abs)
        DotNetRandom r(-5);
        CHECK_EQ(r.next(), 726643700);
        CHECK_EQ(r.next(), 610783965);
    }
    {
        // int32 回绕路径 ①: int.MinValue (特判 subtraction = int.MaxValue)
        DotNetRandom r(-2147483647 - 1);
        CHECK_EQ(r.next(), 1559595546);
        CHECK_EQ(r.next(), 1755192844);
        // 回绕差异点: 第 3 个值与 seed 0 分叉 (±2^32 ≡ ±2 (mod 2^31−1))
        CHECK_EQ(r.next(), 1649316172);
    }
    {
        // int32 回绕路径 ②: |seed| > 161803398 → seedArray[55] = 161803398−seed < 0
        DotNetRandom r(2000000000);
        CHECK_EQ(r.next(), 224431583);
        CHECK_EQ(r.next(), 2141996799);
    }
    printf("[RD dotnet random golden] ok\n");
}

void testRandomPermutation() {
    // shuffledColumns = Range(0,K).OrderBy(_ => rng.Next()) — 键按元素序 0..K−1
    // 各取一次, 稳定排序 (OrderedEnumerable.CompareAnyKeys: c==0 → index1−index2)。
    // 参考值: K=4 seed 42 → [2,1,3,0]; K=7 seed 1 → [1,0,6,5,2,4,3]。
    std::vector<ManiaHitObject> objs{note(0, 0, 0), note(1, 1, 10), note(2, 2, 20),
                                     note(3, 3, 30)};
    applyManiaRandom(objs, 4, 42);
    CHECK_EQ(objs[0].column, 2);
    CHECK_EQ(objs[1].column, 1);
    CHECK_EQ(objs[2].column, 3);
    CHECK_EQ(objs[3].column, 0);
    CHECK_EQ(objs[0].startTimeMs, 0);  // 只换列, 不动时间

    std::vector<ManiaHitObject> objs7;
    for (int c = 0; c < 7; ++c) objs7.push_back(note(c, c, c));
    applyManiaRandom(objs7, 7, 1);
    const int expect7[] = {1, 0, 6, 5, 2, 4, 3};
    for (int c = 0; c < 7; ++c) CHECK_EQ(objs7[c].column, expect7[c]);

    // 双射性 (K=7 seed 1 的像覆盖 0..6)
    std::vector<bool> seen(7, false);
    for (const ManiaHitObject& o : objs7) {
        CHECK(o.column >= 0 && o.column < 7);
        CHECK(!seen[static_cast<size_t>(o.column)]);
        seen[static_cast<size_t>(o.column)] = true;
    }

    // 确定性: 同 seed 两次一致; 不同 seed 不同
    std::vector<ManiaHitObject> a{note(0, 0, 0), note(1, 1, 1), note(2, 2, 2), note(3, 3, 3)};
    std::vector<ManiaHitObject> b = a;
    applyManiaRandom(a, 4, 987654321);
    applyManiaRandom(b, 4, 987654321);
    for (int c = 0; c < 4; ++c) CHECK_EQ(a[c].column, b[c].column);
    std::vector<ManiaHitObject> d = a;
    applyManiaRandom(d, 4, 42);
    bool diff = false;
    for (int c = 0; c < 4; ++c)
        if (a[c].column != d[c].column) diff = true;
    CHECK(diff);
    printf("[RD permutation] ok\n");
}

// HO (Hold Off): LN → 头时刻 Note; 原始 Note 先、转换 Note 后 (Concat 序, 稳定排序)。
void testHoldOff() {
    std::vector<ManiaHitObject> objs{note(0, 0, 1000), hold(1, 1, 1100, 1300),
                                     note(2, 0, 1200)};
    applyManiaHoldOff(objs);
    CHECK_EQ(objs.size(), 3u);
    for (const ManiaHitObject& o : objs) CHECK(o.type == ManiaObjectType::Note);
    CHECK_EQ(objs[0].startTimeMs, 1000);
    CHECK_EQ(objs[0].column, 0);
    CHECK_EQ(objs[1].startTimeMs, 1100);  // LN 头时刻, 尾消失
    CHECK_EQ(objs[1].column, 1);
    CHECK_EQ(objs[2].startTimeMs, 1200);
    CHECK_EQ(objs[2].sourceIndex, 2);
    // 判定面: 每物件 1 判定 (原 LN 是头+尾 2 判定)
    Frames f;
    f.press(1000, 1u << 0);
    f.release(1010, 1u << 0);   // 松开 col0, 让 1200 的 press 形成上升沿
    f.press(1100, 1u << 1);
    f.press(1200, 1u << 0);
    LazerMatchResult r = matchLazer(objs, f.v, opts());
    CHECK_EQ(countOf(r, HitResult::Perfect), 3);
    CHECK_EQ(r.match.holds.size(), 0u);

    // 并列: 原始 Note 在转换 Note 前 (用 rawX 标记区分)
    ManiaHitObject h = hold(0, 0, 1000, 1100);
    h.rawX = 9;
    ManiaHitObject n = note(1, 0, 1000);
    n.rawX = 7;
    std::vector<ManiaHitObject> ties{h, n};
    applyManiaHoldOff(ties);
    CHECK_EQ(ties.size(), 2u);
    CHECK_EQ(ties[0].rawX, 7);  // 原始 Note 前
    CHECK_EQ(ties[1].rawX, 9);  // 转换 Note 后
    printf("[HO hold off] ok\n");
}

// NR (No Release): 尾时刻仍按住 ⇒ GetCappedResult(Perfect); 早松回退基础尾判定。
void testNoRelease() {
    std::vector<ManiaHitObject> objs{hold(0, 0, 1000, 2000)};
    LazerMatchOptions nr = opts();
    nr.noRelease = true;
    // 按住不放: 首个 t >= end 的 tick (t==end 即可) auto-Perfect — 基础规则要等
    // end+205 才强制 Miss。
    {
        Frames f;
        f.press(1000, 1u << 0);
        f.tick(2000);
        LazerMatchResult r = matchLazer(objs, f.v, nr);
        CHECK_EQ(countOf(r, HitResult::Perfect), 2);
    }
    // 回归: 基础规则同场景尾在 end+205 强制 Miss (noRelease 不泄漏进默认路径;
    // 若泄漏, tick 2205 时尾会是 Perfect ⇒ Perfect=2)
    {
        Frames f;
        f.press(1000, 1u << 0);
        f.tick(2205);
        LazerMatchResult r = matchLazer(objs, f.v, opts());
        CHECK_EQ(countOf(r, HitResult::Perfect), 1);
        CHECK_EQ(countOf(r, HitResult::Miss), 1);
    }
    // 晚松 (2500, 远超尾窗): NR → Perfect; 基础规则此时尾早已强制 Miss
    {
        Frames f;
        f.press(1000, 1u << 0);
        f.release(2500, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, nr);
        CHECK_EQ(countOf(r, HitResult::Perfect), 2);
    }
    // 窗内早松 (1990): NR 落回基础路径 → 与基础一致 Perfect (−29/1.5 → Perfect)
    {
        Frames f;
        f.press(1000, 1u << 0);
        f.release(1990, 1u << 0);
        LazerMatchResult r = matchLazer(objs, f.v, nr);
        CHECK_EQ(countOf(r, HitResult::Perfect), 2);
    }
    // 头 Miss + 过尾重按住: 封顶 MeH (GetCappedResult)
    {
        Frames f;
        f.tick(1138);            // 头强制 Miss (>1000+136.5)
        f.press(2000, 1u << 0);  // 重按 → holding
        f.tick(2001);            // auto-Perfect → 封顶 MeH
        LazerMatchResult r = matchLazer(objs, f.v, nr);
        CHECK_EQ(countOf(r, HitResult::Miss), 1);
        CHECK_EQ(countOf(r, HitResult::Meh), 1);
    }
    // 从未按: holding=false → NR 不救, 基础强制 Miss ×2
    {
        Frames f;
        f.tick(1138);
        f.tick(2205);
        LazerMatchResult r = matchLazer(objs, f.v, nr);
        CHECK_EQ(countOf(r, HitResult::Miss), 2);
    }
    printf("[NR no release] ok\n");
}

// DA (Difficulty Adjust): overall_difficulty 绝对值喂进 matcher OD。
void testDifficultyAdjustOd() {
    // DifficultyRange(8, 22.4, 19.4, 13.9) = 19.4 − 3.3 = 16.1 → 16.5
    LazerWindows w8 = lazerHitWindows(8.0, 1.0, false, false);
    CHECK(nearly(w8.perfect, 16.5));
    std::vector<ManiaHitObject> objs{note(0, 0, 0)};
    {
        Frames f;
        f.press(16, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, opts(8.0)), HitResult::Perfect), 1);
    }
    {
        Frames f;
        f.press(17, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, opts(8.0)), HitResult::Great), 1);
    }
    printf("[DA od override] ok\n");
}

// CL (Classic): ManiaHitWindows classic 分支 (native 谱)。
void testClassicWindows() {
    // OD5: invertedOd=5 → 16/49/82/112/136/173 — 只有 perfect 变 (19.5 → 16.5)
    LazerWindows w5 = lazerHitWindows(5.0, 1.0, false, false, true);
    CHECK(nearly(w5.perfect, 16.5) && nearly(w5.great, 49.5) && nearly(w5.good, 82.5) &&
          nearly(w5.ok, 112.5) && nearly(w5.meh, 136.5) && nearly(w5.miss, 173.5));
    // OD10: invertedOd=0 → stable OD10 窗 (perfect 13.5 → 16.5)
    LazerWindows w10 = lazerHitWindows(10.0, 1.0, false, false, true);
    CHECK(nearly(w10.perfect, 16.5) && nearly(w10.great, 34.5) &&
          nearly(w10.good, 67.5) && nearly(w10.ok, 97.5) &&
          nearly(w10.meh, 121.5) && nearly(w10.miss, 158.5));
    // Clamp(·, 0, 10): OD14 (DA 扩展域) → invertedOd=0
    LazerWindows w14 = lazerHitWindows(14.0, 1.0, false, false, true);
    CHECK(nearly(w14.great, 34.5) && nearly(w14.miss, 158.5));
    // 对照: 普通公式 OD14 不 clamp → great = floor(49−27)+0.5 = 22.5
    LazerWindows n14 = lazerHitWindows(14.0, 1.0, false, false);
    CHECK(nearly(n14.great, 22.5));
    // OD0: invertedOd=10 → 与普通公式同值 (除 perfect)
    LazerWindows w0 = lazerHitWindows(0.0, 1.0, false, false, true);
    CHECK(nearly(w0.perfect, 16.5) && nearly(w0.great, 64.5) && nearly(w0.miss, 188.5));
    // HR 叠乘: m = 1/1.4 → perfect floor(16/1.4)+0.5 = 11.5
    LazerWindows whr = lazerHitWindows(5.0, 1.0, true, false, true);
    CHECK(nearly(whr.perfect, 11.5) && nearly(whr.great, 35.5));
    // 4 参旧签名向后兼容 (= classic=false)
    LazerWindows legacy = lazerHitWindows(5.0, 1.0, false, false);
    CHECK(nearly(legacy.perfect, 19.5));
    // 判定: OD5 classic perfect 16.5 → +16 Perfect / +17 Great
    std::vector<ManiaHitObject> objs{note(0, 0, 0)};
    {
        LazerMatchOptions o = opts();
        o.classic = true;
        Frames f;
        f.press(16, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, o), HitResult::Perfect), 1);
    }
    {
        LazerMatchOptions o = opts();
        o.classic = true;
        Frames f;
        f.press(17, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, o), HitResult::Great), 1);
    }
    printf("[CL classic windows] ok\n");
}

// SV2 (Score V2): 单开不改窗口 (普通公式); 与 CL 同开压制 classic 分支。
void testScoreV2Windows() {
    // SV2 单开 = 无 mod 普通公式 (OD5: perfect 19.5)
    {
        LazerMatchOptions o = opts();
        o.scoreV2 = true;
        LazerWindows w = lazerHitWindows(o.od, o.rate, o.hardRock, o.easy,
                                         o.classic && !o.scoreV2);
        CHECK(nearly(w.perfect, 19.5) && nearly(w.great, 49.5) && nearly(w.miss, 173.5));
    }
    // CL+SV2: classic 分支被压制 → 同普通公式 (CL 单开 perfect=16.5)
    {
        LazerMatchOptions o = opts();
        o.classic = true;
        o.scoreV2 = true;
        LazerWindows w = lazerHitWindows(o.od, o.rate, o.hardRock, o.easy,
                                         o.classic && !o.scoreV2);
        CHECK(nearly(w.perfect, 19.5) && nearly(w.great, 49.5));
        // 判定端到端判别式: OD5 普通 perfect=19.5 / CL perfect=16.5 → +18:
        // CL+SV2 = Perfect, CL 单开 = Great
        std::vector<ManiaHitObject> objs{note(0, 0, 0)};
        Frames f;
        f.press(18, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, o), HitResult::Perfect), 1);
    }
    // CL 单开对照: classic 分支仍生效 (+18 → Great, perfect 窗 16.5 不收 +18)
    {
        LazerMatchOptions c = opts();
        c.classic = true;
        std::vector<ManiaHitObject> objs{note(0, 0, 0)};
        Frames f;
        f.press(18, 1u << 0);
        CHECK_EQ(countOf(matchLazer(objs, f.v, c), HitResult::Great), 1);
    }
    printf("[SV2 classic suppression] ok\n");
}

// 组合: IN 先物件变换, RD 再列置换 (转换 mod 按尾块 mods 序叠加)。
void testInvertThenRandom() {
    std::vector<TimingPoint> tps{tpAt(0, 400)};
    std::vector<ManiaHitObject> objs{
        note(0, 0, 1000), note(1, 0, 1100),  // col0 → LN 1000..1050
        note(2, 1, 1000), note(3, 1, 1100),  // col1 → LN 1000..1050
    };
    applyManiaInvert(objs, tps);
    CHECK_EQ(objs.size(), 2u);
    applyManiaRandom(objs, 4, 42);  // [2,1,3,0]: col0→2, col1→1
    CHECK_EQ(objs[0].column, 2);
    CHECK_EQ(objs[1].column, 1);
    CHECK_EQ(objs[0].startTimeMs, 1000);
    CHECK_EQ(objs[0].endTimeMs.value_or(-1), 1050);
    printf("[IN+RD compose] ok\n");
}

}  // namespace

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    } cases[] = {
        {"windows", testWindows},
        {"riceBoundaries", testRiceBoundaries},
        {"riceForceMiss", testRiceForceMissTiming},
        {"holdHeadMissCap", testHoldHeadMissCapsTail},
        {"holdTailBoundaries", testHoldTailBoundaries},
        {"holdNoReleaseMiss", testHoldNoReleaseForcedMiss},
        {"holdEarlyRelease", testHoldEarlyRelease},
        {"holdPastEndRepress", testHoldPastEndRepress},
        {"noteLockFifo", testNoteLockFifo},
        {"chord", testChordColumns},
        {"mirror", testMirror},
        {"hrWindowJudgement", testHrWindowJudgement},
        {"hitCascade", testHitCascadesMiss},
        {"determinism", testDeterminism},
        {"acc305", testAcc305},
        {"emptyInputs", testEmptyInputs},
        {"daycoreRate", testDaycoreRate},
        {"invertGolden", testInvertGolden},
        {"invertBeatLengthEnd", testInvertBeatLengthAtEndObject},
        {"invertTiesOrder", testInvertTiesAndOrder},
        {"randomGolden", testRandomGolden},
        {"randomPermutation", testRandomPermutation},
        {"holdOff", testHoldOff},
        {"noRelease", testNoRelease},
        {"difficultyAdjustOd", testDifficultyAdjustOd},
        {"classicWindows", testClassicWindows},
        {"scoreV2Windows", testScoreV2Windows},
        {"invertThenRandom", testInvertThenRandom},
    };
    for (const Case& c : cases) {
        const int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? "ok  " : "FAIL", c.name);
    }
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED (lazer matcher)\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
