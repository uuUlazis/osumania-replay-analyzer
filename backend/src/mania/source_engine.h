#pragma once
// Source-faithful osu!mania judgement engine.
//
// Class-for-class port of osu!.exe build 20260924.1, derived from the IL-level
// specs in docs/port-specs/ (01 input/replay state machine, 02 judgement core).
//
//   ManiaColumn             <- ManiaColumn (0x02000195)
//   ManiaStage              <- ManiaStage (0x0200098C), judgement subset
//   ManiaNote / ManiaLongNote
//                           <- ManiaNote (0x02000130) / ManiaLongNote (0x020007E8)
//   ManiaHitObjectManager   <- ManiaHitObjectManager (0x020009F4) + the
//                              DifficultySettingsBase window fields
//   applyReplayFrame()      <- InputAndReplayPlayback.Obf014317 (0x06002AD8)
//
// Field tokens from the spec are quoted next to each member so the mapping is
// checkable line by line. Everything visual/audio is skipped (spec 02 §6.1),
// but the state fields those members write (WasHeld / HoldLightActive /
// IsDimmed / IsHiddenAfterHit) are kept because the judgement path reads them.
//
// PORT: notes on the (unavoidable) offline deviations are marked "PORT:" and
// collected in the delivery notes (they are all about the *host clock*, never
// about judgement arithmetic).
#include "mania/judgement.h"
#include "mania/score_record.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace mra {
namespace mania_src {

class ManiaHitObjectManager;

// ---------------------------------------------------------------------------
// ManiaColumn (0x02000195)
// ---------------------------------------------------------------------------
struct ManiaColumn {
    int index = 0;                  // 0x040007D6
    bool wasHeld = false;           // 0x040007DE (previous input)
    bool isHeld = false;            // 0x040007DF (current input)
    bool holdLightActive = false;   // 0x040007DC (纯视觉字段；判定从不读)

    // 0x06000B72 GetReplayBit: (int)Math.Pow(2.0, (double)Index)
    // double pow + conv.i4 — NOT `1 << Index` (spec 01 §2.6.3).
    int replayBit() const;

    // 0x06000B75 get_IsHeld
    bool getIsHeld() const { return isHeld; }

    // 0x06000B76 set_IsHeld:
    //   WasHeld = IsHeld;            <- 无条件、先搬
    //   if (IsHeld == value) return; <- 值相等直接返回
    //   IsHeld = value;              <- 之后全是图形（跳过）
    void setHeld(bool value);

    // 0x06000B74 set_HoldLightActive: early-out on equal, then the field.
    // 只有字段写入参与判定链（判定只读它做灯光）。
    void setHoldLightActive(bool value);
};

// ---------------------------------------------------------------------------
// ManiaStage (0x0200098C) — judgement-relevant subset
// ---------------------------------------------------------------------------
struct ManiaStage {
    // Columns[i].Index == i; the vector index is the column identity used by
    // ManiaNote::Column and by every `[int]column` in the source.
    std::vector<ManiaColumn> columns;
    // 0x060045F7 `Stage.obf020574()` == 2  (skin [Mania] SpecialStyle: Right)
    bool specialStyleRight = false;
    // `Stage.Skin != null` — every real mania stage has a skin; .osr 不记录
    // SpecialStyle，调用方通过 specialStyleRight 声明（spec 01 O-4）。
    bool hasSkin = true;

    int columnCount() const { return static_cast<int>(columns.size()); }

    // 0x0600461B Obf020597: `if (n <= 4) return false; return (n & 1) == 0;`
    bool evenKeysOverFour() const;

    // 0x0600461C Obf020598:
    //   if (!Obf020597()) return false;
    //   if (Skin == null) return false;
    //   return obf020574() == 2;
    bool specialKeyOnRight() const;
};

// ---------------------------------------------------------------------------
// HitObjectBase / HitObjectData judgement state
// ---------------------------------------------------------------------------
struct HitObjectBase {
    int startTime = 0;              // 0x04001573
    int endTime = 0;                // 0x04001574
    int objectType = 0;             // 0x04001575 ObjectType (Virtual0150 bit test)
    bool isJudged = false;          // 0x04002B16
    int32_t judgmentResult = 0;     // 0x04002B19
    JudgeTargetState scoreTarget;   // 0x040020153 / 0x040020154
    int sourceIndex = -1;           // 外部簿记：调用方 objects 向量的下标

