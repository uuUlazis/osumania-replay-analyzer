#include "replay/replay_decoder.h"

#include <cstring>
#include <cstdlib>

#include "LzmaDec.h"

namespace mra {
namespace {

void* szAlloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
void szFree(ISzAllocPtr, void* p) { std::free(p); }

ISzAlloc kAllocator = {szAlloc, szFree};

constexpr uint64_t kUnknownSize = 0xFFFFFFFFFFFFFFFFull;
constexpr uint64_t kMaxKnownOutput = 1ull << 31;        // 2 GB
constexpr size_t kMaxUnknownOutput = 256ull << 20;      // 256 MB safety cap

}  // namespace

LzmaDecodeResult lzmaDecode(const uint8_t* data, size_t size) {
    LzmaDecodeResult result;
    if (size < 13) {
        result.error = "LZMA stream shorter than 13-byte header";
        return result;
    }

    // Props are validated inside LzmaDec_Allocate (LzmaDec_GetProps is not a
    // public symbol in this SDK revision).
    uint64_t declaredSize = 0;
    for (int i = 0; i < 8; ++i)
        declaredSize |= static_cast<uint64_t>(data[LZMA_PROPS_SIZE + i]) << (8 * i);

    bool knownSize = declaredSize != kUnknownSize;
    if (knownSize && declaredSize > kMaxKnownOutput) {
        result.error = "declared LZMA output size too large";
        return result;
    }

    const uint8_t* src = data + LZMA_PROPS_SIZE + 8;
    SizeT srcLen = static_cast<SizeT>(size - (LZMA_PROPS_SIZE + 8));

    CLzmaDec dec;
    LzmaDec_Construct(&dec);
    if (LzmaDec_Allocate(&dec, data, LZMA_PROPS_SIZE, &kAllocator) != SZ_OK) {
        result.error = "LZMA decoder allocation failed";
        return result;
    }
    LzmaDec_Init(&dec);

    size_t capacity = knownSize ? static_cast<size_t>(declaredSize)
                                : static_cast<size_t>(1u << 20);
    if (capacity == 0) capacity = 1;
    result.output.resize(capacity);

    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    SRes res = SZ_OK;
    size_t produced = 0;

    for (;;) {
        SizeT outChunk = result.output.size() - produced;
        SizeT inChunk = srcLen;
        ELzmaFinishMode finish =
            knownSize ? LZMA_FINISH_END : LZMA_FINISH_ANY;
        res = LzmaDec_DecodeToBuf(&dec, result.output.data() + produced,
                                  &outChunk, const_cast<Byte*>(src), &inChunk,
                                  finish, &status);
        if (res != SZ_OK) {
            result.error = "LZMA decode failed, SRes=" + std::to_string(res);
            LzmaDec_Free(&dec, &kAllocator);
            return result;
        }
        produced += outChunk;
        src += inChunk;
        srcLen -= inChunk;

        if (status == LZMA_STATUS_FINISHED_WITH_MARK ||
            status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)
            break;
        if (knownSize && produced >= static_cast<size_t>(declaredSize))
            break;
        if (!knownSize) {
            if (produced > kMaxUnknownOutput) {
                result.error = "LZMA output exceeded safety cap";
                LzmaDec_Free(&dec, &kAllocator);
                return result;
            }
            if (srcLen == 0 && status == LZMA_STATUS_NEEDS_MORE_INPUT) break;
            result.output.resize(result.output.size() * 2);
            continue;
        }
        if (srcLen == 0 && outChunk == 0) break;
    }

    result.output.resize(produced);
    result.ok = true;
    LzmaDec_Free(&dec, &kAllocator);
    return result;
}

}  // namespace mra
