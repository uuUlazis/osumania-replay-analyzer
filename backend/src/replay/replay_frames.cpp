#include "replay/replay_frames.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace mra {
namespace {

// Parses one signed integer token; returns false on trailing garbage.
bool parseInt(std::string_view token, long long& out) {
    if (token.empty()) return false;
    const char* begin = token.data();
    const char* end = token.data() + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end;
}

// osu! writes frames joined by ',' with a trailing "0|0|0" terminator record.
// Some tools emit '\n'-joined lines instead, so both count as separators.
std::vector<std::string_view> splitRecords(const char* data, size_t size) {
    std::vector<std::string_view> records;
    std::string_view view(data, size);
    size_t start = 0;
    while (start < view.size()) {
        size_t sep = view.find_first_of(",\n", start);
        if (sep == std::string_view::npos) sep = view.size();
        std::string_view rec = view.substr(start, sep - start);
        while (!rec.empty() &&
               (rec.back() == '\r' || rec.back() == ' ' || rec.back() == '\t'))
            rec.remove_suffix(1);
        while (!rec.empty() &&
               (rec.front() == ' ' || rec.front() == '\t'))
            rec.remove_prefix(1);
        if (!rec.empty()) records.push_back(rec);
        start = sep + 1;
    }
    // The stream always ends with a literal "0|0|0" - a marker, not a frame.
    if (!records.empty() && records.back() == "0|0|0") records.pop_back();
    return records;
}


}  // namespace

ReplayFrameDecodeResult decodeReplayFrames(
    const std::vector<uint8_t>& decompressed) {
    ReplayFrameDecodeResult result;
    const char* text = reinterpret_cast<const char*>(decompressed.data());
    auto lines = splitRecords(text, decompressed.size());

    int64_t absoluteTime = 0;
    uint32_t previousKeys = 0;
    bool first = true;
    std::string firstBad;

    for (std::string_view line : lines) {
        // RNG seed frame: w = -12345
        {
            long long w = 0;
            size_t firstPipe = line.find('|');
            if (firstPipe != std::string_view::npos &&
                parseInt(line.substr(0, firstPipe), w) && w == -12345) {
                // Format: -12345|x|y|seed (x/y are 0, seed is arbitrary int)
                std::string_view rest = line.substr(firstPipe + 1);
                size_t p2 = rest.find('|');
                size_t p3 = p2 == std::string_view::npos
                                ? std::string_view::npos
                                : rest.find('|', p2 + 1);
                long long seed = 0;
                if (p3 != std::string_view::npos &&
                    parseInt(rest.substr(p3 + 1), seed)) {
                    ReplayFrame frame;
                    frame.isSeedFrame = true;
                    frame.seed = static_cast<uint32_t>(seed);
                    frame.timeMs = absoluteTime;  // informational only
                    frame.rawKeys = previousKeys;  // normalized, unchanged
                    frame.keyDownMask = 0;
                    frame.keyUpMask = 0;
                    result.frames.push_back(frame);
                    continue;
                }
                // Malformed seed-like frame: fall through to normal parsing.
            }
        }

        // Mania records: osu!stable writes "w|x|y", lazer writes
        // "w|x|y|z" with y as FLOAT text ("20.83333"). w = ms since the
        // previous frame, x = pressed column bitmask (bit c = column c).
        // y = EffectiveSpeed for stable mania frames (docs/REPLAY.md §3 +
        // InputAndReplayPlayback IL: new ReplayFrame(now, keyMask,
        // (float)ManiaScrollSpeed.EffectiveSpeed, ...)) and is preserved in
        // ReplayFrame.y for the renderer's scroll model (session-level
        // scrollSpeedEff). Parse failure keeps y = 0 (legacy behavior).
        long long w = 0, x = 0;
        size_t p1 = line.find('|');
        if (p1 == std::string_view::npos) { result.malformedLines++; continue; }
        size_t p2 = line.find('|', p1 + 1);
        if (p2 == std::string_view::npos) { result.malformedLines++; continue; }
        bool ok = parseInt(line.substr(0, p1), w) &&
                  parseInt(line.substr(p1 + 1, p2 - p1 - 1), x);
        if (!ok) {
            if (result.malformedLines == 0)
                firstBad = std::string(line.substr(0, 60));
            result.malformedLines++;
            continue;
        }

        ReplayFrame frame;
        if (first) {
            absoluteTime = w;
            first = false;
        } else {
            if (w < 0) result.negativeDeltaCount++;
            absoluteTime += w;
        }
        frame.timeMs = absoluteTime;
        frame.x = static_cast<float>(x);
        frame.y = 0.0f;
        {
            // y token: stop at a 4th field (lazer "w|x|y|z"); float text.
            size_t p3 = line.find('|', p2 + 1);
            size_t yEnd = p3 == std::string_view::npos ? line.size() : p3;
            std::string_view yTok = line.substr(p2 + 1, yEnd - p2 - 1);
            if (!yTok.empty()) {
                float yValue = 0.f;
                auto [yPtr, yEc] = std::from_chars(yTok.data(),
                                                   yTok.data() + yTok.size(),
                                                   yValue);
                if (yEc == std::errc()) frame.y = yValue;
            }
        }
        frame.rawKeys = static_cast<uint32_t>(x) & 0x3FFFFFFFu;
        frame.keyDownMask = frame.rawKeys & ~previousKeys;
        frame.keyUpMask = ~frame.rawKeys & previousKeys;
        previousKeys = frame.rawKeys;
        result.frames.push_back(frame);
    }

    result.ok = result.malformedLines == 0;
    if (!result.ok) {
        result.error = "malformed replay frame lines: " +
                       std::to_string(result.malformedLines);
        if (!firstBad.empty())
            result.error += "; first bad record: '" + firstBad + "'";
    }
    return result;
}

