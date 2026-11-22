"""Extract only five positively attributed original blood TGAs, offline.

No launch/attach/install or game writes. Refuses existing output directories.
Attribution is to 9-95.LCS blood scripts and the named green-blood pool model,
not a claim to inventory every enemy, level, blood effect or particle texture.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct


TEXTURES = (
    (0x1E0060, "blood-splatter-0", 32, 2,
     "db74cc596032f738f79efacff7d7458c7f74f8bedcbf788d4793b5209b77907d"),
    (0x1DFC20, "blood-splatter-1", 32, 2,
     "ea28e3a9dd00917e68adc5dc5ee5d188b9a87cec1f4b4213e729368b402b1481"),
    (0x1DF7E0, "blood-splatter-2", 32, 2,
     "f68aee3ba871deab29fc94333e3253fe059e772a319c217b59cf784317d2fe04"),
    (0x1DF3A0, "blood-splatter-3", 32, 2,
     "136d3d094796f53cea81e94b2fcb38c1d4d498cd36d1128043eedca2ce794dbb"),
    (0x1C7DE0, "green-blood-pool", 64, 256,
     "faa3da9594ae2e9d503b78c9a9163b279ef6e7e1494a90387e919a4006d2f9dd"),
)
ARCHIVE_SHA = "0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669"
MEMBER_SHA = "390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65"
POOL_SHA = "68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspector():
    spec = importlib.util.spec_from_file_location(
        "blood_cosmetic_inspector", Path(__file__).with_name("inspect-cosmetic-research.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate(pool):
    def expect(offset, value):
        require(struct.unpack_from("<I", pool, offset)[0] == value,
                f"Attribution mismatch A+{offset:X}")

    # Class -> exact names, code/event pointers, and caller -> spawned class.
    for cls, name, code, entry in (
        (0x4D16F4, b"BloodSplatter", 0x4C4934, 0x4C493C),
        (0x4D1720, b"SpawnBloodSplatter", 0x4C4934, 0x4C4AA0),
        (0x4D174C, b"SplatterSpawner", 0x4C4934, 0x4C4D08),
        (0x4D1644, b"NinjaSwordBlood", 0x4C3474, 0x4C44E8),
    ):
        name_ptr = struct.unpack_from("<I", pool, cls + 4)[0]
        require(pool[name_ptr:name_ptr + len(name) + 1] == name + b"\0",
                "Blood class name mismatch")
        expect(cls + 8, entry)
        expect(cls + 12, code)
    expect(0x4C3948, 0x5A300000)
    expect(0x4C394C, 0x4D1720)  # NinjaSword helper spawns splatter class.
    expect(0x4C3964, 0x80FF3000)
    expect(0x4C3968, 0x00020006)
    for operand, texture in zip((0x4C496C, 0x4C4978, 0x4C4984, 0x4C4990),
                                (row[0] for row in TEXTURES[:4])):
        expect(operand, texture)
    expect(0x4C49E0, 0x81000800)
    expect(0x4C49E4, 0x10680005)  # Bank 1/68, not bank 0/68.
    expect(0x4C44EC, 0x4B3E20)
    expect(0x4C44F4, 0x00290006)
    expect(0x4B3E20, 0x4B3920)
    require(pool[0x4B3940:0x4B3940 + 16] == b"pool of green b\0",
            "Green-blood model name mismatch")
    expect(0x4B3924, 0x54FA20)  # Second mesh list, primary is null.
    expect(0x54FA20, 0x54BB54)
    expect(0x54FA30, 0x1C7DE0)
    expect(0x54FA34, 0xFFFFFFFF)  # White material multiplier.
    originals = []
    for descriptor, stem, side, count, digest in TEXTURES:
        marker, w, h, handle, source, size, flags, reserved = struct.unpack_from(
            "<8I", pool, descriptor)
        require((marker, w, h, handle, source, flags, reserved) ==
                (0x54BC30, side, side, 0, descriptor + 32, 1, 0),
                "Unexpected texture descriptor")
        data = pool[source:source + size]
        require(struct.unpack_from("<BBBHHBHHHHBB", data) ==
                (0, 1, 1, 0, count, 32, 0, 0, side, side, 8, 0),
                "Unexpected original TGA format")
        require(size == 18 + count * 4 + side * side
                and all(index < count for index in data[18 + count * 4:]),
                "Invalid original TGA payload")
        require(hashlib.sha256(data).hexdigest() == digest, "Original TGA hash mismatch")
        originals.append((f"{stem}-original-A{descriptor:08x}.tga", data))
    return originals


def extract(game, out):
    require(not out.exists(), "Output already exists; choose a new directory")
    require(not out.resolve().is_relative_to(game.resolve()),
            "Output must be outside installed game")
    inspect = inspector()
    require(hashlib.sha256((game / "I-Ninja.exe").read_bytes()).hexdigest()
            == inspect.mission.EXE_SHA, "Unsupported executable SHA-256")
    pool, identity = inspect.member(game, "9-95.LCS")
    require(identity["archive_sha256"] == ARCHIVE_SHA
            and identity["member_sha256"] == MEMBER_SHA
            and identity["a_sha256"] == POOL_SHA, "Unsupported archive/member/pool")
    originals = validate(pool)  # Validate everything before creating output.
    manifest = dict(identity, executable_sha256=inspect.mission.EXE_SHA,
                    attribution_report="docs/cosmetic-head-blood-research.md",
                    textures=[dict(descriptor_a=hex(row[0]), filename=file[0],
                                   sha256=row[4], width=row[2], height=row[2],
                                   palette_entries=row[3], palette_bits=32)
                              for row, file in zip(TEXTURES, originals)])
    out.mkdir(parents=True, exist_ok=False)
    for filename, data in originals:
        with (out / filename).open("xb") as stream:
            stream.write(data)
    with (out / "provenance.json").open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
    with (out / "README.md").open("x", encoding="utf-8") as stream:
        stream.write("# Attributed original blood textures\n\n"
                     "Original indexed TGAs: 8-bit pixels, 32-bit BGRA palette.\n"
                     "The four splatters encode RGB (78,255,0) with binary alpha;\n"
                     "their green is texture color, not a neutral tint source.\n"
                     "No recolored assets or runtime texture replacement supplied.\n\n"
                     "See docs/cosmetic-head-blood-research.md and provenance.json.\n"
                     "Coverage is limited to the positively validated five resources.\n")
    return [out / filename for filename, _ in originals]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(r"E:\Games\I-Ninja"))
    parser.add_argument("--out", type=Path, default=Path("build/blood-textures"))
    args = parser.parse_args()
    try:
        for path in extract(args.game, args.out):
            print(path)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"Extraction refused: {error}\n")


if __name__ == "__main__":
    main()
