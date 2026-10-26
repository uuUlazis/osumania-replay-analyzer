#pragma once
// ScoreV1 / ScoreV2 submission cluster (spec 03-scoring-acc.md).
//
//   JudgmentRecord            ← Obf001009 (0x02000486) / Obf001972 (0x0200093C)
//   ManiaScoreV2Processor     ← Obf000737 (0x02000340) + ManiaScoreV2Processor
//                               (0x02000362): combo / acc pools, total score
//   ManiaScoreRecord          ← ScoreAndReplayRecord (0x020008C1) +
//                               ManiaScoreRecord (0x02000378): counts, acc, grade
//   ManiaScoreSubmission      ← GameplayRulesetBase::Obf007628 (0x0600119E)
//                               scoring subset + Obf002087 combo counter +
//                               Obf006730 health pool + ManiaGameplay::Obf007601
//                               multiplier (0x060025E9)
//
// Quirks that MUST survive (FIDELITY / spec 03):
//   * every count is a ushort (conv.u2 wraparound);
//   * CountMiss is zeroed twice by the record reset;
//   * V1 score is a truncate-then-add mix of the double accumulator
//     (`(int)doubleAcc` before adding the per-judgement delta);
//   * the ScoreV2 pool term is `200000.0 * pool / snapshotPool` with NO
//     divide-by-zero guard: 0/0 = NaN, then Math::Round(NaN)=NaN and
//     `conv.i4` yields 0x80000000 (x64 cvttsd2si) — reproduced verbatim;
//   * Math::Round is the single-argument overload (midpoint to even).
#include "mania/accuracy.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace mra {
namespace mania_src {

// .NET `conv.i4` (x64 cvttsd2si): NaN / out-of-range converts to 0x80000000.
int32_t dotnetConvI4(double v);

// Math::Round(double) — single argument => MidpointRounding.ToEven.
inline double dotnetRound(double v) { return std::nearbyint(v); }

// ---- Obf001009 / Obf001972: one judgement record ----
struct JudgmentRecord {
    int32_t flags = 0;          // 0x040011810 Obf011810
    bool affectsCombo = false;  // 0x040011808 Obf011808 (flag2)
    bool affectsScore = false;  // 0x040011807 Obf011807 (flag3)
    int32_t comboSnapshot = 0;  // 0x040011809 Obf011809

    // 0x06002187 Obf011812 — `v = flags & ~7; switch (v) { 8->10, 64->30,
    // 128->30, 256->50, 512->100, 1024->300, 8192->100, 16384->500, else 0 }`.
    // Mania flags all fall to `default` => 0 (spec 03 Q1).
    int32_t scoreValue() const;
};

// Per-object judgement state used by the submission path:
//   0x040020153 -> countable   (`bool obf = hitObject.Obf020153`, counts gate)
//   0x040020154 -> scoredValue (`num`, the "判定值" written back each judgement)
struct JudgeTargetState {
    bool countable = true;
    int32_t scoredValue = 0;
};

class ManiaScoreRecord;

// ---- Obf000737 + ManiaScoreV2Processor ----
class ManiaScoreV2Processor {
public:
    explicit ManiaScoreV2Processor(ManiaScoreRecord* host);

    void reset(bool snapshot);                     // 0x06001866 Obf006077(bool)
    void addJudgment(const JudgmentRecord& jud);   // 0x06001867 Obf004900(jud)
    void accumulate(const JudgmentRecord& jud);    // 0x06001962 Obf007871 (mania)
    double comboPart() const;                      // 0x0600195F Obf007868
    double accPart() const;                        // 0x06001960 Obf007869
    int32_t totalScore() const;                    // 0x06001961 Obf007870

    double pool() const { return obf009374_; }
    double snapshotPool() const { return obf009373_; }
    int32_t snapshotCount() const { return obf009372_; }
    int32_t lastComboSnapshot() const { return obf009371_; }
    size_t judgmentCount() const { return obf009376_.size(); }

private:
    ManiaScoreRecord* host_;
    int32_t obf009371_ = 0;                 // 0x04001010
    int32_t obf009372_ = 0;                 // 0x04001011 snapshot judgement count
    double obf009373_ = 0.0;                // 0x04001012 snapshot pool
    double obf009374_ = 0.0;                // 0x04001013 pool (mania: combo award)
    double obf009375_ = 0.0;                // 0x04001014 pool (other rulesets)
    std::vector<JudgmentRecord> obf009376_;  // 0x04001015
};

// ---- ScoreAndReplayRecord + ManiaScoreRecord ----
class ManiaScoreRecord {
public:
    ManiaScoreRecord(uint32_t activeMods, int keyCount, bool attachV2);

