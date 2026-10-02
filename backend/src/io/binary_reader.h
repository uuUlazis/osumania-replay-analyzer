#pragma once
// BinaryReader: bounds-checked little-endian primitive reader for osu! formats.
// Covers Byte/Short/Integer/Long/ULEB128/String as defined by the official
// .osr and legacy-database specifications.
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <string>
#include <vector>
#include <stdexcept>

namespace mra {

class BinaryReaderError : public std::runtime_error {
public:
    explicit BinaryReaderError(const std::string& message)
        : std::runtime_error(message) {}
};

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size)
        : data_(data), size_(size) {}
    explicit BinaryReader(const std::vector<uint8_t>& bytes)
        : data_(bytes.data()), size_(bytes.size()) {}

    size_t position() const { return pos_; }
    size_t remaining() const { return size_ - pos_; }
    bool atEnd() const { return pos_ >= size_; }

    void seek(size_t offset) {
        if (offset > size_) throw BinaryReaderError("seek past end of buffer");
        pos_ = offset;
    }

    uint8_t readU8() { require(1); return data_[pos_++]; }

    uint16_t readU16() {
        require(2);
        uint16_t v = static_cast<uint16_t>(data_[pos_]) |
                     static_cast<uint16_t>(data_[pos_ + 1]) << 8;
        pos_ += 2;
        return v;
    }

    uint32_t readU32() {
        require(4);
        uint32_t v = static_cast<uint32_t>(data_[pos_]) |
                     static_cast<uint32_t>(data_[pos_ + 1]) << 8 |
                     static_cast<uint32_t>(data_[pos_ + 2]) << 16 |
                     static_cast<uint32_t>(data_[pos_ + 3]) << 24;
        pos_ += 4;
        return v;
    }

    uint64_t readU64() {
        require(8);
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i)
            v = (v << 8) | data_[pos_ + i];
        pos_ += 8;
        return v;
    }

    int64_t readI64() { return static_cast<int64_t>(readU64()); }
    int32_t readI32() { return static_cast<int32_t>(readU32()); }
    int16_t readI16() { return static_cast<int16_t>(readU16()); }

    float readF32() {
        uint32_t bits = readU32();
        float f;
        static_assert(sizeof(f) == sizeof(bits), "float size");
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    double readF64() {
        uint64_t bits = readU64();
        double d;
        static_assert(sizeof(d) == sizeof(bits), "double size");
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }

    // ULEB128 variable-length integer. Rejects > 10 bytes / overflow.
    uint64_t readULEB128() {
        uint64_t value = 0;
        int shift = 0;
        for (int i = 0; i < 10; ++i) {
            uint8_t byte = readU8();
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return value;
            shift += 7;
        }
        throw BinaryReaderError("ULEB128 integer too long");
    }

    // osu! serialized string: 0x00 = empty, 0x0b = ULEB128 length + UTF-8 bytes.
    std::string readOsuString() {
        uint8_t tag = readU8();
        if (tag == 0x00) return std::string();
        if (tag != 0x0b)
            throw BinaryReaderError("invalid osu! string tag 0x" +
                                    hexByte(tag));
        uint64_t length = readULEB128();
        if (length > remaining())
            throw BinaryReaderError("string length exceeds buffer");
        std::string s(reinterpret_cast<const char*>(data_ + pos_),
                      static_cast<size_t>(length));
        pos_ += static_cast<size_t>(length);
        return s;
    }

    std::vector<uint8_t> readBytes(size_t count) {
        if (count > remaining())
            throw BinaryReaderError("read exceeds buffer: need " +
                                    std::to_string(count) + ", have " +
                                    std::to_string(remaining()));
        std::vector<uint8_t> out(data_ + pos_, data_ + pos_ + count);
        pos_ += count;
        return out;
    }

    const uint8_t* cursor() const { return data_ + pos_; }

private:
    void require(size_t n) const {
        if (remaining() < n)
            throw BinaryReaderError("unexpected end of data at offset " +
                                    std::to_string(pos_));
    }
    static std::string hexByte(uint8_t b) {
        static const char* digits = "0123456789abcdef";
        std::string s;
        s += digits[b >> 4];
        s += digits[b & 0xF];
        return s;
    }

    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

}  // namespace mra
