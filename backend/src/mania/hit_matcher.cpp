#include "mania/hit_matcher.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>
#include <cstdlib>

namespace mra {
namespace {

// Per-column state machine.
struct ColumnState {
    std::deque<int> pending;          // indices into objects, time-sorted
    std::optional<int> activeHold;    // object index of active hold head
    // ScoreV2 (lazer parity): LN tails whose head was swept-missed. The
    // tail is still judgeable by a later release, capped at Meh
    // (DrawableHoldNoteTail.GetCappedResult: !Head.IsHit -> cap Meh).
    std::deque<int> headMissedHolds;
    // ScoreV2: a hold whose body broke (early release beyond the tail
    // windows). The tail stays open; a re-press resumes the hold.
    int brokenHold = -1;
    bool keyCurrentlyDown = false;
    int pressRelated = -1;            // object the current press hit (-1 empty)
    // 深夜 VIII Walk A2: a late catch (head press at/after the tail) keeps a
    // pending "previous release" candidate — the key-up immediately before
    // the catch press. At settlement the end error is measured against the
    // RELEASE NEAREST the tail within the judgeable band [tail-50w, tail+50w]
    // (mania_number: LN 可多次按，结算用覆盖记录；bz #954 真实松键 +77 最近 ->
    // good；Swan 密集列前一松键 -5..-15 最近 -> 保持高档，两图同时成立).
    bool lateCatchActive = false;
    double lateCatchPrevRel = 0;
    // 深夜 XII: a deep break (release earlier than tail−breakBound, 185ms
    // default: az CREDITED ≥194 / MISS ≤177, bz ≥199 / ≤175 — zero crossings
    // on 243 breaks) leaves the unit awaiting a re-grip instead of dying.
    // The next same-column press re-binds it (最后一次按键覆盖); the settle
    // happens at that cycle's release. Expires at tail+50w → break Miss.
    int v1BrokenAwait = -1;
    // Finished key actions of this column in press order (release time of a
    // press plus the object it hit). Mirrors the reference OsuManiaJudger
    // "actions" list and feeds the LN-head cover scan.
    struct PastAction {
        double end;
        int related;
    };
    std::vector<PastAction> history;
    // Time of the last press on this column that produced a judgement.
    double lastJudgeTime = -1e18;
    double lastResolvedHead = -1e18;
};

HitResult judgeAbs(double absOffsetMs, const ManiaWindows& w) {
    double a = std::abs(absOffsetMs);
    if (a <= w.perfect) return HitResult::Perfect;
    if (a <= w.great) return HitResult::Great;
    if (a <= w.good) return HitResult::Good;
    if (a <= w.ok) return HitResult::Ok;
    if (a <= w.meh) return HitResult::Meh;
    return HitResult::Miss;
}

double windowOf(const ManiaWindows& w, HitResult r) {
    switch (r) {
        case HitResult::Perfect: return w.perfect;
        case HitResult::Great: return w.great;
        case HitResult::Good: return w.good;
        case HitResult::Ok: return w.ok;
        case HitResult::Meh: return w.meh;
        case HitResult::Miss: return w.miss;
    }
    return w.miss;
}

// Bit-exact replication of the legacy .NET System.Random (the generator the
// client draws its RD column shuffle from): the subtraction SeedArray[56]
// constructor plus the InternalSample difference stream. next() mirrors
// Random.Next(): (int)(InternalSample() * (1.0/MBIG) * int.MaxValue).
class DotNetRandom {
public:
    explicit DotNetRandom(int seed) {
        // Seed arrives as an unchecked uint32 -> int32; int.MinValue cannot
        // be negated, the constructor substitutes int.MaxValue for it.
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

    int next() {
        // Left-to-right double math mirrors the C# expression bit for bit.
        return static_cast<int>(internalSample() * (1.0 / 2147483647.0) *
                                2147483647.0);
    }

    // Sample() = InternalSample() * (1.0/MBIG): the protected draw the RD
    // permutation consumes via Next(0,K) = (int)(Sample()*K).
    double sample() { return internalSample() * (1.0 / 2147483647.0); }

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

MatchResult matchReplayToObjects(const std::vector<ManiaHitObject>& objects,
                                 int keyCount,
                                 const std::vector<ReplayFrame>& frames,
                                 const MatchOptions& options) {
    MatchResult result;
    if (keyCount <= 0) return result;

    std::vector<ColumnState> columns(static_cast<size_t>(keyCount));
    std::vector<const HoldJudgement*> holdByObject(objects.size(), nullptr);
    std::vector<HoldJudgement> holds;
    // LN units that already released once: a re-judge cannot exceed 200
    // (reference OsuManiaJudger LNHasJudged cap).
    std::vector<uint8_t> lnJudged(objects.size(), 0);
    // ScoreV2: LN bodies that broke at least once — the tail result caps
    // at Meh (lazer GetCappedResult hasComboBreak).
    std::vector<uint8_t> brokeOnce(objects.size(), 0);
    // V1 deferred grey marks (MRA_GREY_DEFER): notes whose only press so
    // far was a true-grey (beyond 50w, inside missw) tap. The verdict is
    // postponed to the next same-column press or the end-of-replay sweep.
    std::vector<uint8_t> greyMarked(objects.size(), 0);
    // 深夜 XXIII (双写封顶): an early-grey press on an LN head sets
    // greyTouched; if the unit is later HIT in-window and settles
    // Perfect/Great, the interruption caps it at 200 (mania_number
    // 中间断过 → 封顶; probe: #344 0xA4 双写 → good). Units never hit stay
    // Miss (57 例语义不变).
    std::vector<uint8_t> greyTouched(objects.size(), 0);
    std::vector<uint8_t> lnCapped(objects.size(), 0);
    // 深夜 XII: a deep break marks the unit broken — its re-grip settle caps
    // at 200 (mania_number 中间断过 → 彩/黄300封顶200).
    std::vector<uint8_t> v1BrokeOnce(objects.size(), 0);

    // RD (Random) 列置换: stable 权威构造 (osu!.exe IL m18881/m18890 +
    // 原始按键实证): 逐列抽 j = Next(0,K) = (int)(Sample()*K) (double 乘法
    // 截断, 非取模), 命中已用列则重抽; colMap[原列] = 播放列, 物件列先查
    // 表再走镜像 (RD+Mirror 组合顺序: 先随机后镜像). K 偶数且 >4 时
    // stable 另有「首列不消耗随机数」特例 (IL m18881), 7K 不触发 —
    // 本实现未覆盖该分支.
    std::vector<int> rdColumns;
    if (options.randomMod) {
        DotNetRandom rng(static_cast<int>(options.randomSeed));
        rdColumns.assign(static_cast<size_t>(keyCount), 0);
        std::vector<char> used(static_cast<size_t>(keyCount), 0);
        for (int i = 0; i < keyCount; ++i) {
            int j;
            do {
                j = static_cast<int>(rng.sample() *
                                     static_cast<double>(keyCount));
            } while (used[j]);
            used[j] = 1;
            rdColumns[i] = j;
        }
    }

    auto columnOf = [&](const ManiaHitObject& obj) {
        int col = obj.column;
        if (options.randomMod && col >= 0 &&
            col < static_cast<int>(rdColumns.size()))
            col = rdColumns[col];
        if (options.mirrorColumns) col = keyCount - 1 - col;
        return col;
    };

    // Queue objects per column in time order (objects arrive sorted by time
    // from the parser; stable-sort by time anyway for robustness). The whole
    // queue is built up-front: presses can target objects far in the future.
    std::vector<int> order(objects.size());
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return objects[a].startTimeMs < objects[b].startTimeMs;
    });
    for (int idx : order) columns[columnOf(objects[idx])].pending.push_back(idx);

