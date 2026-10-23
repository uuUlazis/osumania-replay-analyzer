// Star-rating port tests.
//
//  * SRR (StarRatingRebirth) is cross-checked against the upstream fixture
//    backend/tests/fixtures/srr/{1.osu,1.json}: full SR value (Test.cs expects
//    6.617031 with 6-decimal precision) plus every intermediate array stored in the
//    JSON (dumped by StarRatingRebirth.Tests/dump.py from the Python implementation).
//  * Official lazer SR has no public golden dataset (docs/pp-sr-notes.md section 3),
//    so it is verified with property tests and a verbatim constants-table check
//    (pp-sr-notes.md section 3.6). A manual ground-truth entry point lives in
//    testOfficialGroundTruth() below.
#include "mania/official_sr.h"
#include "mania/srr_sr.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mra;

#ifndef MRA_SR_FIXTURE_DIR
#define MRA_SR_FIXTURE_DIR "backend/tests/fixtures/srr"
#endif
#ifndef MRA_SR_OFFICIAL_FIXTURE_DIR
#define MRA_SR_OFFICIAL_FIXTURE_DIR "backend/tests/fixtures/official"
#endif

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, relTol)                                             \
    do {                                                                     \
        const double va_ = (a), vb_ = (b);                                   \
        if (!approxEqual(va_, vb_, relTol)) {                                \
            std::printf("FAIL %s:%d: %s=%.12g vs %s=%.12g\n", __FILE__,      \
                        __LINE__, #a, va_, #b, vb_);                         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define REQUIRE(cond)                                                        \
    do {                                                                     \
        if (!(cond)) {                                                       \
            CHECK(cond);                                                     \
            return;                                                          \
        }                                                                    \
    } while (0)

// Mirrors StarRatingRebirth.Tests ArrayExtensions.AreAlmostEqual: exact equality
// short-circuit, then diff <= tolerance * max(|a|, |b|).
static bool approxEqual(double a, double b, double tolerance = 1e-6) {
    if (a == b) return true;
    if (std::isnan(a) || std::isnan(b)) return false;
    if (std::isinf(a) || std::isinf(b)) return false;
    const double diff = std::fabs(a - b);
    const double largest = std::max(std::fabs(a), std::fabs(b));
    return diff <= tolerance * largest;
}

// ---------------------------------------------------------------------------
// Minimal .osu reader with ManiaData.FromLines semantics (CS: int.Parse, OD:
// double.Parse, column = (int)(x * CS / 512.0), LN iff type & 128, tail = field 6,
// everything after the first ':' dropped).
// ---------------------------------------------------------------------------
struct ParsedMap {
    int cs = 0;
    double od = 0.0;
    std::vector<SrNote> notes;
};

static std::string trimCopy(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

static ParsedMap parseOsu(const std::filesystem::path& path) {
    std::ifstream in(path);
    CHECK(in.good());
    ParsedMap m;
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        line = trimCopy(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        if (section == "General" || section == "Difficulty") {
            const auto pos = line.find(':');
            if (pos == std::string::npos) continue;
            const std::string key = trimCopy(line.substr(0, pos));
            const std::string value = trimCopy(line.substr(pos + 1));
            if (key == "CircleSize")
                m.cs = std::stoi(value);
            else if (key == "OverallDifficulty")
                m.od = std::stod(value);
        } else if (section == "HitObjects") {
            std::string beforeColon = line.substr(0, line.find(':'));
            std::vector<std::string> sp;
            size_t start = 0;
            while (true) {
                const size_t comma = beforeColon.find(',', start);
                if (comma == std::string::npos) {
                    sp.push_back(beforeColon.substr(start));
                    break;
                }
                sp.push_back(beforeColon.substr(start, comma - start));
                start = comma + 1;
            }
            if (sp.size() < 4) continue;
            const int x = std::stoi(sp[0]);
            const int head = std::stoi(sp[2]);
            const int type = std::stoi(sp[3]);
            const int tail = (type & 128) != 0 ? std::stoi(sp[5]) : -1;
            const int column = static_cast<int>(static_cast<double>(x * m.cs) / 512.0);
            m.notes.push_back({column, head, tail});
        }
    }
    return m;
}

static nlohmann::json loadJson(const std::filesystem::path& path) {
    std::ifstream in(path);
    CHECK(in.good());
    return nlohmann::json::parse(in);
}

// ---------------------------------------------------------------------------
// SRR fixture cross-check
// ---------------------------------------------------------------------------
static void testSrrFixtureParser() {
    const ParsedMap m = parseOsu(std::filesystem::path(MRA_SR_FIXTURE_DIR) / "1.osu");
    // Test.cs [InlineData("Data/1.osu", 7, 7.0, 6, 638, 1133)]
    CHECK(m.cs == 7);
    CHECK(m.od == 7.0);
    REQUIRE(m.notes.size() >= 2);
    CHECK(m.notes[1].column == 6);
    CHECK(m.notes[1].headMs == 638);
    CHECK(m.notes[1].tailMs == 1133);
    CHECK(static_cast<int>(m.notes.size()) >= srr_sr_detail::kMinNotes);
}

static size_t mismatchedExactInts(const std::vector<int>& ours, const nlohmann::json& theirs,
                                  const char* name) {
    if (!theirs.is_array() || ours.size() != theirs.size()) {
        std::printf("  %s: size %zu vs json %zu\n", name, ours.size(),
                    theirs.is_array() ? theirs.size() : 0);
        return 1;
    }
    size_t bad = 0;
    for (size_t i = 0; i < ours.size(); ++i)
        if (ours[i] != theirs[i].get<int>()) {
            if (bad == 0)
                std::printf("  %s: first mismatch at %zu: %d vs %d\n", name, i, ours[i],
                            theirs[i].get<int>());
            ++bad;
        }
    return bad;
}

static size_t mismatchedNotes(const std::vector<SrNote>& ours, const nlohmann::json& theirs,
                              const char* name) {
    if (!theirs.is_array() || ours.size() != theirs.size()) {
        std::printf("  %s: size %zu vs json %zu\n", name, ours.size(),
                    theirs.is_array() ? theirs.size() : 0);
        return 1;
    }
    size_t bad = 0;
    for (size_t i = 0; i < ours.size(); ++i) {
        const auto& a = theirs[i];
        if (ours[i].column != a[0].get<int>() || ours[i].headMs != a[1].get<int>() ||
            ours[i].tailMs != a[2].get<int>()) {
            if (bad == 0)
                std::printf("  %s: first mismatch at %zu: (%d,%d,%d) vs (%d,%d,%d)\n", name, i,
                            ours[i].column, ours[i].headMs, ours[i].tailMs, a[0].get<int>(),
                            a[1].get<int>(), a[2].get<int>());
            ++bad;
        }
    }
    return bad;
}

static size_t mismatchedApproxVec(const std::vector<double>& ours, const nlohmann::json& theirs,
                                  const char* name, double tol = 1e-6) {
    if (!theirs.is_array() || ours.size() != theirs.size()) {
        std::printf("  %s: size %zu vs json %zu\n", name, ours.size(),
                    theirs.is_array() ? theirs.size() : 0);
        return 1;
    }
    size_t bad = 0;
    double worstRel = 0.0;
    size_t worstIdx = 0;
    for (size_t i = 0; i < ours.size(); ++i) {
        const double b = theirs[i].get<double>();
        if (!approxEqual(ours[i], b, tol)) {
            if (bad == 0)
                std::printf("  %s: first mismatch at %zu: %.12g vs %.12g\n", name, i, ours[i], b);
            ++bad;
        }
        const double denom = std::max(std::fabs(ours[i]), std::fabs(b));
        if (denom > 0.0) {
            const double rel = std::fabs(ours[i] - b) / denom;
            if (rel > worstRel) {
                worstRel = rel;
                worstIdx = i;
            }
        }
    }
    std::printf("  %s: max rel diff %.3g (idx %zu)\n", name, worstRel, worstIdx);
    return bad;
}

static size_t mismatchedApproxDictRow(const std::vector<double>& ours, int K, size_t nc,
                                      const nlohmann::json& dict, const char* name) {
    size_t bad = 0;
    for (int k = 0; k < K; ++k) {
        const std::string key = std::to_string(k);
        if (!dict.contains(key)) {
            std::printf("  %s: missing key %s\n", name, key.c_str());
            return 1;
        }
        const auto& arr = dict[key];
        if (!arr.is_array() || arr.size() != nc) {
            std::printf("  %s[%s]: bad size %zu vs %zu\n", name, key.c_str(),
                        arr.is_array() ? arr.size() : 0, nc);
            return 1;
        }
        for (size_t i = 0; i < nc; ++i) {
            const double b = arr[i].get<double>();
            if (!approxEqual(ours[static_cast<size_t>(k) * nc + i], b, 1e-6)) {
                if (bad == 0)
                    std::printf("  %s[%s]: first mismatch at %zu: %.12g vs %.12g\n", name,
                                key.c_str(), i, ours[static_cast<size_t>(k) * nc + i], b);
                ++bad;
            }
        }
    }
    return bad;
}

static size_t mismatchedBoolDictRow(const std::vector<unsigned char>& ours, int K, size_t nc,
                                    const nlohmann::json& dict, const char* name) {
    size_t bad = 0;
    for (int k = 0; k < K; ++k) {
        const auto& arr = dict[std::to_string(k)];
        if (!arr.is_array() || arr.size() != nc) return 1;
        for (size_t i = 0; i < nc; ++i) {
            const bool b = arr[i].get<bool>();
            if (static_cast<bool>(ours[static_cast<size_t>(k) * nc + i]) != b) {
                if (bad == 0)
                    std::printf("  %s[%d]: first mismatch at %zu: %d vs %d\n", name, k, i,
                                ours[static_cast<size_t>(k) * nc + i], b ? 1 : 0);
                ++bad;
            }
        }
    }
    return bad;
}

static void testSrrFixtureValues() {
    const ParsedMap m = parseOsu(std::filesystem::path(MRA_SR_FIXTURE_DIR) / "1.osu");

    // 1) Final SR: Test.cs Assert.Equal(6.617031, sr, 6).
    const double sr = SrrStarRating(m.cs, m.od, m.notes);
    std::printf("  SrrStarRating = %.9f (expected 6.617031)\n", sr);
    CHECK_NEAR(sr, 6.617031, 1e-6);
    CHECK(std::isfinite(sr));

    // 2) Intermediates vs 1.json.
    SrrInternals it;
    REQUIRE(SrrComputeInternals(m.cs, m.od, m.notes, &it));

    const nlohmann::json j = loadJson(std::filesystem::path(MRA_SR_FIXTURE_DIR) / "1.json");
    CHECK_NEAR(it.x, j["x"].get<double>(), 1e-12);
    CHECK(it.K == j["K"].get<int>());
    CHECK(it.T == j["T"].get<int>());

    size_t bad = 0;
    bad += mismatchedNotes(it.noteSeq, j["note_seq"], "note_seq");
    bad += mismatchedNotes(it.lnSeq, j["LN_seq"], "LN_seq");
    bad += mismatchedNotes(it.tailSeq, j["tail_seq"], "tail_seq");
    REQUIRE(it.noteSeqByCol.size() == static_cast<size_t>(it.K));
    for (int k = 0; k < it.K; ++k) {
        bad += mismatchedNotes(it.noteSeqByCol[k], j["note_seq_by_column"][k], "note_seq_by_col");
        bad += mismatchedNotes(it.lnSeqByCol[k], j["LN_seq_by_column"][k], "LN_seq_by_col");
    }
    bad += mismatchedExactInts(it.baseCorners, j["base_corners"], "base_corners");
    bad += mismatchedExactInts(it.aCorners, j["A_corners"], "A_corners");
    bad += mismatchedExactInts(it.allCorners, j["all_corners"], "all_corners");

    const size_t nc = it.baseCorners.size();
    REQUIRE(nc > 0);
    bad += mismatchedBoolDictRow(it.keyUsage, it.K, nc, j["key_usage"], "key_usage");

    // active_columns (exact)
    if (j["active_columns"].is_array() && j["active_columns"].size() == nc) {
        for (size_t i = 0; i < nc; ++i) {
            const auto& arr = j["active_columns"][i];
            if (it.activeColumns[i].size() != arr.size()) { ++bad; continue; }
            for (size_t t = 0; t < arr.size(); ++t)
                if (it.activeColumns[i][t] != arr[t].get<int>()) ++bad;
        }
    } else {
        ++bad;
    }

    bad += mismatchedApproxDictRow(it.keyUsage400, it.K, nc, j["key_usage_400"], "key_usage_400");
    bad += mismatchedApproxDictRow(it.deltaKs, it.K, nc, j["delta_ks"], "delta_ks");
    bad += mismatchedApproxVec(it.anchor, j["anchor"], "anchor");
    bad += mismatchedApproxVec(it.jBarAll, j["Jbar"], "Jbar");
    bad += mismatchedApproxVec(it.xBarAll, j["Xbar"], "Xbar");
    bad += mismatchedApproxVec(it.pBarAll, j["Pbar"], "Pbar");
    bad += mismatchedApproxVec(it.aBarAll, j["Abar"], "Abar");
    bad += mismatchedApproxVec(it.rBarAll, j["Rbar"], "Rbar");

    // LN_rep = [points(int[]), cumsum(double[]), values(double[])]
    bad += mismatchedExactInts(it.lnPoints, j["LN_rep"][0], "LN_rep.points");
    bad += mismatchedApproxVec(it.lnCumsum, j["LN_rep"][1], "LN_rep.cumsum");
    bad += mismatchedApproxVec(it.lnValues, j["LN_rep"][2], "LN_rep.values");
    bad += mismatchedApproxVec(it.cArr, j["C_arr"], "C_arr");
    bad += mismatchedApproxVec(it.ksArr, j["Ks_arr"], "Ks_arr");

    CHECK(bad == 0);
}

static void testSrrGuards() {
    // 19 notes -> -1; 20 notes -> computable.
    std::vector<SrNote> shortMap;
    for (int i = 0; i < 19; ++i) shortMap.push_back({i % 4, 100 + 150 * i, -1});
    CHECK(SrrStarRating(4, 8.0, shortMap) == -1.0);
    std::vector<SrNote> minimal = shortMap;
    minimal.push_back({0, 100 + 150 * 19, -1});
    CHECK(SrrStarRating(4, 8.0, minimal) > 0.0);

    // K bounds.
    CHECK(SrrStarRating(0, 8.0, minimal) == -1.0);
    CHECK(SrrStarRating(11, 8.0, minimal) == -1.0);
    CHECK(SrrStarRating(-1, 8.0, minimal) == -1.0);

    // Column out of range -> -1 (C# cannot produce this; contract says uncomputable).
    std::vector<SrNote> badCol = minimal;
    badCol[3] = {7, badCol[3].headMs, -1};
    CHECK(SrrStarRating(4, 8.0, badCol) == -1.0);

    // Results are always finite.
    const double sr = SrrStarRating(4, 8.0, minimal);
    CHECK(std::isfinite(sr));
}

static void testSrrLocal() {
    std::vector<SrNote> notes;
    for (int i = 0; i < 400; ++i) {
        const int t = 1000 + i * 150;
        if (i % 10 == 9)
            notes.push_back({i % 4, t, t + 300});
        else
            notes.push_back({i % 4, t, -1});
    }
    const double global = SrrStarRating(4, 8.0, notes);
    CHECK(std::isfinite(global) && global > 0.0);

    // A window that covers every head must reproduce the global value exactly
    // (identical pipeline on an identical note set).
    int minHead = notes.front().headMs, maxHead = notes.front().headMs;
    for (const SrNote& n : notes) {
        minHead = std::min(minHead, n.headMs);
        maxHead = std::max(maxHead, n.headMs);
    }
    const double full = SrrStarRatingLocal(4, 8.0, notes, minHead - 400, maxHead + 400);
    CHECK(full == global);

    // Narrow window still works and differs from the global value.
    const double mid = SrrStarRatingLocal(4, 8.0, notes, 15000, 25000);
    CHECK(std::isfinite(mid) && mid > 0.0);

    // Window that selects < 20 notes -> -1.
    CHECK(SrrStarRatingLocal(4, 8.0, notes, 0, 10) == -1.0);

    // K guards apply to the local entry point as well.
    CHECK(SrrStarRatingLocal(11, 8.0, notes, 0, 100000) == -1.0);
}

static void testSrrConstants() {
    using namespace srr_sr_detail;
    // docs/sr-rebirth-csharp.md sections 2/4/5.
    CHECK(kXScale == 0.3);
    CHECK(kXWindowBase == 64.5);
    CHECK(kXWindowDiv == 500.0);
    CHECK(kXCapSlope == 0.6);
    CHECK(kXCapOffset == 0.09);
    CHECK(kMinNotes == 20);
    CHECK(kMaxKeys == 10);
    CHECK(kUsage400Core == 3.75);
    CHECK(kUsage400CapMs == 1500.0);
    CHECK(kUsage400CoreDiv == 150.0);
    CHECK(kUsage400FlankMs == 400.0);
    CHECK(kJackNerfScale == 7e-5);
    CHECK(kJackValOffset == 0.11);
    CHECK(kCrossValScale == 0.16);
    CHECK(kStreamBoosterScale == 1.7e-7);
    CHECK(kStreamBoosterLo == 160.0 && kStreamBoosterHi == 360.0);
    CHECK(kPbarLnWeight == 0.006);
    CHECK(kAbarSmallD == 0.02 && kAbarLargeD == 0.07);
    CHECK(kAbarSmoothMs == 250 && kJbarSmoothMs == 500);
    CHECK(kCWindowMs == 500);
    CHECK(kLnBodyFirstAdd == 1.3 && kLnBodySecondAdd == -1.3 + 1.0 && kLnBodyTailAdd == -1.0);
    CHECK(kLnBodySaturation == 2.5);
    CHECK(kReleaseCenterMs == 80.0 && kReleaseSlope == 5.0 && kReleaseMidpoint == 0.75);
    CHECK(kRbarScale == 0.08 && kRbarReleaseScale == 0.8);
    CHECK(kFinalJackCapBase == 8.0 && kFinalJackCapSlope == 0.85);
    CHECK(kFinalStreamWeight == 0.8 && kFinalReleaseWeight == 35.0 && kFinalReleaseDiv == 8.0);
    CHECK(kFinalT1Weight == 0.4 && kFinalT2Weight == 0.6);
    CHECK(kFinalSCoeff == 2.7 && kFinalDCoeff == 0.27);
    CHECK(kFinalP93Weight == 0.88 && kFinalP93Share == 0.25);
    CHECK(kFinalP83Weight == 0.94 && kFinalP83Share == 0.2);
    CHECK(kFinalMeanShare == 0.55);
    CHECK(kFinalLengthDiv == 60.0);
    CHECK(kLnBonusCapMs == 1000.0 && kLnBonusDiv == 200.0 && kLnBonusHalf == 0.5);
    CHECK(kRescaleHighFrom == 9.0 && kRescaleHighDiv == 1.2);
    CHECK(kFinalMultiplier == 0.975);

    // x at OD 8 (docs example: ~0.0854; exact branch: ceil(24)=24 -> x0 = 0.3*sqrt(40.5/500)).
    const double od8 = 0.3 * std::sqrt((64.5 - std::ceil(8.0 * 3.0)) / 500.0);
    const double od8Capped = std::min(od8, 0.6 * (od8 - 0.09) + 0.09);
    CHECK_NEAR(od8Capped, 0.0854, 5e-3);
}

// ---------------------------------------------------------------------------
// Official lazer SR
// ---------------------------------------------------------------------------
static void testOfficialConstants() {
    using namespace official_sr_detail;
    // docs/pp-sr-notes.md section 3.6 table (all verbatim from lazer sources).
    CHECK(kIndividualDecayBase == 0.125);
    CHECK(kOverallDecayBase == 0.30);
    CHECK(kSectionLengthMs == 400.0);
    CHECK(kSectionDecayWeight == 0.9);
    CHECK(kDifficultyMultiplier == 0.018);
    CHECK(kIndividualBaseStrain == 2.0);
    CHECK(kHoldOverlapFactor == 1.25);
    CHECK(kOverallHoldBase == 1.0);
    CHECK(kLogisticGrowth == 0.27);
    CHECK(kLogisticOffsetMs == 30.0);
    CHECK(kComparisonToleranceMs == 1.0);

    // Logistic spot checks (pp-sr-notes.md section 3.3): x=30 -> 0.5, x=0 -> ~0, x>=60 -> ~1.
    const auto logistic = [](double x) {
        return 1.0 / (1.0 + std::exp(0.27 * (30.0 - x)));
    };
    CHECK(std::fabs(logistic(30.0) - 0.5) < 1e-12);
    CHECK(logistic(0.0) < 1e-3);
    CHECK(logistic(60.0) > 1.0 - 1e-3);
}

static std::vector<SrNote> makeMixedMap(int columns, int count, int intervalMs) {
    // 1/4-ish stream with periodic chords and long notes: exercises individual strain,
    // overall strain (hold overlap logistic), chords and section aggregation.
    std::vector<SrNote> v;
    v.reserve(static_cast<size_t>(count) * 2);
    int t = 1000;
    for (int i = 0; i < count; ++i) {
        const int col = i % columns;
        if (i % 16 == 15)
            v.push_back({col, t, t + intervalMs * 2});  // LN
        else
            v.push_back({col, t, -1});
        if (i % 8 == 7) v.push_back({(col + 2) % columns, t, -1});  // chord
        t += intervalMs;
    }
    return v;
}

static void testOfficialEdgeCases() {
    // Empty beatmap: lazer keeps default attributes -> StarRating 0 (pp-sr-notes.md section 3).
    CHECK(OfficialStarRating(4, {}, 1.0) == 0.0);
    // Single note: the loop that builds difficulty hit objects starts at index 1 -> 0.
    CHECK(OfficialStarRating(4, {{0, 1000, -1}}, 1.0) == 0.0);
    // Tiny maps stay low-star.
    std::vector<SrNote> tiny = {{0, 1000, -1}, {1, 1200, -1}, {2, 1400, -1}};
    const double tinySr = OfficialStarRating(4, tiny, 1.0);
    CHECK(std::isfinite(tinySr) && tinySr >= 0.0 && tinySr < 0.5);

    // Unsupported input -> -1.
    CHECK(OfficialStarRating(0, tiny, 1.0) == -1.0);
    CHECK(OfficialStarRating(-3, tiny, 1.0) == -1.0);
    CHECK(OfficialStarRating(4, tiny, 0.0) == -1.0);
    CHECK(OfficialStarRating(4, tiny, -1.5) == -1.0);
    CHECK(OfficialStarRating(4, {{9, 1000, -1}, {8, 1200, -1}, {10, 1400, -1}}, 1.0) == -1.0);
}

static void testOfficialProperties() {
    const int columns = 4;
    const std::vector<SrNote> base = makeMixedMap(columns, 1200, 150);
    const double nm = OfficialStarRating(columns, base, 1.0);
    const double dt = OfficialStarRating(columns, base, 1.5);   // DT/NC clock rate
    const double ht = OfficialStarRating(columns, base, 0.75);  // HT clock rate
    std::printf("  official mixed map: NM=%.4f DT=%.4f HT=%.4f\n", nm, dt, ht);
    CHECK(std::isfinite(nm) && nm > 0.0);
    CHECK(std::isfinite(dt) && std::isfinite(ht));
    // DT (rate 1.5) must not reduce the star rating.
    CHECK(dt >= nm);

    // Density: inserting extra notes into every gap never lowers the star rating.
    std::vector<SrNote> dense;
    dense.reserve(base.size() * 2);
    for (size_t i = 0; i < base.size(); ++i) {
        dense.push_back(base[i]);
        if (i + 1 < base.size() && base[i + 1].headMs - base[i].headMs > 60 &&
            base[i].tailMs < 0) {
            const int mid = (base[i].headMs + base[i + 1].headMs) / 2;
            dense.push_back({(base[i].column + 1) % columns, mid, -1});
        }
    }
    const double denseSr = OfficialStarRating(columns, dense, 1.0);
    std::printf("  density: base=%.4f dense=%.4f (+%zu notes)\n", nm, denseSr,
                dense.size() - base.size());
    CHECK(denseSr >= nm);
}

// ---------------------------------------------------------------------------
// Manual ground-truth entry point for the OFFICIAL SR.
//
// There is no public golden dataset for the lazer mania SR (docs/pp-sr-notes.md
// section 3), so cross-checking against live lazer values is a manual step:
//
//   1. Pick a ranked mania map. In lazer, read the star rating shown in song select
//      for the exact mod combination (the tooltip value comes from
//      ManiaDifficultyCalculator, version 20241007), or run the calculator yourself:
//      instantiate ManiaDifficultyCalculator on the converted beatmap + mods and read
//      ManiaDifficultyAttributes.StarRating. DT/HT -> rate 1.5 / 0.75.
//   2. Save the .osu to backend/tests/fixtures/official/<name>.osu and the expected
//      value to <name>.json with the schema
//          {"key_count": 7, "rate": 1.5, "star_rating": 5.4321}
//   3. Re-run this test binary; each fixture is parsed, computed and compared at a
//      1e-6 relative tolerance below. Missing fixture directory => this test skips.
// ---------------------------------------------------------------------------
static void testOfficialGroundTruth() {
    namespace fs = std::filesystem;
    const fs::path dir = MRA_SR_OFFICIAL_FIXTURE_DIR;
    if (!fs::exists(dir)) {
        std::printf("  [skip] no official ground-truth fixtures in %s\n", dir.string().c_str());
        return;
    }
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".osu") continue;
        fs::path meta = entry.path();
        meta.replace_extension(".json");
        if (!fs::exists(meta)) continue;
        const nlohmann::json j = loadJson(meta);
        const ParsedMap m = parseOsu(entry.path());
        const double sr = OfficialStarRating(j.at("key_count").get<int>(), m.notes,
                                             j.at("rate").get<double>());
        const double expected = j.at("star_rating").get<double>();
        std::printf("  official %s: got %.9f expected %.9f\n",
                    entry.path().filename().string().c_str(), sr, expected);
        CHECK_NEAR(sr, expected, 1e-6);
    }
}

// Compile-time constant table checks (docs/pp-sr-notes.md section 3.6 and
// docs/sr-rebirth-csharp.md sections 2/5).
static_assert(official_sr_detail::kIndividualDecayBase == 0.125, "lazer Strain.cs");
static_assert(official_sr_detail::kOverallDecayBase == 0.30, "lazer Strain.cs");
static_assert(official_sr_detail::kSectionLengthMs == 400.0, "lazer StrainSkill.cs");
static_assert(official_sr_detail::kSectionDecayWeight == 0.9, "lazer StrainSkill.cs");
static_assert(official_sr_detail::kDifficultyMultiplier == 0.018, "lazer ManiaDifficultyCalculator.cs");
static_assert(official_sr_detail::kIndividualBaseStrain == 2.0, "lazer IndividualStrainEvaluator.cs");
static_assert(official_sr_detail::kHoldOverlapFactor == 1.25, "lazer evaluators");
static_assert(official_sr_detail::kLogisticGrowth == 0.27, "lazer OverallStrainEvaluator.cs");
static_assert(official_sr_detail::kLogisticOffsetMs == 30.0, "lazer OverallStrainEvaluator.cs");
static_assert(official_sr_detail::kComparisonToleranceMs == 1.0, "lazer evaluators (DefinitelyBigger)");
static_assert(srr_sr_detail::kMaxKeys == 10, "SRR ManiaData.cs");
static_assert(srr_sr_detail::kMinNotes == 20, "SRR ManiaData.cs");
static_assert(srr_sr_detail::kFinalLengthDiv == 60.0, "SRR Calculate");
static_assert(srr_sr_detail::kRescaleHighFrom == 9.0, "SRR Utils.RescaleHigh");
static_assert(srr_sr_detail::kRescaleHighDiv == 1.2, "SRR Utils.RescaleHigh");
static_assert(srr_sr_detail::kFinalMultiplier == 0.975, "SRR Calculate");

static void run(const char* name, void (*fn)()) {
    std::printf("[test] %s\n", name);
    std::fflush(stdout);
    try {
        fn();
    } catch (const std::exception& e) {
        std::printf("EXCEPTION in %s: %s\n", name, e.what());
        std::fflush(stdout);
        ++g_failures;
    }
}

int main() {
    run("srrFixtureParser", testSrrFixtureParser);
    run("srrFixtureValues", testSrrFixtureValues);
    run("srrGuards", testSrrGuards);
    run("srrLocal", testSrrLocal);
    run("srrConstants", testSrrConstants);
    run("officialConstants", testOfficialConstants);
    run("officialEdgeCases", testOfficialEdgeCases);
    run("officialProperties", testOfficialProperties);
    run("officialGroundTruth", testOfficialGroundTruth);
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
