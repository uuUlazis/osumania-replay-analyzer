#include "replay/osr_reader.h"

#include "io/binary_reader.h"

namespace mra {
namespace {

constexpr size_t kMinOsrSize = 32;

}  // namespace

int keyCountFromMods(uint32_t mods) {
    int count = 0;
    bool fixed = false;
    struct { uint32_t bit; int keys; } fixedMods[] = {
        {kModKey4, 4}, {kModKey5, 5}, {kModKey6, 6},
        {kModKey7, 7}, {kModKey8, 8}, {kModKey9, 9},
    };
    for (const auto& m : fixedMods) {
        if (mods & m.bit) { count = m.keys; fixed = true; }
    }
    if (fixed) return count;
    if (mods & kModKey1) count += 1;
    if (mods & kModKey2) count += 2;
    if (mods & kModKey3) count += 3;
    return count;  // 0 when no key mod present
}

OsrParseResult parseOsr(const std::vector<uint8_t>& bytes) {
    OsrParseResult result;
    try {
        if (bytes.size() < kMinOsrSize)
            throw BinaryReaderError("file too small to be a .osr replay (" +
                                    std::to_string(bytes.size()) + " bytes)");
        BinaryReader r(bytes);

        OsrHeader& h = result.header;
        h.mode = r.readU8();
        h.gameVersion = r.readU32();
        h.beatmapMd5 = r.readOsuString();
        h.playerName = r.readOsuString();
        h.replayMd5 = r.readOsuString();
        h.count300 = r.readU16();
        h.count100 = r.readU16();
        h.count50 = r.readU16();
        h.countGeki = r.readU16();
        h.countKatu = r.readU16();
        h.countMiss = r.readU16();
        h.score = r.readU32();
        h.maxCombo = r.readU16();
        h.perfect = r.readU8() != 0;
        h.mods = r.readU32();
        h.lifeGraph = r.readOsuString();
        h.timestamp = r.readI64();

        uint32_t compressedLength = r.readU32();
        // Reject absurd lengths; the length must fit in the remaining file.
        if (compressedLength > r.remaining())
            throw BinaryReaderError(
                "compressed replay data length " +
                std::to_string(compressedLength) +
                " exceeds remaining file size " + std::to_string(r.remaining()));
        h.compressedData = r.readBytes(compressedLength);

        if (!r.atEnd()) {
            h.onlineId = r.readU64();
            if (h.mods & kModTarget) {
                h.hasTargetPracticeInfo = true;
                h.targetPracticeAccuracy = r.readF64();
            }
            h.trailingBytes = r.remaining();
            if (h.trailingBytes > 0)
                h.tailRaw = r.readBytes(h.trailingBytes);
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    // Tail block (osu!lazer >= 30000000 appends i32 length + LZMA(JSON)
    // after the legacy fields). parseLazerExtra never throws; for stable
    // files with junk trailing bytes it just reports present=false + error,
    // which the UI surfaces as a weak hint without blocking. isLazer comes
    // from the version alone: a lazer replay with a stripped/absent tail is
    // still a lazer replay (declared counts fall back to the 6 ushorts).
    if (result.error.empty()) {
        result.header.isLazer =
            isLazerGameVersion(result.header.gameVersion);
        if (!result.header.tailRaw.empty())
            result.header.lazerExtra = parseLazerExtra(
                result.header.tailRaw.data(), result.header.tailRaw.size());
    }
    return result;
}

}  // namespace mra
