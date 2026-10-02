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

ManiaWindows ManiaWindows::fromOD(double od) {
    ManiaWindows w;
    w.perfect = 16.0;
    w.great = 64.0 - 3.0 * od;
    w.good = 97.0 - 3.0 * od;
    w.ok = 127.0 - 3.0 * od;
    w.meh = 151.0 - 3.0 * od;
    w.miss = 188.0 - 3.0 * od;
    return w;
}

HitResult judgeOffset(double offsetMs, const ManiaWindows& windows) {
    double a = offsetMs < 0 ? -offsetMs : offsetMs;
    if (a <= windows.perfect) return HitResult::Perfect;
    if (a <= windows.great) return HitResult::Great;
    if (a <= windows.good) return HitResult::Good;
    if (a <= windows.ok) return HitResult::Ok;
    if (a <= windows.meh) return HitResult::Meh;
    return HitResult::Miss;
}

}  // namespace mra
