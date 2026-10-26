#include "mania/judgement.h"

namespace mra {

const char* hitResultName(HitResult r) {
    switch (r) {
        case HitResult::Perfect: return "MAX";
        case HitResult::Great: return "300";
        case HitResult::Good: return "200";
        case HitResult::Ok: return "100";
        case HitResult::Meh: return "50";
        case HitResult::Miss: return "MISS";
    }
    return "?";
}

int hitResultScoreValue(HitResult r) {
    switch (r) {
        case HitResult::Perfect: return 320;
        case HitResult::Great: return 300;
        case HitResult::Good: return 200;
        case HitResult::Ok: return 100;
        case HitResult::Meh: return 50;
        case HitResult::Miss: return 0;
    }
    return 0;
}

bool hitResultFromFlags(int32_t flags, HitResult* out) {
    switch (flags) {
        case judgment_flag::kMax: *out = HitResult::Perfect; return true;
        case judgment_flag::k300: *out = HitResult::Great; return true;
        case judgment_flag::k200: *out = HitResult::Good; return true;
        case judgment_flag::k100: *out = HitResult::Ok; return true;
        case judgment_flag::k50: *out = HitResult::Meh; return true;
        case judgment_flag::kMiss: *out = HitResult::Miss; return true;
        default: return false;  // 0 / 7 / -1048576: no countable judgement
    }
}

ManiaWindows ManiaWindows::fromOD(double od) {
    // 32-bit mania window signature (path B, spec 02 §2.6):
    //   WindowMax   = 16
    //   Window300   = 34f + 3f * d,  d = min(10f, max(0f, 10f - OD))
    //   Window200   = 67f + 3f * d
    //   Window100   = 97f + 3f * d
    //   Window50    = 121f + 3f * d
    //   EarlyInput  = 158f + 3f * d
    // Written as doubles because this struct is only a reporting / OD carrier;
    // the engine recomputes everything in ApplyDifficultySettings.
    ManiaWindows w;
    w.perfect = 16.0;
    w.great = 64.0 - 3.0 * od;
    w.good = 97.0 - 3.0 * od;
    w.ok = 127.0 - 3.0 * od;
    w.meh = 151.0 - 3.0 * od;
    w.miss = 188.0 - 3.0 * od;
    return w;
}

}  // namespace mra
