#pragma once
// Replay-to-hitobject matching for osu!mania — thin offline driver around the
// source-faithful engine (mania/source_engine.h).
//
// The engine reproduces osu!.exe build 20260924.1 exactly (judgement windows,
// column/note/LN state machines, ActiveObjects window, judgement flags); this
// file only supplies the host-side glue:
//   * builds the ManiaStage / ManiaNote table from the parsed .osu,
//   * feeds the replay frames through InputAndReplayPlayback.Obf014317
//     (`applyReplayFrame`) and runs one judgement tick per frame,
//   * settles the objects that outlive the last frame,
//   * projects the resulting per-object judgement state onto the HitEvent /
//     HoldJudgement view the HTTP + analysis layers consume,
//   * forwards every judgement to the ScoreV1/ScoreV2 submission cluster.
//
// PORT (host clock): the real client advances `Obf002109.Obf008161` from the
// audio clock and consumes replay frames at wall-clock rate. Offline the only
// deterministic time base is the frame times themselves, so one frame == one
// judgement tick and `now == frame.Time` (spec 01 §3.2/§3.3, open issue O-2).
// DT/HT never scale frame times — they only scale the judgement windows via
// AdjustManiaHitWindow (ActiveMods bits 64 / 256), exactly like the source.
#include "beatmap/osu_parser.h"
#include "mania/judgement.h"
#include "replay/replay_frames.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace mra {

struct HitEvent {
    int objectIndex = -1;
    int column = 0;
    int64_t objectTimeMs = 0;
    std::optional<int64_t> actualTimeMs;
    std::optional<double> offsetMs;   // actual - object
    HitResult judgement = HitResult::Miss;
    HitPhase phase = HitPhase::NoteHead;
    bool inferredFromReplay = true;
    bool verifiedAgainstScoreCounts = false;
    // V1 (stable) LN combined judgement (head+tail as ONE unit, per the
    // community reference judger). Set on hold_tail events only; the
    // renderer uses it to colour the whole LN in V1 mode.
    std::optional<HitResult> v1Judgement;
};

struct HoldJudgement {
    HitEvent head;
    HitEvent tail;
    bool releasedEarly = false;     // released before endTime
    bool heldThroughTail = true;    // key still down at/past endTime
};

struct UnmatchedInput {
    int64_t timeMs = 0;
    int column = 0;
};

struct MatchOptions {
    // ---- legacy carriers (kept so callers/tests keep compiling) ----
    // `windows` is NOT used for judging any more: the engine derives the real
    // int32 windows from `od` + `mods` (ApplyDifficultySettings). It is only
    // read when `od` is left unset (legacy callers) to recover the OD.
    ManiaWindows windows;
    // Mirror mod — folded into `mods` (0x40000000).
    bool mirrorColumns = false;
    // ScoreV2 pass (= AlternateScoreProcessor attached).
    bool scoreV2 = false;
    // Legacy rate carrier. IGNORED for judging: DT/HT only enter the source as
    // ModifyHitWindow factors driven by the mod bits (spec 01 §3.3).
    double rate = 1.0;
    // .osr mod bits (ActiveMods). HR 0x10 / EZ 0x2 / DT 0x40 / HT 0x100 /
    // NC 0x200 / Perfect 0x4000 / Random 0x200000 / KeyCoop 0x2000000 /
    // ScoreV2 0x20000000 / Mirror 0x40000000.
    uint32_t mods = 0;
    // RD (Random) column shuffle: seed from the .osr trailing seed frame.
    bool randomMod = false;
    unsigned randomSeed = 0;

    // ---- new-engine inputs ----
    // Raw OverallDifficulty (double from the .osu). NaN ⇒ recover from
    // `windows.miss` with the classic `od = (188 - miss) / 3` identity.
    double od = std::numeric_limits<double>::quiet_NaN();
    // ManiaStage.Obf020598() ("special key on the right"): 键数 > 4 且为偶数且
    // skin [Mania] SpecialStyle == Right。.osr 不记录，由调用方声明。
    // 打开后输入写入侧与 RemapManiaColumn 侧各自按规格旋转（NoMod 下相消）。
    bool specialKeyRight = false;
    // Beatmap.Obf012403() == 3 (native mania map, not a converted one).
    bool nativeManiaBeatmap = true;
    // ManiaScrollSpeed.<0x0600229F>(0, HitPosition) — the scroll-speed preempt
    // used by ApproachTime. PORT: skin/BPM dependent, not recoverable offline.
    double scrollPreemptMs = 1000.0;
    // Settle objects that outlive the last replay frame by ticking at their
    // ExpireManiaNote deadline (EndTime + Window100, V2 LN ×1.5f).
    bool settleTail = true;
};

// Source score view (spec 03). Informational: the HTTP layer keeps deriving its
// counts from the events, this is the score cluster's own authoritative state.
struct SourceScoreSummary {
    int64_t count300 = 0;
    int64_t count100 = 0;
    int64_t count50 = 0;
    int64_t countMax = 0;
    int64_t count200 = 0;
    int64_t countMiss = 0;
    int64_t maxCombo = 0;
    int64_t currentCombo = 0;
    float accuracy = 1.0f;       // faithful single-precision acc (0..1]
    int grade = 8;               // Obf001147 (XH..F)
    int64_t intScore = 0;        // Obf007144 (V1 整型分, 已按 Obf007620 覆盖)
    double doubleAcc = 0.0;      // Obf019481 (V1 无损失累计器)
    int32_t scoreV2 = 0;         // ManiaScoreV2Processor.Obf007870()
    bool scoreV2NanPool = false; // 0/0 ⇒ NaN ⇒ conv.i4 = 0x80000000 (spec 03 Q1)
    int64_t lnTickCount = 0;     // 100 ms LN tick (flags 7) 次数
};

struct MatchResult {
    std::vector<HitEvent> events;         // includes MISS events
    std::vector<HoldJudgement> holds;     // per hold object
    std::vector<UnmatchedInput> unmatchedInputs;
    int unmatchedObjects = 0;             // objects never resolved (should be 0)
    bool replayEndedNormally = true;
    // 仅用于上报 (判定完全不用): 本次 pass 的判定窗两套取值。`miss` 槽承载
    // EarlyInputWindow，`perfect`..`meh` 承载 WindowMax..Window50；窗口现在是
    // int32（积分 = true），故 JSON 的 windows 数值会随之变化。
    ManiaWindows windowsV1;               // V1 档 (path B)
    ManiaWindows windowsV2;               // V2 档 (path A)
    bool integralWindows = true;          // 窗口已整数化（恒 true）
    SourceScoreSummary sourceScore;       // spec 03 计分簇的结果
};

// Matches replay key state transitions against mania hit objects.
// objects: from a parsed beatmap (already mode==mania). frames: absolute-time
// replay frames. Deterministic: same inputs always produce identical events.
MatchResult matchReplayToObjects(const std::vector<ManiaHitObject>& objects,
                                 int keyCount,
                                 const std::vector<ReplayFrame>& frames,
                                 const MatchOptions& options);

}  // namespace mra
