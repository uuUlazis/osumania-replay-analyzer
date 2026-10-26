#pragma once
// Skin element loader for mania column rendering.
// Phase 1 ("minimal"): locate key images per column (note/hold head/hold body
// 2.x patterns from the reference loader) with plain GDI+ HBITMAP-free decode
// via GDI+ not required - we keep raw PNG/JPG bytes and decode on demand.
// Phase 2: full .osu element mapping (extras, colours,combo colours).
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mra {

struct SkinImage {
    std::string elementName;        // e.g. "mania-note1"
    std::vector<uint8_t> bytes;     // raw file bytes (PNG/JPG), lazy-loaded
    std::string fileName;           // resolved file name
    std::string filePath;           // absolute source path (bytes read on demand)
};

struct SkinDefinition {
    std::string name;
    std::string folder;                              // absolute path
    std::map<std::string, SkinImage> images;         // by element name
    std::vector<std::string> warnings;
};

class SkinLoader {
  public:
    // Loads a skin folder: scans for image files, indexes lowercase element
    // names without extension (osu! lookup rules, ini-style @2x suffix).
    static SkinLoader loadFromFolder(const std::string& folder);

    // osu! mania element lookup per column: mania-note1..N, mania-note1T
    // (hold tail/body uses mania-note1 with H suffix on hold bodies).
    std::optional<SkinImage> findManiaNote(int keyCount, int column) const;
    std::optional<SkinImage> findManiaHoldHead(int keyCount, int column) const;
    std::optional<SkinImage> findManiaHoldBody(int keyCount, int column) const;
    std::optional<SkinImage> findElement(const std::string& element) const;

    const SkinDefinition& definition() const { return def_; }

  private:
    SkinDefinition def_;
};

}  // namespace mra
