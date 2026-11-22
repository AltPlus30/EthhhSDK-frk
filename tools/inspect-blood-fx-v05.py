"""Offline, read-only v0.5 effect investigation; never executes the game."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import struct
from collections import Counter

spec = importlib.util.spec_from_file_location(
    "cosmetic_research", Path(__file__).with_name("inspect-cosmetic-research.py"))
research = importlib.util.module_from_spec(spec)
spec.loader.exec_module(research)

ORIGINALS = (
    (0xD25C0, "rax-pool-0", 64, 64, 5138,
     "e64c12bd4c83d000d8812401834835a413c854769201f95376880d5d035e2d2c"),
    (0xD1180, "rax-pool-1", 64, 64, 5138,
     "faa3da9594ae2e9d503b78c9a9163b279ef6e7e1494a90387e919a4006d2f9dd"),
    (0xCFD40, "rax-pool-2", 64, 64, 5138,
     "c671fe748913aa55a3134305c5ba42ff784dbe948692bd0871b15660913b6a96"),
    (0xE6BE0, "enemydeath-stage-0", 128, 32, 5138,
     "e359a924c08bb7ef85ced8382222f33e6bfdae89119e1a389adce606c1aaa0d7"),
    (0xE5B70, "enemydeath-stage-1", 64, 64, 4162,
     "96a43be4f9edc74c34998ffa3dbde539eea4618986a94393a4e66a481302a11e"),
)

def member(game, archive_name, name):
    """Read one packed member without writing or executing installed content."""
    if Path(archive_name).name != archive_name:
        raise ValueError("Archive must be a Data basename")
    archive = (game / "Data" / archive_name).read_bytes()
    if archive[:8] != b"VOLT\x02\0\0\0":
        raise ValueError("Unsupported archive")
    count = struct.unpack_from("<I", archive, 8)[0]
    base = 16 + count * 12
    for i in range(count):
        record = base + struct.unpack_from("<I", archive, 24+i*12)[0]
        actual = archive[record+16:archive.index(b"\0", record+16)].decode()
        if actual != name:
            continue
        start, _, size, _ = struct.unpack_from("<4I", archive, record)
        packed = archive[start:start+size]
        expected, _, compressed, _ = struct.unpack_from("<4I", packed, 128)
        pool = research.mission.unpack(packed[400:400+compressed])
        if len(pool) != expected:
            raise ValueError("Decompressed member length mismatch")
        return pool, dict(archive=archive_name,
                          archive_sha256=hashlib.sha256(archive).hexdigest(),
                          member=name, member_sha256=hashlib.sha256(packed).hexdigest(),
                          a_sha256=hashlib.sha256(pool).hexdigest())
    raise ValueError("Member not found")


def export_originals(game, out):
    """Exact known 9-99 provenance; no hue guessing or art modification."""
    if out.exists() or out.resolve().is_relative_to(game.resolve()):
        raise ValueError("Choose a new output directory outside the game")
    pool, identity = research.member(game, "9-99.LCS")
    if identity["a_sha256"] != "fbd20b892396036df3b52756e3c4df6a2139b149e86b047550ca90144907e8de":
        raise ValueError("Unsupported decompressed member")
    u32 = lambda p: struct.unpack_from("<I", pool, p)[0]
    expected = {
        0x1EB3B4: 0x1CCB20, 0x1EB3B8: 0x1E8AF4,
        0x1EB3E0: 0x1CCB32, 0x1EB3E4: 0x1E8BCC,
        0x1E8960: 0x1CC620, 0x1E8968: 0x00290002,
        0x1E8BF8: 0x1CC630, 0x1E8C0C: 0x1CC640, 0x1E8C20: 0x1CC650,
        0x1CC620: 0x173580, 0x173584: 0x236370, 0x236380: 0x128070,
        0x12807C: 2, 0x128080: 0x128090,
        0x128090: 0xE6BE0, 0x1280D0: 0xE5B70,
        0x1CC630: 0x173440, 0x173444: 0x237D70, 0x237D80: 0xD25C0,
        0x1CC640: 0x1733F0, 0x1733F4: 0x236D40, 0x236D50: 0xD1180,
        0x1CC650: 0x1733A0, 0x1733A4: 0x236EB0, 0x236EC0: 0xCFD40,
    }
    if any(u32(p) != value for p, value in expected.items()):
        raise ValueError("Effect attribution fingerprint mismatch")
    payloads, entries = [], []
    for descriptor, stem, width, height, size, digest in ORIGINALS:
        marker, w, h, handle, source, n, flags, reserved = struct.unpack_from(
            "<8I", pool, descriptor)
        if (marker, w, h, handle, source, n, flags, reserved) != (
                0x54BC30, width, height, 0, descriptor+32, size, 1, 0):
            raise ValueError("Original descriptor mismatch")
        data = pool[source:source+size]
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Original texture hash mismatch")
        filename = f"{stem}-original-A{descriptor:08x}.tga"
        payloads.append((filename, data))
        entries.append(dict(descriptor_a=hex(descriptor), filename=filename,
                            width=width, height=height, bytes=size, sha256=digest))
    out.mkdir(parents=True, exist_ok=False)
    for filename, data in payloads:
        with (out/filename).open("xb") as stream:
            stream.write(data)
    manifest = dict(identity, executable_sha256=research.mission.EXE_SHA,
                    textures=entries,
                    limitations=["Originals only, not neutralized.",
                                 "Enemy-death stages may be shared outside blood.",
                                 "9-99.LCS attribution is not all-level coverage."])
    with (out/"provenance.json").open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2)
    print(json.dumps(manifest, indent=2))

def inspect_split(game, out):
    """Export the attributed central cap, original art and UV/geometry evidence."""
    if out.exists() or out.resolve().is_relative_to(game.resolve()):
        raise ValueError("Choose a new inspection directory outside the game")
    pool, identity = member(game, "09.vol", "9-99.LCS")
    if identity["a_sha256"] != "fbd20b892396036df3b52756e3c4df6a2139b149e86b047550ca90144907e8de":
        raise ValueError("Unsupported split resource pool")
    u32 = lambda p: struct.unpack_from("<I", pool, p)[0]
    if (u32(0x167680), u32(0x232650+0x44), u32(0x2324F0+0x10),
        u32(0x2324F0+0x14), u32(0x2324F0+0xC)) != (
            0x232650, 0x2324F0, 0xC3540, 0xFFFFFFFF, 0x217020):
        raise ValueError("Central-cap model/mesh attribution mismatch")
    buffer = 0x217020
    if (u32(buffer+8), u32(buffer+12), u32(buffer+20)) != (32, 423, 833):
        raise ValueError("Central-cap buffer layout mismatch")
    vertex_bytes = pool[u32(buffer+16):u32(buffer+16)+423*32]
    index_bytes = pool[u32(buffer+24):u32(buffer+24)+833*2]
    vertex_hash = hashlib.sha256(vertex_bytes).hexdigest()
    index_hash = hashlib.sha256(index_bytes).hexdigest()
    if (vertex_hash, index_hash) != (
        "b4e91476cc18b05d6a85586e6205eefc0e8f180748e52360408e926ab9b81be9",
        "cd0ef2ba4e3a03aa68488e2a5399c20977797ab383bf413eeabd579b0330da40"):
        raise ValueError("Central-cap geometry hash mismatch")
    vertices = list(struct.iter_unpack("<8f", vertex_bytes))
    indices = struct.unpack("<833H", index_bytes)
    if not all(math.isfinite(v) for vertex in vertices for v in vertex) or max(indices) >= 423:
        raise ValueError("Invalid central-cap geometry")
    primitive = u32(0x2324F0+0x38)
    if tuple(u32(primitive+i) for i in (0, 4, 8)) != (5, 423, 831):
        raise ValueError("Expected one indexed triangle strip")
    triangles = []
    for i in range(len(indices)-2):
        a, b, c = indices[i:i+3]
        if i & 1:
            a, b = b, a
        if len({a, b, c}) == 3:
            triangles.append((a, b, c))
    sources = (
        (0xC3540, "split-interior-cap",
         "b940a1d4a0afda2e49e019c1c6224af1b1c5da732c97207007a8a9ed751c17b8"),
        (0xAE460, "split-plane-flare",
         "fa4baac8f42dee07a978720a754a43f76c57d4e39c1073815fc44643bda407cf"),
        (0xAF8A0, "split-plane-green-glow",
         "fbde93538ad88d9648c1994fb3c78b43f448a2ef94e745ee9fa31c0619f77185"),
    )
    payloads, assets = [], []
    for descriptor, role, expected in sources:
        source, size = u32(descriptor+16), u32(descriptor+20)
        data = pool[source:source+size]
        digest = hashlib.sha256(data).hexdigest()
        if digest != expected:
            raise ValueError("Inspection texture hash mismatch")
        filename = f"{role}-original-A{descriptor:08x}.tga"
        payloads.append((filename, data))
        assets.append(dict(descriptor_a=hex(descriptor), role=role, filename=filename,
                           width=u32(descriptor+4), height=u32(descriptor+8),
                           bytes=size, sha256=digest))
    bounds = [[min(v[k] for v in vertices), max(v[k] for v in vertices)]
              for k in range(3)]
    report = dict(identity, mesh_a="0x2324f0", model_a="0x167680",
                  vertex_count=423, index_count=833, triangle_count=len(triangles),
                  vertex_sha256=vertex_hash, index_sha256=index_hash,
                  xyz_bounds=bounds,
                  exact_x_normals=sum(abs(v[3]) == 1.0 and v[4] == 0 and v[5] == 0
                                      for v in vertices),
                  textures=assets,
                  interpretation="Thin central split-cap geometry; not armor atlas.",
                  limitations=["Derived offline geometry/UV interpretation, not observed rendering.",
                               "Glow art is shared: use exact parent/effect scope, not global texture admission."])
    glow_diffuse = []
    for mesh in (0x237480, 0x237300, 0x2378F0, 0x237770, 0x237BF0, 0x237A70):
        vertex_buffer, primitive = u32(mesh+12), u32(mesh+56)
        if (u32(mesh+4), u32(vertex_buffer+8), u32(primitive)) != (0x142, 24, 5):
            raise ValueError("Unexpected split-plane vertex/strip schema")
        start_index, index_count = u32(primitive+36), u32(primitive+8)+2
        selected = struct.unpack_from("<"+"H"*index_count, pool,
                                      u32(vertex_buffer+24)+start_index*2)
        if max(selected) >= u32(vertex_buffer+12):
            raise ValueError("Invalid split-plane index range")
        colors = Counter(u32(u32(vertex_buffer+16)+i*24+12)
                         for i in sorted(set(selected)))
        glow_diffuse.append(dict(mesh_a=hex(mesh), fvf="0x142",
                                 stride=24, diffuse_offset=12,
                                 minimum_vertex=u32(primitive+32),
                                 start_index=start_index,
                                 primitive_count=u32(primitive+8),
                                 referenced_vertex_colors={hex(k): v for k, v in colors.items()}))
    report["split_glow_vertex_diffuse"] = glow_diffuse
    report["limitations"].append(
        "Split-plane diffuse contains green and other baked RGB: texture neutralization alone is NOT a complete glow recolor.")
    out.mkdir(parents=True, exist_ok=False)
    for filename, data in payloads:
        with (out/filename).open("xb") as stream:
            stream.write(data)
    with (out/"cap.obj").open("x", encoding="utf-8") as stream:
        stream.write("# Derived original split-cap geometry; native UV V inverted for OBJ.\n")
        for v in vertices:
            stream.write(f"v {v[0]} {v[1]} {v[2]}\n")
        for v in vertices:
            stream.write(f"vt {v[6]} {1-v[7]}\n")
        for v in vertices:
            stream.write(f"vn {v[3]} {v[4]} {v[5]}\n")
        for tri in triangles:
            stream.write("f "+" ".join(f"{i+1}/{i+1}/{i+1}" for i in tri)+"\n")
    with (out/"inspection.json").open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
    # Pillow is optional. Original indexed TGAs and the OBJ/JSON need no image
    # library; specifically decode 24/32-bit palettes rather than asking Pillow
    # to handle its unsupported indexed-BGRA palette mode.
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print(json.dumps(report, indent=2))
        return
    data = payloads[0][1]
    _, _, kind, _, count, bits, _, _, w, h, depth, flags = struct.unpack_from(
        "<BBBHHBHHHHBB", data)
    if (kind, bits, depth, flags) != (1, 24, 8, 0):
        raise ValueError("Unexpected cap TGA schema")
    palette = [tuple(data[18+i*3+j] for j in (2, 1, 0)) for i in range(count)]
    image = Image.new("RGB", (w, h))
    image.putdata([palette[p] for p in data[18+count*3:]])
    image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).resize((512, 512))
    draw = ImageDraw.Draw(image)
    for tri in triangles:
        points = [(vertices[i][6]*512, vertices[i][7]*512) for i in tri]
        draw.line(points+[points[0]], fill=(255, 255, 255), width=1)
    image.save(out/"cap-uv-inspection.png")
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--member", default="9-95.LCS")
    parser.add_argument("--archive", default="09.vol")
    parser.add_argument("--strings", default="blood|ranx|inside|gore|glow")
    parser.add_argument("--refs", type=lambda x: int(x, 0))
    parser.add_argument("--native-refs", type=lambda x: int(x, 0))
    parser.add_argument("--export", type=Path,
                        help="export five attributed original TGAs to a NEW directory")
    parser.add_argument("--inspect-split", type=Path,
                        help="export original cap/glow art, cap OBJ and UV inspection to NEW directory")
    args = parser.parse_args()
    game = Path(r"E:\Games\I-Ninja")
    exe = (game / "I-Ninja.exe").read_bytes()
    if hashlib.sha256(exe).hexdigest() != research.mission.EXE_SHA:
        raise ValueError("Unsupported executable SHA-256")
    if args.export is not None:
        export_originals(game, args.export)
        return
    if args.inspect_split is not None:
        inspect_split(game, args.inspect_split)
        return
    if args.native_refs is not None:
        import pefile
        pe = pefile.PE(data=exe)
        for section in pe.sections:
            data = section.get_data()
            for match in re.finditer(re.escape(struct.pack("<I", args.native_refs)), data):
                print("native absolute", hex(pe.OPTIONAL_HEADER.ImageBase +
                                             section.VirtualAddress + match.start()))
        return
    pool, identity = member(game, args.archive, args.member)
    print(identity)
    if args.refs is not None:
        for offset in range(0, len(pool) - 3, 4):
            if struct.unpack_from("<I", pool, offset)[0] == args.refs:
                print(f"A+{offset:08X}", pool[max(0, offset-16):offset+20].hex())
    else:
        for match in re.finditer(rb"[\x20-\x7e]{3,}\0", pool):
            text = match.group()[:-1].decode("ascii")
            if re.search(args.strings, text, re.I):
                print(f"A+{match.start():08X}", repr(text))


if __name__ == "__main__":
    main()
