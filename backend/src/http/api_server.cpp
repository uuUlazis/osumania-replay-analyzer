#include "http/api_server.h"

#include "io/paths.h"

#include "app/dialogs.h"
#include "beatmap/osu_parser.h"
#include "io/md5.h"
#include "mania/analysis.h"
#include "mania/hit_matcher.h"
#include "mania/judgement.h"
#include "replay/osr_reader.h"
#include "replay/replay_decoder.h"
#include "replay/replay_frames.h"
#include "session/session_store.h"
#include "skin/skin_loader.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <objbase.h>  // CoInitializeEx (native dialogs)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>

namespace mra {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::optional<std::string> readFileBytes(const std::filesystem::path& p,
                                         std::vector<uint8_t>& out) {
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec))
        return "file not found: " + mra::pathToUtf8(p);
    std::ifstream f(p, std::ios::binary);
    if (!f) return "cannot open: " + mra::pathToUtf8(p);
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size > 512ull * 1024 * 1024) return "file too large: " + mra::pathToUtf8(p);
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), size);
    if (!f && size > 0) return "short read: " + mra::pathToUtf8(p);
    return std::nullopt;
}

std::optional<std::string> readFileBytes(const std::string& path,
                                         std::vector<uint8_t>& out) {
    return readFileBytes(mra::pathFromUtf8(path), out);
}

std::optional<std::string> readTextFile(const std::string& path,
                                        std::string& out,
                                        std::string* md5Hex = nullptr) {
    std::vector<uint8_t> bytes;
    auto err = readFileBytes(path, bytes);
    if (err) return err;
    if (md5Hex) {
        Md5 md5;
        md5.update(bytes.data(), bytes.size());
        *md5Hex = Md5::toHex(md5.digest());
    }
    // Strip UTF-8 BOM if present; osu files may include it.
    size_t start = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
        bytes[2] == 0xBF)
        start = 3;
    out.assign(reinterpret_cast<const char*>(bytes.data() + start),
               bytes.size() - start);
    return std::nullopt;
}

json okResponse(const json& data) {
    return json{{"ok", true}, {"data", data}};
}

json errorResponse(const std::string& code, const std::string& message,
                   const json& details = json::object()) {
    return json{{"ok", false},
                {"error", json{{"code", code}, {"message", message},
                               {"details", details}}}};
}

void sendError(httplib::Response& res, int httpStatus,
               const std::string& code, const std::string& message,
               const json& details = json::object()) {
    res.status = httpStatus;
    res.set_content(errorResponse(code, message, details).dump(), "application/json");
}

const char* hitResultNameJson(HitResult r) {
    switch (r) {
        case HitResult::Perfect: return "perfect";
        case HitResult::Great: return "great";
        case HitResult::Good: return "good";
        case HitResult::Ok: return "ok";
        case HitResult::Meh: return "meh";
        case HitResult::Miss: return "miss";
    }
    return "?";
}

const char* hitPhaseNameJson(HitPhase p) {
    switch (p) {
        case HitPhase::NoteHead: return "note_head";
        case HitPhase::HoldHead: return "hold_head";
        case HitPhase::HoldTail: return "hold_tail";
    }
    return "?";
}

json countsJson(const JudgementCounts& c) {
    return json{{"perfect", c.perfect}, {"great", c.great}, {"good", c.good},
                {"ok", c.ok}, {"meh", c.meh}, {"miss", c.miss}};
}

json offsetStatsJson(const OffsetStats& s, int digits = 2) {
    auto r = [](double v, int d) {
        double f = std::pow(10.0, d);
        return std::round(v * f) / f;
    };
    return json{{"count", s.count},
                {"mean", r(s.mean, digits)},
                {"meanAbs", r(s.meanAbs, digits)},
                {"median", r(s.median, digits)},
                {"sd", r(s.sd, digits)},
                {"p95", r(s.p95, digits)},
                {"early", s.early},
                {"late", s.late},
                {"earlyMean", r(s.earlyMean, digits)},
                {"lateMean", r(s.lateMean, digits)},
                {"min", r(s.min, digits)},
                {"max", r(s.max, digits)}};
}

json columnStatsJson(const std::vector<ColumnStats>& cols) {
    json arr = json::array();
    for (const auto& c : cols) {
        arr.push_back(json{{"column", c.column},
                           {"counts", countsJson(c.counts)},
                           {"offsets", offsetStatsJson(c.offsets)}});
    }
    return arr;
}

}  // namespace

