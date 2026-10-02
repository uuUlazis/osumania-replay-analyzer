#include "beatmap/osu_parser.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace mra {
namespace {

std::string stripBom(const std::string& text) {
    if (text.size() >= 3 &&
        static_cast<uint8_t>(text[0]) == 0xEF &&
        static_cast<uint8_t>(text[1]) == 0xBB &&
        static_cast<uint8_t>(text[2]) == 0xBF)
        return text.substr(3);
    return text;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

bool parseInt(std::string_view token, int& out) {
    token = trim(token);
    if (token.empty()) return false;
    const char* begin = token.data();
    const char* end = begin + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    // Accept a prefix match: osu CSV fields frequently carry trailing
    // ":hitSample" data (e.g. endTime "3200:0:0:0:").
    return ec == std::errc() && ptr != begin;
}

bool parseInt64(std::string_view token, int64_t& out) {
    token = trim(token);
    if (token.empty()) return false;
    const char* begin = token.data();
    const char* end = begin + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr != begin;
}

bool parseDouble(std::string_view token, double& out) {
    token = trim(token);
    if (token.empty()) return false;
    // from_chars for double is not available on all MSVC targets; use strtod.
    std::string tmp(token);
    char* endPtr = nullptr;
    out = std::strtod(tmp.c_str(), &endPtr);
    return endPtr && *endPtr == 0;
}

}  // namespace

int maniaColumnFromX(int x, int keyCount) {
    if (keyCount <= 0) return 0;
    double col = std::floor(static_cast<double>(x) * keyCount / 512.0);
    int column = static_cast<int>(col);
    return std::clamp(column, 0, keyCount - 1);
}

OsuParseResult parseOsu(const std::string& rawText) {
    OsuParseResult result;
    OsuBeatmap& bm = result.beatmap;
    std::string text = stripBom(rawText);

    std::string currentSection;
    size_t start = 0;
    const size_t size = text.size();
    bool inHitObjects = false;
    int sourceIndex = 0;

    while (start <= size) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = size;
        std::string_view line = trim(std::string_view(text).substr(start, nl - start));
        start = nl + 1;
        if (start > size) break;
        if (line.empty() || line.front() == '/' ) continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = std::string(line.substr(1, line.size() - 2));
            inHitObjects = currentSection == "HitObjects";
            continue;
        }

        size_t colon = line.find(':');
        auto keyOf = [&](std::string_view& value) {
            if (colon != std::string_view::npos) {
                value = trim(line.substr(colon + 1));
                return trim(line.substr(0, colon));
            }
            value = std::string_view();
            return trim(line);
        };

        if (currentSection == "General") {
            std::string_view value;
            auto key = keyOf(value);
            if (key == "AudioFilename") bm.audioFilename = std::string(value);
            else if (key == "Mode") { int m; if (parseInt(value, m)) bm.mode = m; }
        } else if (currentSection == "Metadata") {
            std::string_view value;
            auto key = keyOf(value);
            if (key == "Title") bm.title = std::string(value);
            else if (key == "TitleUnicode") bm.titleUnicode = std::string(value);
            else if (key == "Artist") bm.artist = std::string(value);
            else if (key == "ArtistUnicode") bm.artistUnicode = std::string(value);
            else if (key == "Creator") bm.creator = std::string(value);
            else if (key == "Version") bm.version = std::string(value);
            else if (key == "BeatmapID") bm.beatmapId = std::string(value);
            else if (key == "BeatmapSetID") bm.beatmapSetId = std::string(value);
        } else if (currentSection == "Difficulty") {
            std::string_view value;
            auto key = keyOf(value);
            double d = 0; int i = 0;
            if (key == "CircleSize") { if (parseInt(value, i)) bm.keyCount = i; else if (parseDouble(value, d)) bm.keyCount = static_cast<int>(d); }
            else if (key == "OverallDifficulty") { if (parseDouble(value, d)) bm.overallDifficulty = d; }
            else if (key == "HPDrainRate") { if (parseDouble(value, d)) bm.hpDrain = d; }
        } else if (currentSection == "TimingPoints") {
            auto parts = split(line, ',');
            if (parts.size() >= 2) {
                TimingPoint tp;
                double time = 0, beat = 0;
                if (parseDouble(parts[0], time) && parseDouble(parts[1], beat)) {
                    tp.timeMs = time;
                    tp.beatLength = beat;
                    tp.uninherited = beat > 0;
                    if (parts.size() >= 3) parseDouble(parts[2], tp.sliderMultiplier);
                    if (parts.size() >= 4) { int meter; if (parseInt(parts[3], meter)) tp.meter = meter; }
                    bm.timingPoints.push_back(tp);
                }
            }
        } else if (currentSection == "Events") {
            auto parts = split(line, ',');
            if (parts.size() >= 3 && (parts[0] == "0" || parts[0] == "0 """)) {
                std::string_view file = trim(parts[2]);
                if (!file.empty() && file.front() == '"') file.remove_prefix(1);
                if (!file.empty() && file.back() == '"') file.remove_suffix(1);
                bm.backgroundFile = std::string(file);
            }
        } else if (inHitObjects) {
            auto parts = split(line, ',');
            if (parts.size() < 5) { bm.malformedObjectLines++; continue; }
            int x = 0, y = 0, type = 0, hitSound = 0;
            int64_t time = 0;
            if (!parseInt(parts[0], x) || !parseInt(parts[1], y) ||
                !parseInt64(parts[2], time) || !parseInt(parts[3], type) ||
                !parseInt(parts[4], hitSound)) {
                bm.malformedObjectLines++;
                continue;
            }

            constexpr int kTypeCircle = 1;
            constexpr int kTypeSlider = 2;
            constexpr int kTypeSpinner = 8;
            constexpr int kTypeHold = 128;

            if (type & kTypeHold) {
                // x,y,startTime,type,hitSound,endTime,hitSample
                if (parts.size() < 6) { bm.malformedObjectLines++; continue; }
                int64_t endTime = 0;
                if (!parseInt64(parts[5], endTime)) {
                    bm.malformedObjectLines++;
                    continue;
                }
                ManiaHitObject obj;
                obj.type = ManiaObjectType::Hold;
                obj.column = maniaColumnFromX(x, bm.keyCount);
                obj.startTimeMs = time;
                obj.endTimeMs = std::max(endTime, time);
                obj.sourceIndex = sourceIndex++;
                obj.rawX = x; obj.rawY = y; obj.rawType = type; obj.rawHitSound = hitSound;
                obj.rawLine = std::string(line);
                bm.objects.push_back(std::move(obj));
            } else if (type & kTypeCircle) {
                ManiaHitObject obj;
                obj.type = ManiaObjectType::Note;
                obj.column = maniaColumnFromX(x, bm.keyCount);
                obj.startTimeMs = time;
                obj.sourceIndex = sourceIndex++;
                obj.rawX = x; obj.rawY = y; obj.rawType = type; obj.rawHitSound = hitSound;
                obj.rawLine = std::string(line);
                bm.objects.push_back(std::move(obj));
            } else if (type & (kTypeSlider | kTypeSpinner)) {
                bm.parseWarnings.push_back(
                    "object " + std::to_string(sourceIndex++) +
                    ": slider/spinner not used in mania, skipped");
            } else {
                bm.malformedObjectLines++;
            }
        }
    }

    if (bm.keyCount <= 0 && bm.isMania())
        bm.parseWarnings.push_back("mania beatmap has no CircleSize/key count");
    result.ok = true;
    return result;
}

}  // namespace mra
