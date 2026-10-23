#pragma once
// Shared judgement vocabulary for the source-faithful mania engine.
//
// Authoritative specs (docs/port-specs):
//   02-judgement-core.md — 判定核心 (windows / note / LN state machines)
//   03-scoring-acc.md    — counts / acc / grade / score
//
// The engine itself (columns, notes, hit-object manager, window arithmetic,
// replay-frame input writes) lives in mania/source_engine.h. This header only
// carries the judgement vocabulary that the analysis / HTTP layers consume, so
// that /summary keeps its exact field shape.
//
// Ported from osu!.exe build 20260924.1 (archive/osu-reverse-engineering).
#include <cstdint>

namespace mra {

// Event category exposed by the API / analysis layer. Values ascend from best
// to worst; code outside the engine relies on that ordering.
enum class HitResult {
    Perfect,  // JudgmentFlags 0x10000000 (mania MAX / 320)
    Great,    // 0x08000000 (300)
    Good,     // 0x04000000 (200)
    Ok,       // 0x02000000 (100)
    Meh,      // 0x01000000 (50)
    Miss,
};

enum class HitPhase { NoteHead, HoldHead, HoldTail };

const char* hitResultName(HitResult r);       // "MAX" | "300" | "200" | "100" | "50" | "MISS"
int hitResultScoreValue(HitResult r);         // 320-style numeric (display only)

// ---- JudgmentFlags (0x0200079D) ----
// The enum has no named members in the binary; these are the raw constants
// emitted by ManiaNote::Judge / ManiaLongNote::Judge (spec 02 §4.12).
namespace judgment_flag {
inline constexpr int32_t kMax = 268435456;     // 0x10000000 MAX / 320
inline constexpr int32_t k300 = 134217728;     // 0x08000000
inline constexpr int32_t k200 = 67108864;      // 0x04000000
inline constexpr int32_t k100 = 33554432;      // 0x02000000
inline constexpr int32_t k50 = 16777216;       // 0x01000000
inline constexpr int32_t kTick = 7;            // LN hold tick (100 ms cadence)
inline constexpr int32_t kNone = 0;            // "no judgement" (Judge early-out)
inline constexpr int32_t kMiss = -65536;       // 0xFFFF0000 mania miss
inline constexpr int32_t kBreak = -1048576;    // 0xFFF00000 LN early release
}  // namespace judgment_flag

// Maps a JudgmentFlags value onto an event category. Returns false for the
// non-counting values (0 / 7 / -1048576) which never become HitEvents — the
// score cluster still sees them (combo +1 for the tick, combo reset for the
// break, no counts for either).
bool hitResultFromFlags(int32_t flags, HitResult* out);

// Window table reported to the API (doubles carrying the engine's int32
// windows, so the existing /summary "windows" JSON keeps its shape):
//   perfect..meh = WindowMax / Window300 / Window200 / Window100 / Window50
//   miss         = EarlyInputWindow
// Reporting carrier only: the engine derives its windows from OD + ActiveMods
// (source_engine.h: ApplyDifficultySettings).
struct ManiaWindows {
    double perfect = 16;
    double great = 40;
    double good = 73;
    double ok = 103;
    double meh = 127;
    double miss = 164;

    // Legacy OD -> window carrier. NOT used for judging: it only feeds the
    // engine's OD when MatchOptions::od is left unset (test / legacy callers).
    static ManiaWindows fromOD(double od);
};

}  // namespace mra
