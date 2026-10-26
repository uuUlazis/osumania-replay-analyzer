// lazer 判定 profile 独立重建引擎 (Phase L2)。
//
// 移植自 ppy/osu master (2026-10-24 抓取, raw.githubusercontent / api.github.com 双通道):
//   [S1] osu.Game.Rulesets.Mania/Scoring/ManiaHitWindows.cs  (blob abff91926a1d)
//        六档 DifficultyRange 常量 + floor(值*totalMultiplier)+0.5 + updateWindows()
//   [S2] osu.Game/Rulesets/Scoring/HitWindows.cs             (blob f4d1fe1e1435)
//        ResultFor (|offset| 从 Perfect 向 Miss 扫, 闭区间 <=) / CanBeHit (<= Meh 窗)
//   [S3] osu.Game/Beatmaps/IBeatmapDifficultyInfo.cs         (blob 0875a60d7510)
//        DifficultyRange 两段线性 (节点 OD 0/5/10), 无 OD clamp
//   [S5/S6] ManiaModHardRock / ManiaModEasy                  (DifficultyMultiplier 1.4 / 1÷1.4)
//   [S4] IManiaRateAdjustmentMod (SpeedMultiplier 写进物件 HitWindows, 谱面时基口径)
//   [S11] TailNote.cs: RELEASE_WINDOW_LENIENCE = 1.5 (判定前 offset/1.5; miss 截止同乘 1.5)
//   [S15] DrawableHoldNote.cs: OnPressed/OnReleased/beginHoldAt/CheckForResult
//   [S16] DrawableHoldNoteTail.cs: CheckForResult(offset/1.5) + GetCappedResult (封顶 Meh)
//   [S19] DrawableNote.cs: CheckForResult(!userTriggered → !CanBeHit ⇒ ApplyMinResult)
//   [S20] DrawableManiaHitObject.cs / [-policy] UI/OrderedHitPolicy.cs (note lock)
//   [S-col] UI/Column.cs: CheckHittable = hitPolicy.IsHittable; OnNewResult → HandleHit 级联
//   [S-hoc] Rulesets/UI/HitObjectContainer.cs: AliveObjects 按 StartTime 升序 (OrderBy 稳定)
//
// 判定语义速查 (每条规则 → 依据):
//   * 窗口 = floor(DifficultyRange(OD) × m) + 0.5, m = rate/(HR 1.4 | EZ 1÷1.4)   [S1][S3]
//   * press: |offset| ≤ 档窗 (闭) 定档; (Meh, Miss] 给 Miss 且消耗物件;
//     超出 Miss 窗 → None → 不消耗                                                [S2][S19]
//   * update tick: 未判且 offset > Meh 窗 ⇒ 强制 Miss (ApplyMinResult)             [S19]
//   * note lock: 同列下一物件 start ≤ t ⇒ 本物件不可判 (press 落到下一物件)         [policy]
//   * press 消耗物件(hit) ⇒ 同列所有 end < start(本物件) 的未判物件强制 Miss/断     [S-col HandleHit]
//   * hold 头: 同 rice 窗; press 早于 start−Miss 窗不记 holding                     [S15 beginHoldAt]
//   * hold press 门槛: AllJudged / CheckHittable / (t>end && t−end>Meh窗) 拒绝      [S15 OnPressed]
//   * hold 尾: 只由松键触发; 档 = ResultFor((t−end)/1.5); 头未中 ∨ body 断 ⇒ 封顶 Meh
//     (封顶判断发生在 body 判定之前 ⇒ 1.5 倍窗内早松不算断)                         [S15][S16]
//   * release 超出 1.5×Miss 窗 → 尾不判, body 立即 ComboBreak; 尾随后在
//     end+1.5×Meh 窗被 update 强制 Miss                                            [S15][S16][S19]
//   * 不松键: 尾永远拿不到命中, end+1.5×Meh 窗强制 Miss + body ComboBreak           [S15][S19]
//   * 六档计数 = 头+尾 (rice 头); body/父物件 (IgnoreHit/IgnoreMiss/ComboBreak) 不计 [规格 §4/§5]
//
// 帧模型: 每条 replay frame 一个 tick, tick 内顺序 = input(press) → input(release) →
// update(强制判定)。上游 InputManager 是 playfield 祖先, 输入在同一帧先于
// DrawableHitObject.UpdateAfterChildren 的 UpdateResult(false), 故「同一 tick 上、
// 恰好越过 Meh 窗的 press」先于强制 Miss 生效 (落在 (Meh, Miss] 窗的 press 消耗物件给 Miss)。
//
// ── 残差归因 (7 局真实对拍, 见 tests/lazer_e2e.cpp) ────────────────────────────
// 重建 N (总判定数) 与尾块 maximum_statistics[perfect] 在 7/7 局逐局相等 —— 状态机、
// 窗口、计数字段全部正确。剩余 L1=22..178 (N 的 0.15%..0.66%) 全部落在 LN 尾档:
// lazer 写盘时对发布时刻 Math.Round (LegacyScoreEncoder), 而尾档 = |d|/1.5 与 x.5 窗
// 比较, 边界在 x.25/x.75 —— 记录整毫秒 ±0.5ms 内的尾档不可从 .osr 复现 (声明值来自
// 实况判定用的未取整时钟)。rice/头是整数 offset, x.5 窗 + 闭区间下取整不可能翻档
// (规格 §2.5 ②的结论只对整数 offset 成立), 各局「可翻尾档」46..340 ≥ 所需翻转数
// L1/2 = 11..90, 翻转率稳定在 19%..33% —— 与该归因定量自洽。e2e 打印 tail-noise
// audit 行供审计; exit 仍按「每档差全 0」的原始契约。
#include "mania/lazer_matcher.h"

