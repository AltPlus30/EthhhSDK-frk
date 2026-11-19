"""Read-only PC cosmetic evidence: packed script words and native disassembly.

No launch/attach, extraction, archive edits, or production hooks. Script offsets
are BIGB A-relative, never executable VAs. Capstone/pefile only for --native.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location(
    "mission_inspector", Path(__file__).with_name("inspect-mission-timer.py"))
mission = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mission)


def member(game, name):
    archive = (game / "Data" / "09.vol").read_bytes()
    if archive[:8] != b"VOLT\x02\0\0\0":
        raise ValueError("unsupported archive")
    count = struct.unpack_from("<I", archive, 8)[0]
    base = 16 + count * 12
    for i in range(count):
        record = base + struct.unpack_from("<I", archive, 24 + i * 12)[0]
        start, _, size, _ = struct.unpack_from("<4I", archive, record)
        actual = archive[record + 16:archive.index(b"\0", record + 16)].decode()
        if actual == name:
            packed = archive[start:start + size]
            expected, _, compressed, _ = struct.unpack_from("<4I", packed, 128)
            pool = mission.unpack(packed[400:400 + compressed])
            if len(pool) != expected:
                raise ValueError("decompressed size mismatch")
            return pool, {
                "archive_sha256": hashlib.sha256(archive).hexdigest(),
                "member": name,
                "member_sha256": hashlib.sha256(packed).hexdigest(),
                "a_sha256": hashlib.sha256(pool).hexdigest(),
            }
    raise ValueError("member not found")


def cosmetic_summary(pool):
    """Narrow fingerprinted observations; no general VM decoder is claimed."""
    u32 = lambda p: struct.unpack_from("<I", pool, p)[0]
    fingerprints = {
        0x4D1330: 0x00130008, 0x4D1334: 0x4BA218,
        0x4D1338: 0x00140008, 0x4D133C: 0x4BA24C,
        0x4D1320: 0x00230008, 0x4D1324: 0x4B8708,
        0x4D13B8: 0x00360008, 0x4D13BC: 0x4C3F9C,
        0x4B76A4: 0x32780017, 0x4B76A8: 0x81787800,
        0x4B76AC: 0x103E0013, 0x4BA26C: 0x7EFF0000,
        0x4B77D8: 0x01840002, 0x4C347C: 0x322C0011,
    }
    for p, expected in fingerprints.items():
        if u32(p) != expected:
            raise ValueError(f"cosmetic fingerprint mismatch at A+{p:X}")
    targets = {0x4B76A0, 0x4C347C}
    calls = []
    # Bounded dword pattern search: not a complete instruction-boundary proof.
    for p in range(0x4B5F20, 0x4D15C0, 4):
        if u32(p) == 0x7EFF0000:
            target = p + 8 + struct.unpack_from("<i", pool, p + 4)[0]
            if target in targets:
                calls.append({"at": hex(p), "target": hex(target)})
    return {
        "provenance": "offline packed PC bytes; not REA/runtime evidence",
        "fingerprints_checked": len(fingerprints),
        "ninja_events": {"19": "0x4ba218", "20": "0x4ba24c", "35": "0x4b8708"},
        "ninja_sword_event_54": "0x4c3f9c",
        "belt_color_helper": "0x4b76a0",
        "sword_color_helper": "0x4c347c",
        "bounded_relative_call_patterns": calls,
        "material_names": {
            hex(p): pool[p:pool.index(b"\0", p)].decode("ascii")
            for p in (0x4B5186, 0x4B5176)
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(r"E:\Games\I-Ninja"))
    parser.add_argument("--member", default="9-95.LCS")
    parser.add_argument("--range", nargs=2, type=lambda s: int(s, 0),
                        metavar=("START", "END"), help="A-relative dword range")
    parser.add_argument("--native", nargs=2, type=lambda s: int(s, 0),
                        metavar=("VA", "SIZE"), help="linear x86 decode, not CFG")
    parser.add_argument("--summary", action="store_true",
                        help="check 9-95.LCS cosmetic fingerprints and report leads")
    args = parser.parse_args()
    exe = (args.game / "I-Ninja.exe").read_bytes()
    if hashlib.sha256(exe).hexdigest() != mission.EXE_SHA:
        raise ValueError("unsupported executable SHA-256")
    print("EXE SHA256", mission.EXE_SHA)
    if args.native:
        import capstone
        import pefile
        va, size = args.native
        pe = pefile.PE(data=exe)
        raw = pe.get_data(va - pe.OPTIONAL_HEADER.ImageBase, size)
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        print("OFFLINE Capstone", capstone.__version__, "linear decode")
        for ins in decoder.disasm(raw, va):
            print(f"{ins.address:08X} {ins.bytes.hex():24} {ins.mnemonic} {ins.op_str}")
    if args.range or args.summary:
        pool, identity = member(args.game, args.member)
        print(json.dumps(identity, indent=2))
        if args.summary:
            if args.member != "9-95.LCS":
                raise ValueError("--summary requires 9-95.LCS")
            print(json.dumps(cosmetic_summary(pool), indent=2))
    if args.range:
        start, end = args.range
        if start < 0 or end > len(pool) or start % 4 or end % 4 or start > end:
            raise ValueError("invalid aligned A range")
        for p in range(start, end, 4):
            word = struct.unpack_from("<I", pool, p)[0]
            print(f"A+{p:08X} {word:08X}")


if __name__ == "__main__":
    main()
