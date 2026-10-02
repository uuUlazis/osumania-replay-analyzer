#include "io/md5.h"

#include <cstring>

namespace mra {
namespace {

constexpr uint32_t kSinTable[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

constexpr int kShifts[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7,
                             12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,
                             14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16,
                             23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21,
                             6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

inline uint32_t leftRotate(uint32_t value, int amount) {
    return (value << amount) | (value >> (32 - amount));
}

}  // namespace

Md5::Md5()
    : a_(0x67452301),
      b_(0xefcdab89),
      c_(0x98badcfe),
      d_(0x10325476),
      bitLength_(0),
      bufferLength_(0) {}

void Md5::update(const uint8_t* data, size_t length) {
    bitLength_ += static_cast<uint64_t>(length) * 8;
    while (length > 0) {
        size_t take = 64 - bufferLength_;
        if (take > length) take = length;
        std::memcpy(buffer_ + bufferLength_, data, take);
        bufferLength_ += take;
        data += take;
        length -= take;
        if (bufferLength_ == 64) {
            processBlock(buffer_);
            bufferLength_ = 0;
        }
    }
}

void Md5::processBlock(const uint8_t* block) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<uint32_t>(block[i * 4]) |
               static_cast<uint32_t>(block[i * 4 + 1]) << 8 |
               static_cast<uint32_t>(block[i * 4 + 2]) << 16 |
               static_cast<uint32_t>(block[i * 4 + 3]) << 24;
    }
    uint32_t a = a_, b = b_, c = c_, d = d_;
    for (int i = 0; i < 64; ++i) {
        uint32_t f, g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = static_cast<uint32_t>(i);
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = static_cast<uint32_t>((5 * i + 1) % 16);
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = static_cast<uint32_t>((3 * i + 5) % 16);
        } else {
            f = c ^ (b | ~d);
            g = static_cast<uint32_t>((7 * i) % 16);
        }
        uint32_t temp = d;
        d = c;
        c = b;
        b = b + leftRotate(a + f + kSinTable[i] + m[g], kShifts[i]);
        a = temp;
    }
    a_ += a;
    b_ += b;
    c_ += c;
    d_ += d;
}

std::vector<uint8_t> Md5::digest() {
    // Append 0x80, pad with zeros to 56 mod 64, then 64-bit LE bit length.
    uint64_t bitLength = bitLength_;
    uint8_t pad = 0x80;
    uint8_t zero = 0;
    update(&pad, 1);
    while (bufferLength_ != 56) update(&zero, 1);
    // Write length directly; update() would mutate bitLength_.
    uint8_t lengthBytes[8];
    for (int i = 0; i < 8; ++i) lengthBytes[i] = (bitLength >> (8 * i)) & 0xFF;
    std::memcpy(buffer_ + 56, lengthBytes, 8);
    processBlock(buffer_);
    bufferLength_ = 0;

    std::vector<uint8_t> out(16);
    uint32_t regs[4] = {a_, b_, c_, d_};
    for (int i = 0; i < 4; ++i) {
        out[i * 4 + 0] = regs[i] & 0xFF;
        out[i * 4 + 1] = (regs[i] >> 8) & 0xFF;
        out[i * 4 + 2] = (regs[i] >> 16) & 0xFF;
        out[i * 4 + 3] = (regs[i] >> 24) & 0xFF;
    }
    return out;
}

std::string Md5::toHex(const std::vector<uint8_t>& raw) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(raw.size() * 2);
    for (uint8_t byte : raw) {
        s += digits[byte >> 4];
        s += digits[byte & 0xF];
    }
    return s;
}

std::string Md5::hexDigest(const std::string& data) {
    Md5 md5;
    md5.update(data);
    return toHex(md5.digest());
}

}  // namespace mra
