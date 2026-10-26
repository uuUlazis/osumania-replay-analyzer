#include "mania/hit_matcher.h"

#include "mania/score_record.h"
#include "mania/source_engine.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace mra {
namespace {

using namespace mania_src;  // NOLINT

// Math.Abs(int) —— 用 64 位差避免 INT_MIN 的 UB（正常范围逐位一致）
int absIntDiff(int a, int b) {
    const long long d = static_cast<long long>(a) - static_cast<long long>(b);
    return static_cast<int>(d < 0 ? -d : d);
}

// ExpireManiaNote 的到期时刻：最小的 now 使
//   !((float)now < (float)EndTime + w100)
// 成立（float 比较，与源一致）。
int32_t expireTimeFor(int32_t endTime, float w100) {
    const double approx =
        static_cast<double>(endTime) + std::ceil(static_cast<double>(w100));
    int32_t when = dotnetConvI4(approx);
    for (int guard = 0; guard < 100000; ++guard) {
        if (static_cast<float>(when) >=
            static_cast<float>(endTime) + w100) {
            return when;
        }
        ++when;
    }
    return when;
}

// ExpireManiaNote 的 w100：`num = (float)Window100`，ScoreV2 的 LN 再 ×1.5f。
float window100For(const ManiaHitObjectManager& manager, const ManiaNote& note,
                   bool scoreV2) {
    float w100 = static_cast<float>(manager.window100);
    if (scoreV2 && dynamic_cast<const ManiaLongNote*>(&note) != nullptr) {
        w100 = w100 * 1.5f;
    }
    return w100;
}

// 判定出口交给计分簇的 flags；0 表示"该物件没有产生可计数判定"。
int32_t reportedFlags(const ManiaNote& note) {
    return note.countedResult != 0 ? note.countedResult : note.judgmentResult;
}

HitResult resultFromFlags(int32_t flags, HitResult fallback) {
    HitResult r;
    if (hitResultFromFlags(flags, &r)) return r;
    return fallback;
}

// PORT: 非 ScoreV2 路径下 JudgeLongNoteHead 恒返回 0（源不产生 LN 头判定），
// 但 renderer 需要一个头档位给 LN 着色。这里用与 ManiaNote::Judge 完全相同的
// 窗口链（|PressTime - StartTime| 对 WindowMax..Window50）给出**仅显示**的档位。
HitResult headDisplayGrade(const ManiaHitObjectManager& manager,
                           const ManiaNote& note) {
    if (!note.pressRecorded) return HitResult::Miss;
    const int err = absIntDiff(note.pressTime, note.startTime);
    if (err <= manager.windowMax) return HitResult::Perfect;
    if (err <= manager.window300) return HitResult::Great;
    if (err <= manager.window200) return HitResult::Good;
    if (err <= manager.window100) return HitResult::Ok;
    if (err <= manager.window50) return HitResult::Meh;
    return HitResult::Miss;
}

// Obf014317 (h) 分支：本 tick 没有消费新帧时 `WasHeld = IsHeld`。离线端在
// "结算 tick"（没有新帧）前显式重同步，避免上一帧的边沿被二次消费。
void resyncColumns(ManiaStage& stage) {
    for (ManiaColumn& column : stage.columns) column.wasHeld = column.isHeld;
}

}  // namespace