    virtual ~HitObjectBase() = default;
    virtual int32_t judge(ManiaHitObjectManager& manager) = 0;

    // 0x060043E6 Virtual0180 —— 空体（spec 02 §7.10.2 逐字节核对：仅 `ret`）
    virtual void virtual0180() {}

    // 0x060043EF Virtual0025 —— 按键音（音频，跳过）
    virtual void virtual0025() {}
};

// ---------------------------------------------------------------------------
// ManiaNote (0x02000130)
// ---------------------------------------------------------------------------
struct ManiaNote : HitObjectBase {
    ManiaColumn* column = nullptr;   // 0x04000452
    int originalColumnIndex = 0;     // 0x04000453
    int noteKind = 0;                // 0x04000454 (0=rice, 1=LN；2 与 0 同路)
    int pressTime = 0;               // 0x04000455
    int releaseTime = 0;             // 0x04000456
    bool isHeld = false;             // 0x04000457
    float duration = 0.f;            // 0x04000459 (LN: (float)(EndTime-StartTime))
    bool isDimmed = false;           // 0x0400045B
    bool isHiddenAfterHit = false;   // 0x0400045C

    // ---- 报告用簿记（不参与判定） ----
    // pressTime / releaseTime 的默认值 0 无法区分"从未按下"，这里单独记一笔。
    bool pressRecorded = false;
    bool releaseRecorded = false;
    // JudgeObject 交给判定出口的最终 flags（PerfectRequires300 过滤之后）；
    // 0 表示该物件没有产生任何可计数的判定。
    int32_t countedResult = 0;

    // 0x06000868 ManiaNote::Judge
    int32_t judge(ManiaHitObjectManager& manager) override;

    // 0x0600087A Virtual0110（命中后隐藏）/ 0x0600087B Virtual0112（变暗）
    void virtual0110();
    void virtual0112(bool dimValue);
};

// ---------------------------------------------------------------------------
// ManiaLongNote (0x020007E8)
// ---------------------------------------------------------------------------
struct ManiaLongNote : ManiaNote {
    int lastTickTime = -1;      // 0x04002521 (ctor -1)
    int pressCount = 0;         // 0x04002525
    int releaseCount = 0;       // 0x04002526
    bool headJudged = false;    // 0x04002527
    bool wasBroken = false;     // 0x04002528
    bool samplePlayed = true;   // 0x0400251E (ctor true)

    // ---- 报告用簿记（不参与判定） ----
    int32_t headJudgment = 0;   // JudgeLongNoteHead 的结果快照（V2 头判）
    int32_t tickCount = 0;      // 100ms tick 的次数（每次返回 7 记一次）

    // 0x06003A09 ProcessLongNoteTick —— 100 ms 一拍，返回 7
    int32_t processLongNoteTick(int32_t now);
    // 0x06003A0A JudgeLongNoteHead
    int32_t judgeLongNoteHead(ManiaHitObjectManager& manager);
    // 0x06003A0B Obf017937 —— 视觉变暗判定（写入 IsDimmed）
    void dimCheck(ManiaHitObjectManager& manager);
    // 0x06003A0D ManiaLongNote::Judge（尾判 / 破 LN）
    int32_t judge(ManiaHitObjectManager& manager) override;
};

// ---------------------------------------------------------------------------
// 物件构造（ManiaNote::.ctor 0x06000863 / ManiaLongNote::.ctor 0x06003A04）
// ---------------------------------------------------------------------------
// 两者都把列号先经 `RemapManiaColumn` 重映射再用 `Stage.Columns[column]` 取列，
// 原始列号保留在 originalColumnIndex（spec 02 §3.4）。
std::unique_ptr<ManiaNote> createManiaNote(ManiaHitObjectManager& manager,
                                           int column, int startTime,
                                           int objectType, int sourceIndex);
std::unique_ptr<ManiaLongNote> createManiaLongNote(ManiaHitObjectManager& manager,
                                                   int column, int startTime,
                                                   int endTime, int objectType,
                                                   int sourceIndex);

// ---------------------------------------------------------------------------
// 判定窗 (§2.1 / §2.2) — 自由函数版本，便于在不持有 manager 的场合复算窗口表
// ---------------------------------------------------------------------------
// 0x060049C7 AdjustManiaHitWindow: HR ÷1.4 else EZ ×1.4; DT ×1.5 else HT ×0.75;
// 末尾唯一一次 conv.i4（向零截断）。
int adjustManiaHitWindowFor(int32_t activeMods, double window);
// 0x060049C8 DifficultyRange（ble.un / bge.un，NaN 走第一支）
double difficultyRangeFor(double difficulty, double min, double mid, double max);

// ApplyDifficultySettings 的结果集合（窗口字段"分裂在子类/基类"是真实布局）。
struct ManiaWindowSet {
    int windowMax = 20;        // 0x04002E44 (子类默认 20)
    int window300 = 0;         // 0x04000C2B
    int window200 = 60;        // 0x04002E45 (子类默认 60)
    int window100 = 0;         // 0x04000C2A
    int window50 = 0;          // 0x04000C29
    int earlyInputWindow = 178;  // 0x04002E43 (子类默认 178)
};

// 0x060049C9 的四条互斥路径：
//   A ScoreV2 (AlternateScoreProcessor != null)
//   B 原生 mania 谱 (Beatmap.Obf012403() == 3)
//   C 转换谱 Round(OD) > 4 / D 转换谱 Round(OD) <= 4
ManiaWindowSet computeManiaWindowSet(double od, int32_t activeMods, bool scoreV2,
                                     bool nativeManiaBeatmap);

// ---------------------------------------------------------------------------
// ManiaHitObjectManager (0x020009F4)
// ---------------------------------------------------------------------------
class ManiaHitObjectManager {
public:
    ManiaHitObjectManager(ManiaStage& stage, const ManiaScoreRecord* currentScore,
                          int32_t activeMods, uint32_t randomSeed);