    // 判定窗 × 速率 (acc-semantics §0.2): the .osr frame clock is real time
    // and comparisons stay raw, so the whole window set scales by the rate.
    // The OD back-derivation below must read the UNSCALED base window.
    ManiaWindows wScaled = options.windows;
    if (options.rate != 1.0) {
        wScaled.perfect *= options.rate;
        wScaled.great *= options.rate;
        wScaled.good *= options.rate;
        wScaled.ok *= options.rate;
        wScaled.meh *= options.rate;
        wScaled.miss *= options.rate;
    }
    const ManiaWindows& w = wScaled;
    // ScoreV2 changes the PERFECT window only: OD <= 5 -> 22.4 - 0.6*OD,
    // else 24.9 - 1.1*OD, floored to the integer ms (popup-measured:
    // od6.5 -> 17, not 17.5 — Negai tails at raw +25 are MAX, +26 are
    // 300; raw 26/1.5 = 17.33 only exceeds the integer-floored bound).
    ManiaWindows w2 = w;
    if (options.scoreV2) {
        const double od = (188.0 - options.windows.miss) / 3.0;  // unscaled base
        const double p2 = od <= 5.0 ? 22.4 - 0.6 * od : 24.9 - 1.1 * od;
        w2.perfect = std::floor(p2) * options.rate;
    }

    // Env-gated state-machine trace (MRA_TRACE=1): press/release/defer
    // transitions for replay-vs-stable semantics debugging.
    const bool trace = std::getenv("MRA_TRACE") != nullptr;
    auto traceEv = [&](const char* kind, int64_t t, int col, const char* detail) {
        if (trace) std::fprintf(stderr, "[TR] %s t=%lld col=%d %s\n", kind,
                                static_cast<long long>(t), col, detail);
    };

    // Sustained-roll pre-scan (MRA_GREY_ROLL3): a true-grey press is
    // ignored only when it belongs to a run of >=4 consecutive same-column
    // presses (gaps <= 1.6x OK) that each sit in the early grey band of a
    // following beatmap note — the Ice Angel 12-press chain at ~149ms.
    // Isolated, paired or tripled grey taps (endemize's 12 labelled
    // phantom misses, Ice Angel's 8 non-chain greys) still consume the
    // note as Miss. Press times mirror the main loop (frames are edge
    // events; a set keyDownMask bit is a press).
    const char* envRoll3Pre = std::getenv("MRA_GREY_ROLL3");
    const bool roll3 = !(envRoll3Pre && envRoll3Pre[0] == '0');
    // ScoreV2 press-eating (lazer OnPressed/beginHold): a press on a column
    // with an open hold (broken body or missed head) re-attaches the hold
    // and is not offered to pending notes.
    const char* envEat = std::getenv("MRA_V2_HOLD_EAT");
    const bool holdEat = envEat && *envEat;
    // ScoreV2 tail single-judgement: a body break (first release beyond the
    // windows) Misses the tail outright; a re-press does not reopen it.
    const char* envNoRj = std::getenv("MRA_V2_TAIL_NOREJUDGE");
    const bool tailNoRejudge = envNoRj && *envNoRj;
    // V1 experiments: disable the early-death requeue, and/or the re-judge
    // 200-cap (dense vibro maps diverge from stable's replay judging).
    const char* envNoRq = std::getenv("MRA_V1_NOREQUEUE");
    // Stable V1: an early release judges the tail exactly once — a broken
    // LN never re-opens. Blue Zenith V1 per-note truth: the requeue revived
    // broken holds into good finals where stable shows Miss (#479 family,
    // 45 miss->good diffs) and double-judged rescued LNs down one grade
    // (#1394: stable Perfect vs our capped Good). Default ON; set
    // MRA_V1_NOREQUEUE=0 to restore the reference-judger requeue.
    const bool v1NoRequeue = !(envNoRq && envNoRq[0] == '0');
    const char* envNoCap = std::getenv("MRA_V1_NOCAP");
    const bool v1NoCap = envNoCap && *envNoCap;
    // 深夜 VIII Walk A: default OFF = the swift settle is disabled (a head
    // press at/after the tail opens the hold and the unit settles with the
    // real key-up). Set MRA_V1_SWIFT_SETTLE=1 to restore the
    // reference-judger press-time settle.
    const char* envSs = std::getenv("MRA_V1_SWIFT_SETTLE");
    const bool v1SwiftSettle = envSs && *envSs;
    // 深夜 VIII Walk D (mania_number 持有过尾): the judgeable state extends
    // to tail+50w — a release beyond it settles AT that moment, so
    // endDiff := min(|release-tail|, 50w). 深夜 VI ② disproved this clamp on
    // the swift-settle base; under Walk A the press>=tail units reach the
    // held-through branch with their real (late) releases — different
    // composition. MRA_V1_HELDTHRU_EXT=0 disables.
    const char* envHe = std::getenv("MRA_V1_HELDTHRU_EXT");
    const bool v1HeldthruExt = !(envHe && *envHe && std::atof(envHe) == 0);
    // Walk A2 disable switch (default ON).
    // 深夜 XII: A2 (nearest-release) 撤销 — 用户拍板「优先满足锚点图」;
    // azimuth 逐物件真值 97 条 late-catch 差异全部为其伪影 (深夜 XI)。
    // MRA_V1_NEARREL=1 可复现 A2 行为。
    const char* envNr = std::getenv("MRA_V1_NEARREL");
    const bool v1NearRel = envNr && *envNr;
    const char* envRg = std::getenv("MRA_V1_NO_REGRIP");
    const bool v1Regrip = !(envRg && *envRg);
    const char* envBb = std::getenv("MRA_V1_BREAK_BOUND");
    const double v1BreakBound = envBb && *envBb ? std::atof(envBb) : 185.0;
    // 深夜 XXIII 双写封顶开关 (default ON). MRA_V1_GREYCAP=0 disables.
    const char* envGc = std::getenv("MRA_V1_GREYCAP");
    const bool v1GreyCap = !(envGc && *envGc && std::atof(envGc) == 0);
    const char* envNrc = std::getenv("MRA_V1_NO_REGRIPCAP");
    const bool v1RegripCap = !(envNrc && *envNrc);
    // V1 dead-note press: a press past a pending note's late window kills
    // that note (Miss); reference behaviour continues the scan so the press
    // can still hit the next candidate. With this knob the press is
    // consumed by the dead note instead (Blue Zenith V1 truth: 74 swapped
    // bindings, 我们偏高/stable偏高 完美对称 214/213).
    const char* envEatD = std::getenv("MRA_V1_EAT_DEAD");
    const bool v1EatDead = envEatD && *envEatD && std::atof(envEatD) != 0;
    // V2 tail late-release bound (absolute ms beyond the tail); 0 = 1.5×ok.
    const char* envTlb = std::getenv("MRA_V2_TAIL_LATEBOUND_MS");
    const double tailLateMs = envTlb ? std::atof(envTlb) : 0.0;
    // V2 head sweep bound (ms past head start); default = miss window.
    const char* envLb = std::getenv("MRA_V2_LATEBOUND");
    const double headLateBound =
        envLb && *envLb ? std::atof(envLb)
                        : static_cast<double>(w.miss);
    // V2 press-miss intercept in (MEH, MISS]; default off: blanket
    // consumption cascades through dense columns (Twin Bloom +0.186
    // overshoot). Enable with MRA_V2_PMISS=1 for experiments.
    const char* envPm = std::getenv("MRA_V2_PMISS");
    const bool pressMissOn = envPm && *envPm && std::atof(envPm) != 0;
    // V2 body-broken resume: default = the press may steal from the
    // resume when it lands a real hit (>=50) on a pending note; a
    // would-be-MISS future target falls back to the resume (test10063),
    // a late dead-zone target is press-missed (Negai). Disable with
    // MRA_V2_RESUME_LATE=0 (legacy resume-consume-first).
    const char* envRl = std::getenv("MRA_V2_RESUME_LATE");
    const bool resumeLate = !(envRl && *envRl && std::atof(envRl) == 0);
    // V2 tail MAX raw bound (ms past the tail): a released tail only gets
    // MAX when the RAW release error is within this bound; the other tail
    // windows keep the 1.5x lenience. -1 = off (scaled windows only).
    // Negai audit: geki excess = exactly the raw 24/25/26 cluster (38).
    const char* envTmr = std::getenv("MRA_V2_TAIL_MAX_RAW");
    const double tailMaxRaw =
        envTmr && *envTmr ? std::atof(envTmr) : -1.0;
    // V2 note-lock boundary offset for gap in [MEH, 2*MEH]: default = the
    // MEH window (start + MEH). 1 = the GOOD window (start + GOOD) —
    // TrueBlue 4 traced pairs (gap 183, press at A+64..+120): stable sent
    // 3 of 4 to the NEXT note, matching a GOOD-window boundary.
    const char* envBg = std::getenv("MRA_V2_BOUND_GOOD");
    const bool boundGood = envBg && *envBg && std::atof(envBg) != 0;
    // V2 released tails cap at Great (300): stable never awards 300g to a
    // released LN tail (Negai geki +36 / n300 −40 signature).
    const char* envTcg = std::getenv("MRA_V2_TAIL_CAP_GREAT");
    const bool tailCapGreat = envTcg && *envTcg;
    std::vector<std::vector<double>> colPressPre;
    std::vector<std::vector<double>> colNotePre;
    if (roll3) {
        colNotePre.resize(static_cast<size_t>(keyCount));
        colPressPre.resize(static_cast<size_t>(keyCount));
        for (const auto& obj : objects)
            colNotePre[static_cast<size_t>(columnOf(obj))].push_back(
                static_cast<double>(obj.startTimeMs));
        for (auto& v : colNotePre) std::sort(v.begin(), v.end());
        for (const ReplayFrame& f : frames) {
            if (f.isSeedFrame) continue;
            for (int c = 0; c < keyCount; ++c)
                if ((f.keyDownMask >> c) & 1u)
                    colPressPre[static_cast<size_t>(c)].push_back(
                        static_cast<double>(f.timeMs));
        }
    }
    auto rollRunLen = [&](double t, int c) {
        int run = 1;
        if (!roll3) return run;
        const auto& ps = colPressPre[static_cast<size_t>(c)];
        auto pit = std::lower_bound(ps.begin(), ps.end(), t - 0.5);
        if (pit == ps.end() || *pit > t + 0.5) return run;
        const double lim = 1.6 * static_cast<double>(w.ok);
        const double mehW = static_cast<double>(w.meh);
        const double missW = static_cast<double>(w.miss);
        const auto& nt = colNotePre[static_cast<size_t>(c)];
        auto candidate = [&](double q) {
            auto n = std::lower_bound(nt.begin(), nt.end(), q + mehW + 0.5);
            return n != nt.end() && *n <= q + missW + 0.5;
        };
        for (int j = static_cast<int>(pit - ps.begin()) + 1;
             j < static_cast<int>(ps.size()); ++j) {
            if (ps[j] - ps[j - 1] > lim) break;
            if (!candidate(ps[j])) break;
            ++run;
        }
        return run;
    };

