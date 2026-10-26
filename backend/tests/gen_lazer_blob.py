# Generates the real LZMA (FORMAT_ALONE) blob embedded as a byte array in
# lazer_format_tests.cpp.
#
# Why this script exists:
#   The lazer .osr extra tail block is `i32 LE compressed length + LZMA stream`,
#   where the LZMA stream uses the .osr "alone" layout: 5 props bytes
#   (props + dict size) + 8-byte LE uncompressed size + payload. Python's
#   lzma.compress(format=lzma.FORMAT_ALONE) produces exactly that layout, so the
#   output below is byte-compatible with what osu!lazer writes and with the
#   vendored LzmaDec decoder (backend/third_party/lzma + replay_decoder.h).
#
# Usage:  python backend/tests/gen_lazer_blob.py
# Paste the printed array into kTailBlobJson/kTailBlob in lazer_format_tests.cpp.
# The JSON inside is fully synthetic (no real player data).
import json
import lzma

payload = json.dumps(
    {
        "mods": [
            {"acronym": "DT", "settings": {"speed_change": 1.25}},
            {"acronym": "RD", "settings": {}},
        ],
        "statistics": {
            "Perfect": 850,
            "Great": 120,
            "Good": 20,
            "Ok": 8,
            "Meh": 1,
            "Miss": 1,
            "IgnoreHit": 5,
            "ComboBreak": 2,
        },
        "maximum_statistics": {
            "Perfect": 900,
            "Great": 130,
            "Good": 25,
            "Ok": 10,
            "Meh": 2,
            "Miss": 3,
        },
        "rank": "S",
        "total_score_without_mods": 1234567,
        "pauses": [],
    },
    separators=(",", ":"),
).encode("utf-8")

blob = bytearray(lzma.compress(payload, format=lzma.FORMAT_ALONE))
# liblzma's alone-format encoder writes "unknown size" (0xFF..FF) because it is
# a streaming encoder, but osu! .osr streams declare the real uncompressed size
# in the 8-byte LE header field. Patch it in (the stream itself also carries an
# end marker, so both variants decode with mra::lzmaDecode).
blob[5:13] = len(payload).to_bytes(8, "little")
blob = bytes(blob)
assert blob[5:13] == len(payload).to_bytes(8, "little"), "header size mismatch"

print(f"// payload {len(payload)} bytes, blob {len(blob)} bytes")
print("// kTailBlob = {")
for i in range(0, len(blob), 16):
    chunk = ", ".join(f"0x{b:02x}" for b in blob[i : i + 16])
    print(f"    {chunk},")
print("};")
