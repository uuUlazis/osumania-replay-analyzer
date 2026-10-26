#include "app/embedded_assets.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#include <filesystem>

#include "embedded_files.h"   // generated: defines MANIA_HAS_EMBEDDED + tables
#if MANIA_HAS_EMBEDDED
#else
static_assert(true, "no embedded frontend; disk fallback only");
#endif

namespace mra {
namespace {

const char* mimeForPath(const std::string& p) {
    auto endsWith = [&p](const char* suffix) {
        size_t n = std::strlen(suffix);
        return p.size() >= n && p.compare(p.size() - n, n, suffix) == 0;
    };
    if (endsWith(".html") || endsWith(".htm")) return "text/html";
    if (endsWith(".js") || endsWith(".mjs")) return "text/javascript";
    if (endsWith(".css")) return "text/css";
    if (endsWith(".svg")) return "image/svg+xml";
    if (endsWith(".png")) return "image/png";
    if (endsWith(".jpg") || endsWith(".jpeg")) return "image/jpeg";
    if (endsWith(".webp")) return "image/webp";
    if (endsWith(".gif")) return "image/gif";
    if (endsWith(".bmp")) return "image/bmp";
    if (endsWith(".ico")) return "image/x-icon";
    if (endsWith(".woff2")) return "font/woff2";
    if (endsWith(".woff")) return "font/woff";
    if (endsWith(".json")) return "application/json";
    if (endsWith(".map")) return "application/json";
    return "application/octet-stream";
}

}  // namespace

bool loadEmbeddedResource(int resourceId, std::vector<uint8_t>& out) {
    HRSRC handle =
        FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId),
                      MAKEINTRESOURCEW(10) /* RT_RCDATA, W form */);
    if (!handle) return false;
    HGLOBAL loaded = LoadResource(nullptr, handle);
    if (!loaded) return false;
    void* data = LockResource(loaded);
    DWORD size = SizeofResource(nullptr, handle);
    if (!data || size == 0) return false;
    out.assign(static_cast<const uint8_t*>(data),
               static_cast<const uint8_t*>(data) + size);
    return true;
}

AssetProvider makeDefaultAssetProvider(const std::wstring& distDir) {
    return [distDir](const std::string& urlPath, EmbeddedAsset& out) -> bool {
        // URL path: "/" -> index.html; strip query. Reject traversal.
        std::string rel = urlPath.empty() ? "/" : urlPath;
        size_t q = rel.find('?');
        if (q != std::string::npos) rel = rel.substr(0, q);
        if (rel == "/") rel = "/index.html";
        if (rel.find("..") != std::string::npos) return false;
        std::string key = rel.substr(1);  // drop leading slash

#if MANIA_HAS_EMBEDDED
        if (const EmbeddedFileEntry* file = embeddedFileFor(key.c_str())) {
            std::vector<uint8_t> bytes;
            if (loadEmbeddedResource(file->resourceId, bytes)) {
                out.mime = file->mime;
                out.bytes = std::move(bytes);
                return true;
            }
        }
#endif
        // Dev fallback: read from distDir on disk.
        // C++20: construct from u8string instead of deprecated u8path().
        std::filesystem::path full =
            std::filesystem::path(distDir) /
            std::filesystem::path(reinterpret_cast<const char8_t*>(key.c_str()));
        std::error_code ec;
        if (!std::filesystem::is_regular_file(full, ec)) return false;
        FILE* f = nullptr;
        if (_wfopen_s(&f, full.c_str(), L"rb") != 0 || !f) return false;
        out.bytes.clear();
        uint8_t buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            out.bytes.insert(out.bytes.end(), buf, buf + n);
        fclose(f);
        out.mime = mimeForPath(key);
        return true;
    };
}

}  // namespace mra
