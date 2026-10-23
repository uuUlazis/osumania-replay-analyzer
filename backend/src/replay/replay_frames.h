#pragma once
// Replay frame model: parses decompressed LZMA frame text and rebuilds
// absolute-time frames with key down/up transition masks.
#include <cstdint>
#include <string>
#include <vector>

namespace mra {

// Legacy .osr action bits (osu!standard only; kept for documentation).
// Mania replays carry the pressed-column bitmask in the x field of each
// "w|x|y" record (bit c = column c) and have no z field at all.
enum class ReplayActionBit : uint32_t {
    M1 = 1,
    M2 = 2,
    K1 = 4,
    K2 = 8,
};

struct ReplayFrame {
    int64_t timeMs = 0;      // rebuilt absolute time
    float x = 0.f;           // 0..512, not used for mania columns
    float y = 0.f;           // 0..384
    uint32_t rawKeys = 0;    // mania: x field, bit c = column c pressed
    uint32_t keyDownMask = 0;
    uint32_t keyUpMask = 0;
    bool isSeedFrame = false;
    uint32_t seed = 0;
};

struct ReplayFrameDecodeResult {
    bool ok = false;
    std::string error;
    std::vector<ReplayFrame> frames;
    // Diagnostics
    size_t malformedLines = 0;
    int64_t negativeDeltaCount = 0;
};

// Decodes the decompressed frame stream: mania records are "w|x|y" joined by
// ',' (newline also accepted) with the key bitmask in x, a trailing "0|0|0"
// terminator, and the 4-field "-12345|x|y|seed" RNG seed frame.
ReplayFrameDecodeResult decodeReplayFrames(const std::vector<uint8_t>& decompressed);

// ── 偶数键 + 特殊键在右 的 lane bit 顺序 (我方挂起项 I-6) ────────────────
//
// 逆向包权威 (archive/osu-reverse-engineering):
//   docs/REPLAY.md §3, docs/MANIA.md §6;
//   focused/replay/InputAndReplayPlayback.cs (GetManiaKeyMask / Obf014317);
//   focused/mania/ManiaStage.cs (Obf020597/020598/020574);
//   focused/mania/ManiaHitObjectManager.cs (RemapManiaColumn).
//
// 真代码语义 (逐条 IL 见 build/tmp_accupdate/re_evenkey_findings.md):
//   * 触发条件 ManiaStage.Obf020598() = 阶段列数 > 4 且为偶数 (6K/8K/10K...) 且
//     SkinMania.SpecialStyle == Right (skin.ini [Mania] SpecialStyle: 2)。
//     4K/5K/7K/9K 与"特殊键在左/无"永不触发。
//   * 编码 (GetManiaKeyMask): 按住列 i 写的是**列 (i+1) mod N 的 bit**;
//     回放 (Obf014317): bit i 作用到**列 (i-1) mod N**, 两者互为逆序。
//   * 物件侧 ManiaHitObjectManager.RemapManiaColumn 同时对物件列施加 -1 位移
//     (列 0 → N-1), 于是无 mod 时两侧相消 → "bit c = 列 c" 恒等。
//     我方现状正是恒等, 所以 6K/8K 纯 NoMod 局本来就是对的。
//   * 偏差只出现在 mods 复合处: Mirror / RD (Random), 见下方旋转表。
//
// 本模块只改**列映射** (帧的 rawKeys/keyDownMask/keyUpMask 位序), 不碰判定:
// 修正后 bit c 依旧表示列 c, 下游 (hit_matcher / api_server 的 /frames) 无需
// 知道偶数键语义。
//
// 触发条件的第二半 (`SkinConfig.SpecialStyle == Right`) **不记录在 .osr 里**,
// 所以 SpecialStyle 必须是可声明的外部输入 (来源优先级):
//   1. `MRA_RE_SPECIAL_STYLE=none|left|right|unknown` 或任务约定的
//      `MRA_RE_SPECIAL_RIGHT=1` (1 = 该局 skin 的 [Mania] SpecialStyle 是 Right);
//   2. `MRA_RE_SKIN_INI=<path>` (skin.ini 文件或 skin 文件夹);
//   3. 调用方给的 skinFolder 参数 (api_server 的 /api/replays/load 已有该字段,
//      loadReplaySession 会把它传进来 → 读 <skinFolder>/skin.ini)。
// 三条来源都没有 → Unknown → auto 查表**不旋转** (fail-safe; 默认即此状态)。

// ManiaSpecialStyle (osu.Graphics.Skinning.ManiaSpecialStyle; skin.ini
// [Mania] SpecialStyle: 0 = None, 1 = Left, 2 = Right)。
inline constexpr int kManiaSpecialStyleUnknown = -1;  // .osr 不记录, 未声明
inline constexpr int kManiaSpecialStyleNone = 0;
inline constexpr int kManiaSpecialStyleLeft = 1;
inline constexpr int kManiaSpecialStyleRight = 2;

// 偶数键 lane bit 修正配置 (env 门控, 默认 OFF = 现行为).
struct EvenKeyLaneOrder {
    bool enabled = false;      // MRA_RE_EVENKEY: 0/off (默认) 或 1/on/auto/stage/mirror/<int>
    int forcedRotation = -1;   // -1 = 按 mods 自动查表; >=0 = 强制该旋转量
    int keyCountFilter = 0;    // MRA_RE_EVENKEY_KEYS: 0 = 所有偶数键(>4); 否则只对指定键数生效
    int specialStyle = kManiaSpecialStyleUnknown;  // 触发条件第二半 (见上)
    std::string specialStyleSource;                // 诊断用: "env" / "skin.ini" / ""
};

// 读 MRA_RE_EVENKEY / MRA_RE_EVENKEY_KEYS / MRA_RE_SPECIAL_RIGHT(_STYLE) /
// MRA_RE_SKIN_INI。skinFolder 为空时不用它 (来源优先级见上)。
EvenKeyLaneOrder evenKeyLaneOrderFromEnv(const std::string& skinFolder = std::string());

// 从 skin.ini 读 [Mania] SpecialStyle (pathOrFolder 可以是 skin.ini 本身,
// 也可以是含 skin.ini 的 skin 文件夹)。返回 0/1/2, 读不到或有未知取值 → -1。
int readManiaSpecialStyleFromSkinIni(const std::string& pathOrFolder);

// 触发条件是否已被确证为「特殊键在右」: 只有 == Right 才允许自动查表旋转。
bool evenKeySpecialStyleIsRight(const EvenKeyLaneOrder& config);

// 诊断用的名字: "unknown" / "none" / "left" / "right"。
const char* maniaSpecialStyleName(int specialStyle);

// 该局需要的旋转量 ρ (单位: 列). 语义: 修正后 bit c 承载修正前的 bit
// (c + ρ) mod keyCount。keyCount 非 (偶数 且 >4) 时恒为 0 —— 4K/7K 逐位不变。
// 调用者必须先确证 SpecialStyle == Right (本函数只表达 mods 复合的偏差)。
//
// 表 (N = keyCount, c = .osu 物件列, R = stable 的 RandomColumnMap):
//   NoMod          : 物件侧 (c-1) 与 bit+1 相消, 我方形似恒等 → ρ = 0
//   Mirror (无 RD) : 真代码 bit = (1 - c) mod N; 我方 N-1-c  → ρ = 2
//   RD             : 真代码 bit = (R[c] + 1) mod N; 我方 R[c] → ρ = 1
//                    (要求物件侧 RD 置换含 stable 的"首列不抽随机数"特例)
int evenKeyRightLaneBitRotation(int keyCount, bool randomMod, bool mirrorMod,
                                int forcedRotation);

// 按 cfg 把整批帧的位序旋转到位 (原地). 返回实际施加的旋转量 (0 = 未改动).
// auto 模式 (forcedRotation < 0) 在 SpecialStyle 未被确证为 Right 时一律返回 0;
// forcedRotation >= 0 是人工探针开关, 跳过这条闸门。
int applyEvenKeyLaneBitOrder(std::vector<ReplayFrame>& frames, int keyCount,
                             bool randomMod, bool mirrorMod,
                             const EvenKeyLaneOrder& config);

}  // namespace mra