MatchResult matchReplayToObjects(const std::vector<ManiaHitObject>& objects,
                                 int keyCount,
                                 const std::vector<ReplayFrame>& frames,
                                 const MatchOptions& options) {
    MatchResult result;
    if (keyCount <= 0) return result;

    // ---- OD：优先用调用方给的原始 OD，否则从 legacy windows 反解 ----
    double od = options.od;
    if (std::isnan(od)) od = (188.0 - options.windows.miss) / 3.0;

    // ---- ActiveMods：把 legacy 的 mirror/random 开关并入 mod 位 ----
    uint32_t mods = options.mods;
    if (options.mirrorColumns) mods |= 0x40000000u;  // Mirror
    if (options.randomMod) mods |= 0x200000u;        // Random

    // ---- Stage ----
    ManiaStage stage;
    stage.columns.resize(static_cast<size_t>(keyCount));
    for (int i = 0; i < keyCount; ++i) {
        stage.columns[static_cast<size_t>(i)].index = i;
    }
    stage.specialStyleRight = options.specialKeyRight;

    // ---- 记分记录 + 提交器（spec 03） ----
    ManiaScoreRecord record(mods, keyCount, options.scoreV2);

    // ---- 判定管理器 ----
    ManiaHitObjectManager manager(stage, &record,
                                  static_cast<int32_t>(mods),
                                  options.randomSeed);
    // 0x060049C1 判定段：PendingKeysoundNotes / KeyPressTimes / RandomColumnMap
    manager.initializeJudgementArrays(options.randomSeed);

    // ---- 物件表（ctor 内做列重映射） ----
    std::vector<std::unique_ptr<ManiaNote>> owned;
    owned.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        const ManiaHitObject& object = objects[i];
        int column = object.column;
        if (column < 0) column = 0;
        if (column >= keyCount) column = keyCount - 1;
        const int startTime = static_cast<int32_t>(object.startTimeMs);
        if (object.type == ManiaObjectType::Hold) {
            const int endTime = object.endTimeMs
                                    ? static_cast<int32_t>(*object.endTimeMs)
                                    : startTime;
            auto note = createManiaLongNote(manager, column, startTime, endTime,
                                            object.rawType,
                                            static_cast<int>(i));
            manager.addObject(note.get());
            owned.push_back(std::move(note));
        } else {
            auto note = createManiaNote(manager, column, startTime,
                                        object.rawType, static_cast<int>(i));
            manager.addObject(note.get());
            owned.push_back(std::move(note));
        }
    }

    // ---- 载入完成 + 难度设置 + 分数倍率 ----
    manager.onLoadComplete(options.scrollPreemptMs);  // 0x060049D1
    manager.applyDifficultySettings(od, options.nativeManiaBeatmap);  // 0x060049C9

    ManiaScoreSubmission submission(&record, manager.scoringObjectCount, keyCount,
                                    true);
    submission.setupMultiplier();  // ManiaGameplay::Obf007601

    // ---- 开局三步（spec 03 §5.5 / Q6）----
    // ManiaGameplay::Obf007600 → Obf011444 的难度模拟会给每个物件发一次
    // `Obf007628(0x10000000, obj)`（ASP 下的 LN 额外再来一次），每轮开头
    // `Obf006077(false)` 清 counts；PlaySession 的开局最后一步再
    // `Obf006077(true)`：counts 清零并**快照** Obf009372/Obf009373。
    // ⇒ counts 净效果为零，但 Obf009372/373 被模拟数据定死（Q6）。循环条件
    // 无法从包内恢复，按单趟处理。不重放这三步的话 Obf009372 会停在 0，
    // Obf007870 会走它自己的除零守卫返回 0，从而**看不到** spec 03 §3.3/Q1
    // 记载的 NaN 路径。
    record.resetScore(false);
    for (const auto& up : owned) {
        if (options.scoreV2 && dynamic_cast<ManiaLongNote*>(up.get()) != nullptr) {
            submission.onJudgment(judgment_flag::kMax, up->scoreTarget);
        }
        submission.onJudgment(judgment_flag::kMax, up->scoreTarget);
    }
    record.resetScore(true);
    submission.resetComboAndHealth();

    // ---- 判定出口 → 计分簇 ----
    manager.judgmentCallback = [&submission, &manager](int32_t flags,
                                                       ManiaNote* note) {
        // 诊断：MRA_DBG_VOTES=1 时逐票打印（stderr）。纯打印，无行为影响。
        if (std::getenv("MRA_DBG_VOTES")) {
            const ManiaLongNote* ln =
                dynamic_cast<const ManiaLongNote*>(note);
            std::fprintf(stderr,
                         "[vote] flags=%d src=%d kind=%d now=%lld press=%lld "
                         "relCnt=%lld broken=%d headJ=%d judged=%d\n",
                         flags, note->sourceIndex, (int)note->noteKind,
                         (long long)manager.now, (long long)note->pressTime,
                         (long long)(ln ? ln->releaseCount : -1),
                         (int)(ln ? ln->wasBroken : 0),
                         (int)(ln ? ln->headJudged : 0), (int)note->isJudged);
        }
        submission.onJudgment(flags, note->scoreTarget);
    };
    manager.unmatchedPressCallback = [&result](int32_t when, int column) {
        UnmatchedInput input;
        input.timeMs = when;
        input.column = column;
        result.unmatchedInputs.push_back(input);
    };

    // ---- 每个回放帧 == 一个判定 tick（输入写入 → 算活动物件 → 判定） ----
    // 源引擎只吃**绝对**按键掩码（.osr 的 X 字段就是绝对掩码，Obf014317 直接
    // 写列状态），所以以 `rawKeys` 为准。ReplayFrame 同时携带边沿掩码
    // (keyDownMask/keyUpMask)：当整份回放没有任何 rawKeys（只有手工构造的边沿
    // 掩码）时用它们折叠出绝对掩码，避免这类输入被静默当成"全程无按键"。
    // 真实解码路径 (decodeReplayFrames) 一定填 rawKeys，故该兜底不影响回放。
    bool hasAbsoluteMask = false;
    for (const ReplayFrame& frame : frames) {
        if (!frame.isSeedFrame && frame.rawKeys != 0) {
            hasAbsoluteMask = true;
            break;
        }
    }
    uint32_t foldedMask = 0;
    for (const ReplayFrame& frame : frames) {
        if (frame.isSeedFrame) continue;  // "-12345|x|y|seed" 哨兵，不是帧
        const int32_t frameTime = static_cast<int32_t>(frame.timeMs);
        int32_t keyMask = 0;
        if (hasAbsoluteMask) {
            keyMask = static_cast<int32_t>(frame.rawKeys);
        } else {
            foldedMask = (foldedMask | frame.keyDownMask) & ~frame.keyUpMask;
            keyMask = static_cast<int32_t>(foldedMask);
        }
        // InputAndReplayPlayback.Obf014317 (g)：时间钉回 + 掩码写列
        applyReplayFrame(manager, frameTime, keyMask);
        // PlaySession.Obf003440 第 12 / 14 步（第 14 步无条件执行）
        manager.virtual0177();
        manager.updateInputJudgments();
    }

    // ---- 结算：回放结束后仍有未判物件 ----
    if (options.settleTail) {
        for (int outer = 0; outer < 1000000; ++outer) {
            bool found = false;
            int32_t nextTime = 0;
            int unjudgedBefore = 0;
            for (const auto& up : owned) {
                const ManiaNote* note = up.get();
                if (note->isJudged) continue;
                ++unjudgedBefore;
                const float w100 = window100For(manager, *note, options.scoreV2);
                const int32_t deadline = expireTimeFor(note->endTime, w100);
                if (!found || deadline < nextTime) {
                    nextTime = deadline;
                    found = true;
                }
            }
            if (!found) break;
            manager.now = nextTime;
            resyncColumns(stage);
            manager.virtual0177();
            manager.updateInputJudgments();
            int unjudgedAfter = 0;
            for (const auto& up : owned) {
                if (!up->isJudged) ++unjudgedAfter;
            }
            if (unjudgedAfter >= unjudgedBefore) break;  // 没有推进
        }
    }

    // ---- 兜底：仍未被判的物件（例如回放早于谱面末尾就结束） ----
    for (const auto& up : owned) {
        ManiaNote* note = up.get();
        if (note->isJudged) continue;
        const float w100 = window100For(manager, *note, options.scoreV2);
        int32_t when = expireTimeFor(note->endTime, w100);
        when = std::max(when, note->endTime + manager.window50 + 1);
        when = std::max(when, note->startTime + manager.window50 + 1);
        manager.now = when;
        resyncColumns(stage);
        manager.judgeObject(note);
        if (!note->isJudged) {
            manager.now = when + 1000;
            resyncColumns(stage);
            manager.judgeObject(note);
        }
    }

    // ---- 计分簇收尾（ManiaGameplay::Obf007620 的分数覆盖 + IsPerfect） ----
    submission.finish();

    // ---- 上报两套判定窗（V1 = 路径 B 表 / V2 = 路径 A 表） ----
    const ManiaWindowSet v1Set = computeManiaWindowSet(
        od, static_cast<int32_t>(mods), false, options.nativeManiaBeatmap);
    const ManiaWindowSet v2Set = computeManiaWindowSet(
        od, static_cast<int32_t>(mods), true, options.nativeManiaBeatmap);
    result.windowsV1 = ManiaWindows{static_cast<double>(v1Set.windowMax),
                                    static_cast<double>(v1Set.window300),
                                    static_cast<double>(v1Set.window200),
                                    static_cast<double>(v1Set.window100),
                                    static_cast<double>(v1Set.window50),
                                    static_cast<double>(v1Set.earlyInputWindow)};
    result.windowsV2 = ManiaWindows{static_cast<double>(v2Set.windowMax),
                                    static_cast<double>(v2Set.window300),
                                    static_cast<double>(v2Set.window200),
                                    static_cast<double>(v2Set.window100),
                                    static_cast<double>(v2Set.window50),
                                    static_cast<double>(v2Set.earlyInputWindow)};
    result.integralWindows = true;

    // ---- 物件状态 → HitEvent / HoldJudgement 视图 ----
    int64_t lnTicks = 0;
    for (const auto& up : owned) {
        ManiaNote* note = up.get();
        ManiaLongNote* ln = dynamic_cast<ManiaLongNote*>(note);
        if (ln != nullptr) lnTicks += ln->tickCount;

        if (ln == nullptr) {
            HitEvent event;
            event.objectIndex = note->sourceIndex;
            event.column = note->column->index;
            event.objectTimeMs = note->startTime;
            event.phase = HitPhase::NoteHead;
            event.judgement = resultFromFlags(reportedFlags(*note), HitResult::Miss);
            if (note->pressRecorded) {
                event.actualTimeMs = note->pressTime;
                event.offsetMs = static_cast<double>(note->pressTime) -
                                 static_cast<double>(note->startTime);
            }
            result.events.push_back(event);
            continue;
        }

        // ---- LN ----
        HitEvent head;
        head.objectIndex = note->sourceIndex;
        head.column = note->column->index;
        head.objectTimeMs = note->startTime;
        head.phase = HitPhase::HoldHead;
        if (note->pressRecorded) {
            head.actualTimeMs = note->pressTime;
            head.offsetMs = static_cast<double>(note->pressTime) -
                            static_cast<double>(note->startTime);
        }
        bool headJudged = false;
        if (ln->headJudgment != 0) {
            head.judgement = resultFromFlags(ln->headJudgment, HitResult::Miss);
            headJudged = true;  // ScoreV2 的真实头判
        } else if (note->pressRecorded) {
            head.judgement = headDisplayGrade(manager, *note);  // 仅显示
            headJudged = true;
        } else {
            head.judgement = HitResult::Miss;
        }

        HitEvent tail;
        tail.objectIndex = note->sourceIndex;
        tail.column = note->column->index;
        tail.objectTimeMs = note->endTime;
        tail.phase = HitPhase::HoldTail;
        tail.judgement = resultFromFlags(reportedFlags(*note), HitResult::Miss);
        if (note->releaseRecorded) {
            tail.actualTimeMs = note->releaseTime;
            tail.offsetMs = static_cast<double>(note->releaseTime) -
                            static_cast<double>(note->endTime);
        } else if (note->isHeld) {
            tail.actualTimeMs = note->pressTime;
            tail.offsetMs = static_cast<double>(note->pressTime) -
                            static_cast<double>(note->endTime);
        }
        // 非 ScoreV2：头尾是**一次**判定（stable 口径），整条 LN 用尾档着色。
        if (!options.scoreV2) tail.v1Judgement = tail.judgement;

        HoldJudgement hold;
        hold.head = head;
        hold.tail = tail;
        hold.releasedEarly =
            note->releaseRecorded && note->releaseTime < note->endTime;
        hold.heldThroughTail = !hold.releasedEarly;
        result.holds.push_back(hold);

        // 头事件只在"头真的产生了判定"或"头被按下过"时出现，避免把源里不存在的
        // 头判定塞进 counts（未按下的 LN 头在源里不产生任何计数）。
        if (headJudged) result.events.push_back(head);
        result.events.push_back(tail);
    }

    // ---- 未判物件统计 ----
    int unmatched = 0;
    for (const auto& up : owned) {
        if (!up->isJudged) ++unmatched;
    }
    result.unmatchedObjects = unmatched;
    result.replayEndedNormally = true;

    // ---- 计分簇结果（spec 03） ----
    SourceScoreSummary& summary = result.sourceScore;
    summary.count300 = record.counts.count300;
    summary.count100 = record.counts.count100;
    summary.count50 = record.counts.count50;
    summary.countMax = record.counts.countMax;
    summary.count200 = record.counts.count200;
    summary.countMiss = record.counts.countMiss;
    summary.maxCombo = record.counts.maxCombo;
    summary.currentCombo = record.counts.currentCombo;
    summary.accuracy = record.accuracy();
    summary.grade = record.grade();
    summary.intScore = record.intScore;
    summary.doubleAcc = record.doubleAcc;
    summary.lnTickCount = lnTicks;
    if (record.hasV2()) {
        summary.scoreV2 = record.alternateScoreProcessor->totalScore();
        // 0/0 = NaN 的 .NET conv.i4 语义：池快照为 0 时该路径必然出现。
        summary.scoreV2NanPool =
            record.alternateScoreProcessor->snapshotPool() == 0.0;
    }

    // ---- 事件按时间/列排序（/hits 依赖该顺序做二分） ----
    std::stable_sort(result.events.begin(), result.events.end(),
                     [](const HitEvent& a, const HitEvent& b) {
                         if (a.objectTimeMs != b.objectTimeMs)
                             return a.objectTimeMs < b.objectTimeMs;
                         return a.column < b.column;
                     });
    return result;
}

}  // namespace mra