    // Emits a MISS event for an object head; hold heads mark tail miss too.
    auto resolveMissHead = [&](int objIdx, int64_t atTimeMs) {
        const ManiaHitObject& obj = objects[objIdx];
        HitEvent ev;
        ev.objectIndex = objIdx;
        ev.column = columnOf(obj);
        ev.objectTimeMs = obj.startTimeMs;
        ev.judgement = HitResult::Miss;
        ev.phase = obj.type == ManiaObjectType::Hold ? HitPhase::HoldHead
                                                     : HitPhase::NoteHead;
        result.events.push_back(ev);
        columns[ev.column].lastResolvedHead = static_cast<double>(obj.startTimeMs);
        if (obj.type == ManiaObjectType::Hold && options.scoreV2) {
            // ScoreV2: head and tail judge separately. The missed head does
            // not pre-judge the tail: a later release inside the tail
            // windows still judges it, capped at Meh. Leftover tails
            // flush as Miss.
            columns[ev.column].headMissedHolds.push_back(objIdx);
            return;
        }
        if (obj.type == ManiaObjectType::Hold) {
            HoldJudgement hold;
            hold.head = ev;
            HitEvent tail;
            tail.objectIndex = objIdx;
            tail.column = ev.column;
            tail.objectTimeMs = *obj.endTimeMs;
            tail.judgement = HitResult::Miss;
            tail.phase = HitPhase::HoldTail;
            hold.tail = tail;
            hold.releasedEarly = false;
            hold.heldThroughTail = false;
            // Tail events must reach result.events too — the /hits route and
            // the frontend renderer (V2 tail band / V1 whole-LN colour) read
            // them from there. The final stable_sort restores ordering.
            result.events.push_back(tail);
            holds.push_back(hold);
            holdByObject[objIdx] = &holds.back();
        }
    };

    // ScoreV2: judge a deferred tail (missed head or broken body). The
    // windows apply with the 1.5x release lenience; any result above MEH
    // caps at Meh (lazer GetCappedResult).
    auto resolveDeferredTail = [&](int objIdx, ColumnState& col,
                                   int64_t releaseTime, bool headMissed,
                                   bool flatMeh) {
        (void)headMissed;
        const ManiaHitObject& obj = objects[objIdx];
        double err = static_cast<double>(releaseTime) -
                     static_cast<double>(*obj.endTimeMs);
        HitEvent tail;
        tail.objectIndex = objIdx;
        tail.column = columnOf(obj);
        tail.objectTimeMs = *obj.endTimeMs;
        tail.phase = HitPhase::HoldTail;
        tail.actualTimeMs = releaseTime;
        tail.offsetMs = err;
        if (flatMeh) {
            // A body-broken hold that was re-attached: the final release
            // always lands the tail at Meh (stable-measured, test1006 A尾).
            tail.judgement = HitResult::Meh;
        } else {
            tail.judgement = err > static_cast<double>(w.ok) * 1.5
                                 ? HitResult::Miss
                                 : judgeAbs(err / 1.5, w2);
        }
        // HitResult ascends Perfect->Miss, so "better than Meh" is <.
        // (A missed head, or a body break, caps the tail at Meh; an
        // outright Miss stays Miss.)
        if (tail.judgement < HitResult::Meh) tail.judgement = HitResult::Meh;
        HoldJudgement hold;
        hold.releasedEarly = err < 0;
        hold.heldThroughTail = !hold.releasedEarly;
        for (auto it = result.events.rbegin(); it != result.events.rend();
             ++it) {
            if (it->objectIndex == objIdx &&
                it->phase == HitPhase::HoldHead) {
                hold.head = *it;
                break;
            }
        }
        hold.tail = tail;
        result.events.push_back(tail);
        holds.push_back(hold);
        holdByObject[objIdx] = &holds.back();
    };
    // ScoreV2: a deferred tail that never got a judging release flushes as
    // an outright Miss (lazer time-pass miss).
    auto forceMissTail = [&](int objIdx, ColumnState& col) {
        (void)col;
        const ManiaHitObject& obj = objects[objIdx];
        HitEvent tail;
        tail.objectIndex = objIdx;
        tail.column = columnOf(obj);
        tail.objectTimeMs = *obj.endTimeMs;
        tail.phase = HitPhase::HoldTail;
        tail.judgement = HitResult::Miss;
        HoldJudgement hold;
        hold.releasedEarly = false;
        hold.heldThroughTail = false;
        for (auto it = result.events.rbegin(); it != result.events.rend();
             ++it) {
            if (it->objectIndex == objIdx &&
                it->phase == HitPhase::HoldHead) {
                hold.head = *it;
                break;
            }
        }
        hold.tail = tail;
        result.events.push_back(tail);
        holds.push_back(hold);
        holdByObject[objIdx] = &holds.back();
    };

