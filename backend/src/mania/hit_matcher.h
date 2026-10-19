#pragma once
// Replay-to-hitobject matcher for osu!mania.
// Column + time + hold-state state machine; NOT nearest-neighbour.
#include "beatmap/osu_parser.h"
#include "mania/judgement.h"
#include "replay/replay_frames.h"

#include <optional>

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
    ManiaWindows windows;
    // Mirror mod flips columns of the beatmap before matching (compat profile).
    bool mirrorColumns = false;
    // ScoreV2 rules: LN head late window ok*1.5, tail judged independently on
    // |endDiff|/1.5 (release beyond ok*1.5 late = tail Miss).
    bool scoreV2 = false;
    // 判定窗 × 速率 (acc-semantics §0.2): DT/NC 1.5, HT 0.75, NM 1.0.
    // .osr 帧时基 = 真实时间, 比较保持原样, 只放窗 (§9 红线: 不对帧/边沿乘速率).
    double rate = 1.0;
    // RD 列置换: seed 来自 .osr 尾部 RNG seed 帧 ("-12345|x|y|seed"). 开启后
    // 物件列先经该 seed 复原的置换表映射, 再走 mirrorColumns (先随机后镜像).
    bool randomMod = false;
    unsigned randomSeed = 0;
};

struct MatchResult {
    std::vector<HitEvent> events;         // includes MISS events
    std::vector<HoldJudgement> holds;     // per hold object
    std::vector<UnmatchedInput> unmatchedInputs;
    int unmatchedObjects = 0;             // objects never resolved (should be 0)
    bool replayEndedNormally = true;
};

// Matches replay key state transitions against mania hit objects.
// objects: from a parsed beatmap (already mode==mania). frames: absolute-time
// replay frames. Deterministic: same inputs always produce identical events.
MatchResult matchReplayToObjects(const std::vector<ManiaHitObject>& objects,
                                 int keyCount,
                                 const std::vector<ReplayFrame>& frames,
                                 const MatchOptions& options);

}  // namespace mra
