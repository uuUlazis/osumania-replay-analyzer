#include "lazer/lazer_format.h"

#include "replay/replay_decoder.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <cstring>

namespace mra {
namespace {

// ASCII-only lowercase normalization: statistics keys are HitResult enum names
// ("Perfect"/"GREAT"/"perfect" — the exact casing on disk is not pinned down,
// so every lookup goes through this). Non-ASCII bytes are left untouched.
std::string normalizeKey(const std::string& key) {
    std::string out = key;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Tolerant integer extraction: accepts signed/unsigned JSON integers and
// integral-valued floats (some serializers emit 850.0). Strings, bools, null
// and non-integral floats are rejected -> caller skips the key.
bool tryGetInt64(const nlohmann::json& value, int64_t& out) {
    if (value.is_number_integer()) {  // covers signed and unsigned
        if (value.is_number_unsigned()) {
            const uint64_t u = value.get<uint64_t>();
            if (u > static_cast<uint64_t>(INT64_MAX)) return false;
            out = static_cast<int64_t>(u);
        } else {
            out = value.get<int64_t>();
        }
        return true;
    }
    if (value.is_number_float()) {
        const double d = value.get<double>();
        if (!std::isfinite(d) || d != std::floor(d) ||
            d < -9.2e18 || d > 9.2e18)
            return false;
        out = static_cast<int64_t>(d);
        return true;
    }
    return false;
}

// Copies an integer-valued JSON object into a lowercase-keyed map. Unknown
// keys are stored too (they are cheap and informative); non-integer values are
// skipped. Never throws for object inputs.
void collectIntObject(const nlohmann::json& obj,
                      std::map<std::string, int64_t>& out) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        int64_t v = 0;
        if (tryGetInt64(it.value(), v))
            out[normalizeKey(it.key())] = v;
    }
}

}  // namespace

bool isLazerGameVersion(uint32_t gameVersion) {
    return gameVersion >= kFirstLazerVersion;
}

LazerScoreExtra parseLazerExtra(const uint8_t* data, size_t size) {
    LazerScoreExtra extra;
    if (data == nullptr || size == 0)
        return extra;  // nothing after the stable fields: not an error
    if (size < 4) {
        extra.error = "lazer tail too short for the 4-byte length frame";
        return extra;
    }

    uint32_t compressed = 0;
    std::memcpy(&compressed, data, sizeof(compressed));  // i32 LE, little-endian host
    if (compressed == 0) {
        extra.error = "lazer tail compressed length is zero";
        return extra;
    }
    if (compressed > size - 4) {
        extra.error = "lazer tail compressed length " +
                      std::to_string(compressed) + " exceeds the " +
                      std::to_string(size - 4) + " trailing bytes";
        return extra;
    }

    // Same stream layout as stable replay data: 5 props + 8 LE size + payload.
    const LzmaDecodeResult decoded = lzmaDecode(data + 4, compressed);
    if (!decoded.ok) {
        extra.error = "lazer tail LZMA decode failed: " + decoded.error;
        return extra;
    }

    std::string json(reinterpret_cast<const char*>(decoded.output.data()),
                     decoded.output.size());
    LazerScoreExtra parsed = parseLazerExtraJson(json);
    if (!parsed.error.empty())
        return parsed;  // JSON-level problem: stays present=false with error
    parsed.jsonRaw = std::move(json);
    return parsed;
}