LoadOutcome loadReplaySession(const std::string& replayPath,
                              const std::string& beatmapPath,
                              const std::string& audioPath,
                              const std::string& skinFolder) {
    // 1. .osr
    std::vector<uint8_t> osrBytes;
    if (auto err = readFileBytes(replayPath, osrBytes))
        return LoadOutcome{false, "", "REPLAY_FILE_NOT_FOUND", *err, ""};
    OsrParseResult osr = parseOsr(osrBytes);
    if (!osr.error.empty())
        return LoadOutcome{false, "", "REPLAY_PARSE_FAILED", osr.error, ""};

    // 2. LZMA decode
    LzmaDecodeResult decoded = lzmaDecode(osr.header.compressedData.data(),
                                          osr.header.compressedData.size());
    if (!decoded.ok)
        return LoadOutcome{false, "", "REPLAY_DECODE_FAILED",
                           decoded.error.empty() ? "unknown LZMA error"
                                                 : decoded.error,
                           ""};

    // 3. frames
    ReplayFrameDecodeResult decodedFrames = decodeReplayFrames(decoded.output);
    if (!decodedFrames.ok)
        return LoadOutcome{false, "", "REPLAY_FRAMES_INVALID",
                           decodedFrames.error, ""};
    std::vector<ReplayFrame> frames = std::move(decodedFrames.frames);

    // 4. .osu
    std::string osuText;
    std::string osuMd5;
    if (auto err = readTextFile(beatmapPath, osuText, &osuMd5))
        return LoadOutcome{false, "", "BEATMAP_FILE_NOT_FOUND", *err, ""};
    OsuParseResult parsedBeatmap = parseOsu(osuText);
    if (!parsedBeatmap.ok)
        return LoadOutcome{false, "", "BEATMAP_PARSE_FAILED",
                           parsedBeatmap.error.empty()
                               ? "unable to parse beatmap"
                               : parsedBeatmap.error,
                           ""};
    OsuBeatmap beatmap = std::move(parsedBeatmap.beatmap);
    if (!beatmap.isMania())
        return LoadOutcome{false, "", "BEATMAP_NOT_MANIA",
                           "beatmap mode is not osu!mania", ""};

    // 5. match + analyze
    int keyCount = keyCountFromMods(osr.header.mods);
    if (keyCount <= 0) keyCount = beatmap.keyCount;
    MatchOptions matchOptions;
    matchOptions.windows = ManiaWindows::fromOD(beatmap.overallDifficulty);
    matchOptions.mirrorColumns = (osr.header.mods & kModMirror) != 0;
    MatchResult match =
        matchReplayToObjects(beatmap.objects, keyCount, frames, matchOptions);
    GlobalAnalysis analysis = analyzeMatch(match, keyCount);

    // Second pass under ScoreV2 rules for the renderer's V2 judgement view
    // (LN head late window ok*1.5, tail judged independently on |endDiff|/1.5).
    MatchOptions v2Options = matchOptions;
    v2Options.scoreV2 = true;
    MatchResult matchV2 =
        matchReplayToObjects(beatmap.objects, keyCount, frames, v2Options);

    ReplaySession session;
    session.replayPath = replayPath;
    session.beatmapPath = beatmapPath;
    session.beatmapMd5 = osuMd5;
    session.skinFolder = skinFolder.empty() ? std::nullopt
                                            : std::optional<std::string>(skinFolder);
    session.replay = std::move(osr);
    session.frames = std::move(frames);
    session.keyCount = keyCount;
    session.beatmap = std::move(beatmap);
    session.match = std::move(match);
    session.matchV2 = std::move(matchV2);
    session.analysis = std::move(analysis);
    if (!audioPath.empty()) {
        std::error_code ec;
        if (fs::exists(audioPath, ec) && fs::is_regular_file(audioPath, ec))
            session.audioPath = audioPath;
    }
    session.ready = true;

    std::string id = SessionStore::instance().create(std::move(session));
    return LoadOutcome{true, id, "", "", ""};
}

