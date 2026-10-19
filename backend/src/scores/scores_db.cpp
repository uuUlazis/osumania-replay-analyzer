#include "scores/scores_db.h"

#include "io/paths.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace mra {
namespace {

constexpr int64_t kTicksPerMs = 10000;
constexpr int64_t kNetEpochToUnixMs = 62135596800000LL;  // 0001-01-01 -> 1970-01-01
constexpr int64_t kModTargetPractice = 1 << 23;

// Bounds-checked little-endian reader over an in-memory buffer.
class ByteReader {
  public:
    explicit ByteReader(std::vector<uint8_t> data) : data_(std::move(data)) {}

    size_t pos() const { return pos_; }
    size_t remaining() const { return data_.size() - pos_; }

    uint8_t u8() {
        need(1);
        return data_[pos_++];
    }
    uint16_t u16() {
        need(2);
        uint16_t v;
        std::memcpy(&v, data_.data() + pos_, 2);
        pos_ += 2;
        return v;
    }
    int32_t i32() {
        need(4);
        int32_t v;
        std::memcpy(&v, data_.data() + pos_, 4);
        pos_ += 4;
        return v;
    }
    int64_t i64() {
        need(8);
        int64_t v;
        std::memcpy(&v, data_.data() + pos_, 8);
        pos_ += 8;
        return v;
    }
    double f64() {
        need(8);
        double v;
        std::memcpy(&v, data_.data() + pos_, 8);
        pos_ += 8;
        return v;
    }
    void seek(size_t position) { pos_ = position; }

    // osu!string: 0x00 flag = empty, 0x0b flag = ULEB128 length + UTF-8 bytes.
    std::string osuString() {
        size_t at = pos_;
        uint8_t flag = u8();
        if (flag == 0x00) return std::string();
        if (flag != 0x0b)
            throw ParseError("bad osu!string flag at offset " + std::to_string(at));
        uint64_t len = 0;
        int shift = 0;
        for (;;) {
            uint8_t b = u8();
            len |= static_cast<uint64_t>(b & 0x7f) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
            if (shift > 35)
                throw ParseError("runaway ULEB128 length at offset " + std::to_string(at));
        }
        if (len > remaining())
            throw ParseError("osu!string overruns file at offset " + std::to_string(at));
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_),
                      static_cast<size_t>(len));
        pos_ += static_cast<size_t>(len);
        return s;
    }

  private:
    struct ParseError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    void need(size_t n) {
        if (pos_ + n > data_.size())
            throw ParseError("unexpected end of scores.db at offset " +
                             std::to_string(pos_));
    }
    std::vector<uint8_t> data_;
    size_t pos_ = 0;
};

ScoreEntry parseScore(ByteReader& r) {
    ScoreEntry e;
    e.mode = r.u8();
    e.clientVersion = r.i32();
    e.beatmapMd5 = r.osuString();
    e.playerName = r.osuString();
    e.replayMd5 = r.osuString();
    e.c300 = r.u16();
    e.c100 = r.u16();
    e.c50 = r.u16();
    e.geki = r.u16();
    e.katu = r.u16();
    e.miss = r.u16();
    e.totalScore = r.i32();
    e.maxCombo = r.u16();
    e.fullCombo = r.u8() != 0;
    e.mods = r.i32();
    // The HP-graph string is written as an empty osu!string in every known
    // scores.db, but some writers omit it entirely. Try to read it; on a bad
    // flag byte, rewind and continue as if absent. A wrong guess is always
    // caught by the strict -1 sentinel three fields later.
    size_t backtrack = r.pos();
    try {
        e.hpGraph = r.osuString();
    } catch (const std::exception&) {
        r.seek(backtrack);
    }
    e.playedTicks = r.i64();
    int32_t sentinel = r.i32();
    if (sentinel != -1)
        throw std::runtime_error("broken score record at offset " +
                                 std::to_string(r.pos() - 4) +
                                 " (sentinel != -1)");
    e.onlineId = r.i64();
    if (e.mods & kModTargetPractice) r.f64();  // TargetPractice accumulator
    e.playedMs = e.playedTicks / kTicksPerMs - kNetEpochToUnixMs;
    return e;
}

}  // namespace

bool parseScoresDb(const std::string& path, ScoresDb& out, std::string& err) {
    try {
        std::ifstream in(pathFromUtf8(path), std::ios::binary);
        if (!in) {
            err = "cannot open file: " + path;
            return false;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        if (data.size() < 8) {
            err = "file too small to be a scores.db";
            return false;
        }
        ByteReader r(std::move(data));
        out = ScoresDb{};
        out.version = r.i32();
        int32_t groupCount = r.i32();
        if (groupCount < 0 || groupCount > 1000000) {
            err = "implausible beatmap group count";
            return false;
        }
        out.groups.reserve(static_cast<size_t>(groupCount));
        for (int32_t g = 0; g < groupCount; ++g) {
            ScoreGroup group;
            group.beatmapMd5 = r.osuString();
            int32_t scoreCount = r.i32();
            if (scoreCount < 0 || scoreCount > 1000000) {
                err = "implausible score count in group " + std::to_string(g);
                return false;
            }
            group.scores.reserve(static_cast<size_t>(scoreCount));
            for (int32_t i = 0; i < scoreCount; ++i)
                group.scores.push_back(parseScore(r));
            out.groups.push_back(std::move(group));
        }
        if (r.remaining() != 0) {
            err = "trailing bytes after last group: parsed " +
                  std::to_string(r.pos()) + " of " +
                  std::to_string(r.pos() + r.remaining());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

}  // namespace mra
