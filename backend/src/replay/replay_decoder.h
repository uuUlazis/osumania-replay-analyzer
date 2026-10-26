#pragma once
// LZMA in-memory decode for .osr replay data (public-domain LzmaDec).
#include <cstdint>
#include <string>
#include <vector>

namespace mra {

struct LzmaDecodeResult {
    bool ok = false;
    std::string error;
    std::vector<uint8_t> output;
};

// Decodes a raw .lzma stream: 5-byte props + 8-byte LE uncompressed size
// (size may be 0xFFFFFFFFFFFFFFFF meaning "unknown, read until end marker").
LzmaDecodeResult lzmaDecode(const uint8_t* data, size_t size);

}  // namespace mra