void ApiServer::registerRoutes(httplib::Server& svr) {
    // ---- App ----
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(okResponse(json{{"status", "up"}}).dump(),
                        "application/json");
    });
    svr.Get("/api/app/info", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(okResponse(json{
            {"name", "osu!mania Replay Analyzer"},
            {"apiVersion", 1},
            {"platform", "windows"}}).dump(),
                        "application/json");
    });

    // ---- Replay load ----
    svr.Post("/api/replays/load",
             [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(res, 400, "INVALID_JSON", "request body is not valid JSON");
        }
        std::string replayPath = body.value("replayPath", "");
        std::string beatmapPath = body.value("beatmapPath", "");
        std::string audioPath = body.value("audioPath", "");
        std::string skinFolder = body.value("skinFolder", "");
        if (replayPath.empty() || beatmapPath.empty())
            return sendError(res, 400, "MISSING_PATH",
                             "replayPath and beatmapPath are required");
        LoadOutcome out =
            loadReplaySession(replayPath, beatmapPath, audioPath, skinFolder);
        if (!out.ok)
            return sendError(res, 422, out.errorCode, out.message);
        res.set_content(
            okResponse(json{{"sessionId", out.sessionId}}).dump(),
            "application/json");
    });

    // ---- Replay metadata ----
    svr.Get(R"(/api/replays/([^/]+)/metadata)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        const OsrHeader& h = s.replay.header;
        res.set_content(okResponse(json{
            {"player", h.playerName},
            {"gameMode", h.mode},
            {"gameVersion", h.gameVersion},
            {"beatmapMd5", h.beatmapMd5},
            {"replayMd5", h.replayMd5},
            {"score", h.score},
            {"maxCombo", h.maxCombo},
            {"perfectCombo", h.perfect},
            {"mods", h.mods},
            {"timestamp", h.timestamp},
            {"counts", json{{"n300", h.count300}, {"n100", h.count100},
                            {"n50", h.count50}, {"gekki", h.countGeki},
                            {"katu", h.countKatu}, {"miss", h.countMiss}}},
            {"keyCount", s.keyCount},
            {"frameCount", s.frames.size()},
            {"replayPath", s.replayPath}}).dump(),
                        "application/json");
    });

    // ---- Frames (paged by time range) ----
    svr.Get(R"(/api/replays/([^/]+)/frames)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        int64_t from = req.has_param("from") ? std::stoll(req.get_param_value("from")) : 0;
        int64_t to = req.has_param("to")
                         ? std::stoll(req.get_param_value("to"))
                         : INT64_MAX;
        size_t limit = req.has_param("limit")
                           ? static_cast<size_t>(std::stoll(req.get_param_value("limit")))
                           : 20000;
        json arr = json::array();
        size_t startIdx = 0, endIdx = s.frames.size();
        // frames are time-sorted: binary search the range
        auto itLo = std::lower_bound(
            s.frames.begin(), s.frames.end(), from,
            [](const ReplayFrame& f, int64_t t) { return f.timeMs < t; });
        if (itLo != s.frames.end()) startIdx = itLo - s.frames.begin();
        auto itHi = std::upper_bound(
            s.frames.begin(), s.frames.end(), to,
            [](int64_t t, const ReplayFrame& f) { return t < f.timeMs; });
        endIdx = itHi - s.frames.begin();
        for (size_t i = startIdx; i < endIdx && arr.size() < limit; ++i) {
            const ReplayFrame& f = s.frames[i];
            arr.push_back(json{{"timeMs", f.timeMs},
                               {"keys", f.rawKeys},
                               {"isSeed", f.isSeedFrame}});
        }
        res.set_content(okResponse(json{
            {"from", from},
            {"to", to == INT64_MAX ? json(nullptr) : json(to)},
            {"returned", arr.size()},
            {"totalInRange", endIdx - startIdx},
            {"frames", std::move(arr)}}).dump(),
                        "application/json");
    });

    // ---- Beatmap metadata ----
    svr.Get(R"(/api/beatmaps/([^/]+)/metadata)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        const OsuBeatmap& b = s.beatmap;
        res.set_content(okResponse(json{
            {"title", b.title}, {"artist", b.artist}, {"creator", b.creator},
            {"version", b.version}, {"beatmapId", b.beatmapId},
            {"beatmapSetId", b.beatmapSetId}, {"mode", b.mode},
            {"keyCount", b.keyCount}, {"od", b.overallDifficulty},
            {"hp", b.hpDrain},
            {"audioFilename", b.audioFilename},
            {"backgroundFile", b.backgroundFile},
            {"objectCount", b.objects.size()},
            {"beatmapPath", s.beatmapPath}}).dump(),
                        "application/json");
    });

    // ---- Beatmap objects (by time range) ----
    svr.Get(R"(/api/beatmaps/([^/]+)/objects)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        int64_t from = req.has_param("from") ? std::stoll(req.get_param_value("from")) : 0;
        int64_t to = req.has_param("to")
                         ? std::stoll(req.get_param_value("to"))
                         : INT64_MAX;
        json arr = json::array();
        for (const auto& o : s.beatmap.objects) {
            if (o.startTimeMs < from) continue;
            if (to != INT64_MAX && o.startTimeMs >= to) continue;
            json j = json{{"index", o.sourceIndex},
                          {"column", o.column},
                          {"startTimeMs", o.startTimeMs},
                          {"type", o.type == ManiaObjectType::Hold ? "hold" : "note"}};
            if (o.endTimeMs) j["endTimeMs"] = *o.endTimeMs;
            arr.push_back(std::move(j));
        }
        res.set_content(okResponse(json{{"from", from},
                                        {"to", to == INT64_MAX ? json(nullptr) : json(to)},
                                        {"objects", std::move(arr)}}).dump(),
                        "application/json");
    });

    // ---- Analysis summary ----
    svr.Get(R"(/api/analysis/([^/]+)/summary)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        const GlobalAnalysis& g = s.analysis;
        json hist = json::array();
        for (size_t i = 0; i < g.histogram.buckets.size(); ++i) {
            hist.push_back(json{
                {"fromMs", g.histogram.minMs + static_cast<int64_t>(i) * 5},
                {"toMs", g.histogram.minMs + static_cast<int64_t>(i + 1) * 5},
                {"count", g.histogram.buckets[i]}});
        }
        json breakdown = json::array();
        for (const auto& b : g.breakdown)
            breakdown.push_back(json{{"result", hitResultNameJson(b.result)},
                                     {"count", b.count},
                                     {"share", std::round(b.share * 100) / 100}});
        json consistency = json::array();
        // OSR declared counts vs reconstructed.
        const OsrHeader& h = s.replay.header;
        // Per-grade reconstruction can't reproduce stable's exact MAX/300 or
        // geki/katu split (LN tail leniency), so only require the judged
        // TOTAL (V1/V2 both tally heads + tails) to match the declared one.
        const int64_t declaredTotal =
            static_cast<int64_t>(h.count300) + static_cast<int64_t>(h.count100) +
            static_cast<int64_t>(h.count50) + static_cast<int64_t>(h.countGeki) +
            static_cast<int64_t>(h.countKatu) + static_cast<int64_t>(h.countMiss);
        bool countsMatch = declaredTotal == static_cast<int64_t>(g.counts.judged());
        consistency.push_back(json{{"check", "beatmapMd5Match"},
                                   {"pass", s.beatmapMd5 == h.beatmapMd5}});
        consistency.push_back(json{{"check", "modeIsMania"},
                                   {"pass", s.beatmap.isMania()}});
        consistency.push_back(json{{"check", "judgementCountsMatch"},
                                   {"pass", countsMatch}});
        consistency.push_back(json{{"check", "replayEndedNormally"},
                                   {"pass", s.match.replayEndedNormally}});
        res.set_content(okResponse(json{
            {"counts", countsJson(g.counts)},
            {"headCounts", countsJson(g.headCounts)},
            {"accuracy", json{{"scoreV1", std::round(g.accuracyV1 * 100000) / 100000},
                              {"scoreV2", std::round(g.accuracyV2 * 100000) / 100000}}},
            {"offsets", offsetStatsJson(g.offsets)},
            {"tailOffsets", offsetStatsJson(g.tailOffsets)},
            {"histogram", hist},
            {"histogramBucketMs", 5.0},
            {"perColumn", columnStatsJson(g.perColumn)},
            {"breakdown", breakdown},
            {"consistency", consistency},
            {"consistencyReport", json{
                {"rolling20Sd", offsetStatsJson(g.consistency.rolling20Sd)},
                {"chordOffsetSpread", g.consistency.chordOffsetSpread},
                {"chordCount", g.consistency.chordCount},
                {"tapStability", std::round(g.consistency.tapStability * 1000) / 1000}}},
            {"unmatchedInputCount", g.unmatchedInputCount},
            {"unmatchedObjects", s.match.unmatchedObjects},
            {"firstEventTimeMs", g.firstEventTimeMs},
            {"lastEventTimeMs", g.lastEventTimeMs},
            {"osrDeclared", json{
                {"counts", json{{"n300", h.count300}, {"n100", h.count100},
                                {"n50", h.count50}, {"gekki", h.countGeki},
                                {"katu", h.countKatu}, {"miss", h.countMiss}}},
                {"maxCombo", h.maxCombo},
                {"score", h.score}}}}).dump(),
                        "application/json");
    });

    // ---- Hits in range ----
    svr.Get(R"(/api/analysis/([^/]+)/hits)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        int64_t from = req.has_param("from") ? std::stoll(req.get_param_value("from")) : INT64_MIN;
        int64_t to = req.has_param("to") ? std::stoll(req.get_param_value("to")) : INT64_MAX;
        size_t limit = req.has_param("limit")
                           ? static_cast<size_t>(std::stoll(req.get_param_value("limit")))
                           : 5000;
        json arr = json::array();
        // version=v2 selects the ScoreV2 match pass (default v1/stable).
        const bool v2 = req.has_param("version") &&
                        req.get_param_value("version") == "v2";
        const std::vector<HitEvent>& events =
            v2 ? s.matchV2.events : s.match.events;
        // events are time-sorted by objectTimeMs: binary search lower bound
        auto lo = std::lower_bound(
            events.begin(), events.end(), from,
            [](const HitEvent& e, int64_t t) { return e.objectTimeMs < t; });
        for (auto it = lo; it != events.end() && arr.size() < limit; ++it) {
            if (it->objectTimeMs >= to) break;
            // objectIndex is a vector position; expose the source-stable id
            // (same value as /objects "index") so the frontend can join.
            const size_t oi = static_cast<size_t>(it->objectIndex);
            json j = json{{"objectIndex", oi < s.beatmap.objects.size()
                                         ? s.beatmap.objects[oi].sourceIndex
                                         : it->objectIndex},
                          {"column", it->column},
                          {"objectTimeMs", it->objectTimeMs},
                          {"judgement", hitResultNameJson(it->judgement)},
                          {"phase", hitPhaseNameJson(it->phase)}};
            if (it->v1Judgement)
                j["v1Judgement"] = hitResultNameJson(*it->v1Judgement);
            if (it->actualTimeMs) j["actualTimeMs"] = *it->actualTimeMs;
            if (it->offsetMs)
                j["offsetMs"] = std::round(*it->offsetMs * 100) / 100;
            arr.push_back(std::move(j));
        }
        res.set_content(okResponse(json{{"hits", std::move(arr)}}).dump(),
                        "application/json");
    });

    // ---- Optional session audio (music synced to the preview) ----
    svr.Get(R"(/api/audio/([^/]+))",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        if (!s.audioPath || s.audioPath->empty())
            return sendError(res, 404, "NO_AUDIO", "session has no audio");
        std::vector<uint8_t> bytes;
        if (auto err = readFileBytes(*s.audioPath, bytes))
            return sendError(res, 422, "AUDIO_READ_FAILED", *err);
        std::string lower = *s.audioPath;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const char* mime = "application/octet-stream";
        if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".flac") == 0)
            mime = "audio/flac";
        else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".mp3") == 0)
            mime = "audio/mpeg";
        else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".ogg") == 0)
            mime = "audio/ogg";
        else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".wav") == 0)
            mime = "audio/wav";
        else if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".m4a") == 0)
            mime = "audio/mp4";
        res.set_content(std::string(bytes.begin(), bytes.end()), mime);
    });

    // ---- Hold notes (head/tail judgements) ----
    svr.Get(R"(/api/analysis/([^/]+)/holds)",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        json arr = json::array();
        for (const auto& h : s.match.holds) {
            json j;
            const size_t hoi = static_cast<size_t>(h.head.objectIndex);
            j["objectIndex"] = hoi < s.beatmap.objects.size()
                                    ? s.beatmap.objects[hoi].sourceIndex
                                    : h.head.objectIndex;
            j["column"] = h.head.column;
            j["headTimeMs"] = h.head.objectTimeMs;
            j["tailTimeMs"] = h.tail.objectTimeMs;
            j["headJudgement"] = hitResultNameJson(h.head.judgement);
            j["tailJudgement"] = hitResultNameJson(h.tail.judgement);
            j["releasedEarly"] = h.releasedEarly;
            j["heldThroughTail"] = h.heldThroughTail;
            j["headOffsetMs"] = h.head.offsetMs
                                    ? json(std::round(*h.head.offsetMs * 100) / 100)
                                    : json(nullptr);
            j["tailOffsetMs"] = h.tail.offsetMs
                                    ? json(std::round(*h.tail.offsetMs * 100) / 100)
                                    : json(nullptr);
            arr.push_back(std::move(j));
        }
        res.set_content(okResponse(json{{"holds", std::move(arr)}}).dump(),
                        "application/json");
    });

    // ---- Segment analysis ----
    svr.Post(R"(/api/analysis/([^/]+)/segment)",
             [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(res, 400, "INVALID_JSON", "request body is not valid JSON");
        }
        if (!body.contains("startMs") || !body.contains("endMs"))
            return sendError(res, 400, "MISSING_RANGE",
                             "startMs and endMs are required");
        int64_t startMs = body["startMs"].get<int64_t>();
        int64_t endMs = body["endMs"].get<int64_t>();
        if (endMs <= startMs)
            return sendError(res, 400, "INVALID_RANGE", "endMs must be > startMs");
        SegmentAnalysis seg = analyzeSegment(s.match, s.keyCount, startMs, endMs);
        res.set_content(okResponse(json{
            {"startMs", seg.startTimeMs},
            {"endMs", seg.endTimeMs},
            {"counts", countsJson(seg.counts)},
            {"headCounts", countsJson(seg.headCounts)},
            {"accuracyScoreV1", std::round(seg.accuracyV1 * 100000) / 100000},
            {"accuracyScoreV2", std::round(seg.accuracyV2 * 100000) / 100000},
            {"offsets", offsetStatsJson(seg.offsets)},
            {"perColumn", columnStatsJson(seg.perColumn)}}).dump(),
                        "application/json");
    });

    // ---- Skin ----
    svr.Post("/api/skins/load",
             [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return sendError(res, 400, "INVALID_JSON", "request body is not valid JSON");
        }
        std::string folder = body.value("folder", "");
        if (folder.empty())
            return sendError(res, 400, "MISSING_PATH", "folder is required");
        SkinLoader loader = SkinLoader::loadFromFolder(folder);
        json resources = json::array();
        for (auto& [name, img] : loader.definition().images)
            resources.push_back(name);
        res.set_content(okResponse(json{{"name", loader.definition().name},
                                        {"folder", loader.definition().folder},
                                        {"warnings", loader.definition().warnings},
                                        {"resources", resources}}).dump(),
                        "application/json");
    });

    svr.Get(R"(/api/skins/([^/]+)/resource/([a-zA-Z0-9_@/.-]+))",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        if (!s.skinFolder)
            return sendError(res, 404, "SKIN_NOT_LOADED", "no skin loaded");
        SkinLoader loader = SkinLoader::loadFromFolder(*s.skinFolder);
        std::string element = req.matches[2];
        auto img = loader.findElement(element);
        if (!img) return sendError(res, 404, "RESOURCE_NOT_FOUND", "no such element");
        std::string lower = img->fileName;
        for (char& c : lower) c = static_cast<char>(std::tolower((unsigned char)c));
        const char* mime = lower.ends_with(".png") ? "image/png" : "image/jpeg";
        res.set_content(reinterpret_cast<const char*>(img->bytes.data()),
                        img->bytes.size(), mime);
    });

    // Raw skin file (skin.ini and other whitelisted names), served as text
    // so the frontend can parse [Mania] layout/colour settings locally.
    svr.Get(R"(/api/skins/([^/]+)/file/([a-zA-Z0-9_.-]+))",
            [](const httplib::Request& req, httplib::Response& res) {
        ReplaySession s;
        if (!SessionStore::instance().get(req.matches[1], s))
            return sendError(res, 404, "SESSION_NOT_FOUND", "unknown session id");
        if (!s.skinFolder)
            return sendError(res, 404, "SKIN_NOT_LOADED", "no skin loaded");
        std::string name = req.matches[2];
        if (name.empty() || name.find("..") != std::string::npos)
            return sendError(res, 400, "INVALID_NAME", "bad file name");
        fs::path p = mra::pathFromUtf8(*s.skinFolder) / mra::pathFromUtf8(name);
        std::vector<uint8_t> bytes;
        if (auto err = readFileBytes(p, bytes))
            return sendError(res, 404, "SKIN_FILE_NOT_FOUND", *err);
        res.set_content(reinterpret_cast<const char*>(bytes.data()),
                        bytes.size(), "text/plain; charset=utf-8");
    });

    // ---- Native dialogs ----
    // The HTTP thread pool calls these; COM must be initialized per-thread
    // (STA). RPC_E_CHANGED_MODE means another mode is active - dialogs still
    // work in MTA on modern Windows, so tolerate it.
    auto dialogRoute = [](const httplib::Request& req, httplib::Response& res) {
        HRESULT hrInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool changedMode = (hrInit == RPC_E_CHANGED_MODE);
        struct ComGuard {
            bool changed;
            ~ComGuard() { if (!changed) ::CoUninitialize(); }
        } guard{changedMode};
        json body;
        try {
            if (!req.body.empty()) body = json::parse(req.body);
        } catch (...) {
            return sendError(res, 400, "INVALID_JSON",
                             "request body is not valid JSON");
        }
        if (req.path == "/api/dialog/open-file") {
            std::vector<DialogFilter> filters;
            std::vector<std::pair<std::wstring, std::wstring>> storage;
            if (body.contains("filters") && body["filters"].is_array()) {
                for (const auto& f : body["filters"]) {
                    storage.emplace_back(utf8ToWide(f.value("name", "File")),
                                         utf8ToWide(f.value("pattern", "*.*")));
                }
            }
            if (storage.empty()) {
                storage.emplace_back(L"All Files", L"*.*");
            }
            for (const auto& s : storage)
                filters.push_back({s.first.c_str(), s.second.c_str()});
            std::wstring title =
                utf8ToWide(body.value("title", "Select file"));
            auto picked = openFileDialog(title, filters);
            if (!picked) {
                res.set_content(
                    okResponse(json{{"code", "DIALOG_CANCELLED"}}).dump(),
                    "application/json");
                return;
            }
            res.set_content(okResponse(json{{"path", *picked}}).dump(),
                            "application/json");
            return;
        }
        // /api/dialog/open-folder
        auto picked =
            openFolderDialog(utf8ToWide(body.value("title", "Select folder")));
        if (!picked) {
            res.set_content(
                okResponse(json{{"code", "DIALOG_CANCELLED"}}).dump(),
                "application/json");
            return;
        }
        res.set_content(okResponse(json{{"path", *picked}}).dump(),
                        "application/json");
    };
    svr.Post("/api/dialog/open-file", dialogRoute);
    svr.Post("/api/dialog/open-folder", dialogRoute);

    svr.set_exception_handler(
        [](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
            std::string msg = "unknown error";
            try { if (ep) std::rethrow_exception(ep); }
            catch (const std::exception& e) { msg = e.what(); }
            sendError(res, 500, "INTERNAL_ERROR", msg);
        });

    // ---- Static frontend (embedded RCDATA in release, disk in dev) ----
    svr.Get("/.*", [this](const httplib::Request& req, httplib::Response& res) {
        if (!assetProvider_) return sendError(res, 404, "NOT_FOUND", "no route");
        std::string urlPath = req.path;
        if (urlPath == "/") urlPath = "/index.html";
        EmbeddedAsset asset;
        if (assetProvider_(urlPath, asset)) {
            res.set_content(reinterpret_cast<const char*>(asset.bytes.data()),
                            asset.bytes.size(), asset.mime.c_str());
            return;
        }
        sendError(res, 404, "NOT_FOUND", "asset not found: " + urlPath);
    });
}

ApiServer::ApiServer() = default;

ApiServer::~ApiServer() { stop(); }

bool ApiServer::start(int requestedPort) {
    svr_.reset(new httplib::Server());
    registerRoutes(*svr_);
    // Static frontend assets: provided via staticAssetProvider (embedded in
    // release, disk in dev). Registered by app/main.cpp if available.
    if (requestedPort > 0) {
        if (!svr_->bind_to_port("127.0.0.1", requestedPort)) {
            svr_.reset();
            return false;
        }
        port_ = requestedPort;
    } else {
        int bound = svr_->bind_to_any_port("127.0.0.1");
        if (bound <= 0) {
            svr_.reset();
            return false;
        }
        port_ = bound;
    }
    httplib::Server* raw = svr_.get();
    listenThread_.reset(new std::thread([raw] { raw->listen_after_bind(); }));
    return true;
}

void ApiServer::stop() {
    if (svr_) svr_->stop();
    if (listenThread_ && listenThread_->joinable()) listenThread_->join();
    listenThread_.reset();
    svr_.reset();
}

}  // namespace mra
