#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace mra {

// Minimal MD5 (RFC 1321) used for beatmap/replay hash verification.
class Md5 {
public:
    Md5();
    void update(const uint8_t* data, size_t length);
    void update(const std::string& data) {
        update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }
    // Finalizes and returns 16 raw bytes.
    std::vector<uint8_t> digest();

    static std::string hexDigest(const std::string& data);
    static std::string toHex(const std::vector<uint8_t>& raw);

private:
    void processBlock(const uint8_t* block);
    uint32_t a_, b_, c_, d_;
    uint64_t bitLength_;
    uint8_t buffer_[64];
    size_t bufferLength_;
};

}  // namespace mra