// ── 偶数键 + 特殊键在右 的 lane bit 顺序 (I-6) ───────────────────────────
namespace {

// 只看掩码的低 keyCount 位 (更高位是帧里本来就有的杂位, 原样保留)。
// rotation ρ 的语义: 结果 bit c = 输入 bit (c + ρ) mod keyCount,
// 即把整块位序向低位方向旋转 ρ 列。
uint32_t rotateLaneBits(uint32_t value, int keyCount, int rotation) {
    if (keyCount <= 1 || keyCount > 30) return value;
    const int n = keyCount;
    const int r = ((rotation % n) + n) % n;
    if (r == 0) return value;
    const uint32_t mask = (1u << n) - 1u;
    const uint32_t low = value & mask;
    const uint32_t rotated =
        ((low >> r) | (low << (n - r))) & mask;  // 低 r 位环回高位
    return (value & ~mask) | rotated;
}

bool envIsOff(const char* v) {
    if (v == nullptr) return true;
    if (*v == '\0') return true;
    return std::strcmp(v, "0") == 0 || std::strcmp(v, "off") == 0 ||
           std::strcmp(v, "false") == 0 || std::strcmp(v, "no") == 0;
}

// ── SpecialStyle 来源 (skin.ini [Mania]) ─────────────────────────────────

std::string_view trimView(std::string_view v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t' ||
                          v.front() == '\r' || v.front() == '\n'))
        v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' ||
                          v.back() == '\r' || v.back() == '\n'))
        v.remove_suffix(1);
    return v;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

// skin.ini 的取值: 数字 0/1/2 或 none/left/right。
int specialStyleFromToken(std::string_view token) {
    token = trimView(token);
    if (token.empty()) return kManiaSpecialStyleUnknown;
    if (equalsIgnoreCase(token, "none") || token == "0")
        return kManiaSpecialStyleNone;
    if (equalsIgnoreCase(token, "left") || token == "1")
        return kManiaSpecialStyleLeft;
    if (equalsIgnoreCase(token, "right") || token == "2")
        return kManiaSpecialStyleRight;
    long long v = 0;
    if (parseInt(token, v) && v >= 0 && v <= 2) return static_cast<int>(v);
    return kManiaSpecialStyleUnknown;
}

}  // namespace

const char* maniaSpecialStyleName(int specialStyle) {
    switch (specialStyle) {
        case kManiaSpecialStyleNone:
            return "none";
        case kManiaSpecialStyleLeft:
            return "left";
        case kManiaSpecialStyleRight:
            return "right";
        default:
            return "unknown";
    }
}

