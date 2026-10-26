#pragma once
// Serves frontend/dist assets embedded into the executable as RCDATA
// resources (see backend/cmake/embed_frontend.cmake). In dev builds without
// embedding, falls back to a dist directory on disk.
#include <functional>
#include <string>
#include <vector>

namespace mra {

struct EmbeddedAsset {
    std::string mime;
    std::vector<uint8_t> bytes;
};

// Tries the embedded RCDATA resources first, then the distDir fallback.
// Returns false when the path is unknown (404).
using AssetProvider =
    std::function<bool(const std::string& urlPath, EmbeddedAsset& out)>;

AssetProvider makeDefaultAssetProvider(const std::wstring& distDir);

// Resource identifiers used in the generated .rc (path -> resource name).
bool loadEmbeddedResource(int resourceId, std::vector<uint8_t>& out);

}  // namespace mra