LazerScoreExtra parseLazerExtraJson(const std::string& json) {
    LazerScoreExtra extra;

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        extra.error = std::string("lazer tail JSON parse failed: ") + e.what();
        return extra;
    } catch (...) {
        extra.error = "lazer tail JSON parse failed: unknown error";
        return extra;
    }
    if (!root.is_object()) {
        extra.error = "lazer tail JSON root is not an object";
        return extra;
    }

    // mods: [{acronym: "DT", settings: {speed_change: 1.25}}, ...]
    // Uniform tolerance: a missing OR wrong-typed field is skipped (only a
    // non-object root above is a structural failure).
    if (root.contains("mods") && root["mods"].is_array()) {
        const nlohmann::json& mods = root["mods"];
        for (const nlohmann::json& m : mods) {
            if (!m.is_object()) continue;  // tolerant: skip malformed entries
            LazerMod mod;
            if (m.contains("acronym") && m["acronym"].is_string())
                mod.acronym = m["acronym"].get<std::string>();
            if (m.contains("settings") && m["settings"].is_object()) {
                for (auto it = m["settings"].begin();
                     it != m["settings"].end(); ++it) {
                    // Raw JSON value text: numbers keep their literal form,
                    // strings stay quoted (value.dump()).
                    mod.settings[it.key()] = it.value().dump();
                }
            }
            extra.mods.push_back(std::move(mod));
        }
    }

    // statistics / maximum_statistics: HitResult name -> count. Keys are
    // normalized to lowercase here so every consumer can look up in one form.
    if (root.contains("statistics") && root["statistics"].is_object()) {
        collectIntObject(root["statistics"], extra.statistics);
        extra.hasStatistics = true;
    }
    // LegacyReplaySoloScoreInfo serializes snake_case; accept the camelCase
    // alias too in case the wire format shifts.
    const nlohmann::json* maxStats = nullptr;
    if (root.contains("maximum_statistics") &&
        root["maximum_statistics"].is_object())
        maxStats = &root["maximum_statistics"];
    else if (root.contains("maximumStatistics") &&
             root["maximumStatistics"].is_object())
        maxStats = &root["maximumStatistics"];
    if (maxStats) {
        collectIntObject(*maxStats, extra.maximumStatistics);
        extra.hasMaximumStatistics = true;
    }

    if (root.contains("rank") && root["rank"].is_string())
        extra.rank = root["rank"].get<std::string>();

    const nlohmann::json* score = nullptr;
    if (root.contains("total_score_without_mods"))
        score = &root["total_score_without_mods"];
    else if (root.contains("totalScoreWithoutMods"))
        score = &root["totalScoreWithoutMods"];
    if (score) {
        int64_t v = 0;
        if (tryGetInt64(*score, v))
            extra.totalScoreWithoutMods = v;
    }

    extra.present = true;
    extra.error.clear();
    return extra;
}

LazerManiaCounts lazerManiaCountsFromStatistics(
    const std::map<std::string, int64_t>& statistics) {
    LazerManiaCounts counts;
    counts.fromJson = true;

    // Only the six mania judgements count; IgnoreHit / ComboBreak / small-drum
    // keys and anything unknown are ignored. Keys arrive already lowercase.
    bool any = false;
    auto pick = [&](const char* key, int64_t& out) {
        auto it = statistics.find(key);
        if (it == statistics.end()) return;
        out = it->second;
        any = true;
    };
    pick("perfect", counts.perfect);
    pick("great", counts.great);
    pick("good", counts.good);
    pick("ok", counts.ok);
    pick("meh", counts.meh);
    pick("miss", counts.miss);

    counts.valid = any;
    return counts;
}

double lazerAccuracy305Percent(const LazerManiaCounts& c) {
    const double total = static_cast<double>(c.perfect) +
                         static_cast<double>(c.great) +
                         static_cast<double>(c.good) +
                         static_cast<double>(c.ok) +
                         static_cast<double>(c.meh) +
                         static_cast<double>(c.miss);
    if (total <= 0.0) return 100.0;
    const double weighted = 305.0 * static_cast<double>(c.perfect) +
                            300.0 * static_cast<double>(c.great) +
                            200.0 * static_cast<double>(c.good) +
                            100.0 * static_cast<double>(c.ok) +
                            50.0 * static_cast<double>(c.meh);
    return weighted / (305.0 * total) * 100.0;
}

}  // namespace mra