int readManiaSpecialStyleFromSkinIni(const std::string& pathOrFolder) {
    if (pathOrFolder.empty()) return kManiaSpecialStyleUnknown;
    // Windows 的 std::ifstream 接受 '/' 分隔符, 这里不引 <filesystem>:
    // 先按「这就是 skin.ini」打开, 失败再按「这是 skin 文件夹」追加 skin.ini。
    std::ifstream in(pathOrFolder, std::ios::binary);
    if (!in.is_open()) {
        std::string file = pathOrFolder;
        const char last = file.back();
        if (last != '/' && last != '\\') file += '/';
        file += "skin.ini";
        in.clear();
        in.open(file, std::ios::binary);
    }
    if (!in.is_open()) return kManiaSpecialStyleUnknown;
    bool inManiaSection = false;
    std::string lineText;
    while (std::getline(in, lineText)) {
        std::string_view line = trimView(lineText);
        if (line.empty()) continue;
        if (line.front() == ';' || line.front() == '#') continue;
        if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;
        if (line.front() == '[') {
            inManiaSection = equalsIgnoreCase(line, "[Mania]");
            continue;
        }
        if (!inManiaSection) continue;
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        if (equalsIgnoreCase(trimView(line.substr(0, colon)), "SpecialStyle"))
            return specialStyleFromToken(line.substr(colon + 1));
    }
    return kManiaSpecialStyleUnknown;
}

bool evenKeySpecialStyleIsRight(const EvenKeyLaneOrder& config) {
    return config.specialStyle == kManiaSpecialStyleRight;
}

namespace {

// 从 env 值解析 SpecialStyle 声明 (MRA_RE_SPECIAL_RIGHT 的短开关语义:
// 1 = 在右; 0 = 未确证在右 → None)。
int specialStyleFromEnvFlag(const char* raw) {
    std::string_view v = trimView(raw == nullptr ? std::string_view() : raw);
    if (v.empty()) return kManiaSpecialStyleUnknown;
    if (v == "1" || equalsIgnoreCase(v, "on") || equalsIgnoreCase(v, "true") ||
        equalsIgnoreCase(v, "yes") || equalsIgnoreCase(v, "right"))
        return kManiaSpecialStyleRight;
    if (v == "0" || equalsIgnoreCase(v, "off") || equalsIgnoreCase(v, "false") ||
        equalsIgnoreCase(v, "no") || equalsIgnoreCase(v, "left"))
        return kManiaSpecialStyleNone;
    return kManiaSpecialStyleUnknown;
}

}  // namespace