    auto resolveTail = [&](int objIdx, ColumnState& col, int64_t releaseTime,
                           int64_t lastFrameTime, bool fromFlush) {
        const ManiaHitObject& obj = objects[objIdx];
        HoldJudgement hold;
        hold.head = HitEvent();
        hold.releasedEarly = releaseTime < *obj.endTimeMs;
        hold.heldThroughTail = !hold.releasedEarly;
        HitEvent tail;
        tail.objectIndex = objIdx;
        tail.column = columnOf(obj);
        tail.objectTimeMs = *obj.endTimeMs;
        tail.phase = HitPhase::HoldTail;
        tail.actualTimeMs = releaseTime;
        bool rejudgeable = false;
        if (options.scoreV2) {
            // ScoreV2: head and tail are judged separately, like two regular
            // notes, and the tail release window is 1.5x. A hold still down
            // at the end of the replay is held-through Perfect. A real
            // release judges |endDiff| against 1.5x windows; a release later
            // than 1.5x OK is Miss (a late release never gets MEH).
            if (fromFlush) {
                tail.offsetMs = 0.0;
                // Held-through tail after a prior body break caps at Meh
                // (lazer GetCappedResult: Body.HasHoldBreak -> cap Meh).
                tail.judgement = brokeOnce[objIdx] ? HitResult::Meh
                                                   : HitResult::Perfect;
            } else {
                double err = static_cast<double>(releaseTime - *obj.endTimeMs);
                if (hold.releasedEarly &&
                    -err > static_cast<double>(w2.meh)) {
                    if (tailNoRejudge) {
                        // stable-measured: the tail is judged exactly once,
                        // at its first release. A break (release beyond the
                        // windows) is an outright Miss; a re-press does not
                        // reopen the tail (Twin Bloom 12/12 fitting required
                        // no re-judgement of broken tails).
                        tail.offsetMs = err;
                        tail.judgement = HitResult::Miss;
                    } else {
                        // lazer-style: the tail stays open (HasHoldBreak). A
                        // re-press resumes the hold; a later release inside
                        // the windows judges the tail capped at Meh; otherwise
                        // it flushes as Miss.
                        traceEv("DEFER", releaseTime, columnOf(objects[objIdx]), (std::string("oi") + std::to_string(objIdx) + " brokenHold").c_str());
                        brokeOnce[objIdx] = 1;
                        col.brokenHold = objIdx;
                        // The tail is no longer active: leaving activeHold
                        // set made the next release resolve the tail here AND
                        // keep brokenHold dangling, so a third release judged
                        // the same LN twice (two tails, double-counted unit).
                        col.activeHold.reset();
                        return;
                    }
                }
                tail.offsetMs = err;
                tail.judgement =
                    !hold.releasedEarly &&
                            err > (tailLateMs > 0 ? tailLateMs : w.ok * 1.5)
                        ? HitResult::Miss
                        : judgeAbs(err / 1.5, w2);
                if (tail.judgement == HitResult::Perfect &&
                    tailMaxRaw >= 0 && err > tailMaxRaw)
                    tail.judgement = HitResult::Great;
                if (tailCapGreat && tail.judgement == HitResult::Perfect)
                    tail.judgement = HitResult::Great;
                if (brokeOnce[objIdx] && tail.judgement < HitResult::Meh)
                    tail.judgement = HitResult::Meh;
            }
        } else if (!hold.releasedEarly) {
            // Held through the tail: legacy osu!stable awards the tail hit
            // (MAX300 when windows allow). Verified against a real replay
            // .osr header: held-through=Great shifts 813 tails to 300 and
            // diverges from osu by -729 MAX.
            tail.offsetMs = 0.0;
            tail.judgement = HitResult::Perfect;
        } else {
            double err = static_cast<double>(releaseTime - *obj.endTimeMs);
            tail.offsetMs = err;
            tail.judgement = judgeAbs(err, w);
        }
        // Attach head reference (the V1 combined judgement below needs it).
        for (auto it = result.events.rbegin(); it != result.events.rend(); ++it) {
            if (it->objectIndex == objIdx && it->phase == HitPhase::HoldHead) {
                hold.head = *it;
                break;
            }
        }
        // V1 (stable) shows ONE judgement per LN combining head+tail errors
        // — faithful port of the reference OsuManiaJudger judgeRelease:
        //   * head never pressed (swept) or release earlier than tail-50w:
        //     the unit is Miss; an early release leaves the LN re-judgeable
        //     by a later press on the same column.
        //   * a head pressed earlier than head-50w ("fxxking ppy" in the
        //     reference) merges as if the key was held from head to tail:
        //     startDiff = duration - 1.
        //   * otherwise tier windows scaled by rate (MAX 1.2, 300 1.1,
        //     200/100 1.0), startDiff and startDiff+endDiff both bounded,
        //     floor 50. A re-judge caps MAX/300 down to 200.
        if (!options.scoreV2) {
            const bool headAlive = hold.head.offsetMs.has_value();
            // Held through the tail: the judgeable state survives until
            // tail+50w and the press end is taken to be that moment
            // (mania_number acc.html), so a late release never widens the
            // end error past the 50 window. Explains the late-release
            // family (end +312 -> Ok, +1311 -> Good, +161 -> Good ...).
            double endDiff = std::abs(
                static_cast<double>(releaseTime) -
                static_cast<double>(*obj.endTimeMs));
            if (v1HeldthruExt)
                endDiff = std::min(endDiff,
                                   static_cast<double>(w.meh));
            if (col.lateCatchActive) {
                col.lateCatchActive = false;
                if (v1NearRel && col.lateCatchPrevRel > 0) {
                    const double d0 = std::abs(
                        col.lateCatchPrevRel -
                        static_cast<double>(*obj.endTimeMs));
                    if (d0 <= static_cast<double>(w.meh) && d0 < endDiff)
                        endDiff = d0;
                }
            }
            const bool earlyDeath = hold.releasedEarly &&
                static_cast<double>(releaseTime) <
                    static_cast<double>(*obj.endTimeMs) -
                        static_cast<double>(w.meh);
            if (v1Regrip && headAlive && earlyDeath && !fromFlush &&
                static_cast<double>(*obj.endTimeMs - releaseTime) >=
                    v1BreakBound) {
                // 深夜 XII: deep break — the unit awaits a re-grip instead of
                // dying (az CREDITED 63 例全为深断重握; breakErr 阈值两图零跨界).
                v1BrokeOnce[objIdx] = 1;
                col.v1BrokenAwait = objIdx;
                col.activeHold.reset();
                return;
            }
            if (!headAlive || earlyDeath) {
                tail.v1Judgement = HitResult::Miss;
            } else {
                double startDiff = std::abs(*hold.head.offsetMs);
                if (*hold.head.offsetMs < -static_cast<double>(w.meh)) {
                    startDiff = std::abs(
                        static_cast<double>(*obj.endTimeMs - 1) -
                        static_cast<double>(obj.startTimeMs));
                }
                const double totalDiff = startDiff + endDiff;
                const std::pair<HitResult, double> tiers[] = {
                    {HitResult::Perfect, 1.2}, {HitResult::Great, 1.1},
                    {HitResult::Good, 1.0}, {HitResult::Ok, 1.0},
                };
                tail.v1Judgement = HitResult::Meh;
                for (const auto& tier : tiers) {
                    double win = windowOf(w, tier.first) * tier.second;
                    if (startDiff <= win && startDiff + endDiff <= 2.0 * win) {
                        tail.v1Judgement = tier.first;
                        break;
                    }
                }
            }
            if (v1RegripCap && v1BrokeOnce[objIdx] &&
                !earlyDeath &&
                (tail.v1Judgement == HitResult::Perfect ||
                 tail.v1Judgement == HitResult::Great)) {
                // 中断过 → 封顶 200 (the break happened; the re-grip settle
                // covers but caps).
                tail.v1Judgement = HitResult::Good;
            }
            if (v1GreyCap && lnCapped[objIdx] && !earlyDeath &&
                (tail.v1Judgement == HitResult::Perfect ||
                 tail.v1Judgement == HitResult::Great)) {
                // 双写封顶: the grey interruption caps the unit at 200.
                tail.v1Judgement = HitResult::Good;
            }
            rejudgeable = headAlive && earlyDeath;
            if (!v1NoCap && lnJudged[objIdx] && !rejudgeable &&
                (tail.v1Judgement == HitResult::Perfect ||
                 tail.v1Judgement == HitResult::Great)) {
                tail.v1Judgement = HitResult::Good;
            }
        }
        hold.tail = tail;
        // Expose the tail event to /hits and the renderer (V2 tail band,
        // V1 whole-LN combined colour). Sorted chronologically at the end.
        result.events.push_back(tail);
        if (!options.scoreV2 && rejudgeable && !v1NoRequeue) {
            // The LN returns to the queue: a later press may judge it again
            // (capped at 200 by lnJudged). No HoldJudgement is stored yet —
            // the final release pushes exactly one entry, so the stable
            // tally counts the LN once.
            lnJudged[objIdx] = 1;
            col.activeHold.reset();
            col.pending.push_front(objIdx);
            return;
        }
        holds.push_back(hold);
        holdByObject[objIdx] = &holds.back();
        col.activeHold.reset();
    };