    // ---- 判定出口：HitObjectManagerBase 0x04001E87 委托 ----
    // `void (JudgmentFlags, string, HitObjectBase)` —— string.Empty 只是标签，
    // 离线端不需要，故签名收敛为 (flags, object)。
    std::function<void(int32_t flags, ManiaNote* object)> judgmentCallback;
    // 诊断出口：本 tick 有"新按下边沿"但同列没有任何物件被消费（第二趟兜底）。
    std::function<void(int32_t now, int column)> unmatchedPressCallback;

    // ---- 全局时间 Obf002109.Obf008161（谱面时间轴 int ms） ----
    int32_t now = 0;
    // PORT: PlaySession.<0x04000A51> == 2 的语义未定论（spec 02 Q1）。原版在
    // 进入 gameplay 时写 2；离线复算恒视为"playing"（第二趟统计/按键音生效）。
    bool clockPlaying = true;

    // ---- 协作者 ----
    ManiaStage& stage;
    const ManiaScoreRecord* currentScore = nullptr;  // PlaySession.CurrentScore
    int32_t activeMods = 0;                          // HitObjectManagerBase.ActiveMods

    // ---- 字段 (§3.0) ----
    int earlyInputWindow = 178;   // 0x04002E43 (ctor)
    int windowMax = 20;           // 0x04002E44 (ctor)
    int window200 = 60;           // 0x04002E45 (ctor)
    int window300 = 0;            // DifficultySettingsBase 0x04000C2B
    int window100 = 0;            // 0x04000C2A
    int window50 = 0;             // 0x04000C29
    int approachTime = 200;       // 0x04000C28 (Virtual0113 之前只有 +200 项)
    int firstStartTime = INT32_MAX;    // 0x04002E46
    int lastEndTime = INT32_MIN;       // 0x04002E47
    int longestDuration = INT32_MIN;   // 0x04002E48
    int riceCount = 0;                 // 0x04002E49
    int longNoteCount = 0;             // 0x04002E4A
    int scoringObjectCount = 0;        // 0x04002E4B
    bool perfectRequires300 = false;   // 0x04002E4F
    ManiaNote* lastEndingObject = nullptr;  // 0x04002E55
    std::vector<ManiaNote*> pendingKeysoundNotes;  // 0x04002E50
    std::vector<int> randomColumnMap;              // 0x04002E51
    std::vector<int> keyPressTimes;                // 0x04002E52
    int rapidPressWeight = 0;                      // 0x04002E53
    int shortPressCount = 0;                       // 0x04002E54

    // ---- HitObjectManagerBase ----
    std::vector<ManiaNote*> hitObjects;    // 0x04001E7E (StartTime 升序)
    std::vector<ManiaNote*> activeObjects; // 0x04001E80
    ManiaNote* currentJudgmentObject = nullptr;  // 0x04001E82
    int32_t lastJudgedIndex = -1;                // 0x04001E7B

