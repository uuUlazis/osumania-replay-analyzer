#include "skin/skin_loader.h"

#include "io/paths.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace mra {
namespace {

namespace fs = std::filesystem;

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string stemOf(const std::string& fileName) {
    std::string name = fileName;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return toLower(name);
}

const char* kImageExts[] = {".png", ".jpg", ".jpeg"};

bool isImageFile(const std::string& name) {
    std::string lower = toLower(name);
    for (const char* ext : kImageExts)
        if (lower.size() > strlen(ext) &&
            lower.compare(lower.size() - strlen(ext), strlen(ext), ext) == 0)
            return true;
    return false;
}

std::string columnSuffix(int column, int keyCount) {
    // osu! mania column keys: 1-9 for 1K..9K, then A-Z cycles (10K->0, etc).
    if (keyCount <= 9) return std::to_string(column + 1);
    int v = column - 9;  // 10K starts at 0
    const char* letters = "0ABCDEFGHIPJKLMNPQSTUVXYZ";
    if (v >= 0 && v < 24) return std::string(1, letters[v]);
    return std::to_string(column + 1);
}

}  // namespace

SkinLoader SkinLoader::loadFromFolder(const std::string& folder) {
    SkinLoader loader;
    loader.def_.folder = folder;
    std::error_code ec;
    fs::path dir = mra::pathFromUtf8(folder);
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        loader.def_.warnings.push_back("skin folder missing: " + folder);
        return loader;
    }
    // Recursive scan so skins organized in subfolders resolve. Shallow
    // paths win on element-name collisions (osu! prefers root files).
    std::vector<fs::path> files;
    for (auto& entry : fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc)) continue;
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(),
              [](const fs::path& a, const fs::path& b) {
                  size_t ad = std::distance(a.begin(), a.end());
                  size_t bd = std::distance(b.begin(), b.end());
                  if (ad != bd) return ad < bd;
                  return a.native() < b.native();
              });
    for (const fs::path& p : files) {
        std::string name = mra::pathToUtf8(p.filename());
        if (!isImageFile(name)) continue;
        SkinImage img;
        img.fileName = name;
        // Bytes are read on demand in findElement: a resource request only
        // needs one file, not every image in the skin. Stored as UTF-8;
        // findElement converts back via pathFromUtf8.
        img.filePath = mra::pathToUtf8(p);
        // Index by plain element name (shallowest wins; files were sorted
        // shallow-first above) ...
        img.elementName = stemOf(name);
        loader.def_.images.emplace(img.elementName, img);
        // ... and by slash-separated relative path so skin.ini references
        // like "NoteImage0L: newnote/LNTail1" resolve directly.
        std::string rel = mra::pathToUtf8(fs::relative(p, dir, ec));
        // Normalize separators: frontend references use forward slashes.
        for (char& c : rel)
            if (c == '\\') c = '/';
        img.elementName = toLower(rel.substr(0, rel.rfind('.')));
        loader.def_.images.emplace(img.elementName, std::move(img));
    }
    loader.def_.name = mra::pathToUtf8(dir.filename());
    return loader;
}

std::optional<SkinImage> SkinLoader::findElement(const std::string& element) const {
    auto it = def_.images.find(toLower(element));
    if (it == def_.images.end()) return std::nullopt;
    if (it->second.bytes.empty() && !it->second.filePath.empty()) {
        std::ifstream imgFile(mra::pathFromUtf8(it->second.filePath),
                              std::ios::binary);
        std::vector<char> raw((std::istreambuf_iterator<char>(imgFile)),
                              std::istreambuf_iterator<char>());
        if (raw.empty()) return std::nullopt;
        SkinImage img = it->second;
        img.bytes.assign(raw.begin(), raw.end());
        return img;
    }
    return it->second;
}

std::optional<SkinImage> SkinLoader::findManiaNote(int keyCount, int column) const {
    std::string suffix = columnSuffix(column, keyCount);
    // 1. mania-note<col>
    if (auto img = findElement("mania-note" + suffix)) return img;
    // 2. stage-hint fallback for empty columns; 3. note-circle generic
    if (auto img = findElement("note-circle")) return img;
    return std::nullopt;
}

std::optional<SkinImage> SkinLoader::findManiaHoldHead(int keyCount, int column) const {
    std::string suffix = columnSuffix(column, keyCount);
    if (auto img = findElement("mania-note" + suffix + "H")) return img;
    return findManiaNote(keyCount, column);
}

std::optional<SkinImage> SkinLoader::findManiaHoldBody(int keyCount, int column) const {
    std::string suffix = columnSuffix(column, keyCount);
    if (auto img = findElement("mania-note" + suffix + "B")) return img;
    if (auto img = findElement("mania-note" + suffix + "T")) return img;
    return std::nullopt;
}

}  // namespace mra
