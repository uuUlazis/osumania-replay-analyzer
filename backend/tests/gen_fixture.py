# Generates a synthetic mania .osu + .osr fixture pair for end-to-end tests.
# The replay exactly matches the beatmap notes with small offsets.
import lzma
import struct
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "fixtures")

def osu_string(s: bytes) -> bytes:
    if not s:
        return b"\x00"
    return b"\x0b" + uleb128(len(s)) + s

def uleb128(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)

OSU = """osu file format v14

[General]
AudioFilename: audio.mp3
Mode: 3

[Metadata]
Title:Fixture Song
Artist:Fixture Artist
Creator:Fixture Mapper
Version:4K Test
BeatmapID:101
BeatmapSetID:10

[Difficulty]
HPDrainRate:8
CircleSize:4
OverallDifficulty:8
ApproachRate:8
SliderMultiplier:1.8

[HitObjects]
64,192,1000,1,0,0:0:0:0:
192,192,1000,1,0,0:0:0:0:
320,192,2000,1,0,0:0:0:0:
448,192,3000,128,0,3200:0:0:0:
"""

def write_osu(path):
    with open(path, "wb") as f:
        f.write(OSU.encode("utf-8"))

# frames: (absoluteTimeMs, keysBitmask) - real mania .osr encoding: records
# are "w|x|y" (keys in x, bit c = column c, y unused), ','-joined, ending
# with the "0|0|0" terminator.
FRAMES = [
    (0, 0),
    (1002, 0b0011),  # col0+col1 down (chord at 1000)
    (1050, 0),       # release
    (2005, 0b0100),  # col2 down (note at 2000)
    (2050, 0),       # release
    (3001, 0b1000),  # col3 down (hold head at 3000)
    (3250, 0),       # release (after hold tail 3200)
]

def build_frame_data() -> bytes:
    lines = ["-12345|0|0|0"]  # RNG seed frame (4 fields)
    prev = 0
    for t, keys in FRAMES:
        lines.append("%d|%d|0" % (t - prev, keys))
        prev = t
    lines.append("0|0|0")  # osu! terminator record
    return (",".join(lines)).encode("ascii")

def write_osr(path, beatmap_md5: bytes, replay_md5: bytes):
    raw = build_frame_data()
    comp = lzma.compress(raw, format=lzma.FORMAT_ALONE, preset=3)
    assert len(comp) >= 13
    data = bytearray()
    data += struct.pack("<B", 3)            # mode: mania
    data += struct.pack("<i", 20240999)     # game version
    data += osu_string(beatmap_md5)
    data += osu_string("FixturePlayer".encode("utf-8"))
    data += osu_string(replay_md5)
    data += struct.pack("<h", 0)            # 300
    data += struct.pack("<h", 0)            # 100
    data += struct.pack("<h", 0)            # 50
    data += struct.pack("<h", 5)            # geki (MAX): 3 notes + hold head + held-through tail
    data += struct.pack("<h", 0)            # katu (200)
    data += struct.pack("<h", 0)            # miss
    data += struct.pack("<i", 100000)       # score
    data += struct.pack("<h", 4)            # max combo
    data += struct.pack("<B", 1)            # perfect
    data += struct.pack("<i", 0)            # mods
    data += osu_string(b"1|0|0,")           # life graph
    data += struct.pack("<q", 17000000000000000)  # timestamp ticks
    data += struct.pack("<i", len(comp))
    data += comp
    data += struct.pack("<q", 0)            # online id
    with open(path, "wb") as f:
        f.write(bytes(data))
    print("osr bytes:", len(data), "compressed:", len(comp), "raw:", len(raw))

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    osu_path = os.path.join(OUT, "beatmaps", "fixture_4k.osu")
    osr_path = os.path.join(OUT, "replays", "fixture_4k.osr")
    os.makedirs(os.path.dirname(osu_path), exist_ok=True)
    os.makedirs(os.path.dirname(osr_path), exist_ok=True)
    write_osu(osu_path)
    # MD5 of raw osu bytes; replay md5 not verified locally.
    import hashlib
    with open(osu_path, "rb") as f:
        osu_bytes = f.read()
    write_osr(osr_path, hashlib.md5(osu_bytes).hexdigest().encode(), b"0" * 32)
    print("osu:", osu_path)
    print("osr:", osr_path)