    // ---- PlaySession cross-state ----
    bool isLongNoteHeld = false;   // PlaySession.0x04000A04

    // ---- 窗口 (§2.1 / §2.2) ----
    // 0x060049C7 / 0x060049C9 —— 委托给上面的自由函数（字段落位不变）。
    int adjustManiaHitWindow(double window) const;
    // 0x060049C8
    static double difficultyRange(double difficulty, double min, double mid,
                                  double max);
    // 0x060049C9 —— 四条互斥路径（A ScoreV2 / B 原生 mania / C,D 转换谱）
    // `nativeManiaBeatmap` = (Beatmap.Obf012403() == 3)。
    void applyDifficultySettings(double od, bool nativeManiaBeatmap);
    ManiaWindowSet windowSet() const;

    // ---- 列映射 (§3.4 / §5.4) ----
    // 0x060049CA: Random(RandomColumnMap) → special-key shift → Mirror
    int remapManiaColumn(int column) const;

    // ---- 物件表 ----
    void obf021376(ManiaNote* note);        // 0x060049CB (rice/LN 计数)
    void addObject(ManiaNote* note);        // 0x060049CD + base Virtual0162
    // 0x060049D1 Virtual0113 —— 载入完成；scrollPreemptMs 是
    // ManiaScrollSpeed.Obf012146(0, HitPosition) 的结果（PORT: 皮肤/BPM 相关，
    // 引擎不可知，由调用方给定）。
    void onLoadComplete(double scrollPreemptMs);
    // 0x060049C1 Virtual0063 判定段：建 PendingKeysoundNotes / KeyPressTimes /
    // RandomColumnMap（RandomColumnMap 用 .NET System.Random(seed) 绘制）。
    void initializeJudgementArrays(uint32_t randomSeed);

    // ---- 每帧 (§3.8 / §3.10) ----
    void virtual0177();             // 0x060049D9 ActiveObjects 窗口
    void updateInputJudgments();    // 0x060049DA 按键状态机本体
    bool expireManiaNote(ManiaNote* note);   // 0x060049DB
    int32_t judgeObject(ManiaNote* note);    // 0x060049E0
    int32_t judgeLongNoteHead(ManiaLongNote* ln);  // 0x060049DF
    int32_t processLongNoteTick(ManiaLongNote* ln);  // 0x060049DE

    // ScoreV2 判定 (= PlaySession.CurrentScore?.AlternateScoreProcessor != null)
    bool hasAlternateScoreProcessor() const;

    // 0x060049BE Obf021373 —— rapid-press 比例（当前 build 无调用点，保留）
    bool obf021373() const;

    // List<HitObjectBase>.BinarySearch(real object)：StartTime 三路比较。
    // 返回 (>=0) 命中下标 / (<0) ~lo。
    int binarySearchByStartTime(const ManiaNote* key) const;

private:
    // spec 02 §7.10.1 —— Virtual0177 的键是 EndTime 三路比较，且 ~i 直接当切片
    // 起点（上界语义）；含 LN 段必须逐比较复刻二分循环。
    int rangeStartForEndTimeKey(int32_t key) const;
};

// ---------------------------------------------------------------------------
// InputAndReplayPlayback.Obf014317 —— 回放帧写入（判定链上唯一的列输入写入者）
// ---------------------------------------------------------------------------
// `frameTime` = (int)ReplayFrame.Time（绝对 ms）; `keyMask` = (int)ReplayFrame.X。
// 语义（spec 01 §2.3.8(g) / §2.6.2 / §5.1）：
//   1. 时间被帧钉回：Obf002109.Obf008161 = frame.Time；
//   2. for i in 0..Count-1: b = (keyMask & Columns[i].GetReplayBit()) > 0
//        特殊键在右 ⇒ 目标列 = (i == 0 ? Count-1 : i-1)（掩码位右旋一位）
//        否则       ⇒ 目标列 = i
//      Columns[target].set_IsHeld(b)
// 每调用一次就等同于一个"消费了新帧"的 tick；(h) 分支的
// `WasHeld = IsHeld` 重同步由 set_IsHeld 自身完成，故连续调用即可。
void applyReplayFrame(ManiaHitObjectManager& manager, int32_t frameTime,
                      int32_t keyMask);

}  // namespace mania_src
}  // namespace mra