    // Sweep misses for column heads older than their late-bound at time t.
    // Playback-stepped model (V1 and V2): the clock advances frame by frame
    // and the game loop itself expires an unhit head once its judgeable
    // range has passed — a press never sees an expired head again.
    auto lateBound = [&]() -> int64_t {
        if (!options.scoreV2) {
            // Stable V1: a head is judgeable up to start+50w only
            // (mania_number acc.html: the miss window marks where the
            // judgeable range ENDS; an LN head's range is clipped at the
            // head — the fix1 press rule recast as a time rule). A press
            // landing past start+50w flows straight to the next candidate
            // (bz #293: press@+157 passed the dead #293 to hit #297).
            return static_cast<int64_t>(w.meh);
        }
        // V2 heads stay pressable past the OK window: a late press in
        // (MEH, MISS] press-misses the head and is consumed — it never
        // flows to the next note. Twin Bloom dense-column audit: the old
        // +OK sweep bound let those presses fall through to k+1, which
        // reproduced a −0.259% dACC almost exactly (29 traced cases).
        return static_cast<int64_t>(headLateBound);
    };
    auto sweepMisses = [&](int64_t t) {
        for (auto& col : columns) {
            while (!col.pending.empty()) {
                int idx = col.pending.front();
                const ManiaHitObject& obj = objects[idx];
                if (obj.startTimeMs + lateBound() >= t) break;
                resolveMissHead(idx, t);
                col.pending.pop_front();
            }
        }
    };

