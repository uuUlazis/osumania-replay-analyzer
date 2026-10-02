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

}  // namespace mra
