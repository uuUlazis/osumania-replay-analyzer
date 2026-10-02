#include "mania/hit_matcher.h"

#include <algorithm>
#include <deque>

namespace mra {
namespace {

// Per-column state machine.
struct ColumnState {
    std::deque<int> pending;          // indices into objects, time-sorted
    std::optional<int> activeHold;    // object index of active hold head
    bool keyCurrentlyDown = false;
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

    auto columnOf = [&](const ManiaHitObject& obj) {
        int col = obj.column;
        if (options.mirrorColumns) col = keyCount - 1 - col;
        return col;
    };

    // Queue objects per column in time order (objects arrive sorted by time
    // from the parser; stable-sort by time anyway for robustness).
    std::vector<int> order(objects.size());
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return objects[a].startTimeMs < objects[b].startTimeMs;
    });
    for (int idx : order) columns[columnOf(objects[idx])].pending.push_back(idx);

    const ManiaWindows& w = options.windows;

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

    auto resolveTail = [&](int objIdx, ColumnState& col, int64_t releaseTime,
                           int64_t lastFrameTime) {
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
        if (!hold.releasedEarly) {
            // Held through the tail: legacy osu!stable awards the tail hit
            // (MAX300 when windows allow). Verified against a real replay's
            // .osr header: held-through=Great shifts 813 tails to 300 and
            // diverges from osu by -729 MAX.
            tail.offsetMs = 0.0;
            tail.judgement = HitResult::Perfect;
        } else if (options.scoreV2) {
            // ScoreV2: tail judged independently on |endDiff|/1.5; a release
            // later than ok*1.5 is no judgement (= Miss).
            double err = static_cast<double>(releaseTime - *obj.endTimeMs);
            tail.offsetMs = err;
            tail.judgement = err > w.ok * 1.5 ? HitResult::Miss
                                              : judgeAbs(err / 1.5, w);
        } else {
            double err = static_cast<double>(releaseTime - *obj.endTimeMs);
            tail.offsetMs = err;
            tail.judgement = judgeAbs(err, w);
        }
        hold.tail = tail;
        // Expose the tail event to /hits and the renderer (V2 tail band,
        // V1 whole-LN combined colour). Sorted chronologically at the end.
        result.events.push_back(tail);
        // Attach head reference
        for (auto it = result.events.rbegin(); it != result.events.rend(); ++it) {
            if (it->objectIndex == objIdx && it->phase == HitPhase::HoldHead) {
                hold.head = *it;
                break;
            }
        }
        // V1 (stable) shows ONE judgement per LN combining head+tail errors
        // (community reference OsuManiaJudger.judgeRelease): startDiff and
        // startDiff+endDiff against windows scaled by rate (MAX 1.2, 300 1.1,
        // 200/100 1.0, else 50). Head or tail Miss -> Miss.
        if (!options.scoreV2) {
            if (hold.head.judgement == HitResult::Miss ||
                tail.judgement == HitResult::Miss) {
                tail.v1Judgement = HitResult::Miss;
            } else {
                double startDiff =
                    std::abs(hold.head.offsetMs.value_or(0.0));
                double endDiff = std::abs(tail.offsetMs.value_or(0.0));
                const std::pair<HitResult, double> tiers[] = {
                    {HitResult::Perfect, 1.2}, {HitResult::Great, 1.1},
                    {HitResult::Good, 1.0}, {HitResult::Ok, 1.0},
                };
                tail.v1Judgement = HitResult::Meh;
                for (const auto& [jr, rate] : tiers) {
                    double win = windowOf(w, jr) * rate;
                    if (startDiff <= win && startDiff + endDiff <= 2.0 * win) {
                        tail.v1Judgement = jr;
                        break;
                    }
                }
            }
        }
        holds.push_back(hold);
        holdByObject[objIdx] = &holds.back();
        col.activeHold.reset();
    };

    // Sweep misses for column heads older than their late-bound at time t.
    // ScoreV2 tightens the late bound: normal notes ok, LN heads ok*1.5.
    auto lateBoundOf = [&](const ManiaHitObject& obj) -> int64_t {
        if (!options.scoreV2) return static_cast<int64_t>(w.miss);
        return obj.type == ManiaObjectType::Hold
                   ? static_cast<int64_t>(w.ok * 1.5)
                   : static_cast<int64_t>(w.ok);
    };
    auto sweepMisses = [&](int64_t t) {
        for (auto& col : columns) {
            while (!col.pending.empty()) {
                int idx = col.pending.front();
                const ManiaHitObject& obj = objects[idx];
                if (obj.startTimeMs + lateBoundOf(obj) >= t) break;
                resolveMissHead(idx, t);
                col.pending.pop_front();
            }
        }
    };

    int64_t lastFrameTime = 0;
    for (const ReplayFrame& frame : frames) {
        if (frame.isSeedFrame) continue;  // never an input event
        const int64_t t = frame.timeMs;
        lastFrameTime = t;
        sweepMisses(t);

        // Process releases before presses at identical timestamps? Presses
        // first: keyDown/keyUp in the same frame at the same time means a
        // quick tap; presses must be able to hit notes.
        for (int c = 0; c < keyCount; ++c) {
            ColumnState& col = columns[c];
            bool down = (frame.keyDownMask >> c) & 1u;
            if (!down) continue;
            if (col.activeHold) continue;  // re-press during hold: ignored
            col.keyCurrentlyDown = true;
            if (col.pending.empty()) {
                result.unmatchedInputs.push_back({t, c});
                continue;
            }
            int idx = col.pending.front();
            const ManiaHitObject& obj = objects[idx];
            double err = static_cast<double>(t - obj.startTimeMs);
            if (err < -static_cast<double>(w.miss) ||
                err > static_cast<double>(lateBoundOf(obj))) {
                // Head not in window: ghost press (front note will be swept).
                result.unmatchedInputs.push_back({t, c});
                continue;
            }
            HitEvent ev;
            ev.objectIndex = idx;
            ev.column = c;
            ev.objectTimeMs = obj.startTimeMs;
            ev.actualTimeMs = t;
            ev.offsetMs = err;
            ev.judgement = judgeAbs(err, w);
            ev.phase = obj.type == ManiaObjectType::Hold ? HitPhase::HoldHead
                                                         : HitPhase::NoteHead;
            result.events.push_back(ev);
            col.pending.pop_front();
            if (obj.type == ManiaObjectType::Hold) {
                col.activeHold = idx;
                if (t >= *obj.endTimeMs) {
                    // Head hit at/after tail time: tail resolves as held-through.
                    resolveTail(idx, col, t, t);
                }
            }
        }

        for (int c = 0; c < keyCount; ++c) {
            ColumnState& col = columns[c];
            bool up = (frame.keyUpMask >> c) & 1u;
            if (!up) continue;
            col.keyCurrentlyDown = false;
            if (col.activeHold) resolveTail(*col.activeHold, col, t, t);
        }
    }

    // End-of-replay sweep: unresolved notes -> MISS; active holds resolved as
    // held-through (or early-release-miss if head was missed already handled).
    sweepMisses(lastFrameTime + static_cast<int64_t>(w.miss) + 1);
    for (int c = 0; c < keyCount; ++c) {
        ColumnState& col = columns[c];
        if (col.activeHold)
            resolveTail(*col.activeHold, col, lastFrameTime, lastFrameTime);
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