#include "lazer/lazer_format.h"  // LazerManiaCounts / lazerAccuracy305Percent (L1 复用)

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace mra {

double lazerDifficultyRange(double od, double min, double mid, double max) {
    // [S3] IBeatmapDifficultyInfo.DifficultyRange — 逐字移植
    if (od > 5.0) return mid + (max - mid) * ((od - 5.0) / 5.0);
    if (od < 5.0) return mid + (mid - min) * ((od - 5.0) / 5.0);
    return mid;
}

LazerWindows lazerHitWindows(double od, double rate, bool hardRock, bool easy) {
    return lazerHitWindows(od, rate, hardRock, easy, false);
}

LazerWindows lazerHitWindows(double od, double rate, bool hardRock, bool easy, bool classic) {
    // [S1] totalMultiplier = SpeedMultiplier / DifficultyMultiplier
    // [S5] HR DifficultyMultiplier = 1.4 → 窗 ÷1.4
    // [S6] EZ DifficultyMultiplier = 1/1.4 → 窗 ×1.4
    double difficultyMultiplier = 1.0;
    if (hardRock) difficultyMultiplier = 1.4;
    if (easy && !hardRock) difficultyMultiplier = 1.0 / 1.4;
    const double m = rate / difficultyMultiplier;
    LazerWindows w;
    if (classic) {
        // [S-CL] ManiaHitWindows.updateWindows() 的 ClassicModActive && !ScoreV2Active
        // 分支 (native 谱 IsConvert=false; classic 入参已由 matchLazer 入口折算含 SV2
        // 压制, DA/HR/EZ 与 CL 的叠加与上游同窗口对象上一致)。
        // Math.Clamp(10 - od, 0, 10); 常量 = stable OD10 窗 + 3×invertedOd。
        const double invertedOd = std::min(10.0, std::max(0.0, 10.0 - od));
        w.perfect = std::floor(16.0 * m) + 0.5;
        w.great = std::floor((34.0 + 3.0 * invertedOd) * m) + 0.5;
        w.good = std::floor((67.0 + 3.0 * invertedOd) * m) + 0.5;
        w.ok = std::floor((97.0 + 3.0 * invertedOd) * m) + 0.5;
        w.meh = std::floor((121.0 + 3.0 * invertedOd) * m) + 0.5;
        w.miss = std::floor((158.0 + 3.0 * invertedOd) * m) + 0.5;
        return w;
    }
    // floor 必须发生在乘 m 之后 [规格 §2.7 端口一致性陷阱]
    w.perfect = std::floor(lazerDifficultyRange(od, 22.4, 19.4, 13.9) * m) + 0.5;
    w.great = std::floor(lazerDifficultyRange(od, 64.0, 49.0, 34.0) * m) + 0.5;
    w.good = std::floor(lazerDifficultyRange(od, 97.0, 82.0, 67.0) * m) + 0.5;
    w.ok = std::floor(lazerDifficultyRange(od, 127.0, 112.0, 97.0) * m) + 0.5;
    w.meh = std::floor(lazerDifficultyRange(od, 151.0, 136.0, 121.0) * m) + 0.5;
    w.miss = std::floor(lazerDifficultyRange(od, 188.0, 173.0, 158.0) * m) + 0.5;
    return w;
}

namespace {

constexpr int kNone = -1;  // ResultFor 的 None (超窗不受理)

// [S2] HitWindows.ResultFor: |offset| 自 Perfect 向 Miss 扫, 闭区间
int resultFor(double offset, const LazerWindows& w) {
    const double a = offset < 0 ? -offset : offset;
    if (a <= w.perfect) return static_cast<int>(HitResult::Perfect);
    if (a <= w.great) return static_cast<int>(HitResult::Great);
    if (a <= w.good) return static_cast<int>(HitResult::Good);
    if (a <= w.ok) return static_cast<int>(HitResult::Ok);
    if (a <= w.meh) return static_cast<int>(HitResult::Meh);
    if (a <= w.miss) return static_cast<int>(HitResult::Miss);
    return kNone;
}

bool resultIsHit(int r) { return r != kNone && r != static_cast<int>(HitResult::Miss); }

HitResult asResult(int r) { return static_cast<HitResult>(r); }

// 一个可判定单元: rice = 头一档; hold = 头 + 尾 (+ body/父物件两个不计分标记)。
struct Unit {
    const ManiaHitObject* obj = nullptr;
    bool isHold = false;
    int column = 0;       // 已按 mirror 折算
    int64_t start = 0;
    int64_t end = 0;      // hold only
    int nextInColumn = -1;  // 同列排序中的下一个物件 (note lock 用), -1 = 无

    // 判定状态
    bool headJudged = false;
    bool tailJudged = false;
    bool bodyJudged = false;
    int headRes = kNone;
    int tailRes = kNone;
    bool bodyHit = false;    // body = IgnoreHit (true) / ComboBreak (false)
    bool holding = false;
    bool parentMarked = false;  // 父物件 IgnoreHit/IgnoreMiss 记号码 (不计分)
    int64_t headJudgeTime = 0;
    int64_t tailJudgeTime = 0;
    // 诊断: 尾判之前发生过 t < end 的松键
    bool releasedEarly = false;
    bool released = false;
};

}  // namespace

struct LazerMatcherImpl {
    LazerWindows w{};
    std::vector<Unit> units;
    // 每列的 unit 下标, 按 (start, sourceIndex) 升序
    std::vector<std::vector<int>> columns;
    int keyCount = 0;
    bool noRelease = false;  // [NR] options.noRelease

    LazerMatchResult* out = nullptr;

    // ---- 判定落地 (events + counts) ----------------------------------------

    HitEvent makeEvent(const Unit& u, bool tail, HitResult res, int64_t t) const {
        HitEvent e;
        e.objectIndex = u.obj->sourceIndex;
        e.column = u.column;
        e.objectTimeMs = tail ? u.end : u.start;
        e.actualTimeMs = t;
        e.offsetMs = static_cast<double>(t - e.objectTimeMs);
        e.judgement = res;
        e.phase = !u.isHold ? HitPhase::NoteHead : (tail ? HitPhase::HoldTail : HitPhase::HoldHead);
        e.inferredFromReplay = true;
        e.verifiedAgainstScoreCounts = false;
        return e;
    }

    void judgeHead(Unit& u, int64_t t, int res, bool force) {
        u.headJudged = true;
        u.headRes = res;
        u.headJudgeTime = t;
        const HitResult hr = asResult(res);
        out->counts.add(hr);
        out->match.events.push_back(makeEvent(u, false, hr, t));
        (void)force;
    }

    void judgeTail(Unit& u, int64_t t, int res) {
        u.tailJudged = true;
        u.tailRes = res;
        u.tailJudgeTime = t;
        const HitResult hr = asResult(res);
        out->counts.add(hr);
        out->match.events.push_back(makeEvent(u, true, hr, t));
    }

    void judgeBody(Unit& u, bool hit) {
        u.bodyJudged = true;
        u.bodyHit = hit;
    }

    // [S15] DrawableHoldNote.OnPressed 的 AllJudged 门 = 父+头+尾+body; 父标记跟尾同步,
    // 所以 (头&&尾&&body) 即等价。
    bool allJudged(const Unit& u) const {
        return u.headJudged && u.tailJudged && u.bodyJudged;
    }

    // [S16] GetCappedResult: result 高于 MeH 且 (头未中 ∨ body 断) ⇒ 压到 MeH。
    // 上游底层序 None<Miss<Meh<Ok<Good<Great<Perfect, `result > Meh` 即好于 MeH 的四档。
    int capTail(const Unit& u, int res) const {
        const bool betterThanMeh = res == static_cast<int>(HitResult::Perfect) ||
                                   res == static_cast<int>(HitResult::Great) ||
                                   res == static_cast<int>(HitResult::Good) ||
                                   res == static_cast<int>(HitResult::Ok);
        if (!betterThanMeh) return res;
        const bool headIsHit = u.headJudged && resultIsHit(u.headRes);
        // 松键即时序: 尾判时 body 尚未判 ⇒ HasHoldBreak=false (只有级联提前打断才有值)
        const bool bodyHasBreak = u.bodyJudged && !u.bodyHit;
        if (!headIsHit || bodyHasBreak)
            return static_cast<int>(HitResult::Meh);
        return res;
    }

    // [S-col] Column.OnNewResult: result.IsHit ⇒ hitPolicy.HandleHit(judgedObject)。
    // 枚举同列 end < target 的物件及其嵌套 (head→tail→body, 各自 end < target 才算),
    // 未判者 MissForcefully (head/tail/rice → Miss; body → ComboBreak; 父 → IgnoreMiss)。
    void handleHit(int column, int64_t target, int64_t now) {
        if (column < 0 || column >= keyCount) return;
        for (int idx : columns[column]) {
            Unit& u = units[idx];
            const int64_t objEnd = u.isHold ? u.end : u.start;
            if (objEnd >= target) return;  // enumerateHitObjectsUpTo 的 yield break
            if (!u.isHold) {
                if (!u.headJudged) judgeHead(u, now, static_cast<int>(HitResult::Miss), true);
                continue;
            }
            if (!u.parentMarked) {
                u.parentMarked = true;   // IgnoreMiss 记号码 (不计分)
                u.holding = false;       // MissForcefully → ReportHoldState(false)
            }
            // 嵌套枚举: head(end=start) → tail(end) → body(end); 遇 end >= target 断
            if (u.start < target) {
                if (!u.headJudged) judgeHead(u, now, static_cast<int>(HitResult::Miss), true);
                if (u.end < target) {
                    if (!u.tailJudged) judgeTail(u, now, static_cast<int>(HitResult::Miss));
                    if (!u.bodyJudged) judgeBody(u, false);
                }
            }
        }
    }

    // [policy + S19] press 分发: 同列按 start 升序逐个提供 OnPressed, 第一个「接受」
    // (判成了) 的物件吃掉 press, 之后不再回退到更早物件外的其它候选。顺序 = 列
    // HitObjectContainer 的 AliveObjects OrderBy(StartTime) 升序 (并列按文件序)。
    // [实证] 反向 (最新优先) 的假设已被 7 局真实对拍否证 (perfect 会崩掉 30-80%),
    // 升序分发与 lazer 声明值收敛。
    void offerPress(int column, int64_t t) {
        if (column < 0 || column >= keyCount) return;
        for (int idx : columns[column]) {
            Unit& u = units[idx];
            if (u.isHold ? allJudged(u) : u.headJudged) continue;  // Judged → OnPressed false
            // [policy] IsHittable: 下一物件 start <= t ⇒ 本物件被锁
            if (u.nextInColumn >= 0 && t >= units[u.nextInColumn].start) continue;
            if (u.isHold) {
                // [S15] 尾后 lenience 门槛: 过尾且超出「尾自己的 Meh 窗」→ 拒绝
                if (t > u.end && static_cast<double>(t - u.end) > w.meh) continue;
                // [S15] beginHoldAt: 早于 start−Miss 窗不记 holding
                if (static_cast<double>(t - u.start) >= -w.miss) u.holding = true;
                // Head.UpdateResult(true)
                if (!u.headJudged) {
                    const int res = resultFor(static_cast<double>(t - u.start), w);
                    if (res != kNone) {
                        judgeHead(u, t, res, false);
                        if (resultIsHit(res)) handleHit(column, u.start, t);
                        return;  // OnPressed true → press 已消耗
                    }
                }
                continue;  // Head.UpdateResult false → press 落到下一物件
            }
            // rice: DrawableNote.OnPressed → UpdateResult(true) → ResultFor
            const int res = resultFor(static_cast<double>(t - u.start), w);
            if (res == kNone) continue;  // 不消耗
            judgeHead(u, t, res, false);
            if (resultIsHit(res)) handleHit(column, u.start, t);
            return;
        }
        // 没有任何物件消耗这次 press
        UnmatchedInput ui;
        ui.timeMs = t;
        ui.column = column;
        out->match.unmatchedInputs.push_back(ui);
    }

    // [S15] OnReleased: AllJudged 门 → isHolding 门 → Tail.UpdateResult(true) →
    // Body.TriggerResult(Tail.IsHit) → holding=false。松键是广播事件 (逐 hold 独立)。
    void offerRelease(int column, int64_t t) {
        if (column < 0 || column >= keyCount) return;
        for (int idx : columns[column]) {
            Unit& u = units[idx];
            if (!u.isHold) continue;
            if (allJudged(u)) continue;
            if (!u.holding) continue;
            if (t < u.end) u.releasedEarly = true;
            u.released = true;
            // Tail.UpdateResult(): CheckForResult(userTriggered, (t−end)/1.5)   [S16]
            if (!u.tailJudged) {
                if (noRelease && t >= u.end) {
                    // [NR] NoReleaseTailNote.CheckForResult: IsHolding &&
                    // timeOffset >= 0 ⇒ GetCappedResult(Perfect) — 不走 1.5× 窗。
                    // 松键路径里 isHolding 在 Tail.UpdateResult() 时仍为 true
                    // [DrawableHoldNote.OnReleased], 与此处的 u.holding 门一致。
                    const int capped = capTail(u, static_cast<int>(HitResult::Perfect));
                    judgeTail(u, t, capped);
                    if (resultIsHit(capped)) handleHit(column, u.end, t);
                } else {
                    const double off = static_cast<double>(t - u.end) / 1.5;  // RELEASE_WINDOW_LENIENCE
                    const int res = resultFor(off, w);
                    if (res != kNone) {
                        const int capped = capTail(u, res);
                        judgeTail(u, t, capped);
                        if (resultIsHit(capped)) handleHit(column, u.end, t);
                    }
                }
            }
            // Body.TriggerResult(Tail.IsHit) — 尾未判/失 ⇒ ComboBreak
            if (!u.bodyJudged) judgeBody(u, u.tailJudged && resultIsHit(u.tailRes));
            u.holding = false;
        }
    }

    // [S19] update tick: !userTriggered 且 !CanBeHit ⇒ ApplyMinResult。
    // rice/head 截止 = start + Meh 窗; 尾 = end + 1.5×Meh 窗 (offset 先 /1.5 再 CanBeHit)。
    // 尾被强制 Miss 的同一 tick 上, 父物件补 IgnoreMiss、body 补 ComboBreak。
    void updatePass(int64_t t) {
        for (Unit& u : units) {
            if (!u.isHold) {
                if (!u.headJudged && static_cast<double>(t - u.start) > w.meh)
                    judgeHead(u, t, static_cast<int>(HitResult::Miss), true);
                continue;
            }
            if (!u.headJudged && static_cast<double>(t - u.start) > w.meh)
                judgeHead(u, t, static_cast<int>(HitResult::Miss), true);
            if (!u.tailJudged) {
                if (noRelease && u.holding && t >= u.end) {
                    // [NR] auto-Perfect 不问 userTriggered ⇒ update tick 也吃:
                    // 首个 t >= end 的 tick 判尾 (GetCappedResult(Perfect)),
                    // body 随 Tail.IsHit (= 封顶 MeH 也是 hit), 永不走到强制 Miss。
                    const int capped = capTail(u, static_cast<int>(HitResult::Perfect));
                    judgeTail(u, t, capped);
                    if (!u.bodyJudged) judgeBody(u, resultIsHit(capped));
                    u.parentMarked = true;
                } else if (static_cast<double>(t - u.end) > 1.5 * w.meh) {
                    judgeTail(u, t, static_cast<int>(HitResult::Miss));  // ApplyMinResult, 无封顶
                    if (!u.bodyJudged) judgeBody(u, false);              // ComboBreak
                    u.parentMarked = true;
                }
            }
        }
    }
};

LazerMatchResult matchLazer(const std::vector<ManiaHitObject>& objects,
                            const std::vector<ReplayFrame>& frames,
                            const LazerMatchOptions& options) {
    LazerMatchResult result;
    LazerMatcherImpl impl;
    impl.out = &result;

    // 输入净化: NaN/非有限 OD → 0; 非有限或非正 rate → 1 (不抛异常契约)
    double od = options.od;
    if (!(od >= 0.0) || !std::isfinite(od)) od = 0.0;  // NaN 比较恒 false
    double rate = options.rate;
    if (!(rate > 0.0) || !std::isfinite(rate)) rate = 1.0;
    // SV2 压制 CL stable 分支: 上游条件 = ClassicModActive && !ScoreV2Active
    // (ManiaHitWindows.cs master L129)。SV2 单开不改变任何窗口。
    impl.w = lazerHitWindows(od, rate, options.hardRock, options.easy,
                             options.classic && !options.scoreV2);
    impl.noRelease = options.noRelease;

    int keyCount = options.keyCount;
    if (keyCount <= 0) {
        for (const ManiaHitObject& o : objects)
            keyCount = std::max(keyCount, o.column + 1);
    }
    keyCount = std::max(0, std::min(keyCount, 32));
    impl.keyCount = keyCount;
    impl.columns.assign(static_cast<size_t>(keyCount), {});

    // 物件表 (mirror 折算在物件列上; 帧不动 — 上游 Mirror 是 ApplyToBeatmap 的物件变换)
    impl.units.reserve(objects.size());
    for (const ManiaHitObject& o : objects) {
        Unit u;
        u.obj = &o;
        u.isHold = o.type == ManiaObjectType::Hold && o.endTimeMs.has_value();
        u.column = o.column;
        if (options.mirror && keyCount > 0) u.column = keyCount - 1 - u.column;
        if (u.column < 0 || u.column >= keyCount) continue;  // 越界列防炸 (正常谱面不会有)
        u.start = o.startTimeMs;
        u.end = u.isHold ? *o.endTimeMs : o.startTimeMs;
        impl.units.push_back(u);
    }

    // 每列排序 (start, sourceIndex) + note-lock 的「下一物件」链
    for (size_t i = 0; i < impl.units.size(); ++i)
        impl.columns[static_cast<size_t>(impl.units[i].column)].push_back(static_cast<int>(i));
    for (std::vector<int>& col : impl.columns) {
        std::sort(col.begin(), col.end(), [&impl](int a, int b) {
            if (impl.units[a].start != impl.units[b].start)
                return impl.units[a].start < impl.units[b].start;
            return impl.units[a].obj->sourceIndex < impl.units[b].obj->sourceIndex;
        });
        for (size_t k = 0; k + 1 < col.size(); ++k)
            impl.units[col[k]].nextInColumn = col[k + 1];
    }

    // 逐帧 tick: press → release → update (见文件头「帧模型」)。
    // keyDownMask/keyUpMask 由 decodeReplayFrames 按上一帧掩码算好的上升/下降沿。
    for (const ReplayFrame& f : frames) {
        if (f.isSeedFrame) continue;  // RNG 种子帧不是输入
        for (int c = 0; c < keyCount; ++c)
            if (f.keyDownMask & (1u << c)) impl.offerPress(c, f.timeMs);
        for (int c = 0; c < keyCount; ++c)
            if (f.keyUpMask & (1u << c)) impl.offerRelease(c, f.timeMs);
        impl.updatePass(f.timeMs);
    }

    // ---- 结果装配 -----------------------------------------------------------

    size_t unjudged = 0;
    for (const Unit& u : impl.units) {
        if (u.isHold) {
            HoldJudgement h;
            h.head = u.headJudged ? impl.makeEvent(u, false, asResult(u.headRes), u.headJudgeTime)
                                  : impl.makeEvent(u, false, HitResult::Miss, u.start);
            if (!u.headJudged) h.head.actualTimeMs.reset(), h.head.offsetMs.reset();
            h.tail = u.tailJudged ? impl.makeEvent(u, true, asResult(u.tailRes), u.tailJudgeTime)
                                  : impl.makeEvent(u, true, HitResult::Miss, u.end);
            if (!u.tailJudged) h.tail.actualTimeMs.reset(), h.tail.offsetMs.reset();
            h.releasedEarly = u.releasedEarly;
            h.heldThroughTail = !u.releasedEarly;
            result.match.holds.push_back(h);
            if (!u.headJudged) ++unjudged;
            if (!u.tailJudged) ++unjudged;
        } else {
            if (!u.headJudged) ++unjudged;
        }
    }
    result.match.unmatchedObjects = static_cast<int>(unjudged);
    result.match.replayEndedNormally = true;

    // lazer 六档计数 (头+尾) 与 305 口径 acc
    LazerManiaCounts lc;
    lc.perfect = result.counts.perfect;
    lc.great = result.counts.great;
    lc.good = result.counts.good;
    lc.ok = result.counts.ok;
    lc.meh = result.counts.meh;
    lc.miss = result.counts.miss;
    lc.fromJson = false;
    lc.valid = result.counts.judged() > 0;
    result.acc305 = lazerAccuracy305Percent(lc);
    return result;
}

// ── mania 转换类 mod 的物件变换 (Phase L3) ─────────────────────────────────

namespace {

// [IN] ControlPointInfo.TimingPointAt(time):
//   BinarySearchWithFallback(TimingPoints, time, TimingPoints.Count > 0
//       ? TimingPoints[0] : TimingControlPoint.DEFAULT)
// = 最后一个 time <= t 的非继承点; t 早于全部点 → 首点; 无点 → DEFAULT。
// TimingControlPoint.DEFAULT_BEAT_LENGTH = 1000 (TimingControlPoint.cs)。
// tps 必须已按 timeMs 升序。
double invertBeatLengthAt(const std::vector<TimingPoint>& tps, double t) {
    if (tps.empty()) return 1000.0;  // DEFAULT_BEAT_LENGTH
    if (t < tps.front().timeMs) return tps.front().beatLength;
    const TimingPoint* active = &tps.front();
    for (const TimingPoint& tp : tps) {
        if (tp.timeMs > t) break;
        active = &tp;
    }
    return active->beatLength;
}

// C# unchecked int32 回绕 (用于 DotNetRandom 初始化的减法)。
int wrapToI32(int64_t v) {
    const int64_t low = v & 0xFFFFFFFFLL;  // 二进制补码低 32 位
    const int64_t signed32 = low <= 2147483647LL ? low : low - 4294967296LL;
    return static_cast<int>(signed32);
}

// 变换后的最终物件表: 上游 OrderBy(StartTime) 稳定排序 + sourceIndex 重编号。
void finalizeTransformedObjects(std::vector<ManiaHitObject>& objects) {
    std::stable_sort(objects.begin(), objects.end(),
                     [](const ManiaHitObject& a, const ManiaHitObject& b) {
                         return a.startTimeMs < b.startTimeMs;
                     });
    for (size_t i = 0; i < objects.size(); ++i)
        objects[i].sourceIndex = static_cast<int>(i);
}

}  // namespace

void applyManiaInvert(std::vector<ManiaHitObject>& objects,
                      const std::vector<TimingPoint>& timingPoints) {
    // TimingPoints = 非继承点 (beatLength > 0), 按时间升序 (上游加载时已排序;
    // .osu 文件序一般即升序, stable_sort 兼容异常输入)。
    std::vector<TimingPoint> tps;
    for (const TimingPoint& tp : timingPoints)
        if (tp.uninherited && tp.beatLength > 0) tps.push_back(tp);
    std::stable_sort(tps.begin(), tps.end(),
                     [](const TimingPoint& a, const TimingPoint& b) { return a.timeMs < b.timeMs; });

    // GroupBy(h => h.Column): 组按首次出现序, 组内保持文件序 (LINQ GroupBy 语义)。
    std::vector<int> colOrder;
    std::map<int, std::vector<const ManiaHitObject*>> byColumn;
    for (const ManiaHitObject& o : objects) {
        if (!byColumn.count(o.column)) colOrder.push_back(o.column);
        byColumn[o.column].push_back(&o);
    }

    std::vector<ManiaHitObject> newObjects;
    for (const int col : colOrder) {
        const std::vector<const ManiaHitObject*>& colObjs = byColumn[col];
        // locations = notes (OfType<Note>, 文件序) Concat LN 头 (OfType<HoldNote>,
        // 文件序) → OrderBy(startTime) 稳定排序 (并列: Note 全部在 LN 头之前,
        // 组内各按文件序)。
        struct Loc {
            int64_t t = 0;
        };
        std::vector<Loc> locations;
        locations.reserve(colObjs.size());
        for (const ManiaHitObject* o : colObjs)
            if (o->type == ManiaObjectType::Note) locations.push_back({o->startTimeMs});
        for (const ManiaHitObject* o : colObjs)
            if (o->type == ManiaObjectType::Hold) locations.push_back({o->startTimeMs});
        std::stable_sort(locations.begin(), locations.end(),
                         [](const Loc& a, const Loc& b) { return a.t < b.t; });

        // for (int i = 0; i < locations.Count - 1; i++) — 逐字: 末物件不产 LN,
        // 单物件列 (Count <= 1) 整列消失。
        for (size_t i = 0; i + 1 < locations.size(); ++i) {
            // Full duration of the hold note.
            const int64_t delta = locations[i + 1].t - locations[i].t;
            // Beat length at the end of the hold note (尾物件时刻, 不是 t[i])。
            const double beatLength = invertBeatLengthAt(tps, static_cast<double>(locations[i + 1].t));
            // Decrease the duration by at most a 1/4 beat to ensure there's no
            // instantaneous notes.
            const double duration = std::max(delta / 2.0, static_cast<double>(delta) - beatLength / 4.0);

            ManiaHitObject h;
            h.type = ManiaObjectType::Hold;
            h.column = col;
            h.startTimeMs = locations[i].t;
            // 上游 Duration 是 double (EndTime = StartTime + Duration); 物件表是
            // int64 — llround 落点 (偏差 ≤0.5ms, 与尾档残差同一不可复现带)。
            h.endTimeMs = locations[i].t + static_cast<int64_t>(std::llround(duration));
            newObjects.push_back(h);
        }
    }

    // maniaBeatmap.HitObjects = newObjects.OrderBy(h => h.StartTime).ToList();
    finalizeTransformedObjects(newObjects);
    objects = std::move(newObjects);
}

void applyManiaHoldOff(std::vector<ManiaHitObject>& objects) {
    // maniaBeatmap.HitObjects.OfType<Note>() (文件序) Concat 每条 HoldNote 的头
    // Note (文件序), 再 OrderBy(StartTime) 稳定排序 ⇒ 并列时原始 Note 在前。
    std::vector<ManiaHitObject> newObjects;
    newObjects.reserve(objects.size());
    for (const ManiaHitObject& o : objects) {
        if (o.type != ManiaObjectType::Note) continue;
        newObjects.push_back(o);  // 原始 Note 原样保留 (含 raw 诊断字段)
    }
    for (const ManiaHitObject& o : objects) {
        if (o.type != ManiaObjectType::Hold) continue;
        ManiaHitObject n;  // new Note { Column, StartTime, Samples=GetNodeSamples(0) }
        n.type = ManiaObjectType::Note;
        n.column = o.column;
        n.startTimeMs = o.startTimeMs;
        n.rawX = o.rawX;
        n.rawY = o.rawY;
        n.rawType = o.rawType;
        n.rawHitSound = o.rawHitSound;
        n.rawLine = o.rawLine;
        newObjects.push_back(std::move(n));
    }
    finalizeTransformedObjects(newObjects);
    objects = std::move(newObjects);
}

DotNetRandom::DotNetRandom(int seed) {
    // CompatPrng.Initialize — 逐行 (dotnet/runtime main Random.CompatImpl.cs;
    // release/8.0 Random.Net5CompatImpl.cs 同一算法)。
    constexpr int kIntMax = 2147483647;
    // int subtraction = (seed == int.MinValue) ? int.MaxValue : Math.Abs(seed);
    const int subtraction = seed == (-2147483647 - 1) ? kIntMax : (seed < 0 ? -seed : seed);
    int mj = wrapToI32(161803398LL - subtraction);  // magic number based on Phi
    seedArray_[55] = mj;
    int mk = 1;
    int ii = 0;
    for (int i = 1; i < 55; i++) {
        // The range [1..55] is special (Knuth) and so we're wasting the 0'th position.
        ii += 21;
        if (ii >= 55) ii -= 55;
        seedArray_[ii] = mk;
        mk = wrapToI32(static_cast<int64_t>(mj) - mk);
        if (mk < 0) mk += kIntMax;
        mj = seedArray_[ii];
    }
    for (int k = 1; k < 5; k++) {
        for (int i = 1; i < 56; i++) {
            int n = i + 30;
            if (n >= 55) n -= 55;
            // C# int32 回绕点: seedArray[55] 初始可为负 (|seed| > 161803398)。
            seedArray_[i] = wrapToI32(static_cast<int64_t>(seedArray_[i]) - seedArray_[1 + n]);
            if (seedArray_[i] < 0) seedArray_[i] += kIntMax;
        }
    }
    inext_ = 0;
    inextp_ = 21;
}

int DotNetRandom::next() {
    // CompatPrng.InternalSample — 逐行。此时全部 seedArray 项 ∈ [0, int.MaxValue),
    // 减差 ∈ (-int.MaxValue, int.MaxValue) 开区间, 无回绕。
    constexpr int kIntMax = 2147483647;
    int locINext = inext_;
    if (++locINext >= 56) locINext = 1;
    int locINextp = inextp_;
    if (++locINextp >= 56) locINextp = 1;
    int retVal = seedArray_[locINext] - seedArray_[locINextp];
    if (retVal == kIntMax) retVal--;
    if (retVal < 0) retVal += kIntMax;
    seedArray_[locINext] = retVal;
    inext_ = locINext;
    inextp_ = locINextp;
    return retVal;
}

void applyManiaRandom(std::vector<ManiaHitObject>& objects, int keyCount, int seed) {
    // availableColumns = TotalColumns (native 谱 = CircleSize; keyCount<=0 时从物件推导,
    // 与 matchLazer 的推导一致)。
    if (keyCount <= 0) {
        for (const ManiaHitObject& o : objects)
            keyCount = std::max(keyCount, o.column + 1);
    }
    if (keyCount <= 0) return;
    DotNetRandom rng(seed);
    // Enumerable.Range(0, K).OrderBy(_ => rng.Next()): 键按元素序 0..K-1 各取一次
    // [OrderedEnumerable.ComputeKeys], 稳定序 [CompareAnyKeys: c==0 → index1-index2
    // "ensure stability of sort"]。
    std::vector<int> keys(static_cast<size_t>(keyCount));
    for (int i = 0; i < keyCount; ++i) keys[static_cast<size_t>(i)] = rng.next();
    std::vector<int> shuffled(static_cast<size_t>(keyCount));
    for (int i = 0; i < keyCount; ++i) shuffled[static_cast<size_t>(i)] = i;
    std::stable_sort(shuffled.begin(), shuffled.end(),
                     [&keys](int a, int b) { return keys[static_cast<size_t>(a)] < keys[static_cast<size_t>(b)]; });
    // beatmap.HitObjects.OfType<ManiaHitObject>().ForEach(h => h.Column = shuffledColumns[h.Column]);
    for (ManiaHitObject& o : objects)
        if (o.column >= 0 && o.column < keyCount) o.column = shuffled[static_cast<size_t>(o.column)];
}

}  // namespace mra