    // 深夜 XII: settle a broken-await LN whose re-grip never came as a
    // break Miss (tail event + one hold record; head event already exists).
    auto settleBrokenMiss = [&](ColumnState& col) {
        const int bi = col.v1BrokenAwait;
        col.v1BrokenAwait = -1;
        const ManiaHitObject& obj = objects[bi];
        HitEvent tail;
        tail.objectIndex = bi;
        tail.column = columnOf(obj);
        tail.objectTimeMs = *obj.endTimeMs;
        tail.phase = HitPhase::HoldTail;
        tail.judgement = HitResult::Miss;
        tail.v1Judgement = HitResult::Miss;
        result.events.push_back(tail);
        HoldJudgement hold;
        for (auto it = result.events.rbegin(); it != result.events.rend(); ++it) {
            if (it->objectIndex == bi && it->phase == HitPhase::HoldHead) {
                hold.head = *it;
                break;
            }
        }
        hold.tail = tail;
        hold.releasedEarly = true;
        hold.heldThroughTail = false;
        holds.push_back(hold);
        holdByObject[bi] = &holds.back();
    };
    int64_t lastFrameTime = 0;
    for (const ReplayFrame& frame : frames) {
        if (frame.isSeedFrame) continue;  // never an input event
        const int64_t t = frame.timeMs;
        lastFrameTime = t;
        // Per-frame order (stable playback loop): 1) the clock sweep
        // expires unhit heads, 2) key presses migrate/scan, 3) key releases
        // settle LNs. V1 LN state machine per unit:
        //   pending  — head unjudged; the sweep MISSes it at start+50w,
        //              a press inside [-50w, +50w] opens it (early-grey
        //              presses in (-miss,-meh) neither judge nor kill it)
        //   holding  — head judged and key down; immune to the sweep
        //   settled  — resolveTail pushed the one combined judgement
        //   dead     — a break (early release beyond the tail windows)
        //              settles as Miss at once and never re-opens
        //              (MRA_V1_NOREQUEUE default ON). Pre-head taps beyond
        //              -miss record nothing.
        sweepMisses(t);
        if (!options.scoreV2) {
            for (int c = 0; c < keyCount; ++c) {
                ColumnState& col = columns[c];
                if (col.v1BrokenAwait < 0) continue;
                if (t > *objects[col.v1BrokenAwait].endTimeMs +
                             static_cast<int64_t>(w.meh)) {
                    // 尾+50w 过去仍未重握: break Miss 成立
                    settleBrokenMiss(col);
                }
            }
        }

        // Presses are processed before releases at identical timestamps:
        // keyDown/keyUp in the same frame means a quick tap; presses must
        // be able to hit notes.
        for (int c = 0; c < keyCount; ++c) {
            ColumnState& col = columns[c];
            bool down = (frame.keyDownMask >> c) & 1u;
            if (!down) continue;
            if (col.activeHold) continue;  // re-press during hold: ignored
            if (!options.scoreV2 && col.v1BrokenAwait >= 0) {
                // 深夜 XII: the re-grip press re-binds the deeply-broken LN
                // (最后一次按键覆盖 — the new head event becomes the merge
                // input; the settle happens at this cycle's release).
                const int bi = col.v1BrokenAwait;
                col.v1BrokenAwait = -1;
                col.activeHold = bi;
                col.pressRelated = bi;
                col.keyCurrentlyDown = true;
                col.lastJudgeTime = static_cast<double>(t);
                col.lastResolvedHead = static_cast<double>(objects[bi].startTimeMs);
                HitEvent ev;
                ev.objectIndex = bi;
                ev.column = c;
                ev.objectTimeMs = objects[bi].startTimeMs;
                ev.actualTimeMs = t;
                ev.offsetMs = static_cast<double>(t - objects[bi].startTimeMs);
                ev.judgement = judgeAbs(*ev.offsetMs, w);
                ev.phase = HitPhase::HoldHead;
                result.events.push_back(ev);
                continue;
            }
            if (options.scoreV2 && col.brokenHold >= 0 && !resumeLate) {
                // A body-broken hold resumes on the next press: the press is
                // consumed by the resume — no head re-judge (stable-measured,
                // test1006/10063: mid-break taps never re-judged the head).
                col.pressRelated = col.brokenHold;
                col.activeHold = col.brokenHold;
                col.brokenHold = -1;
                continue;
            }
            if (options.scoreV2 && holdEat && !col.headMissedHolds.empty()) {
                col.pressRelated = col.headMissedHolds.front();
                continue;
            }
            col.keyCurrentlyDown = true;
            col.pressRelated = -1;
            if (col.pending.empty()) {
                result.unmatchedInputs.push_back({t, c});
                continue;
            }
            int idx = -1;
            bool greyIgnore = false;
        bool deadEat = false;
        bool greyConsume = false;
            double err = 0.0;
            if (!options.scoreV2) {
                // Stable V1 press targeting, faithful port of the reference
                // OsuManiaJudger findTarget: scan the pending list in time
                // order.
                //  * earlier than the miss window: no target at all — every
                //    later note is even further away.
                //  * too late — rice past the OK window, LN heads past
                //    tail+OK, or an LN whose span is already covered by the
                //    previous same-column action: the note DIES (Miss) and
                //    the scan continues with the next candidate.
                //  * otherwise the first candidate is hit. Rice leaves the
                //    queue (an |offset| beyond the 50 window judges Miss —
                //    the grey press consumes the note, which reproduces the
                //    endemize 12 labelled phantom misses); LN heads stay
                //    pressed until release.
                for (auto it = col.pending.begin();
                     it != col.pending.end();) {
                    const ManiaHitObject& cand = objects[*it];
                    const double e = static_cast<double>(t) -
                                     static_cast<double>(cand.startTimeMs);
                    if (e < -static_cast<double>(w.miss)) break;  // too early
                    if (greyMarked[*it]) {
                        // A grey press already flagged this note. The next
                        // press settles it: within the MAX window the chain
                        // lead is rescued (Ice Angel +4..+6 roll presses);
                        // any wider offset confirms the stale tap and the
                        // note is consumed as Miss right here (endemize's
                        // isolated taps: follow-up lands +20 → Miss).
                        const double pWin =
                            windowOf(w, HitResult::Perfect);
                        if (e >= -pWin && e <= pWin) {
                            greyMarked[*it] = 0;
                        } else {
                            resolveMissHead(*it, t);
                            it = col.pending.erase(it);
                            continue;
                        }
                    }
                    bool tooLate = false;
                    if (cand.type == ManiaObjectType::Hold) {
                        // Stable V1: an early-grey press (inside the miss
                        // window but before the 50 window) neither judges
                        // nor consumes an LN head — the note stays pending
                        // for a later press. Blue Zenith V1 per-note truth:
                        // presses at -139..-173 all left the head alive and
                        // it was hit by the next press (1224:0 against
                        // consumption). Rice keeps the endemize consume
                        // rule in its own early-grey branch below.
                        if (e < -static_cast<double>(w.meh)) {
                            if (v1GreyCap) greyTouched[*it] = 1;
                            greyIgnore = true;
                            break;
                        }
                        // Stable V1 LN head press bound: a press within the
                        // head 50 window binds; the head's judgeable range is
                        // clipped at the head by the per-frame sweep
                        // (start+50w), so this bound is a redundant defense
                        // line — presses earlier than start-50w were already
                        // grey-ignored above (neither judge nor consume).
                        // Only once the head range has passed does the press
                        // flow on (+157 past a 55ms jack LN -> expired, next
                        // head takes it as MAX; the former
                        // `t > endTime + ok` bound kept jack heads pending
                        // ~30ms past expiry and cascaded columns into
                        // misses, 427 symmetric diffs).
                        tooLate = e > static_cast<double>(w.meh);
                    } else if (e >= static_cast<double>(w.ok)) {
                        tooLate = true;
                    } else if (e < -static_cast<double>(w.ok)) {
                        // Early grey zone: the press only judges the note when
                        // it is at least halfway between the 50 and miss
                        // windows ((50+miss)/2 — separates endemize od8
                        // -147ms consumed misses from Ice Angel od8.5 -137ms
                        // ignored presses). A closer grey press judges
                        // nothing and consumes nothing.
                        const char* envDef = std::getenv("MRA_GREY_DEFER");
                        if (envDef && *envDef &&
                            e < -static_cast<double>(w.meh)) {
                            // Deferred grey judgement: the press itself
                            // judges nothing; the note is marked and the
                            // verdict lands on the NEXT same-column press
                            // (or the sweep). A follow-up within the MAX
                            // window saves the note; anything wider — or no
                            // follow-up at all — resolves the whole unit as
                            // Miss, which keeps endemize's isolated taps on
                            // the declared miss count while letting Ice
                            // Angel's roll chains survive as MAX300s.
                            greyMarked[*it] = 1;
                            greyIgnore = true;
                            break;
                        }
                        const char* envGmi = std::getenv("MRA_GREY_MEH_IGNORE");
                        if (envGmi && *envGmi &&
                            e < -static_cast<double>(w.meh)) {
                            // True grey (beyond the 50 window, inside the
                            // miss window): stable judges nothing here — the
                            // press is discarded, the note stays pending and
                            // dies on the per-frame sweep at its own beat
                            // unless a same-frame press saves it (roll
                            // chains: every chain press lands grey on the
                            // next note, and the FOLLOWING press lands +4 in
                            // the same frame and hits it as MAX300).
                            greyIgnore = true;
                            break;
                        }
                        const char* envRoll = std::getenv("MRA_GREY_ROLL_IGNORE");
                        if (envRoll && *envRoll &&
                            e < -static_cast<double>(w.meh)) {
                            // Roll-context rule: a true-grey press is
                            // ignored when the next note on the column is
                            // within 1.5x the OK window (a roll/stream —
                            // the Ice Angel chain sits at 145-152ms gaps vs
                            // 1.5x101.5=152.25); isolated mistaps with the
                            // next note far away consume the note as Miss
                            // (endemize: all 12 gaps >= 167ms vs
                            // 1.5x103=154.5).
                            double rollLimit = 1.5 *
                                static_cast<double>(w.ok);
                            bool roll = false;
                            auto nx = std::next(it);
                            if (nx != col.pending.end()) {
                                double gap = static_cast<double>(
                                    objects[*nx].startTimeMs) -
                                    static_cast<double>(cand.startTimeMs);
                                roll = gap <= rollLimit;
                            }
                            if (roll) {
                                greyIgnore = true;
                                break;
                            }
                            // else: fall through to the consume path.
                        }
                        const char* envRoll3 = std::getenv("MRA_GREY_ROLL3");
                        if ((!envRoll3 || envRoll3[0] != '0') &&
                            e < -static_cast<double>(w.meh)) {
                            // Sustained-roll rule: ignore the grey press only
                            // inside a run of >=4 consecutive same-column
                            // presses (gaps <= 1.6x OK) where every press sits
                            // in the early grey band of a following note. The
                            // Ice Angel chain (11 roll presses at ~149ms plus
                            // the tail tap) qualifies; its 8 isolated/paired/
                            // tripled greys and all of endemize's taps do not
                            // and still consume the note as Miss.
                            if (rollRunLen(static_cast<double>(t), c) >= 4) {
                                greyIgnore = true;
                                break;
                            }
                            // else: fall through to the consume path.
                        }
                        const char* envB = std::getenv("MRA_GREY_BOUND");
                        const char* envL = std::getenv("MRA_GREY_LOCK");
                        if (envL && *envL) {
                            // Column grey-press lock: a grey press is ignored
                            // when the previous judging press on this column is
                            // closer than D ms.
                            double D = std::atof(envL);
                            if (static_cast<double>(t) - col.lastJudgeTime < D) {
                                greyIgnore = true;
                                break;
                            }
                            greyConsume = true;
                        }
                        const char* envN = std::getenv("MRA_GREY_NOTELOCK");
                        if (envN && *envN && !greyIgnore) {
                            double G = std::atof(envN);
                            if (cand.startTimeMs - col.lastResolvedHead < G) {
                                greyIgnore = true;
                            }
                        }
                        const char* envI = std::getenv("MRA_GREY_IGNORE");
                        if (envI && *envI) {
                            // Full grey-ignore: the press judges nothing and
                            // consumes nothing; the note stays pending so a
                            // later press can still hit it (Ice Angel roll
                            // chains: every chain press lands grey on the next
                            // note, and the declared counts show the chain
                            // notes as MAX300 via the following press).
                            greyIgnore = true;
                            break;
                        }
                        if (envB) {
                            double greyBound = 0.5 * (static_cast<double>(w.meh) +
                                                      static_cast<double>(w.miss));
                            if (*envB) greyBound = std::atof(envB);
                            if (e > -greyBound) {
                                greyIgnore = true;
                                break;
                            }
                            greyConsume = true;
                        }
                    }
                    if (tooLate) {
                        resolveMissHead(*it, t);
                        it = col.pending.erase(it);
                        // True dead zone only (rice: (meh, miss]): the
                        // (ok, meh] band must keep flowing to the next
                        // candidate (per-object confirmed on True Blue).
                        if (v1EatDead &&
                            e > static_cast<double>(w.meh)) {
                            deadEat = true;
                            break;
                        }
                        continue;
                    }
                    idx = *it;
                    err = e;
                    col.pending.erase(it);
                    break;
                }
                if (deadEat) {
                    // The press was consumed by the dead note's press-miss
                    // (Miss event already emitted). No unmatched record: in
                    // stable the press did produce a judgement.
                    continue;
                }
                if (idx < 0 || greyIgnore) {
                    // Grey press inside the (50, (50+miss)/2) band: it judges
                    // nothing, so the note stays available for a later press.
                    result.unmatchedInputs.push_back({t, c});
                    continue;
                }
                col.pressRelated = idx;
            } else {
                // Stable ScoreV2 note-lock: the press binds to the LAST
                // pending note whose start time is before the press, unless
                // the press lands before that note's boundary. The boundary
                // between note k and the next pending note k+1 depends on
                // their start-time gap (MEH = the 50 window):
                //   gap <  MEH          -> next.start - 1
                //   gap in [MEH, 2MEH]  -> k.start + MEH
                //   gap >  2MEH         -> next.start - MEH
                // Presses before the boundary belong to k; at/after it they
                // belong to k+1 (docs: mania_number, ScoreV2 判定方式).
                const double mehW = static_cast<double>(w2.meh);
                int target = -1;
                double boundErr = 0.0;
                bool pressMiss = false;
                for (auto it = col.pending.begin();
                     it != col.pending.end(); ++it) {
                    const ManiaHitObject& cand = objects[*it];
                    const double start =
                        static_cast<double>(cand.startTimeMs);
                    if (start > static_cast<double>(t)) {
                        // Future note: the press can only early-hit it.
                        const double e = static_cast<double>(t) - start;
                        const double early = static_cast<double>(w.miss);
                        if (e >= -early) {
                            target = *it;
                            boundErr = e;
                            break;
                        }
                        // Beyond this note's early window: the press may
                        // still hit a LATER future note — keep walking.
                        continue;
                    }
                    // Stable press-miss: a press landing beyond the 50
                    // window but within the miss window consumes this note
                    // as MISS and is itself consumed — it never flows to
                    // the next note. Symmetric with the early side (which
                    // commits judgeAbs -> Miss in (miss, meh]). Twin Bloom
                    // dense-column audit: letting these presses fall
                    // through reproduced a −0.259% dACC (29 traced cases,
                    // weights Σ7650 vs the 7525 gap).
                    const double lateErr =
                        static_cast<double>(t) - start;
                    if (pressMissOn && lateErr > mehW &&
                        lateErr <= static_cast<double>(w.miss)) {
                        traceEv("PMISS", t, c,
                                (std::string("oi") +
                                 std::to_string(*it) + " off " +
                                 std::to_string(lateErr)).c_str());
                        resolveMissHead(*it, t);
                        it = col.pending.erase(it);
                        pressMiss = true;
                        break;
                    }
                    auto nx = std::next(it);
                    double boundary;
                    if (nx == col.pending.end()) {
                        boundary = start + static_cast<double>(w.ok);
                    } else {
                        const double gap =
                            static_cast<double>(objects[*nx].startTimeMs) -
                            start;
                        if (gap < mehW)
                            boundary = static_cast<double>(
                                           objects[*nx].startTimeMs) -
                                       1.0;
                        else if (gap <= 2.0 * mehW)
                            boundary = start +
                                       (boundGood
                                            ? static_cast<double>(w.good)
                                            : mehW);
                        else
                            boundary = static_cast<double>(
                                           objects[*nx].startTimeMs) -
                                       mehW;
                    }
                    if (static_cast<double>(t) < boundary) {
                        target = *it;
                        boundErr = static_cast<double>(t) - start;
                        break;
                    }
                    // t >= boundary: the press flows past this note (which
                    // stays pending for its own sweep) to the next one.
                }
                if (pressMiss) {
                    // The press was consumed by the press-miss above; the
                    // MISS event is already emitted. No further judging.
                    continue;
                }
                if (target >= 0) {
                    const ManiaHitObject& cand = objects[target];
                    const double e = boundErr;
                    // §5 heads judge independently against the windows: a
                    // press commits the head anywhere in [head-miss,
                    // head+lateBound]; judgeAbs grades it right away —
                    // beyond the 50 window on the early side that is an
                    // outright Miss at press time (stable-measured,
                    // test10063).
                    const double early = static_cast<double>(w.miss);
                    if (e >= -early &&
                        e <= static_cast<double>(lateBound())) {
                        idx = target;
                        err = e;
                    }
                }
                if (idx < 0) {
                    // Empty press: a body-broken (deferred) hold resumes on
                    // the next press that hits nothing else — the press is
                    // consumed by the resume (no head re-judge).
                    if (options.scoreV2 && col.brokenHold >= 0) {
                        col.pressRelated = col.brokenHold;
                        col.activeHold = col.brokenHold;
                        col.brokenHold = -1;
                        continue;
                    }
                    result.unmatchedInputs.push_back({t, c});
                    continue;
                }
            }
            const ManiaHitObject& obj = objects[idx];
            if (greyTouched[idx]) {
                if (obj.type == ManiaObjectType::Hold) lnCapped[idx] = 1;
                greyTouched[idx] = 0;
            }
            for (auto it = col.pending.begin(); it != col.pending.end(); ++it) {
                if (*it == idx) { col.pending.erase(it); break; }
            }
            if (options.scoreV2 && resumeLate && col.brokenHold >= 0) {
                // Body-time rule: while the broken hold's body is still
                // live (press before its tail) the press belongs to the
                // hold — resume-consume it (test10063: press -144 from the
                // next head never killed it). Once the tail has passed, the
                // hold is over and the press is free: a real hit steals
                // (broken tail settles flat Meh), a dead-zone press
                // press-misses the note (Negai).
                const int bi = col.brokenHold;
                if (t < *objects[bi].endTimeMs) {
                    col.pending.insert(
                        std::upper_bound(
                            col.pending.begin(), col.pending.end(),
                            objects[idx].startTimeMs,
                            [&](double v, int i) {
                                return v < objects[i].startTimeMs;
                            }),
                        idx);
                    col.pressRelated = bi;
                    col.activeHold = bi;
                    col.brokenHold = -1;
                    continue;
                }
                traceEv("RFREE", t, c,
                        (std::string("oi") + std::to_string(idx) +
                         " off " + std::to_string(err) + " brk oi" +
                         std::to_string(bi)).c_str());
                resolveDeferredTail(bi, col, t, false, true);
                col.brokenHold = -1;
                if (judgeAbs(err, options.scoreV2 ? w2 : w) ==
                    HitResult::Miss) {
                    resolveMissHead(idx, t);
                    continue;
                }
            }
            HitEvent ev;
            ev.objectIndex = idx;
            ev.column = c;
            ev.objectTimeMs = obj.startTimeMs;
            ev.actualTimeMs = t;
            ev.offsetMs = err;
            ev.judgement = judgeAbs(err, options.scoreV2 ? w2 : w);
            ev.phase = obj.type == ManiaObjectType::Hold ? HitPhase::HoldHead
                                                         : HitPhase::NoteHead;
            traceEv("PRESS", t, c, (std::string("oi") + std::to_string(idx) +
                " off " + std::to_string(err) + " j" +
                std::to_string(static_cast<int>(ev.judgement))).c_str());
            result.events.push_back(ev);
            {
                const char* envM = std::getenv("MRA_GREY_LOCK_MODE");
                const bool onlyReal = envM && *envM && std::atof(envM) >= 2;
                if (!onlyReal || !greyConsume) col.lastJudgeTime = static_cast<double>(t);
            }
            col.lastResolvedHead = static_cast<double>(obj.startTimeMs);
            if (obj.type == ManiaObjectType::Hold) {
                col.activeHold = idx;
                if (!options.scoreV2 && v1NearRel && t >= *obj.endTimeMs) {
                    col.lateCatchActive = true;
                    col.lateCatchPrevRel =
                        col.history.empty() ? 0.0 : col.history.back().end;
                }
                // 深夜 VIII Walk A: a head press at/after the tail no longer
                // settles the LN on the spot. Stable keeps the unit open and
                // settles with the real key-up (bz frame truth: #954
                // press@tail+0 up@tail+77 -> good; #911 press@tail+5
                // up@tail+112; 21 of 26 in-range 一级差 holds were swift-settled
                // one tier high). V2 always waited for the key-up (test10072).
                if (v1SwiftSettle && !options.scoreV2 &&
                    t >= *obj.endTimeMs) {
                    resolveTail(idx, col, t, t, false);
                }
            }
        }

        for (int c = 0; c < keyCount; ++c) {
            ColumnState& col = columns[c];
            bool up = (frame.keyUpMask >> c) & 1u;
            if (!up) continue;
            const bool wasDown = col.keyCurrentlyDown;
            col.keyCurrentlyDown = false;
            int related = -1;
            if (col.activeHold) related = *col.activeHold;
            else if (wasDown) related = col.pressRelated;
            if (wasDown) col.history.push_back(
                {static_cast<double>(t), related});
            col.pressRelated = -1;
            if (col.activeHold) {
                traceEv("RELEASE", t, c, (std::string("oi") +
                    std::to_string(*col.activeHold)).c_str());
                resolveTail(*col.activeHold, col, t, t, false);
            } else if (options.scoreV2) {
                // One key release resolves at most one open tail.
                if (col.brokenHold >= 0) {
                    int bi = col.brokenHold;
                    col.brokenHold = -1;
                    const ManiaHitObject& bo = objects[bi];
                    double berr = static_cast<double>(t) -
                                  static_cast<double>(*bo.endTimeMs);
                    if (berr < 0 &&
                        -berr > static_cast<double>(w2.meh)) {
                        col.brokenHold = bi;  // broke again: still open
                    } else {
                        resolveDeferredTail(bi, col, t, false, true);
                    }
                } else if (!col.headMissedHolds.empty()) {
                    int hi = col.headMissedHolds.front();
                    const ManiaHitObject& ho = objects[hi];
                    double herr = static_cast<double>(t) -
                                  static_cast<double>(*ho.endTimeMs);
                    if (herr >= 0 ||
                        -herr <= static_cast<double>(w2.meh)) {
                        col.headMissedHolds.pop_front();
                        resolveDeferredTail(hi, col, t, true, false);
                    }
                }
            }
        }
    }

