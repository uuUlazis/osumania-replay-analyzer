#pragma once
// Star Rating Rebirth (SRR) - C++ port of zzzzv/StarRatingRebirth (SRCalculator.cs,
// Version "2025/04/15", itself a C# port of sunnyxxy's algorithm).
// Mechanism reference: docs/sr-rebirth-csharp.md.
//
// The full pipeline is reproduced without modification, including all corrections:
//   * length correction  SR *= totalNotes / (totalNotes + 60)
//   * high-star rescale  SR > 9 -> 9 + (SR - 9) / 1.2
//   * final multiplier   SR *= 0.975
#include "mania/official_sr.h"  // mra::SrNote (single shared note definition)

#include <cstddef>
#include <vector>

namespace mra {

// Constants mirrored from SRCalculator.cs / Utils.cs; exposed so the unit tests can assert
// them against the values recorded in docs/sr-rebirth-csharp.md.
namespace srr_sr_detail {
// PreProcess (docs/sr-rebirth-csharp.md section 2)
inline constexpr double kXScale        = 0.3;    // x = 0.3 * sqrt((64.5 - ceil(OD*3)) / 500)
inline constexpr double kXWindowBase   = 64.5;
inline constexpr double kXWindowDiv    = 500.0;
inline constexpr double kXCapSlope     = 0.6;    // x = min(x, 0.6*(x - 0.09) + 0.09)
inline constexpr double kXCapOffset    = 0.09;
inline constexpr int    kMinNotes      = 20;     // < 20 notes is not computable
inline constexpr int    kMaxKeys       = 10;     // only 1-10K supported
// GetKeyUsage / GetKeyUsage400 (section 4.1/4.2)
inline constexpr int    kKeyUsageHalfWindowMs = 150;
inline constexpr double kUsage400Core      = 3.75;
inline constexpr double kUsage400CoreDiv   = 150.0;
inline constexpr double kUsage400CapMs     = 1500.0;
inline constexpr double kUsage400FlankMs   = 400.0;   // parabolic flank width (400^2 in code)
inline constexpr double kAnchorRatioBias   = 0.18;    // anchor = 1 + min(r - 0.18, 5*(r - 0.22)^3)
inline constexpr double kAnchorCubeBias    = 0.22;
inline constexpr double kAnchorCubeScale   = 5.0;
inline constexpr double kAnchorWalkScale   = 4.0;     // 1 - 4*(0.5 - ratio)^2
inline constexpr double kAnchorWalkCenter  = 0.5;
// ComputeJbar (section 4.3)
inline constexpr double kJackNerfScale   = 7e-5;     // 1 - 7e-5 * (0.15 + |d - 0.08|)^-4
inline constexpr double kJackNerfOffset  = 0.15;
inline constexpr double kJackNerfCenter  = 0.08;
inline constexpr double kJackValOffset   = 0.11;     // 1/(d + 0.11 * x^(1/4))
inline constexpr double kJbarSmoothMs    = 500;      // smoothing window (sum), scale 0.001
inline constexpr double kJbarPower       = 5.0;      // (sum w*v^5 / sum w)^(1/5)
// ComputeXbar (section 4.4)
inline constexpr double kCrossValScale   = 0.16;     // 0.16 * max(x, d)^-2
inline constexpr double kFastCrossFloor  = 0.06;     // max(d, max(0.06, 0.75x))
inline constexpr double kFastCrossSlope  = 0.75;
inline constexpr double kFastCrossScale  = 0.4;      // 0.4 * (...)^-2 - 80
inline constexpr double kFastCrossShift  = 80.0;
// ComputePbar (section 4.6)
inline constexpr double kStreamBoosterScale = 1.7e-7; // 1 + 1.7e-7*(r-160)*(r-360)^2
inline constexpr double kStreamBoosterLo    = 160.0;
inline constexpr double kStreamBoosterHi    = 360.0;
inline constexpr double kStreamBoosterRatio = 7.5;    // ratio = 7.5 / delta
inline constexpr double kPbarLnWeight       = 0.006;  // v = 1 + 6*0.001*LNSum
inline constexpr double kPbarPeakScale      = 0.08;   // (0.08/x * (1 - 24/x*(...)^2))^(1/4)
inline constexpr double kPbarPeakCurv       = 24.0;
inline constexpr double kPbarDeltaFrac      = 2.0 / 3.0;  // delta < 2x/3 branch point
inline constexpr double kPbarSpikeScale     = 0.02;   // 1000 * (0.02*(4/x - 24))^(1/4)
inline constexpr double kPbarSpikeMs        = 1000.0;
inline constexpr double kPbarAnchorGuard    = 10.0;   // min(inc*anchor, max(inc, 2*inc - 10))
// ComputeAbar (section 4.7)
inline constexpr double kAbarSmallD      = 0.02;     // d < 0.02 branch
inline constexpr double kAbarLargeD      = 0.07;     // d < 0.07 branch
inline constexpr double kAbarSmallBase   = 0.75;     // min(0.75 + 0.5*maxDelta, 1)
inline constexpr double kAbarSmallSlope  = 0.5;
inline constexpr double kAbarLargeBase   = 0.65;     // min(0.65 + 5d + 0.5*maxDelta, 1)
inline constexpr double kAbarLargeSlope  = 5.0;
inline constexpr double kAbarDeltaBias   = 0.4;      // d = |d0-d1| + 0.4*max(0, max(d)-0.11)
inline constexpr double kAbarDeltaSoft   = 0.11;
inline constexpr int    kAbarSmoothMs    = 250;      // smoothing window (avg)
// LNBodies / ComputeRbar (section 4.5/4.8)
inline constexpr double kLnBodyFirstMs    = 60.0;    // t0 = min(head+60, tail): +1.3
inline constexpr double kLnBodySecondMs   = 120.0;   // t1 = min(head+120, tail): -0.3
inline constexpr double kLnBodyFirstAdd   = 1.3;
inline constexpr double kLnBodySecondAdd  = -1.3 + 1.0;  // exactly the C# expression (-1.3 + 1), NOT -0.3 (different double!)
inline constexpr double kLnBodyTailAdd    = -1.0;
inline constexpr double kLnBodySaturation = 2.5;     // v = min(curr, 2.5 + 0.5*curr)
inline constexpr double kReleaseCenterMs  = 80.0;    // |tail - head - 80| / x windows
inline constexpr double kReleaseSlope     = 5.0;     // exp(-5*(i - 0.75))
inline constexpr double kReleaseMidpoint  = 0.75;
inline constexpr double kRbarScale        = 0.08;    // 0.08 * dr^-0.5 * (1/x)
inline constexpr double kRbarReleaseScale = 0.8;     // (1 + 0.8*(I_i + I_i+1))
// ComputeCAndKs (section 4.9)
inline constexpr int    kCWindowMs = 500;            // C(s) = heads within [s-500, s+500)
// Final aggregation (section 5)
inline constexpr double kFinalJackCapBase   = 8.0;    // min(jBar, 8 + 0.85*jBar)
inline constexpr double kFinalJackCapSlope  = 0.85;
inline constexpr double kFinalStreamWeight  = 0.8;    // 0.8*pBar + rBar*35/(C + 8)
inline constexpr double kFinalReleaseWeight = 35.0;
inline constexpr double kFinalReleaseDiv    = 8.0;
inline constexpr double kFinalT1Weight      = 0.4;    // (0.4*T1^1.5 + 0.6*T2^1.5)^(2/3)
inline constexpr double kFinalT2Weight      = 0.6;
inline constexpr double kFinalSCoeff        = 2.7;    // D = 2.7*S^0.5*T^1.5 + 0.27*S
inline constexpr double kFinalDCoeff        = 0.27;
inline constexpr double kFinalP93Weight     = 0.88;   // 0.88*p93*0.25 + 0.94*p83*0.2 + wm*0.55
inline constexpr double kFinalP93Share      = 0.25;
inline constexpr double kFinalP83Weight     = 0.94;
inline constexpr double kFinalP83Share      = 0.2;
inline constexpr double kFinalMeanShare     = 0.55;
inline constexpr double kFinalLengthDiv     = 60.0;   // SR *= totalNotes/(totalNotes + 60)
inline constexpr double kLnBonusCapMs       = 1000.0; // min(LN length, 1000) / 200 * 0.5
inline constexpr double kLnBonusDiv         = 200.0;
inline constexpr double kLnBonusHalf        = 0.5;
inline constexpr double kRescaleHighFrom    = 9.0;    // SR > 9 -> 9 + (SR - 9)/1.2
inline constexpr double kRescaleHighDiv     = 1.2;
inline constexpr double kFinalMultiplier    = 0.975;  // SR *= 0.975
}  // namespace srr_sr_detail

// Full-map SRR. Returns -1 when uncomputable: keyCount < 1, keyCount > 10 (kMaxKeys),
// fewer than 20 notes, a column outside [0, keyCount), or a non-finite intermediate.
// Never returns NaN/Inf.
double SrrStarRating(int keyCount, double od, const std::vector<SrNote>& notes);

// Local SRR: keeps the notes whose headMs lies in [startMs - 500, endMs + 500]
// (tails are preserved unchanged) and then runs the exact same pipeline and guards as
// SrrStarRating on the selected notes.
double SrrStarRatingLocal(int keyCount, double od, const std::vector<SrNote>& notes,
                          int startMs, int endMs);

// Intermediate state of the pipeline, exposed for the fixture cross-check test
// (mirrors the locals dumped by StarRatingRebirth.Tests/dump.py into 1.json).
// Bars suffixed "All" are the values interpolated onto allCorners, matching what the
// JSON fixture stores (the C# test compares the interpolated variants).
struct SrrInternals {
    double x = 0.0;
    int K = 0;
    int T = 0;
    std::vector<SrNote> noteSeq;
    std::vector<SrNote> lnSeq;
    std::vector<SrNote> tailSeq;
    std::vector<std::vector<SrNote>> noteSeqByCol;
    std::vector<std::vector<SrNote>> lnSeqByCol;
    std::vector<int> baseCorners;
    std::vector<int> aCorners;
    std::vector<int> allCorners;
    std::vector<unsigned char> keyUsage;             // K x baseCorners, row-major
    std::vector<std::vector<int>> activeColumns;     // per baseCorner
    std::vector<double> keyUsage400;                 // K x baseCorners, row-major
    std::vector<double> anchor;                      // baseCorners
    std::vector<double> deltaKs;                     // K x baseCorners, row-major
    std::vector<double> jBar;                        // baseCorners (raw)
    std::vector<double> jBarAll;                     // allCorners (interpolated)
    std::vector<double> xBar;
    std::vector<double> xBarAll;
    std::vector<double> pBar;
    std::vector<double> pBarAll;
    std::vector<double> aBar;                        // aCorners (raw)
    std::vector<double> aBarAll;                     // allCorners (interpolated)
    std::vector<double> rBar;
    std::vector<double> rBarAll;
    std::vector<int> lnPoints;                       // LN sparse representation
    std::vector<double> lnCumsum;
    std::vector<double> lnValues;
    std::vector<double> cArr;                        // allCorners (step-interpolated)
    std::vector<double> ksArr;                       // allCorners (step-interpolated)
};

// Runs the SRR pipeline and fills p out. Returns false on the same conditions that make
// SrrStarRating return -1 (invalid keyCount / note count / column range).
bool SrrComputeInternals(int keyCount, double od, const std::vector<SrNote>& notes,
                         SrrInternals* out);

}  // namespace mra