EvenKeyLaneOrder evenKeyLaneOrderFromEnv(const std::string& skinFolder) {
    EvenKeyLaneOrder cfg;
    const char* raw = std::getenv("MRA_RE_EVENKEY");
    if (envIsOff(raw)) return cfg;  // 默认 OFF = 现行为 (也不去读 skin.ini)
    cfg.enabled = true;
    if (std::strcmp(raw, "stage") == 0) {
        // 强制把帧搬进"stage 列空间" (ρ=1): 只在物件侧也按忠实
        // RemapManiaColumn 建模时才是正解 (与 hit_matcher 配合的开关)。
        cfg.forcedRotation = 1;
    } else if (std::strcmp(raw, "mirror") == 0) {
        cfg.forcedRotation = 2;
    } else if (std::strcmp(raw, "1") == 0 || std::strcmp(raw, "on") == 0 ||
               std::strcmp(raw, "true") == 0 ||
               std::strcmp(raw, "auto") == 0) {
        cfg.forcedRotation = -1;  // 自动查表
    } else {
        long long v = 0;
        const char* begin = raw;
        const char* end = raw + std::strlen(raw);
        auto [ptr, ec] = std::from_chars(begin, end, v);
        if (ec == std::errc() && ptr == end && v >= 0 && v <= 30)
            cfg.forcedRotation = static_cast<int>(v);
        else
            cfg.forcedRotation = -1;  // 不认识的取值 → 退回自动查表
    }
    const char* keys = std::getenv("MRA_RE_EVENKEY_KEYS");
    if (keys != nullptr && *keys != '\0') {
        long long v = 0;
        const char* begin = keys;
        const char* end = keys + std::strlen(keys);
        auto [ptr, ec] = std::from_chars(begin, end, v);
        if (ec == std::errc() && ptr == end && v > 0 && v <= 30)
            cfg.keyCountFilter = static_cast<int>(v);
    }

    // ── 触发条件第二半: SpecialStyle == Right (来源可选, 见 replay_frames.h) ──
    const char* styleEnv = std::getenv("MRA_RE_SPECIAL_STYLE");
    const char* rightEnv = std::getenv("MRA_RE_SPECIAL_RIGHT");
    if (styleEnv != nullptr && *styleEnv != '\0') {
        cfg.specialStyle = specialStyleFromToken(styleEnv);
        cfg.specialStyleSource = "env";
    } else if (rightEnv != nullptr && *rightEnv != '\0') {
        cfg.specialStyle = specialStyleFromEnvFlag(rightEnv);
        cfg.specialStyleSource = "env";
    } else {
        const char* iniEnv = std::getenv("MRA_RE_SKIN_INI");
        if (iniEnv != nullptr && *iniEnv != '\0') {
            cfg.specialStyle = readManiaSpecialStyleFromSkinIni(iniEnv);
            if (cfg.specialStyle != kManiaSpecialStyleUnknown)
                cfg.specialStyleSource = "skin.ini";
        } else if (!skinFolder.empty()) {
            cfg.specialStyle = readManiaSpecialStyleFromSkinIni(skinFolder);
            if (cfg.specialStyle != kManiaSpecialStyleUnknown)
                cfg.specialStyleSource = "skin.ini";
        }
    }
    return cfg;
}

int evenKeyRightLaneBitRotation(int keyCount, bool randomMod, bool mirrorMod,
                                int forcedRotation) {
    // 真代码闸门 ManiaStage.Obf020598(): 阶段列数 > 4 且为偶数。
    // 4K / 5K / 7K / 9K 不进任何旋转 —— 锚点逐位不变。
    if (keyCount <= 4 || (keyCount % 2) != 0) return 0;
    if (forcedRotation >= 0) return forcedRotation % keyCount;
    // RD (Random): 真代码 bit = (RandomColumnMap[c] + 1) mod N, 我方 R[c]。
    if (randomMod) return 1;
    // Mirror (无 RD): 真代码 bit = (1 - c) mod N, 我方 N-1-c → 差 2。
    if (mirrorMod) return 2;
    // NoMod: 物件侧 (c-1) 与编码的 bit+1 相消, 我方恒等即真值。
    return 0;
}

int applyEvenKeyLaneBitOrder(std::vector<ReplayFrame>& frames, int keyCount,
                             bool randomMod, bool mirrorMod,
                             const EvenKeyLaneOrder& config) {
    if (!config.enabled) return 0;
    if (config.keyCountFilter != 0 && config.keyCountFilter != keyCount)
        return 0;
    // 触发条件的第二半 (SpecialStyle == Right) 不在 .osr 里, 必须由显式来源
    // 确证 (env / skin.ini / 调用方的 skinFolder)。未确证时 auto 查表一律不动帧。
    // forcedRotation >= 0 是人工探针开关, 跳过这条闸门 (调用者已显式断言)。
    if (config.forcedRotation < 0 && !evenKeySpecialStyleIsRight(config))
        return 0;
    const int rotation =
        evenKeyRightLaneBitRotation(keyCount, randomMod, mirrorMod,
                                    config.forcedRotation);
    if (rotation == 0 || frames.empty()) return 0;
    // rawKeys / keyDownMask / keyUpMask 用同一个位排列, 平移后的边沿关系
    // (down = new & ~prev, up = ~new & prev) 在低 keyCount 位上保持不变。
    for (ReplayFrame& f : frames) {
        f.rawKeys = rotateLaneBits(f.rawKeys, keyCount, rotation);
        f.keyDownMask = rotateLaneBits(f.keyDownMask, keyCount, rotation);
        f.keyUpMask = rotateLaneBits(f.keyUpMask, keyCount, rotation);
    }
    return rotation;
}

}  // namespace mra
