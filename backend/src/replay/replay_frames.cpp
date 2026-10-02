#include "replay/replay_frames.h"

#include <charconv>
#include <cstring>

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
        // "w|x|y|z" with y as FLOAT text ("20.83333"). Only w and x are
        // meaningful here: w = ms since the previous frame, x = pressed
        // column bitmask (bit c = column c). y/z (cursor position, unused
        // for mania) are ignored and never parsed.
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

}  // namespace mra
