#pragma once
// lazer 判定 profile: 对 lazer .osr (谱面时基帧) 的独立重建引擎。
// 与 stable 引擎 (hit_matcher) 并列, 不共享实现; 红线: 不改动 stable 引擎任何文件。
// 规格来源: docs/lazer-research/03-lazer-mania-judgement-acc.md (§0/§2/§3)。
#include "beatmap/osu_parser.h"
#include "mania/hit_matcher.h"      // MatchResult/HitEvent/HoldJudgement/UnmatchedInput 结构复用
#include "mania/judgement.h"
#include "mania/accuracy.h"         // JudgementCounts (契约 LazerMatchResult::counts 的类型; 契约
                                    // 原文漏了这条 include, 不加无法编译 — 已在交付说明中注明)
#include "replay/replay_frames.h"

#include <cstdint>

namespace mra {

struct LazerMatchOptions {
    double od = 0;          // 谱面 OD (double, 不取整; DA 直接传覆盖后的绝对值)
    double rate = 1.0;      // 谱面时基 rate (HT·DC 0.75 / DT·NC 1.5 / 自定义 speed_change)
    bool hardRock = false;  // difficultyMultiplier 1.4 (窗口 ÷1.4)
    bool easy = false;      // difficultyMultiplier 1/1.4 (窗口 ×1.4)
    bool mirror = false;    // 列镜像 (lazer Mirror: col -> total-1-col)
    // NR (No Release): NoReleaseTailNote.CheckForResult — IsHolding && timeOffset >= 0
    // ⇒ GetCappedResult(Perfect) (userTriggered 无关 ⇒ update tick 也吃 auto-Perfect);
    // 否则回退既有尾判定。判定语义, 不是物件变换。 [ManiaModNoRelease.cs]
    bool noRelease = false;
    // CL (Classic): ManiaHitWindows.updateWindows() 的 ClassicModActive 分支
    // (native 谱 IsConvert=false: invertedOd = Clamp(10-OD, 0, 10), 常量=stable OD10 窗
    // + 3×invertedOd; perfect 恒 16)。 [ManiaModClassic.cs + ManiaHitWindows.cs]
    bool classic = false;
    // SV2 (Score V2, lazer): ManiaModScoreV2.ApplyToBeatmap 给每个 Note / LN 头尾的
    // ManiaHitWindows 置 ScoreV2Active=true。对窗口的唯一效果是压制 CL 的 stable 分支:
    // ManiaHitWindows.cs (master) L129 `if (ClassicModActive && !ScoreV2Active)` —
    // SV2 单开不改窗口; CL+SV2 同开走普通 DifficultyRange 公式。折算在 matchLazer
    // 入口做 (classic && !scoreV2)。 [docs/lazer-sv2-window-findings.md]
    bool scoreV2 = false;
    int keyCount = 0;
};

struct LazerMatchResult {
    MatchResult match;      // 复用 stable 结果结构: events (NoteHead/HoldHead/HoldTail 判定,
                            // 含 actualTimeMs/offsetMs), holds, unmatchedInputs,
                            // replayEndedNormally, unmatchedObjects
    JudgementCounts counts; // 重建六档 = 头+尾 (等价 lazer statistics 口径; body 不计)
    double acc305 = 0;      // lazerAccuracy305Percent(counts)
};

// frames 为谱面时基 (lazer .osr 直出); 判定窗口 = base(OD) × rate / HR·EZ 倍率,
// 取整 floor(x)+0.5 ms, 闭区间 |offset| <= window。确定性: 同输入恒同输出; 不抛异常。
LazerMatchResult matchLazer(const std::vector<ManiaHitObject>& objects,
                            const std::vector<ReplayFrame>& frames,
                            const LazerMatchOptions& options);

// ── 以下为本引擎追加的辅助接口 (契约之外的增量, 非改动) ──────────────────────
//
// 六档窗口值 (毫秒, 单边), 语义与上游 ManiaHitWindows.updateWindows() 一致:
//   perfect = floor(DifficultyRange(OD, 22.4, 19.4, 13.9) * m) + 0.5
//   great   = floor(DifficultyRange(OD, 64, 49, 34)      * m) + 0.5
//   good    = floor(DifficultyRange(OD, 97, 82, 67)      * m) + 0.5
//   ok      = floor(DifficultyRange(OD, 127, 112, 97)    * m) + 0.5
//   meh     = floor(DifficultyRange(OD, 151, 136, 121)   * m) + 0.5
//   miss    = floor(DifficultyRange(OD, 188, 173, 158)   * m) + 0.5
// 其中 m = SpeedMultiplier / DifficultyMultiplier = rate / (HR 1.4 | EZ 1/1.4 | 1)。
// 来源: osu.Game.Rulesets.Mania/Scoring/ManiaHitWindows.cs (blob abff91926a1d);
//       osu.Game/Beatmaps/IBeatmapDifficultyInfo.cs DifficultyRange (两段线性, OD 节点 0/5/10)。
struct LazerWindows {
    double perfect = 0, great = 0, good = 0, ok = 0, meh = 0, miss = 0;
};

// 上游 DifficultyRange(od; min, mid, max): od>5 段 mid+(max-mid)*(od-5)/5,
// od<5 段 mid+(mid-min)*(od-5)/5, od==5 恒 mid; 不做 OD clamp。
double lazerDifficultyRange(double od, double min, double mid, double max);

// 按上面的公式计算六档窗口 (floor 发生在乘 m 之后, 端口一致性关键)。
LazerWindows lazerHitWindows(double od, double rate, bool hardRock, bool easy);

// 同上 + CL (Classic) 分支。classic=true 时走 ManiaHitWindows.updateWindows() 的
// ClassicModActive && !ScoreV2Active && !IsConvert 路径:
//   invertedOd = Clamp(10 - od, 0, 10)
//   perfect = floor(16 * m) + 0.5
//   great   = floor((34 + 3*invertedOd) * m) + 0.5   (good/ok/meh/miss 同构 67/97/121/158)
// 来源: osu.Game.Rulesets.Mania/Scoring/ManiaHitWindows.cs (master, 2026-10-25 抓取)。
LazerWindows lazerHitWindows(double od, double rate, bool hardRock, bool easy, bool classic);

// ── mania 转换类 mod 的物件变换 (Phase L3; 全部作用于传入 vector, 调用方传副本) ──
//
// 三个变换共同约定:
//   * 对应上游 IApplicableAfterBeatmapConversion / IApplicableToBeatmap 的
//     ApplyToBeatmap; api_server 按尾块 mods 列表顺序依次调用。
//   * 上游最终 HitObjects = ...OrderBy(h => h.StartTime) (LINQ 稳定排序), 之后
//     lazer 按该序消费物件 ⇒ 变换后 sourceIndex 按最终序列重编号 0..n-1 (matcher
//     的 note-lock FIFO 并列消歧用 sourceIndex, 重编号即承接上游并列序)。
//   * 输入物件表不被读取第二次; 不抛异常。
//
// IN (Invert) [ManiaModInvert.cs ApplyToBeatmap]: 逐列 (GroupBy 首现序) 收集
// Note.StartTime 与 HoldNote.StartTime (LN 头), Note 先/LN 头后拼接后按时间稳定
// 排序; 对相邻对 i→i+1 生成 HoldNote{StartTime=t[i], Duration=
// max((t[i+1]-t[i])/2, (t[i+1]-t[i]) - beatLength/4)}, beatLength =
// TimingPointAt(t[i+1]) 的非继承 beatLength (尾物件时刻生效, 不是 t[i]!);
// 最后一对循环到 Count-2 ⇒ 最末物件不产生 LN, 单物件列整列消失 (物件数收缩)。
// Duration 为 double 无取整, 这里以 llround 落到 int64 (±0.5ms 内; 与尾档
// 残差同一不可复现带, 见文件头)。Breaks 清空 (不影响判定)。
void applyManiaInvert(std::vector<ManiaHitObject>& objects,
                      const std::vector<TimingPoint>& timingPoints);

// HO (Hold Off) [ManiaModHoldOff.cs ApplyToBeatmap]: 每条 HoldNote → 头时刻的
// Note (GetNodeSamples(0) 样本, 判定无关); 最终 = 原始 Note (文件序) Concat
// 转换 Note (文件序) 后按 StartTime 稳定排序 (并列: 原始 Note 在转换 Note 前)。
// EffectPoint 滚速补偿只影响视觉滚动, 不进判定。
void applyManiaHoldOff(std::vector<ManiaHitObject>& objects);

// RD (Random) [ManiaModRandom.cs ApplyToBeatmap]:
//   shuffledColumns = Enumerable.Range(0, TotalColumns).OrderBy(_ => rng.Next());
//   h.Column = shuffledColumns[h.Column];
// rng = new Random((int)seed) ⇒ DotNetRandom (见下); LINQ OrderBy 键按元素序
// 0..K-1 各取一次, 排序并列时保持原序 (OrderedEnumerable.CompareAnyKeys:
// c == 0 → index1 - index2 "ensure stability of sort") ⇒ std::stable_sort 等价。
void applyManiaRandom(std::vector<ManiaHitObject>& objects, int keyCount, int seed);

// .NET System.Random(int seed) 逐行移植。lazer master 跑 net10.0 (osu.Game.csproj),
// net8..net10 的 new Random(int)/Next() 都是 CompatPrng (Knuth 减法; .NET Framework
// 同源算法, 逐行一致):
//   dotnet/runtime main        Random.cs: Random(int Seed) → CompatSeedImpl
//                              Random.CompatImpl.cs: CompatPrng.Initialize/InternalSample
//   dotnet/runtime release/8.0 Random.Net5CompatImpl.cs: Net5CompatSeedImpl (同一算法)
// 注意: splitmix64 派生 xoshiro256** 只用于无参 new Random(); ManiaModRandom 用
// 带种构造 ⇒ 走本算法。next() ∈ [0, int.MaxValue]。
// C# unchecked int32 回绕: seedArray[55] 可为负 (|seed| > 161803398), 初始化第二
// 个双循环的减法可越 int32 —— 用 int64 差值 + wrapToI32 复刻。
class DotNetRandom {
public:
    explicit DotNetRandom(int seed);
    int next();  // CompatPrng.InternalSample(): [0, int.MaxValue]
private:
    int seedArray_[56] = {};
    int inext_ = 0;
    int inextp_ = 21;
};

}  // namespace mra
