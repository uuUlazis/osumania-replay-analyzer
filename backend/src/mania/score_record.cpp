#include "mania/score_record.h"

#include <algorithm>

namespace mra {
namespace mania_src {
namespace {

// osu_common.Mods bits used by the scoring cluster (spec 03 §0.2).
constexpr uint32_t kModNoFail = 0x1u;
constexpr uint32_t kModEasy = 0x2u;
constexpr uint32_t kModHidden = 0x8u;
constexpr uint32_t kModHardRock = 0x10u;
constexpr uint32_t kModDoubleTime = 0x40u;
constexpr uint32_t kModRelax = 0x80u;
constexpr uint32_t kModHalfTime = 0x100u;
constexpr uint32_t kModRandom = 0x200000u;
constexpr uint32_t kModRelax2 = 0x2000u;
constexpr uint32_t kModSpunOut = 0x1000u;
constexpr uint32_t kModKeyCoop = 0x2000000u;
constexpr uint32_t kModScoreV2 = 0x20000000u;

// Obf020609 / Obf020610 (mania branch): both read the beatmap's circle size
// (0x040016417) as `(int)Math.Round(...)`, so a == b unless KeyCoop doubles the
// played key count. (spec 03 Q3: the helper fields are unnamed in metadata.)
int maniaBeatmapKeyCount(int keyCount) {
    return static_cast<int>(std::nearbyint(static_cast<double>(keyCount)));
}

}  // namespace

int32_t dotnetConvI4(double v) {
    // x64 cvttsd2si: NaN / out-of-range converts to the "integer indefinite"
    // value 0x80000000. This is the behaviour the ScoreV2 NaN path relies on.
    if (!(v > -2147483649.0 && v < 2147483648.0)) return INT32_MIN;
    return static_cast<int32_t>(v);  // truncation toward zero
}

double maniaModMultiplier(uint32_t mods, int keyCount, bool keyCoop) {
    double mult = 1.0;
    // mania branch of Obf007573(Mods, PlayModes, Beatmap) — 只有这些位，顺序固定
    if ((mods & kModNoFail) > 0u) mult *= 0.5;
    if ((mods & kModEasy) > 0u) mult *= 0.5;
    if ((mods & kModHalfTime) > 0u) mult *= 0.5;
    if ((mods & kModSpunOut) > 0u) mult *= 0.0;
    if ((mods & kModRelax) > 0u) mult *= 0.0;
    if ((mods & kModRelax2) > 0u) mult *= 0.0;
    if ((mods & kModRandom) > 0u) {
        // IL: `ldloc.0; stloc.0` —— 语义占位，空操作
    }
    // 键数修正 (beatmap != null): a = Obf020609, b = Obf020610
    const int a = maniaBeatmapKeyCount(keyCount);
    int b = maniaBeatmapKeyCount(keyCount);
    if (keyCoop) b = b * 2;
    if (b > a) {
        mult *= 0.9;
    } else if (b < a) {
        // `0.9 - 0.04 * (double)(a - b)`：先 int 减法再 conv.r8
        mult *= 0.9 - 0.04 * static_cast<double>(a - b);
    }
    return std::max(0.0, mult);
}

double maniaTwoArgMultiplier(uint32_t mods, int keyCount, bool keyCoop) {
    // Obf008174(Mods, PlayModes, Beatmap); mania falls through the HR/DT
    // branches untouched, then the NoFail bit is cleared before recursing.
    double mult = 1.0;
    if ((mods & kModNoFail) > 0u) mods &= ~kModNoFail;  // 只清 bit，mult 不变
    return mult * maniaModMultiplier(mods, keyCount, keyCoop);
}

// ---- JudgmentRecord ----
int32_t JudgmentRecord::scoreValue() const {
    // 0x06002187: v = flags & -8 (0xFFFFFFF8)
    const int32_t v = flags & -8;
    switch (v) {
        case 8: return 10;
        case 64: return 30;
        case 128: return 30;
        case 256: return 50;
        case 512: return 100;
        case 1024: return 300;
        case 8192: return 100;
        case 16384: return 500;
        default: return 0;
    }
}

// ---- ManiaScoreV2Processor ----
ManiaScoreV2Processor::ManiaScoreV2Processor(ManiaScoreRecord* host)
    : host_(host) {}

void ManiaScoreV2Processor::reset(bool snapshot) {
    // 0x06001866: `if ((Count > 0) & arg1)` —— 位与，非短路
    if ((!obf009376_.empty()) & snapshot) {
        obf009371_ = obf009376_.back().comboSnapshot;
        int32_t n = 0;
        for (const JudgmentRecord& j : obf009376_)
            if ((j.flags & 520095488) > 0) ++n;  // 0x1F000700
        obf009372_ = n;
        obf009373_ = obf009374_;
    }
    obf009374_ = 0.0;
    obf009375_ = 0.0;
    obf009376_.clear();
}

void ManiaScoreV2Processor::addJudgment(const JudgmentRecord& jud) {
    // 0x06001867
    JudgmentRecord j = jud;
    if (!obf009376_.empty()) j.comboSnapshot = obf009376_.back().comboSnapshot;
    if (j.affectsCombo) {
        // 原始 flags（未掩码）参与正负判断：7（LN tick）是正数 ⇒ combo+1
        if (j.flags > 0) j.comboSnapshot = j.comboSnapshot + 1;
        else j.comboSnapshot = 0;
    }
    obf009376_.push_back(j);
    accumulate(j);
}

void ManiaScoreV2Processor::accumulate(const JudgmentRecord& jud) {
    // 0x06001962 (mania override): combo-scaled award pool
    if (jud.comboSnapshot <= 0) return;
    const double scaled =
        std::log(static_cast<double>(jud.comboSnapshot)) / std::log(4.0);
    const double upper = std::log(400.0) / std::log(4.0);
    obf009374_ = obf009374_ +
                 static_cast<double>(jud.scoreValue()) *
                     std::min(std::max(0.5, scaled), upper);
}

double ManiaScoreV2Processor::comboPart() const {
    // 0x0600195F: 200000.0 * Obf009374 / Obf009373 —— NO divide guard on the
    // snapshot pool: 0/0 must produce NaN (spec 03 §3.3 / Q1).
    return 200000.0 * obf009374_ / obf009373_;
}

double ManiaScoreV2Processor::accPart() const {
    // 0x06001960. The exponent `2f + 2f*acc` is computed in SINGLE precision
    // (two ldc.r4 2.0 + mul + add) and only then widened by conv.r8; the
    // accuracy accessor is called twice (order/IL preserved).
    const float acc = host_->accuracy();                  // IL_0009
    const double accD = static_cast<double>(acc);          // conv.r8
    const float two = 2.0f;                                // ldc.r4 2.0
    const float twoAgain = 2.0f;                           // ldc.r4 2.0
    const float acc2 = host_->accuracy();                   // IL_001F (第二次调用)
    const float exponentF = two + (twoAgain * acc2);        // 2f + 2f*acc (float)
    const double powTerm =
        std::pow(accD, static_cast<double>(exponentF));
    return 800000.0 * powTerm *
           (static_cast<double>(host_->counts.total()) /
            static_cast<double>(obf009372_));
}

int32_t ManiaScoreV2Processor::totalScore() const {
    // 0x06001961
    if (obf009372_ == 0) return 0;  // 唯一的除零守卫（Obf009372）
    double a = comboPart();
    double b = accPart();
    double s = a + b;
    // Obf000587.Obf007573(this.Obf009370, true) —— 2 参重载 => Obf008174
    s = s * maniaTwoArgMultiplier(host_->activeMods, host_->keyCount,
                                  (host_->activeMods & kModKeyCoop) > 0u);
    return dotnetConvI4(dotnetRound(s));
}

// ---- ManiaScoreRecord ----
ManiaScoreRecord::ManiaScoreRecord(uint32_t mods, int keyCountIn, bool attachV2)
    : activeMods(mods), mode(3), keyCount(keyCountIn) {
    if (attachV2) alternateScoreProcessor =
        std::make_unique<ManiaScoreV2Processor>(this);
}

float ManiaScoreRecord::accuracy() const {
    // 0x060019E2: V1 only when no processor AND the ScoreV2 mod bit is clear.
    const bool v2 = hasV2() || (activeMods & kModScoreV2) > 0u;
    return counts.accuracy(v2);
}

int ManiaScoreRecord::grade() const {
    // 0x060019E5
    return maniaGrade(accuracy(), activeMods, passed);
}

int32_t ManiaScoreRecord::score() const {
    // 0x060040A3
    return alternateScoreProcessor ? alternateScoreProcessor->totalScore()
                                   : intScore;
}

void ManiaScoreRecord::resetScore(bool snapshot) {
    // 0x060040BA
    if (alternateScoreProcessor) {
        alternateScoreProcessor->reset(snapshot);
    } else {
        setIntScore(0);
        doubleAcc = 0.0;
    }
    counts.currentCombo = 0;
    counts.maxCombo = 0;
    counts.countMiss = 0;
    counts.count50 = 0;
    counts.count100 = 0;
    counts.count300 = 0;
    counts.countMax = 0;
    counts.countMiss = 0;  // 源码怪癖：CountMiss 第二次清零（保留）
    counts.count200 = 0;
}

// ---- ManiaScoreSubmission ----
ManiaScoreSubmission::ManiaScoreSubmission(ManiaScoreRecord* record,
                                           int scoringObjectCount,
                                           int keyCountIn, bool maniaMode)
    : record_(record),
      scoringObjectCount_(scoringObjectCount),
      keyCount_(keyCountIn),
      maniaMode_(maniaMode) {}

void ManiaScoreSubmission::setupMultiplier() {
    // 0x060025E9 ManiaGameplay::Obf007601:
    //   Obf007573 = (double)500000 / (double)ScoringObjectCount *
    //               Obf000587.Obf007573(ActiveMods & ~0x100458, mode, beatmap)
    const double base = 500000.0 / static_cast<double>(scoringObjectCount_);
    const double mult = maniaModMultiplier(
        record_->activeMods & ~0x100458u, keyCount_,
        (record_->activeMods & kModKeyCoop) > 0u);
    obf007573_ = base * mult;
}

void ManiaScoreSubmission::onJudgment(int32_t flags, JudgeTargetState& target) {
    // GameplayRulesetBase::Obf007628 (0x0600119E) — scoring-relevant subset.
    if (flags == 0) return;  // ① 无判定 ⇒ 不产生任何计数/分数/combo 变化

    ManiaScoreRecord& rec = *record_;
    ScoreCounts& c = rec.counts;
    const bool obf = target.countable;  // ② hitObject.Obf020153 —— counts 总开关

    // ④ flag normalisation
    int32_t obf4 = flags;
    if (obf4 > 0) obf4 &= -12648449;                    // 0xFF3EFFFF
    if (rec.hasV2() && obf4 > 0) obf4 &= -14745601;     // 0xFF1EFFFF

    // ⑤ switch: num (判定值), flag2 (影响 combo), flag3 (参与出分) + counts.
    //    mania 物件从不是 slider ⇒ flag2 = true；mania 的每种判定都参与出分
    //    ⇒ flag3 = true（7 / -1048576 的 num 为 0，对分数无影响）。
    int32_t num = 0;
    bool flag2 = true;
    bool flag3 = true;
    switch (obf4) {
        case judgment_flag::kMax: num = 320; if (obf) c.addMax(); break;
        case judgment_flag::k300: num = 300; if (obf) c.add300(); break;
        case judgment_flag::k200: num = 200; if (obf) c.add200(); break;
        case judgment_flag::k100: num = 100; if (obf) c.add100(); break;
        case judgment_flag::k50: num = 50; if (obf) c.add50(); break;
        case judgment_flag::kMiss: num = 0; if (obf) c.addMiss(); break;
        case judgment_flag::kTick: num = 0; break;   // LN hold tick：不加计数
        case judgment_flag::kBreak: num = 0; break;  // LN 提前松手：不加计数
        default: num = 0; break;
    }

    // ⑥ mania 专用块：血量 + 本次判定的分数权重
    double num2 = 0.0;
    if (maniaMode_) {
        double hp = obf006730_;
        int w = 0;
        // 局部倍率用 ActiveMods & ScoreIncreaseMods(0x100458) 的 3 参重载
        const double localMult = maniaModMultiplier(
            rec.activeMods & 0x100458u, keyCount_,
            (rec.activeMods & kModKeyCoop) > 0u);
        switch (obf4) {
            case judgment_flag::kMiss: hp = -56.0 / localMult; w = 0; break;
            case judgment_flag::k50: hp -= 44.0 / localMult; w = 4; break;
            case judgment_flag::k100: hp -= 24.0 / localMult; w = 8; break;
            case judgment_flag::k200: hp -= 8.0 / localMult; w = 16; break;
            case judgment_flag::k300: hp += 1.0; w = 32; break;
            case judgment_flag::kMax: hp += 2.0; w = 32; break;
            default: break;  // 7 / -1048576：hp、w 均不变
        }
        // `% 384` 读的是**更新前**的 CurrentCombo
        if (c.currentCombo != 0 && (c.currentCombo % 384) == 0) hp = 100.0;
        hp = std::max(0.0, std::min(hp, 100.0));  // Min 在前、Max 在后
        obf006730_ = hp;
        num2 = std::sqrt(hp) * static_cast<double>(w) * obf007573_ / 320.0;
    }

    // ⑦ 第一块后置修正（mania 的 flag 与 0x00C10000 无缘，照抄保留）
    int32_t num6 = 0;
    if (flags > 0 && (flags & 12648448) > 0) {
        if (num > 0) {
            if (!flag2) num = num + num / 5;
        } else {
            num = target.scoredValue / 50 * 50;
        }
        if ((flags & 8388608) > 0) num6 = -1;
    }
    (void)num6;

    // ⑧ hitObject.Obf020154 = num —— 每次判定都会覆盖（即使 obf == false）
    target.scoredValue = num;

    // ⑨ V1 分数增量（mania）：先 `(int)doubleAcc` 截断，再加本次增量
    int32_t numInt = num;
    double num2Settled = num2;
    if (flag3 && maniaMode_) {
        if (!rec.hasV2()) rec.setIntScore(dotnetConvI4(rec.doubleAcc));
        num2Settled = num2Settled +
                      static_cast<double>(numInt) * obf007573_ / 320.0;
        numInt = dotnetConvI4(num2Settled);
    }

    // ⑩ 第二块后置修正（num*2 / CountMax++ —— mania 不可达，照抄保留）
    if (flags > 0) {
        if ((flags & 12582912) > 0) {  // 0x00C00000
            if (numInt > 0) {
                if (!flag2) numInt = numInt + numInt / 5;
            } else {
                numInt = target.scoredValue / 50 * 50;
            }
            if ((flags & 8388608) > 0) flag2 = false;
        }
        if ((flags & 2162688) > 0) {  // 0x00210000
            if (flag2) {
                if (target.scoredValue == 300) c.addMax();
                else if (target.scoredValue == 150) c.add200();
            }
            numInt = numInt * 2;
        }
    }

    // ⑪ combo 推进 / 断开
    if (flag2) {
        if (flags > 0) {
            const int32_t nxt = comboCounter_ + 1;
            comboCounter_ = nxt;  // Obf011172
            c.maxCombo = std::max(c.maxCombo, nxt);
            c.currentCombo = static_cast<uint16_t>(comboCounter_);  // conv.u2
        } else {
            comboCounter_ = 0;
            c.currentCombo = 0;
        }
    }

    // ⑬ 分数提交
    if (obf || (flags & 8388608) > 0) {
        if (rec.hasV2()) {
            JudgmentRecord jud;
            jud.flags = flags;
            jud.affectsCombo = flag2;
            jud.affectsScore = flag3;
            rec.alternateScoreProcessor->addJudgment(jud);
        } else {
            rec.setIntScore(rec.score() + numInt);
            rec.doubleAcc = rec.doubleAcc + num2Settled;
        }
    }
}

void ManiaScoreSubmission::resetComboAndHealth() {
    comboCounter_ = 0;
    obf006730_ = 100.0;
}

void ManiaScoreSubmission::finish() {
    ManiaScoreRecord& rec = *record_;
    // ManiaGameplay::Obf007620: 用无损失累计器覆盖整型分（中点取偶）
    rec.setIntScore(dotnetConvI4(dotnetRound(rec.doubleAcc)));
    if (rec.score() == 999999) rec.setIntScore(1000000);
    const ScoreCounts& c = rec.counts;
    // IsPerfect = (CMiss + C50 + C100 == 0) && (float)C200/(float)total < 0.1
    rec.isPerfect =
        (static_cast<int32_t>(c.countMiss) + static_cast<int32_t>(c.count50) +
             static_cast<int32_t>(c.count100) ==
         0) &&
        (static_cast<double>(static_cast<float>(c.count200) /
                             static_cast<float>(c.total())) < 0.1);
    rec.passed = true;  // PlaySession.Obf006493 (failed) 不建模
}

}  // namespace mania_src
}  // namespace mra
