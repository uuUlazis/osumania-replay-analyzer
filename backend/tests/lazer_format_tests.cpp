// L1 lazer 数据通道: lazer_format.h 单元测试。
//
// 覆盖 (调研口径见 lazer/lazer_format.h 注释):
//   - version gate 边界 (29999999 / 30000000);
//   - parseLazerExtraJson: statistics 大小写两形态、IgnoreHit/ComboBreak 过滤、
//     mods settings 原文保留、缺键/坏 JSON 容错;
//   - 305 acc 手算对拍 (合成数据);
//   - parseLazerExtra 压缩路径: gen_lazer_blob.py 离线生成的真实 LZMA blob
//     (lazer_tail_blob.h), 以及长度帧截断/超界/坏 props 容错。
//
// 单独 console target (mra_tests_lazer_format): mra_tests 的 main 归
// test_mania_core.cpp, 沿用 mra_tests_star_rating / mra_tests_replay_lanes 的
// 既有做法。合成数据随意, 不含真实玩家名/成绩 (红线)。
#include "lazer/lazer_format.h"
#include "lazer_tail_blob.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace mra;

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        const long long _va = (long long)(a);                                \
        const long long _vb = (long long)(b);                                \
        if (_va != _vb) {                                                    \
            std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__,   \
                        __LINE__, #a, #b, _va, _vb);                         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                \
    do {                                                                     \
        const double _va = (a);                                              \
        const double _vb = (b);                                              \
        if (!std::isfinite(_va) || std::fabs(_va - _vb) > (eps)) {           \
            std::printf("FAIL %s:%d: %s ~= %s (%.9f vs %.9f)\n", __FILE__,   \
                        __LINE__, #a, #b, _va, _vb);                         \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

namespace {

// ---- 合成 statistics JSON (三种大小写形态 + 过滤项) --------------------------

constexpr const char* kStatsPascal =
    R"({"statistics":{"Perfect":850,"Great":120,"Good":20,"Ok":8,"Meh":1,"Miss":1,"IgnoreHit":5,"ComboBreak":2}})";
constexpr const char* kStatsLower =
    R"({"statistics":{"perfect":850,"great":120,"good":20,"ok":8,"meh":1,"miss":1,"ignorehit":5,"combobreak":2}})";
constexpr const char* kStatsUpper =
    R"({"statistics":{"PERFECT":850,"GREAT":120,"GOOD":20,"OK":8,"MEH":1,"MISS":1}})";

void expectSyntheticCounts(const LazerManiaCounts& c) {
    CHECK(c.fromJson);
    CHECK(c.valid);
    CHECK_EQ(c.perfect, 850);
    CHECK_EQ(c.great, 120);
    CHECK_EQ(c.good, 20);
    CHECK_EQ(c.ok, 8);
    CHECK_EQ(c.meh, 1);
    CHECK_EQ(c.miss, 1);
}

void testVersionGate() {
    CHECK(!isLazerGameVersion(0));
    CHECK(!isLazerGameVersion(20251203));  // stable 日期型版本号
    CHECK(!isLazerGameVersion(29999999));  // 边界下一格
    CHECK(isLazerGameVersion(30000000));   // FIRST_LAZER_VERSION
    CHECK(isLazerGameVersion(30000019));   // 实测 lazer 版本
    CHECK(isLazerGameVersion(30000020));
}

void testJsonCaseVariants() {
    // Pascal / Lower: 六档 + IgnoreHit + ComboBreak = 8 键; Upper 只有六档。
    for (const char* js : {kStatsPascal, kStatsLower}) {
        const LazerScoreExtra e = parseLazerExtraJson(js);
        CHECK(e.present);
        CHECK(e.error.empty());
        CHECK(e.hasStatistics);
        CHECK_EQ(e.statistics.size(), 8);
        expectSyntheticCounts(lazerManiaCountsFromStatistics(e.statistics));
    }
    {
        const LazerScoreExtra e = parseLazerExtraJson(kStatsUpper);
        CHECK(e.present);
        CHECK(e.hasStatistics);
        CHECK_EQ(e.statistics.size(), 6);
        expectSyntheticCounts(lazerManiaCountsFromStatistics(e.statistics));
    }
    // 归一化键必须全小写可查
    const LazerScoreExtra e = parseLazerExtraJson(kStatsPascal);
    auto it = e.statistics.find("ignorehit");
    CHECK(it != e.statistics.end());
    if (it != e.statistics.end()) CHECK_EQ(it->second, 5);
    CHECK_EQ(e.statistics.count("Perfect"), 0);  // 不保留原始大小写键
}

void testIgnoreHitFiltered() {
    // 只有过滤键: 六档全缺 -> valid=false
    const LazerScoreExtra e = parseLazerExtraJson(
        R"({"statistics":{"IgnoreHit":12,"ComboBreak":3}})");
    CHECK(e.hasStatistics);
    CHECK_EQ(e.statistics.size(), 2);
    const LazerManiaCounts c = lazerManiaCountsFromStatistics(e.statistics);
    CHECK(c.fromJson);
    CHECK(!c.valid);
    CHECK_EQ(c.perfect + c.great + c.good + c.ok + c.meh + c.miss, 0);

    // 空统计对象同样 valid=false
    const LazerManiaCounts empty =
        lazerManiaCountsFromStatistics(parseLazerExtraJson("{}").statistics);
    CHECK(!empty.valid);

    // 完全空 map
    const std::map<std::string, int64_t> noKeys;
    CHECK(!lazerManiaCountsFromStatistics(noKeys).valid);
}

void testModsSettingsRawText() {
    const LazerScoreExtra e = parseLazerExtraJson(
        R"({"mods":[{"acronym":"DT","settings":{"speed_change":1.25}},)"
        R"({"acronym":"RD","settings":{}},)"
        R"({"acronym":"MR"},)"
        R"({"acronym":"SF","settings":{"score_factor":12345,"hard_rock":true}}]})");
    CHECK(e.present);
    CHECK_EQ(e.mods.size(), 4);
    if (e.mods.size() == 4) {
        CHECK_EQ(e.mods[0].acronym.compare("DT"), 0);
        CHECK(e.mods[0].settings.count("speed_change") == 1);
        CHECK_EQ(e.mods[0].settings.at("speed_change").compare("1.25"), 0);
        CHECK(e.mods[1].settings.empty());
        CHECK_EQ(e.mods[2].acronym.compare("MR"), 0);
        CHECK(e.mods[2].settings.empty());
        CHECK_EQ(e.mods[3].settings.at("score_factor").compare("12345"), 0);
        CHECK_EQ(e.mods[3].settings.at("hard_rock").compare("true"), 0);
    }
    // mods 里混入非对象元素: 宽容跳过, 其余保留
    const LazerScoreExtra e2 = parseLazerExtraJson(
        R"({"mods":[{"acronym":"DT"},"junk",42,{"settings":{"x":1}}]})");
    CHECK(e2.present);
    CHECK_EQ(e2.mods.size(), 2);  // DT + 无 acronym 的对象
    if (e2.mods.size() == 2)
        CHECK_EQ(e2.mods[1].settings.at("x").compare("1"), 0);
}

void testMissingKeysAndTolerance() {
    // 空 JSON: 全部默认值
    const LazerScoreExtra e = parseLazerExtraJson("{}");
    CHECK(e.present);
    CHECK(e.error.empty());
    CHECK(!e.hasStatistics);
    CHECK(!e.hasMaximumStatistics);
    CHECK(e.mods.empty());
    CHECK_EQ(e.rank.size(), 0);
    CHECK_EQ(e.totalScoreWithoutMods, -1);

    // rank / total_score_without_mods (snake_case 为主, camelCase 别名)
    const LazerScoreExtra r = parseLazerExtraJson(
        R"({"rank":"S","total_score_without_mods":1234567})");
    CHECK_EQ(r.rank.compare("S"), 0);
    CHECK_EQ(r.totalScoreWithoutMods, 1234567);
    const LazerScoreExtra r2 = parseLazerExtraJson(
        R"({"rank":"ss","totalScoreWithoutMods":42})");
    CHECK_EQ(r2.rank.compare("ss"), 0);
    CHECK_EQ(r2.totalScoreWithoutMods, 42);

    // maximum_statistics 键归一化
    const LazerScoreExtra m = parseLazerExtraJson(
        R"({"maximum_statistics":{"Perfect":900,"Miss":3}})");
    CHECK(m.hasMaximumStatistics);
    CHECK_EQ(m.maximumStatistics.at("perfect"), 900);
    CHECK_EQ(m.maximumStatistics.at("miss"), 3);

    // 错误类型字段: 宽容跳过不报错
    const LazerScoreExtra w = parseLazerExtraJson(
        R"({"mods":5,"statistics":7,"rank":3,"maximum_statistics":[]})");
    CHECK(w.present);
    CHECK(w.error.empty());
    CHECK(!w.hasStatistics);
    CHECK(!w.hasMaximumStatistics);
    CHECK(w.mods.empty());

    // 非整数值被跳过, 整数保留
    const LazerScoreExtra v = parseLazerExtraJson(
        R"({"statistics":{"perfect":850,"great":"x","good":null,"ok":true}})");
    CHECK_EQ(v.statistics.size(), 1);
    CHECK_EQ(v.statistics.at("perfect"), 850);

    // 整数值的浮点形态 (850.0) 也接受
    const LazerScoreExtra vf = parseLazerExtraJson(
        R"({"statistics":{"perfect":850.0,"great":120}})");
    CHECK_EQ(vf.statistics.at("perfect"), 850);
    CHECK_EQ(vf.statistics.at("great"), 120);
}

void testBadJson() {
    const LazerScoreExtra a = parseLazerExtraJson("not json at all");
    CHECK(!a.present);
    CHECK(!a.error.empty());

    const LazerScoreExtra b = parseLazerExtraJson("[1,2,3]");
    CHECK(!b.present);
    CHECK(!b.error.empty());

    const LazerScoreExtra c = parseLazerExtraJson("");
    CHECK(!c.present);
    CHECK(!c.error.empty());

    const LazerScoreExtra d = parseLazerExtraJson("{\"statistics\":}");
    CHECK(!d.present);
    CHECK(!d.error.empty());
}

void testAccuracy305() {
    LazerManiaCounts c;
    c.perfect = 850; c.great = 120; c.good = 20; c.ok = 8; c.meh = 1; c.miss = 1;
    // 手算: (305*850+300*120+200*20+100*8+50*1)/(305*1000)*100 = 98.3934...%
    CHECK_NEAR(lazerAccuracy305Percent(c), 98.3934, 1e-4);
    CHECK_NEAR(lazerAccuracy305Percent(c),
               300100.0 / 305000.0 * 100.0, 1e-12);

    // 逐档权重抽查
    LazerManiaCounts p; p.perfect = 10;
    CHECK_NEAR(lazerAccuracy305Percent(p), 100.0, 1e-12);
    LazerManiaCounts g; g.great = 10;
    CHECK_NEAR(lazerAccuracy305Percent(g), 300.0 / 305.0 * 100.0, 1e-12);
    LazerManiaCounts go; go.good = 10;
    CHECK_NEAR(lazerAccuracy305Percent(go), 200.0 / 305.0 * 100.0, 1e-12);
    LazerManiaCounts o; o.ok = 10;
    CHECK_NEAR(lazerAccuracy305Percent(o), 100.0 / 305.0 * 100.0, 1e-12);
    LazerManiaCounts m; m.meh = 10;
    CHECK_NEAR(lazerAccuracy305Percent(m), 50.0 / 305.0 * 100.0, 1e-12);
    LazerManiaCounts ms; ms.miss = 10;
    CHECK_NEAR(lazerAccuracy305Percent(ms), 0.0, 1e-12);

    // N=0 -> 100
    LazerManiaCounts zero;
    CHECK(!zero.valid);
    CHECK_NEAR(lazerAccuracy305Percent(zero), 100.0, 1e-12);

    // 端到端: statistics -> counts -> acc
    expectSyntheticCounts(lazerManiaCountsFromStatistics(
        parseLazerExtraJson(kStatsPascal).statistics));
    CHECK_NEAR(
        lazerAccuracy305Percent(lazerManiaCountsFromStatistics(
            parseLazerExtraJson(kStatsPascal).statistics)),
        98.3934, 1e-4);
}

// ---- 压缩路径: 真实 LZMA blob -----------------------------------------------

std::vector<uint8_t> framedTail(const unsigned char* blob, size_t blobSize,
                                size_t declaredLenOverride = 0,
                                bool overrideLen = false) {
    std::vector<uint8_t> out;
    const uint32_t declared = overrideLen
                                  ? static_cast<uint32_t>(declaredLenOverride)
                                  : static_cast<uint32_t>(blobSize);
    out.push_back(static_cast<uint8_t>(declared & 0xff));
    out.push_back(static_cast<uint8_t>((declared >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((declared >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((declared >> 24) & 0xff));
    out.insert(out.end(), blob, blob + blobSize);
    return out;
}

void testCompressedPath() {
    const auto& blob = mra_lazer_test::kTailBlob;
    const std::vector<uint8_t> tail =
        framedTail(blob.data(), mra_lazer_test::kTailBlobSize);
    const LazerScoreExtra e = parseLazerExtra(tail.data(), tail.size());
    CHECK(e.present);
    CHECK(e.error.empty());
    CHECK(!e.jsonRaw.empty());
    CHECK(e.jsonRaw.find("\"total_score_without_mods\"") != std::string::npos);
    CHECK_EQ(e.rank.compare("S"), 0);
    CHECK_EQ(e.totalScoreWithoutMods, 1234567);
    CHECK_EQ(e.mods.size(), 2);
    if (e.mods.size() == 2) {
        CHECK_EQ(e.mods[0].acronym.compare("DT"), 0);
        CHECK_EQ(e.mods[0].settings.at("speed_change").compare("1.25"), 0);
        CHECK_EQ(e.mods[1].acronym.compare("RD"), 0);
    }
    CHECK(e.hasStatistics);
    CHECK_EQ(e.statistics.at("perfect"), 850);
    CHECK_EQ(e.statistics.at("ignorehit"), 5);
    CHECK(e.hasMaximumStatistics);
    CHECK_EQ(e.maximumStatistics.at("great"), 130);

    expectSyntheticCounts(lazerManiaCountsFromStatistics(e.statistics));
    CHECK_NEAR(lazerAccuracy305Percent(
                   lazerManiaCountsFromStatistics(e.statistics)),
               98.3934, 1e-4);
}

void testCompressedPathTolerance() {
    const auto& blob = mra_lazer_test::kTailBlob;
    const size_t n = mra_lazer_test::kTailBlobSize;

    // 空尾 / null: 无尾块, 也不算错误
    const LazerScoreExtra none = parseLazerExtra(nullptr, 0);
    CHECK(!none.present);
    CHECK(none.error.empty());
    std::vector<uint8_t> two = {0x01, 0x02};
    const LazerScoreExtra tiny = parseLazerExtra(two.data(), two.size());
    CHECK(!tiny.present);
    CHECK(!tiny.error.empty());  // 有字节但装不下长度帧

    // 声明长度超界
    {
        std::vector<uint8_t> t = framedTail(blob.data(), n, n + 1, true);
        const LazerScoreExtra e = parseLazerExtra(t.data(), t.size());
        CHECK(!e.present);
        CHECK(!e.error.empty());
    }
    // 声明长度为 0
    {
        std::vector<uint8_t> t = framedTail(blob.data(), n, 0, true);
        t.resize(4);  // 只留长度帧
        const LazerScoreExtra e = parseLazerExtra(t.data(), t.size());
        CHECK(!e.present);
        CHECK(!e.error.empty());
    }
    // 截断: 声明 235 但只给 234
    {
        std::vector<uint8_t> t = framedTail(blob.data(), n);
        t.pop_back();
        const LazerScoreExtra e = parseLazerExtra(t.data(), t.size());
        CHECK(!e.present);
        CHECK(!e.error.empty());
    }
    // 坏 props (0xFF 非法) -> LZMA 分配失败
    {
        std::vector<uint8_t> bad(mra_lazer_test::kTailBlob.begin(),
                                 mra_lazer_test::kTailBlob.end());
        bad[0] = 0xff;
        std::vector<uint8_t> t = framedTail(bad.data(), bad.size());
        const LazerScoreExtra e = parseLazerExtra(t.data(), t.size());
        CHECK(!e.present);
        CHECK(!e.error.empty());
    }
    // 帧后多余字节: 声明长度恰好框住流, 后面的垃圾被忽略
    {
        std::vector<uint8_t> t = framedTail(blob.data(), n);
        t.push_back(0xde); t.push_back(0xad); t.push_back(0xbe); t.push_back(0xef);
        const LazerScoreExtra e = parseLazerExtra(t.data(), t.size());
        CHECK(e.present);
        CHECK(e.error.empty());
        CHECK_EQ(e.statistics.at("perfect"), 850);
    }
}

}  // namespace

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    } cases[] = {
        {"versionGate", testVersionGate},
        {"jsonCaseVariants", testJsonCaseVariants},
        {"ignoreHitFiltered", testIgnoreHitFiltered},
        {"modsSettingsRawText", testModsSettingsRawText},
        {"missingKeysAndTolerance", testMissingKeysAndTolerance},
        {"badJson", testBadJson},
        {"accuracy305", testAccuracy305},
        {"compressedPath", testCompressedPath},
        {"compressedPathTolerance", testCompressedPathTolerance},
    };
    for (const Case& c : cases) {
        const int before = g_failures;
        c.fn();
        std::printf("[%s] %s\n", g_failures == before ? "ok  " : "FAIL", c.name);
    }
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED (lazer format)\n");
        return 0;
    }
    std::printf("%d FAILURES\n", g_failures);
    return 1;
}