    // End-of-replay sweep: unresolved heads whose judgeable range has
    // passed -> MISS; future heads beyond the last frame stay unjudged.
    // Active holds resolve as held-through (or early-release-miss if the
    // head was missed). With the per-frame V1 sweep at start+50w the flush
    // set is exactly the old single flush ({start <= lastFrameTime}).
    sweepMisses(lastFrameTime + lateBound() + 1);
    for (int c = 0; c < keyCount; ++c) {
        ColumnState& col = columns[c];
        if (col.v1BrokenAwait >= 0) settleBrokenMiss(col);
        if (col.keyCurrentlyDown) {
            col.history.push_back({static_cast<double>(lastFrameTime),
                                   col.activeHold ? *col.activeHold
                                                  : col.pressRelated});
            col.keyCurrentlyDown = false;
        }
        if (col.activeHold)
            resolveTail(*col.activeHold, col, lastFrameTime, lastFrameTime,
                        true);
        if (options.scoreV2) {
            if (col.brokenHold >= 0) {
                int bi = col.brokenHold;
                col.brokenHold = -1;
                forceMissTail(bi, col);
            }
            while (!col.headMissedHolds.empty()) {
                int hi = col.headMissedHolds.front();
                col.headMissedHolds.pop_front();
                forceMissTail(hi, col);
            }
        }
    }
    result.unmatchedObjects = 0;
    for (auto& col : columns)
        result.unmatchedObjects += static_cast<int>(col.pending.size());
    result.replayEndedNormally = true;

    // Events in chronological order (stable for identical times: head first).
    std::stable_sort(result.events.begin(), result.events.end(),
                     [](const HitEvent& a, const HitEvent& b) {
                         if (a.objectTimeMs != b.objectTimeMs)
                             return a.objectTimeMs < b.objectTimeMs;
                         return a.column < b.column;
                     });
    // Keep result.holds references valid: they were built as values.
    result.holds = std::move(holds);
    return result;
}

}  // namespace mra
