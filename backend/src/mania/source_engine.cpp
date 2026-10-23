#include "mania/source_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mra {
namespace mania_src {
namespace {

// ManiaScoringConstants (0x02000192, .cctor 0x06000B41)
[[maybe_unused]] constexpr int kBaseScorePool = 500000;    // 0x040007C3
[[maybe_unused]] constexpr int kNominalMax = 320;          // 0x040007C4
constexpr int kLongNoteTickMs = 100;                       // 0x040007C5
[[maybe_unused]] constexpr int kSecondHundred = 100;       // 0x040007C6
[[maybe_unused]] constexpr int kComboBonusPeriod = 384;    // 0x040007C7

// Math.Abs(int) 的语义（避免 INT_MIN 的 C++ UB，正常范围逐位一致）
int absIntDiff(int a, int b) {
    const long long d = static_cast<long long>(a) - static_cast<long long>(b);
    return static_cast<int>(d < 0 ? -d : d);
}

// Int32.CompareTo —— 三路比较（spec 02 §7.10.1）
int compareThreeWay(int32_t a, int32_t b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

// Bit-exact replication of .NET System.Random (the generator
// ManiaHitObjectManager::Virtual0063 (0x060049C1) uses for RandomColumnMap:
// `do { k = rand.Next(Columns.Count); } while (used.ContainsKey(k))`, where
// Random.Next(int) == (int)(Sample() * maxValue) with Sample() =
// InternalSample() * (1.0 / MBIG)).
class DotNetRandom {
public:
    explicit DotNetRandom(int seed) {
        const int subtraction =
            seed == (-2147483647 - 1) ? 2147483647 : (seed < 0 ? -seed : seed);
        int mj = 161803398 - subtraction;
        seedArray_[55] = mj;
        int mk = 1;
        for (int i = 1; i < 55; ++i) {
            const int ii = (21 * i) % 55;
            seedArray_[ii] = mk;
            mk = mj - mk;
            if (mk < 0) mk += 2147483647;
            mj = seedArray_[ii];
        }
        for (int k = 1; k < 5; ++k) {
            for (int i = 1; i < 56; ++i) {
                seedArray_[i] -= seedArray_[1 + (i + 30) % 55];
                if (seedArray_[i] < 0) seedArray_[i] += 2147483647;
            }
        }
        inext_ = 0;
        inextp_ = 21;
    }

    // Random.Sample() —— InternalSample() * (1.0 / MBIG)
    double sample() { return internalSample() * (1.0 / 2147483647.0); }

    // Random.Next(int maxValue) == (int)(Sample() * (double)maxValue)
    int next(int maxValue) {
        return static_cast<int>(sample() * static_cast<double>(maxValue));
    }

private:
    int internalSample() {
        if (++inext_ >= 56) inext_ = 1;
        if (++inextp_ >= 56) inextp_ = 1;
        int r = seedArray_[inext_] - seedArray_[inextp_];
        if (r == 2147483647) --r;
        if (r < 0) r += 2147483647;
        seedArray_[inext_] = r;
        return r;
    }

    int seedArray_[56] = {};
    int inext_ = 0;
    int inextp_ = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// ManiaColumn
// ---------------------------------------------------------------------------
int ManiaColumn::replayBit() const {
    // 0x06000B72: ldc.r8 2.0; Index -> conv.r8; Math::Pow; conv.i4
    return dotnetConvI4(std::pow(2.0, static_cast<double>(index)));
}

void ManiaColumn::setHeld(bool value) {
    wasHeld = isHeld;  // IL_0000: 无条件、先搬
    if (isHeld == value) return;  // IL_000C: 值相等直接返回
    isHeld = value;
    // 之后是按键贴图 / 灯光 / 判定线淡入淡出（纯图形，跳过）
}

void ManiaColumn::setHoldLightActive(bool value) {
    if (holdLightActive == value) return;  // early-out
    holdLightActive = value;
    // 之后是容器 show/play + 80 / 120 动画参数（纯图形，跳过）
}

// ---------------------------------------------------------------------------
// ManiaStage
// ---------------------------------------------------------------------------
bool ManiaStage::evenKeysOverFour() const {
    // 0x0600461B
    const int n = columnCount();
    if (n <= 4) return false;
    return (n & 1) == 0;
}

bool ManiaStage::specialKeyOnRight() const {
    // 0x0600461C
    if (!evenKeysOverFour()) return false;
    if (!hasSkin) return false;
    return specialStyleRight;
}

// ---------------------------------------------------------------------------
// ManiaNote
// ---------------------------------------------------------------------------
int32_t ManiaNote::judge(ManiaHitObjectManager& manager) {
    // 0x06000868
    if (isJudged) return judgmentResult;
    isJudged = true;
    judgmentResult = judgment_flag::kMiss;  // -65536
    if (isHeld) {
        const int err = absIntDiff(pressTime, startTime);
        if (err <= manager.windowMax) judgmentResult = judgment_flag::kMax;
        else if (err <= manager.window300) judgmentResult = judgment_flag::k300;
        else if (err <= manager.window200) judgmentResult = judgment_flag::k200;
        else if (err <= manager.window100) judgmentResult = judgment_flag::k100;
        else if (err <= manager.window50) judgmentResult = judgment_flag::k50;
        virtual0025();
    }
    if (judgmentResult > judgment_flag::k50) virtual0110();
    else virtual0112(true);
    return judgmentResult;
}

void ManiaNote::virtual0110() {
    // 0x0600087A —— 命中后隐藏
    isHiddenAfterHit = true;
    // Sprites.ForEach(...) 视觉，跳过
}

void ManiaNote::virtual0112(bool dimValue) {
    // 0x0600087B —— 变暗（early-out）
    if (dimValue == isDimmed) return;
    isDimmed = dimValue;
    // Sprites.ForEach(...) 视觉，跳过
}

// ---------------------------------------------------------------------------
// ManiaLongNote
// ---------------------------------------------------------------------------
int32_t ManiaLongNote::processLongNoteTick(int32_t now) {
    // 0x06003A09
    int32_t r = 0;
    if (lastTickTime == -1) return 0;
    if (now > endTime) return 0;
    if (now < startTime) return 0;
    if (now - lastTickTime < kLongNoteTickMs) return 0;
    // LastTickTime 每次只前进一格（不是重设为 now）⇒ 会追赶
    lastTickTime = lastTickTime + kLongNoteTickMs;
    r = 7;  // mania LN hold tick
    return r;
}

int32_t ManiaLongNote::judgeLongNoteHead(ManiaHitObjectManager& manager) {
    // 0x06003A0A
    if (isHeld) {
        // BodySprite.<0x040009E0> = true （视觉）
        lastTickTime = manager.now;  // 按下时把 tick 起点设为 now
        // Virtual0090() —— 播 head 按键音（音频，跳过）
        if (!wasBroken) {
            // ((ManiaHitObjectManager*)Manager).Obf021387(this) —— LN 体精灵（视觉）
        }
    }
    const bool scoreV2 = manager.hasAlternateScoreProcessor();
    if (scoreV2 && !wasBroken && !headJudged) {
        // IL_006D: JudgmentResult 默认 = MISS(-65536)。
        // IL_007E: !IsHeld ⇒ brfalse.s IL_011D 直接跳 HeadJudged=true，
        // 【跳过】IL_0116 的清零 —— -65536（head miss）保留。
        // （规格 §4.4 伪代码曾把内外层 else 混淆成 "!IsHeld ⇒ 0"，2026-10-23
        // 经 stable 声明账本 + ManiaLongNote.il 原文裁决纠正。）
        judgmentResult = judgment_flag::kMiss;
        if (isHeld) {
            const int err = absIntDiff(pressTime, startTime);
            if (err <= manager.windowMax) judgmentResult = judgment_flag::kMax;
            else if (err <= manager.window300) judgmentResult = judgment_flag::k300;
            else if (err <= manager.window200) judgmentResult = judgment_flag::k200;
            else if (err <= manager.window100) judgmentResult = judgment_flag::k100;
            else if (err <= manager.window50) judgmentResult = judgment_flag::k50;
            virtual0025();  // IL_010E-0114：仅 IsHeld 路径调用
        }
    } else {
        // IL_0116：非 ScoreV2 / 已破 / 已判头 ⇒ JudgmentResult = 0
        judgmentResult = 0;
    }
    headJudged = true;  // IL_011D-011F：无条件
    return judgmentResult;
}

void ManiaLongNote::dimCheck(ManiaHitObjectManager& manager) {
    // 0x06003A0B Obf017937 —— 视觉变暗判定（字段写入参与后续判定读取）
    bool dim;
    if (judgmentResult == judgment_flag::kMiss) {
        dim = true;
    } else if (isHeld) {
        dim = false;
    } else if (manager.now <= startTime + manager.window50) {
        dim = false;
    } else {
        dim = !isJudged;
    }
    virtual0112(dim);
}

int32_t ManiaLongNote::judge(ManiaHitObjectManager& manager) {
    // 0x06003A0D
    // BodySprite.<0x040009E0> = false （视觉）
    lastTickTime = -1;
    dimCheck(manager);
    if (isJudged) return judgmentResult;

    // 守卫 1：还没到 StartTime + W50 且从未被按下 ⇒ 不判（不写 IsJudged）
    if (manager.now < startTime + manager.window50 && pressTime == 0) return 0;

    // 守卫 2：提前松手 ⇒ LN break（不写 IsJudged，可重复触发）
    if (manager.now < endTime - manager.window50) {
        if (isHeld) return 0;
        // Virtual0131()（音频）+ Obf021388(this)（LN 体精灵重建，视觉）
        wasBroken = true;
        return judgment_flag::kBreak;  // -1048576
    }

    // 守卫 3：还按着且没到 EndTime + W50 ⇒ 继续等
    if (manager.now < endTime + manager.window50 && isHeld) return 0;

    // Virtual0131()（音频）
    isJudged = true;
    judgmentResult = judgment_flag::kMiss;

    if (manager.hasAlternateScoreProcessor()) {
        // ---- ScoreV2 尾判 ----
        const int relErr = absIntDiff(releaseTime, endTime);
        const float err = static_cast<float>(relErr) / 1.5f;  // conv.r4 后 float div
        if (err <= static_cast<float>(manager.windowMax))
            judgmentResult = judgment_flag::kMax;
        else if (err <= static_cast<float>(manager.window300))
            judgmentResult = judgment_flag::k300;
        else if (err <= static_cast<float>(manager.window200))
            judgmentResult = judgment_flag::k200;
        else if (err <= static_cast<float>(manager.window100))
            judgmentResult = judgment_flag::k100;
        else if (err <= static_cast<float>(manager.window50))
            judgmentResult = judgment_flag::k50;
        // 封顶 50：破 LN 或头未判过
        if (judgmentResult > judgment_flag::k50 && (wasBroken || !headJudged))
            judgmentResult = judgment_flag::k50;
    } else {
        // ---- 传统（V1）尾判 ----
        int effStart;
        if (pressTime < startTime - manager.window50) {
            effStart = endTime - 1;
        } else if (pressTime < startTime) {
            effStart = startTime + (startTime - pressTime);
        } else {
            effStart = pressTime;
        }
        int effEnd;
        if (isHeld) {
            effEnd = manager.now;
        } else if (releaseTime > endTime) {
            effEnd = endTime - (releaseTime - endTime);
        } else {
            effEnd = releaseTime;
        }

        // 被丢弃的求值（FIDELITY：必须保留）
        (void)absIntDiff(effEnd, effStart);
        const int headErr = absIntDiff(effStart, startTime);
        const int tailErr = absIntDiff(effEnd, endTime);
        const int combined = headErr + tailErr;
        // 被丢弃的 float 除法（必须保留）
        (void)(static_cast<float>(effEnd - effStart) / duration);

        if (effEnd < endTime - manager.window50) {
            judgmentResult = judgment_flag::kMiss;
        } else if (static_cast<double>(headErr) <=
                       static_cast<double>(manager.windowMax) * 1.2 &&
                   static_cast<double>(combined) <=
                       static_cast<double>(manager.windowMax) * 2.4) {
            judgmentResult = judgment_flag::kMax;
        } else if (static_cast<double>(headErr) <=
                       static_cast<double>(manager.window300) * 1.1 &&
                   static_cast<double>(combined) <=
                       static_cast<double>(manager.window300) * 2.2) {
            judgmentResult = judgment_flag::k300;
        } else if (headErr <= manager.window200 &&
                   combined <= manager.window200 * 2) {
            judgmentResult = judgment_flag::k200;
        } else if (headErr <= manager.window100 &&
                   combined <= manager.window100 * 2) {
            judgmentResult = judgment_flag::k100;
        } else {
            judgmentResult = judgment_flag::k50;
        }
        // 破 LN 封顶 200
        if (judgmentResult > judgment_flag::k200 && wasBroken)
            judgmentResult = judgment_flag::k200;
    }

    if (judgmentResult > 0) virtual0025();
    if (judgmentResult < 0 || wasBroken ||
        manager.now < endTime - manager.window50) {
        virtual0112(true);
    } else {
        virtual0110();
    }
    return judgmentResult;
}

// ---------------------------------------------------------------------------
// 判定窗
// ---------------------------------------------------------------------------
int adjustManiaHitWindowFor(int32_t activeMods, double window) {
    // 0x060049C7 —— 顺序不可改，conv.i4 只在末尾一次
    if ((activeMods & 16) > 0) {
        window = window / 1.4;  // HR
    } else if ((activeMods & 2) > 0) {
        window = window * 1.4;  // EZ
    }
    if ((activeMods & 64) > 0) {
        window = window * 1.5;  // DT
    } else if ((activeMods & 256) > 0) {
        window = window * 0.75;  // HT
    }
    return dotnetConvI4(window);  // 向零截断
}

double difficultyRangeFor(double difficulty, double min, double mid, double max) {
    // 0x060049C8 —— ble.un / bge.un（NaN 走第一支）
    if (!(difficulty <= 5.0)) {
        return mid + (max - mid) * (difficulty - 5.0) / 5.0;
    }
    if (difficulty >= 5.0) return mid;
    return mid - (mid - min) * (5.0 - difficulty) / 5.0;
}

ManiaWindowSet computeManiaWindowSet(double od, int32_t activeMods, bool scoreV2,
                                     bool nativeManiaBeatmap) {
    ManiaWindowSet set;
    // 0x060049C9. OD 在源里是 float：先 conv.r4 到 float，再按需 conv.r8。
    const float odF = static_cast<float>(od);
    const double odD = static_cast<double>(odF);
    const auto adjust = [activeMods](double w) {
        return adjustManiaHitWindowFor(activeMods, w);
    };

    if (scoreV2) {
        // ---- 路径 A：ScoreV2 ----
        set.windowMax = adjust(difficultyRangeFor(odD, 22.4, 19.4, 13.9));
        set.window300 = adjust(difficultyRangeFor(odD, 64.0, 49.0, 34.0));
        set.window200 = adjust(difficultyRangeFor(odD, 97.0, 82.0, 67.0));
        set.window100 = adjust(difficultyRangeFor(odD, 127.0, 112.0, 97.0));
        set.window50 = adjust(difficultyRangeFor(odD, 151.0, 136.0, 121.0));
        // EarlyInputWindow 是裸 conv.i4（**不经** mod 调整）
        set.earlyInputWindow =
            dotnetConvI4(difficultyRangeFor(odD, 188.0, 173.0, 158.0));
        return set;
    }

    if (nativeManiaBeatmap) {
        // ---- 路径 B：原生 mania 谱（Beatmap.Obf012403() == 3）----
        // d = Math.Min(10f, Math.Max(0f, 10f - OD))，全程 float
        const float d = std::min(10.0f, std::max(0.0f, 10.0f - odF));
        set.windowMax = adjust(16.0);
        set.window300 = adjust(static_cast<double>(34.0f + 3.0f * d));
        set.window200 = adjust(static_cast<double>(67.0f + 3.0f * d));
        set.window100 = adjust(static_cast<double>(97.0f + 3.0f * d));
        set.window50 = adjust(static_cast<double>(121.0f + 3.0f * d));
        set.earlyInputWindow = adjust(static_cast<double>(158.0f + 3.0f * d));
        return set;
    }

    // ---- 路径 C / D：转换谱（Math.Round 单参 = ToEven）----
    const bool highOd = dotnetRound(odD) > 4.0;
    set.windowMax = adjust(16.0);
    set.window300 = adjust(highOd ? 34.0 : 47.0);
    set.window200 = adjust(highOd ? 67.0 : 77.0);
    set.window100 = adjust(97.0);
    set.window50 = adjust(121.0);
    set.earlyInputWindow = adjust(158.0);
    return set;
}

// ---------------------------------------------------------------------------
// ManiaHitObjectManager
// ---------------------------------------------------------------------------
ManiaHitObjectManager::ManiaHitObjectManager(ManiaStage& stageIn,
                                            const ManiaScoreRecord* currentScoreIn,
                                            int32_t activeModsIn,
                                            uint32_t randomSeed)
    : stage(stageIn), currentScore(currentScoreIn), activeMods(activeModsIn) {
    // 0x060049BD: 先写窗口默认值，再 base..ctor，再 RapidPressWeight/ShortPressCount
    earlyInputWindow = 178;
    windowMax = 20;
    window200 = 60;
    firstStartTime = INT32_MAX;
    lastEndTime = INT32_MIN;
    longestDuration = INT32_MIN;
    rapidPressWeight = 0;
    shortPressCount = 0;
    (void)randomSeed;  // RandomColumnMap 由 initializeJudgementArrays 建立
}

int ManiaHitObjectManager::adjustManiaHitWindow(double window) const {
    return adjustManiaHitWindowFor(activeMods, window);
}

double ManiaHitObjectManager::difficultyRange(double difficulty, double min,
                                              double mid, double max) {
    return difficultyRangeFor(difficulty, min, mid, max);
}

ManiaWindowSet ManiaHitObjectManager::windowSet() const {
    ManiaWindowSet set;
    set.windowMax = windowMax;
    set.window300 = window300;
    set.window200 = window200;
    set.window100 = window100;
    set.window50 = window50;
    set.earlyInputWindow = earlyInputWindow;
    return set;
}

void ManiaHitObjectManager::applyDifficultySettings(double od,
                                                    bool nativeManiaBeatmap) {
    // 0x060049C9. `Beatmap == null` 时源直接 return（调用方保证有 beatmap）。
    const ManiaWindowSet set =
        computeManiaWindowSet(od, activeMods, hasAlternateScoreProcessor(),
                              nativeManiaBeatmap);
    windowMax = set.windowMax;
    window300 = set.window300;
    window200 = set.window200;
    window100 = set.window100;
    window50 = set.window50;
    earlyInputWindow = set.earlyInputWindow;
}

int ManiaHitObjectManager::remapManiaColumn(int column) const {
    // 0x060049CA
    int c = column;
    if ((activeMods & 2097152) > 0) {  // 0x200000 Random
        c = randomColumnMap[static_cast<size_t>(column)];
    } else if (stage.specialKeyOnRight()) {
        if (column == 0) c = stage.columnCount() - 1;
        else c = column - 1;
    }
    if ((activeMods & 1073741824) > 0) {  // 0x40000000 Mirror
        c = (stage.columnCount() - 1) - c;
    }
    return c;
}

void ManiaHitObjectManager::obf021376(ManiaNote* note) {
    // 0x060049CB
    const int kind = note->noteKind;
    if (kind == 0) ++riceCount;
    else if (kind == 1) ++longNoteCount;
}

void ManiaHitObjectManager::addObject(ManiaNote* note) {
    // 0x060049CD Obf021377 —— 字段副作用（权威）
    if (note->startTime < firstStartTime) firstStartTime = note->startTime;
    if (note->endTime > lastEndTime) {
        lastEndTime = note->endTime;
        lastEndingObject = note;
    }
    if ((note->endTime - note->startTime) > longestDuration)
        longestDuration = note->endTime - note->startTime;
    obf021376(note);

    // base.Virtual0162(o, false, false)：按 StartTime 有序插入；相同 StartTime
    // 的元素全部推过去后再 Insert（spec 02 §7.10.1(e)）。
    int i = binarySearchByStartTime(note);
    if (i < 0) i = ~i;
    while (i < static_cast<int>(hitObjects.size()) &&
           hitObjects[static_cast<size_t>(i)]->startTime == note->startTime) {
        ++i;
    }
    hitObjects.insert(hitObjects.begin() + i, note);
}

void ManiaHitObjectManager::initializeJudgementArrays(uint32_t randomSeed) {
    // 0x060049C1 Virtual0063 判定相关段
    const int n = stage.columnCount();
    pendingKeysoundNotes.assign(static_cast<size_t>(n), nullptr);
    keyPressTimes.assign(static_cast<size_t>(n), 0);
    randomColumnMap.assign(static_cast<size_t>(n), 0);

    std::vector<uint8_t> used(static_cast<size_t>(n), 0);
    DotNetRandom rand(static_cast<int32_t>(randomSeed));
    for (int i = 0; i < n; ++i) {
        if (stage.evenKeysOverFour() && i == 0) {
            if (stage.specialKeyOnRight()) {
                randomColumnMap[0] = n - 1;
                used[static_cast<size_t>(n - 1)] = 1;
            } else {
                randomColumnMap[0] = 0;
                used[0] = 1;
            }
        } else {
            int k;
            do {
                k = rand.next(stage.columnCount());
            } while (used[static_cast<size_t>(k)] != 0);
            randomColumnMap[static_cast<size_t>(i)] = k;
            used[static_cast<size_t>(k)] = 1;
        }
    }
}

void ManiaHitObjectManager::onLoadComplete(double scrollPreemptMs) {
    // 0x060049D1 Virtual0113
    // ApproachTime = Max((int)ManiaScrollSpeed.<0x0600229F>(0, HitPosition),
    //                    LongestDuration) + 200
    const int scrollPreempt = dotnetConvI4(scrollPreemptMs);
    approachTime = std::max(scrollPreempt, longestDuration) + 200;
    scoringObjectCount = riceCount + longNoteCount;
    // this.Beatmap.Obf012403() —— 丢弃返回值（保留调用）
    if (currentScore != nullptr && (currentScore->activeMods & 16384) > 0)
        perfectRequires300 = true;  // Perfect (0x4000)
    // this.Obf021378(-1) —— 每帧体首调（条码/按键音/精灵，跳过）
}

bool ManiaHitObjectManager::hasAlternateScoreProcessor() const {
    // PlaySession.CurrentScore?.AlternateScoreProcessor != null
    return currentScore != nullptr && currentScore->hasV2();
}

bool ManiaHitObjectManager::obf021373() const {
    // 0x060049BE: 先 conv.r4 两遍再 div，结果 conv.r8 后与 double 0.6 比较
    if (shortPressCount == 0) return false;
    const float ratio = static_cast<float>(rapidPressWeight) /
                        static_cast<float>(shortPressCount);
    return static_cast<double>(ratio) > 0.6;
}

int ManiaHitObjectManager::binarySearchByStartTime(const ManiaNote* key) const {
    // List<HitObjectBase>.BinarySearch(real object) → HitObjectBase.CompareTo
    // (0x060043F6): StartTime 主键，等值时用 ObjectType 位测试的 bool 次序键。
    int lo = 0;
    int hi = static_cast<int>(hitObjects.size()) - 1;
    while (lo <= hi) {
        const int mid = lo + ((hi - lo) >> 1);
        const ManiaNote* cand = hitObjects[static_cast<size_t>(mid)];
        int c = compareThreeWay(cand->startTime, key->startTime);
        if (c == 0) {
            // Boolean.CompareTo: other(key).Virtual0150().CompareTo(this...)
            const bool candBit = (cand->objectType & 4) != 0;
            const bool keyBit = (key->objectType & 4) != 0;
            if (keyBit != candBit) c = keyBit ? 1 : -1;
        }
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return ~lo;
}

int ManiaHitObjectManager::rangeStartForEndTimeKey(int32_t key) const {
    // spec 02 §7.10.1 —— 键包装类 StartTime = EndTime = key，比较体是
    // HitObjectBase::CompareTo 的 EndTime 三路比较；Array.BinarySearch 未命中
    // 返回 ~lo（lo = 第一个 EndTime > key 的下标），Virtual0177 直接把 ~i 当
    // 切片起点（**没有** ~i - 1）⇒ 上界语义。
    const int count = static_cast<int>(hitObjects.size());
    int lo = 0;
    int hi = count - 1;
    int i = 0;
    bool hit = false;
    while (lo <= hi) {
        const int mid = lo + ((hi - lo) >> 1);
        const int c = compareThreeWay(hitObjects[static_cast<size_t>(mid)]->endTime, key);
        if (c == 0) {
            i = mid;  // 命中：同键组内任意下标（Array.BinarySearch 不保证取首个）
            hit = true;
            break;
        }
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    if (!hit) i = ~lo;
    if (i < 0) i = ~i;  // IL_0024–IL_002A
    return i;
}

void ManiaHitObjectManager::virtual0177() {
    // 0x060049D9
    const int objectCount = static_cast<int>(hitObjects.size());
    // key = now - ApproachTime - 200（int32 减法，可能下溢）
    const int32_t key = static_cast<int32_t>(
        static_cast<uint32_t>(now) - static_cast<uint32_t>(approachTime) - 200u);
    int i = rangeStartForEndTimeKey(key);
    int j = i;
    // 上界循环：j 停在"第一个 StartTime > now + 300"的元素上 —— 该元素本身仍被
    // GetRange(i, 1 + j - i) 计入切片（闭区间，spec 02 §7.10.1(f)）。
    while (j < objectCount - 1) {
        if (hitObjects[static_cast<size_t>(j)]->startTime > now + 300) break;
        ++j;
    }
    if (i == objectCount) {
        activeObjects.clear();
        return;
    }
    activeObjects.assign(hitObjects.begin() + i, hitObjects.begin() + j + 1);
}

void ManiaHitObjectManager::updateInputJudgments() {
    // 0x060049DA
    const int columnCount = stage.columnCount();
    isLongNoteHeld = false;  // PlaySession.0x04000A04 = false

    std::vector<uint8_t> consumed(static_cast<size_t>(columnCount), 0);
    std::vector<ManiaNote*> selected(static_cast<size_t>(columnCount), nullptr);
    const bool scoreV2 = hasAlternateScoreProcessor();

    // ---- (A) ScoreV2 每列预选（逆序扫描） ----
    if (scoreV2) {
        std::vector<uint8_t> decided(static_cast<size_t>(columnCount), 0);
        for (int i = static_cast<int>(activeObjects.size()) - 1; i >= 0; --i) {
            ManiaNote* note = activeObjects[static_cast<size_t>(i)];
            const int c = note->column->index;
            if (decided[static_cast<size_t>(c)]) continue;
            if (note->isJudged) {
                decided[static_cast<size_t>(c)] = 1;
                continue;
            }
            ManiaNote* cur = selected[static_cast<size_t>(c)];
            if (cur == nullptr || now < note->startTime) {
                selected[static_cast<size_t>(c)] = note;
                continue;
            }
            // limit = Min(obj.Start+W50, cur.Start-1) + Max(0, cur.Start-obj.Start-W50-W50)
            int limit = std::min(note->startTime + window50, cur->startTime - 1);
            limit += std::max(0, cur->startTime - note->startTime - window50 -
                                     window50);
            if (now <= limit) selected[static_cast<size_t>(c)] = note;
            decided[static_cast<size_t>(c)] = 1;
        }
    }

    // ---- (B) 主循环（ActiveObjects 顺序 = 谱面 StartTime 升序） ----
    for (ManiaNote* o : activeObjects) {
        ManiaNote* note = o;
        ManiaLongNote* ln = dynamic_cast<ManiaLongNote*>(note);
        note->virtual0180();  // 0x060043E6：空体
        const int col = note->column->index;

        // (A) 已判定物件
        if (note->isJudged) {
            if (scoreV2) continue;
            if (note->startTime <= now) continue;
            // ★ 未来时间的已判定物件封锁该列
            consumed[static_cast<size_t>(col)] = 1;
            continue;
        }
        // (B) 本趟该列已被消耗
        if (consumed[static_cast<size_t>(col)]) continue;
        // (C) 早于 EarlyInputWindow 下界（先做整数减法再比较）
        if (now < note->startTime - earlyInputWindow) continue;
        // (D) 到期处理（先于边沿判定）
        if (expireManiaNote(o)) continue;

        const bool held = note->column->getIsHeld();
        const bool wasHeld = note->column->wasHeld;

        if (held && !wasHeld) {
            // (E) 新按下边沿
            if (scoreV2 && selected[static_cast<size_t>(col)] != note) continue;
            if (note->noteKind == 0 || note->noteKind == 2) {
                // ---- rice ----
                note->isHeld = true;
                note->pressTime = now;
                note->pressRecorded = true;
                consumed[static_cast<size_t>(col)] = 1;
                pendingKeysoundNotes[static_cast<size_t>(col)] = nullptr;
                // PlaySession.Obf006742(note, null) —— 命中音/统计勾子（音频）
                judgeObject(note);
                continue;
            }
            if (note->noteKind != 1) continue;
            if (note->isHeld) continue;
            // ---- LN 头判（顺序与 rice 不同，必须分别照抄）----
            isLongNoteHeld = true;
            note->pressTime = now;
            note->pressRecorded = true;
            consumed[static_cast<size_t>(col)] = 1;
            pendingKeysoundNotes[static_cast<size_t>(col)] = nullptr;
            note->isHeld = true;
            ln->pressCount = ln->pressCount + 1;
            if (ln->pressCount <= 1) {
                // PlaySession.Obf006742(note, null)
            }
            judgeLongNoteHead(ln);
            note->column->setHoldLightActive(true);
            continue;
        }

        if (held && wasHeld) {
            // (F) 持续按住
            if (note->noteKind != 1) continue;
            if (!note->isHeld) continue;
            consumed[static_cast<size_t>(col)] = 1;
            pendingKeysoundNotes[static_cast<size_t>(col)] = nullptr;
            processLongNoteTick(ln);  // 返回值被 pop，不是判定
            isLongNoteHeld = true;
            note->column->setHoldLightActive(true);
            continue;
        }

        // (G) 已松开
        if (held) continue;
        if (note->noteKind != 1) {
            note->column->setHoldLightActive(false);
            continue;
        }
        if (!note->isHeld) {
            if (!note->isDimmed) judgeObject(note);
            note->column->setHoldLightActive(false);
            continue;
        }
        // LN 松手
        note->isHeld = false;
        note->releaseTime = now;
        note->releaseRecorded = true;
        ln->releaseCount = ln->releaseCount + 1;
        if (ln->releaseCount <= 1) {
            // PlaySession.Obf006742(note, new int?(now - note.EndTime))（音频）
            (void)absIntDiff(now, note->endTime);
        }
        judgeObject(note);
        note->column->setHoldLightActive(false);
        continue;
    }

    // ---- (F) 第二趟：rapid-press 统计 + 按键音兜底 ----
    if (!clockPlaying) return;  // PlaySession.0x04000A51 != 2
    for (int idx = 0; idx < columnCount; ++idx) {
        ManiaColumn& col = stage.columns[static_cast<size_t>(idx)];
        if (col.getIsHeld() && !col.wasHeld) {
            keyPressTimes[static_cast<size_t>(idx)] = now;
        } else if (!col.getIsHeld() && col.wasHeld &&
                   keyPressTimes[static_cast<size_t>(idx)] != 0) {
            const int delta = now - keyPressTimes[static_cast<size_t>(idx)];
            if (delta < 20) rapidPressWeight += 2;
            else if (delta < 38) rapidPressWeight += 1;
            if (delta < 100) shortPressCount += 1;
        }
        if (!consumed[static_cast<size_t>(idx)] && col.getIsHeld() && !col.wasHeld) {
            if (unmatchedPressCallback) unmatchedPressCallback(now, idx);
            if (pendingKeysoundNotes[static_cast<size_t>(idx)] == nullptr) {
                // HitObjects.Find(o => o.StartTime > now && (int)((ManiaNote)o).Column == idx)
                for (ManiaNote* candidate : hitObjects) {
                    if (candidate->startTime > now &&
                        candidate->column->index == idx) {
                        pendingKeysoundNotes[static_cast<size_t>(idx)] = candidate;
                        break;
                    }
                }
            }
            if (pendingKeysoundNotes[static_cast<size_t>(idx)] != nullptr) {
                pendingKeysoundNotes[static_cast<size_t>(idx)]->virtual0025();
            }
        }
    }
}

bool ManiaHitObjectManager::expireManiaNote(ManiaNote* note) {
    // 0x060049DB
    ManiaLongNote* ln = dynamic_cast<ManiaLongNote*>(note);
    float w100 = static_cast<float>(window100);  // conv.r4

    if (hasAlternateScoreProcessor() && ln != nullptr) {
        w100 = w100 * 1.5f;  // ldc.r4 0x3FC00000 —— float 乘
        if (!(now < ln->startTime + window100)) {  // 整数加法 + 整数比较
            if (!ln->headJudged) judgeLongNoteHead(ln);  // 返回值 pop
        }
    }
    // `(float)now >= (float)EndTime + w100` —— 两次 conv.r4 后 float 比较
    if (!(static_cast<float>(now) <
          static_cast<float>(note->endTime) + w100)) {
        if (!note->isJudged) {
            judgeObject(note);
            return true;
        }
    }
    return false;
}

int32_t ManiaHitObjectManager::judgeObject(ManiaNote* note) {
    // 0x060049E0
    if (note->isJudged) return 0;
    currentJudgmentObject = note;
    int32_t r = note->judge(*this);  // 虚调用
    const int idx = binarySearchByStartTime(note);
    lastJudgedIndex = (idx >= 0) ? idx : ~idx;
    note->countedResult = 0;
    if (r == 0) return r;
    if (perfectRequires300 && r < 134217728) r = -65536;  // 只改局部返回值
    // 分数数字显示 / SeparateScore —— 视觉，跳过
    if (r > 0) {
        // PlaySession.Obf004813(o) + note.Column.Obf005390() —— 视觉
    }
    note->column->setHoldLightActive(false);
    note->countedResult = r;
    if (judgmentCallback) judgmentCallback(r, note);
    return r;
}

int32_t ManiaHitObjectManager::judgeLongNoteHead(ManiaLongNote* ln) {
    // 0x060049DF
    currentJudgmentObject = ln;
    const int32_t r = ln->judgeLongNoteHead(*this);
    if (r != 0) ln->headJudgment = r;  // 报告用快照（0 = 无判定）
    if (r == 0) return r;
    // 分数数字显示 / SeparateScore —— 视觉，跳过
    if (r > 0) {
        // PlaySession.Obf004813(ln) + ln.Column.Obf005390() —— 视觉
    }
    if (judgmentCallback) judgmentCallback(r, ln);
    return r;
}

int32_t ManiaHitObjectManager::processLongNoteTick(ManiaLongNote* ln) {
    // 0x060049DE
    currentJudgmentObject = ln;
    const int32_t r = ln->processLongNoteTick(now);
    if (r > 0) {
        if (r == 7) ++ln->tickCount;  // 报告用
        if (judgmentCallback) judgmentCallback(r, ln);
    }
    return r;
}

// ---------------------------------------------------------------------------
// 物件构造
// ---------------------------------------------------------------------------
std::unique_ptr<ManiaNote> createManiaNote(ManiaHitObjectManager& manager,
                                           int column, int startTime,
                                           int objectType, int sourceIndex) {
    // 0x06000863 ManiaNote::.ctor
    auto note = std::make_unique<ManiaNote>();
    note->originalColumnIndex = column;  // 0x04000453（重映射之前的原始列）
    column = manager.remapManiaColumn(column);  // starg 回写形参
    note->column = &manager.stage.columns[static_cast<size_t>(column)];
    note->startTime = startTime;
    note->endTime = startTime;  // rice：起 == 止
    note->objectType = objectType;
    note->sourceIndex = sourceIndex;
    // NoteKind 未赋值 ⇒ 默认 0；PressTime / ReleaseTime / IsHeld 默认 0 / false
    return note;
}

std::unique_ptr<ManiaLongNote> createManiaLongNote(ManiaHitObjectManager& manager,
                                                   int column, int startTime,
                                                   int endTime, int objectType,
                                                   int sourceIndex) {
    // 0x06003A04 ManiaLongNote::.ctor
    auto note = std::make_unique<ManiaLongNote>();
    note->lastTickTime = -1;  // 0x04002521，先于 base..ctor
    // base..ctor(mgr, column, startTime, hs) —— ManiaNote::.ctor（含列重映射）
    note->originalColumnIndex = column;
    column = manager.remapManiaColumn(column);
    note->column = &manager.stage.columns[static_cast<size_t>(column)];
    note->startTime = startTime;
    note->endTime = startTime;
    note->objectType = objectType;
    note->sourceIndex = sourceIndex;
    note->noteKind = 1;    // 0x04000454 —— 全汇编唯一的 NoteKind 写入点
    note->endTime = endTime;
    // Duration = (float)(EndTime - StartTime)：int 相减后 conv.r4
    note->duration = static_cast<float>(note->endTime - note->startTime);
    note->samplePlayed = true;  // 0x0400251E
    return note;
}

// ---------------------------------------------------------------------------
// InputAndReplayPlayback.Obf014317 (g) —— 帧写入
// ---------------------------------------------------------------------------
void applyReplayFrame(ManiaHitObjectManager& manager, int32_t frameTime,
                      int32_t keyMask) {
    // Obf002109.Obf008161 = CurrentReplayFrame.Time
    manager.now = frameTime;
    ManiaStage& stage = manager.stage;
    const int n = stage.columnCount();
    for (int i = 0; i < n; ++i) {
        // (num & Columns[i].GetReplayBit()) > 0 —— 有符号 cgt
        const bool b =
            (keyMask & stage.columns[static_cast<size_t>(i)].replayBit()) > 0;
        if (stage.specialKeyOnRight()) {
            // [怪癖] 掩码位整体右旋一位（与录制侧的左旋互为逆）
            if (i == 0) stage.columns[static_cast<size_t>(n - 1)].setHeld(b);
            else stage.columns[static_cast<size_t>(i - 1)].setHeld(b);
        } else {
            stage.columns[static_cast<size_t>(i)].setHeld(b);
        }
    }
}

}  // namespace mania_src
}  // namespace mra
