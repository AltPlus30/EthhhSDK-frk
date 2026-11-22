"""Extract five attributed ORIGINAL Ninja TGAs without changing game files.

Requires the matching installed PC build. Output must be a new directory;
existing output is never replaced, including previously extracted originals.
Uses only the standard library and the adjacent read-only cosmetic inspector.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
from pathlib import Path
import struct


GAME = Path(r"E:\Games\I-Ninja")
ARCHIVE_SHA = "0ad6602d01915e1fcca7493d90012f6c9d15480a4c8a702089cae8c5f3a42669"
MEMBER_SHA = "390cd18e7271ca29e43cafe35483829731e3f32b32bc9c964f7ae0a37e7e7a65"
POOL_SHA = "68c7d2af9e307c50646867984b9db33a811807fe68516826f10048e1e349befd"
# Descriptor, filename stem, width, height, palette entries, original byte hash.
TEXTURES = (
    (0x63740, "ninja-blue-original", 256, 256, 256,
     "9d60a2714e673d751a1d74b20bcc5965f976fee3855edfa845d1b7dd64b064c3"),
    (0x73A80, "ninja-body-secondary-original", 256, 128, 256,
     "4c4b7a0f4c99d737d26044836adb2427c775a5275f252eefee8f29e4e1ad7158"),
    (0x7BDC0, "ninja-sash-knot-white-original", 32, 32, 16,
     "f3c31aff3fba59eb5ca32c5dfab0d8affe10f6c9082ffe92396daff006f30337"),
    (0x7C230, "ninja-sash-white-original", 256, 32, 256,
     "10d1ea8f8be91221a478708aba22424cac9a9db1fb800992db3a45efda942c04"),
    (0x1B0720, "ninja-head-original", 128, 128, 256,
     "43ae1d9af9e068476b5cc77249d9d63138526d2d02ad946f9c3cf5bb6e0d03c1"),
)
BODY_MESHES = (
    0x54DD90, 0x54DAD0, 0x54D970, 0x54D810, 0x54D6B0,
    0x54D550, 0x54D3F0, 0x54D130, 0x54CFD0, 0x54CBB0,
    0x54C8F0, 0x54C630, 0x54C4D0, 0x54C210, 0x54B5B0,
    0x54B450, 0x54B190,
)
BODY_NAMES = (
    "blue", "sash knot white", "sash white", "blue", "blue",
    "blue", "blue", "blue_double", "blue", "blue_double",
    "blue", "blue", "blue", "blue", "blue", "blue", "blue",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def u32(pool, offset):
    require(0 <= offset <= len(pool) - 4, f"A+{offset:X}: word outside pool")
    return struct.unpack_from("<I", pool, offset)[0]


def expect(pool, offset, value):
    actual = u32(pool, offset)
    require(actual == value,
            f"A+{offset:X}: expected {value:08X}, found {actual:08X}")


def name_at(pool, offset, size):
    raw = pool[offset:offset + size]
    require(len(raw) == size and b"\0" in raw,
            f"A+{offset:X}: invalid fixed-size name")
    return raw.split(b"\0", 1)[0].decode("ascii")


def inspector():
    path = Path(__file__).with_name("inspect-cosmetic-research.py")
    spec = importlib.util.spec_from_file_location("cosmetic_inspector", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_attribution(pool):
    """Validate selected serialized resource links, not inferred filenames."""
    expect(pool, 0x4B3D30, 0x4A2BD0)
    expect(pool, 0x4B3D34, 0x4A2C30)
    require(name_at(pool, 0x4A2BF0, 16) == "Ninja", "Wrong body model")
    expect(pool, 0x4A2BD0, BODY_MESHES[0])
    expect(pool, 0x4A2C0C, 0x4B1CE0)
    for offset, value in zip(range(0x4B1CE0, 0x4B1CF4, 4),
                             (17, 0x4B1D00, 17, 0x4B1D50, 0x4B1DA0)):
        expect(pool, offset, value)
    evidence = []
    for i, (mesh, name) in enumerate(zip(BODY_MESHES, BODY_NAMES)):
        slot, label = 0x4B1D50 + i * 4, 0x4B1DA0 + i * 32
        expect(pool, slot, mesh)
        require(name_at(pool, label, 32) == name, "Body entry name mismatch")
        texture = (0x7BDC0 if i == 1 else 0x7C230 if i == 2 else 0x63740)
        expect(pool, mesh + 0x10, texture)
        expect(pool, mesh + 0x14, 0xFFFFFFFF)
        evidence.append(f"- `{name}` name A+{label:X}, slot A+{slot:X}"
                        f" -> mesh A+{mesh:X} -> +10 texture A+{texture:X}.")
    # Includes non-lookup mesh records: do not silently omit the secondary atlas.
    seen, counts, mesh = set(), Counter(), BODY_MESHES[0]
    expect(pool, mesh + 0x44, 0x54DC30)
    expect(pool, 0x54DC30 + 0x10, 0x73A80)
    while mesh:
        require(mesh not in seen and len(seen) < 100, "Invalid body mesh chain")
        seen.add(mesh)
        expect(pool, mesh, 0x0054BB54)
        texture = u32(pool, mesh + 0x10)
        counts[texture] += 1
        next_mesh = u32(pool, mesh + 0x44)
        evidence.append(f"- Body chain mesh A+{mesh:X}: +10 -> A+{texture:X},"
                        f" +44 -> A+{next_mesh:X}.")
        mesh = next_mesh
    require(counts == Counter({0x63740: 15, 0x73A80: 21,
                               0x7BDC0: 1, 0x7C230: 1}),
            "Unexpected body texture chain")
    require(set(BODY_MESHES) <= seen, "Named meshes missing from body chain")
    # Separate normal head resource, deliberately not every expression/form.
    expect(pool, 0x4B3BA0, 0x4B3740)
    require(name_at(pool, 0x4B3760, 16) == "Ninja_Head", "Wrong head model")
    expect(pool, 0x4B3740, 0x5465C0)
    expect(pool, 0x4B377C, 0x4B3790)
    for offset, value in zip(range(0x4B3790, 0x4B37A4, 4),
                             (3, 0x4B37B0, 3, 0x4B37C0, 0x4B37D0)):
        expect(pool, offset, value)
    require(name_at(pool, 0x4B37D0, 32) == "head", "Wrong head entry")
    expect(pool, 0x4B37C0, 0x5465C0)
    expect(pool, 0x5465C0, 0x0054BB54)
    expect(pool, 0x5465D0, 0x1B0720)
    evidence.append("- `head`: model A+4B3740 -> lookup A+4B3790;"
                    " name A+4B37D0, slot A+4B37C0 -> mesh A+5465C0"
                    " -> field A+5465D0 -> texture A+1B0720.")
    return evidence


def original_tga(pool, entry):
    descriptor, stem, width, height, count, digest = entry
    require(descriptor + 32 <= len(pool), "Truncated texture descriptor")
    marker, w, h, handle, source, size, flags, reserved = struct.unpack_from(
        "<8I", pool, descriptor)
    require(marker == 0x0054BC30 and (w, h) == (width, height)
            and source == descriptor + 32 and flags == 1,
            f"A+{descriptor:X}: unexpected serialized texture descriptor")
    require(source + size <= len(pool), "TGA outside pool")
    data = pool[source:source + size]
    expected_header = (0, 1, 1, 0, count, 24, 0, 0, width, height, 8, 0)
    require(len(data) >= 18
            and struct.unpack_from("<BBBHHBHHHHBB", data) == expected_header,
            f"A+{source:X}: unsupported TGA header")
    pixel_start = 18 + count * 3
    require(size == pixel_start + width * height, "TGA payload length mismatch")
    require(all(index < count for index in data[pixel_start:]),
            "TGA palette index outside declared palette")
    require(sha(data) == digest, f"A+{source:X}: original TGA hash mismatch")
    return f"{stem}-A{descriptor:08x}.tga", data


def extract(game, out):
    require(not out.exists(), f"Output already exists; use a new directory: {out}")
    # Never create an extraction directory within the installed game tree.
    require(not out.resolve().is_relative_to(game.resolve()),
            "Output must be outside the installed game directory")
    inspect = inspector()
    require(sha((game / "I-Ninja.exe").read_bytes()) == inspect.mission.EXE_SHA,
            "Unsupported executable SHA-256")
    pool, identity = inspect.member(game, "9-95.LCS")
    for key, expected in (("archive_sha256", ARCHIVE_SHA),
                          ("member_sha256", MEMBER_SHA), ("a_sha256", POOL_SHA)):
        require(identity[key] == expected, f"Unsupported {key}")
    evidence = validate_attribution(pool)
    originals = [original_tga(pool, entry) for entry in TEXTURES]
    lines = [
        "# Ninja original cosmetic textures",
        "",
        "Reproduce: `python -B tools/extract-ninja-cosmetic-textures.py"
        " --game \"E:\\Games\\I-Ninja\" --out <new-directory>`",
        "",
        "Read-only offline extraction; no launch/attach/native REA or game writes.",
        "All A+ coordinates below are decompressed BIGB A offsets, NOT native VAs.",
        f"Source: `{game / 'Data' / '09.vol'}`, member `9-95.LCS`.",
        f"Executable SHA-256: `{inspect.mission.EXE_SHA}`",
        f"Archive SHA-256: `{ARCHIVE_SHA}`",
        f"Member SHA-256: `{MEMBER_SHA}`",
        f"A-pool SHA-256: `{POOL_SHA}`",
        "",
        "## Original files",
        "",
        "All five are uncompressed indexed TGA type 1, 8-bit indices, 24-bit BGR",
        "palettes (no alpha), origin flags 0; no embedded mip chain or footer.",
        "Original bytes only: no palette conversion, monochrome or recoloring.",
        "",
    ]
    for entry, (filename, data) in zip(TEXTURES, originals):
        descriptor, _, width, height, count, digest = entry
        lines.append(f"- `{filename}`: {width}x{height}, {count} palette entries,"
                     f" {len(data)} bytes; descriptor A+{descriptor:X},"
                     f" inline TGA A+{descriptor + 32:X}; SHA-256 `{digest}`.")
    lines += [
        "", "## Validated attribution", "",
        "A+4B3D30 contains render/skeleton [004A2BD0,004A2C30].",
        "`Ninja` model A+4A2BD0 +3C -> lookup A+4B1CE0;",
        "mesh array A+4B1D50, parallel 32-byte names A+4B1DA0.",
        *evidence,
        "", "## Tint and limitations", "",
        "Assuming multiplicative RGB tint, the blue atlas needs a neutral/grayscale",
        "derivative for hue-independent tint, not merely for darkening/filtering.",
        "Grayscale alone does not supply brightness headroom. Sash textures are",
        "already near-neutral and do not need monochrome for practical belt tint;",
        "strictly neutral hue would require removing their residual warm bias.",
        "Secondary body and head atlases contain colored/mixed regions: use masks",
        "rather than blanket desaturation if skin/eye/details must be preserved.",
        "",
        "The secondary atlas is positively reached by 21 Ninja mesh-chain records,",
        "not a distinct named lookup entry; skin/leather role is inference only.",
        "This validates normal Ninja and the selected normal head, not all forms,",
        "levels or expressions. Head chain also references non-TGA descriptor",
        "A+1FD070 (marker 0054BAEC), not decoded/exported here.",
        "No complete head inventory, GPU ownership, rendering equation, texture",
        "replacement ABI or safe LCS repacking is established by this extraction.",
        "",
    ]
    # Validate everything before creating anything. Exclusive directory/file
    # creation also protects against another extraction racing this one.
    out.mkdir(parents=True, exist_ok=False)
    for filename, data in originals:
        with (out / filename).open("xb") as stream:
            stream.write(data)
    with (out / "README.md").open("x", encoding="utf-8") as stream:
        stream.write("\n".join(lines))
    return [out / filename for filename, _ in originals]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=GAME)
    parser.add_argument("--out", type=Path, default=Path("build/cosmetic-textures"))
    args = parser.parse_args()
    try:
        files = extract(args.game, args.out)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, f"Extraction refused: {error}\n")
    for path in files:
        print(path)
    print(f"Provenance: {args.out / 'README.md'}")


if __name__ == "__main__":
    main()