    ScoreCounts counts;
    uint32_t activeMods = 0;   // 0x04002960
    int32_t mode = 3;          // 0x04002961 PlayModes.OsuMania
    int keyCount = 0;          // ManiaStage key count (score multiplier helpers)
    bool passed = true;        // 0x04002968
    bool isPerfect = false;    // 0x0400296C
    int32_t intScore = 0;      // 0x04002975 Obf007144 (V1 integer score)
    double doubleAcc = 0.0;    // 0x04002976 Obf019481 (V1 double accumulator)
    std::unique_ptr<ManiaScoreV2Processor> alternateScoreProcessor;  // 0x0400297E

    bool hasV2() const { return alternateScoreProcessor != nullptr; }

    // 0x060019E2 — V1 formula unless a V2 processor is attached or the
    // ScoreV2 mod bit (0x20000000) is set.
    float accuracy() const;
    // 0x060019E5
    int grade() const;
    // 0x060040A3 — `AlternateScoreProcessor?.Obf007870() ?? Obf007144`
    int32_t score() const;
    // 0x060040A4 — no clamp
    void setIntScore(int32_t v) { intScore = v; }
    // 0x060040BA — record level reset (CountMiss is written twice: 源码怪癖)
    void resetScore(bool snapshot);
    // 0x060019E1 — mania override of Obf008186 (replay availability)
    bool replayAvailable() const { return false; }
};

// ---- Obf000587.Obf007573 / Obf008174, mania branch (spec 03 §3.4) ----
// `mods` should already be masked by the caller (the 3-argument overload,
// mania branch: NoFail/Easy/HalfTime x0.5, SpunOut/Relax/Relax2 x0.0,
// Random no-op, then the key-count correction). Returns Math::Max(0.0, mult).
double maniaModMultiplier(uint32_t mods, int keyCount, bool keyCoop);

// `Obf008174(mods, mode, beatmap)` for mania: clears the NoFail bit and
// multiplies by the 3-argument result (mania's HR/DT do NOT add anything).
double maniaTwoArgMultiplier(uint32_t mods, int keyCount, bool keyCoop);

// ---- GameplayRulesetBase::Obf007628 (scoring subset) ----
class ManiaScoreSubmission {
public:
    // GameplayRulesetBase ctor: Obf007571/572/573 = 1.0,
    // Obf006730 = (double)ManiaScoringConstants.Obf005337 = 100.0
    ManiaScoreSubmission(ManiaScoreRecord* record, int scoringObjectCount,
                         int keyCount, bool maniaMode);

    // ManiaGameplay::Obf007601 (0x060025E9) — mania score multiplier:
    //   Obf007573 = 500000.0 / (double)ScoringObjectCount *
    //               Obf000587.Obf007573(ActiveMods & ~0x100458, mode, beatmap)
    void setupMultiplier();

    // One judgement entering the score cluster. `flags` is the raw
    // JudgmentFlags value; `target` is the hit object's 0x040020153 /
    // 0x040020154 state.
    void onJudgment(int32_t flags, JudgeTargetState& target);

    // ManiaGameplay::Obf007620 (end of play settlement): V1 integer score is
    // overwritten by `(int)Math.Round(doubleAcc)` before the 999999 -> 1000000
    // rewrite, then IsPerfect is recomputed.
    void finish();

    // PlaySession::Obf006686 的附属复位：combo 计数器 (Obf002087) 与血量
    // (Obf007559) 也各有一个 Obf006077 ⇒ 归零 / 回到 100。
    void resetComboAndHealth();

    double health() const { return obf006730_; }
    double multiplier() const { return obf007573_; }
    int32_t comboCounter() const { return comboCounter_; }
    ManiaScoreRecord& record() { return *record_; }
    const ManiaScoreRecord& record() const { return *record_; }

private:
    ManiaScoreRecord* record_;
    int scoringObjectCount_ = 0;
    int keyCount_ = 0;
    bool maniaMode_ = true;
    int32_t comboCounter_ = 0;   // Obf002087.Obf011171 / Obf011172
    double obf006730_ = 100.0;   // health pool
    double obf007573_ = 1.0;     // score multiplier (ManiaGameplay::Obf007601)
};

}  // namespace mania_src
}  // namespace mra
