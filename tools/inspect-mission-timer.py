"""Reproduce MissionTimer evidence without launching or modifying I-Ninja.

Reads 09.vol/9-96.LCS, using the decoder at executable VA 0x444EE0.
Prints a compact JSON report, not extracted game content.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

EXE_SHA = "49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0"
COUNTDOWN = (
    0x42251033, 0x0C020225, 0xF4FF0200, 0x42700000,
    0x00000008, 0x86020100, 0x320F0001, 0x7A010012,
)


def unpack(data):
    """Decoder at I-Ninja.exe VA 0x444EE0; offsets are output-relative."""
    out = bytearray()
    pos = 0
    while pos < len(data):
        token = data[pos]
        pos += 1
        if token >= 128:
            low = data[pos]
            pos += 1
            displacement = ((token << 8 | low) >> 4) - 4096 - 1
            for _ in range((low & 15) + 2):
                if len(out) + displacement < 0:
                    raise ValueError("backreference before start")
                out.append(out[len(out) + displacement])
        elif token < 64:
            count = token + 1
            if pos + count > len(data):
                raise ValueError("truncated literal")
            out.extend(data[pos:pos + count])
            pos += count
        else:
            out.extend(bytes([data[pos]]) * (token - 62))
            pos += 1
        if len(out) > 64 * 1024 * 1024:
            raise ValueError("decompressed size limit")
    return bytes(out)


def inspect(game):
    exe = (game / "I-Ninja.exe").read_bytes()
    if hashlib.sha256(exe).hexdigest() != EXE_SHA:
        raise ValueError("unsupported executable SHA-256")
    data = (game / "Data" / "09.vol").read_bytes()
    if data[:8] != b"VOLT\x02\0\0\0":
        raise ValueError("unsupported archive")
    count = struct.unpack_from("<I", data, 8)[0]
    base = 16 + count * 12
    for index in range(count):
        offset = base + struct.unpack_from("<I", data, 24 + index * 12)[0]
        start, _, size, _ = struct.unpack_from("<4I", data, offset)
        name = data[offset + 16:data.index(b"\0", offset + 16)].decode()
        if name != "9-96.LCS":
            continue
        wad = data[start:start + size]
        expected, _, compressed, _ = struct.unpack_from("<4I", wad, 0x80)
        blob = unpack(wad[0x190:0x190 + compressed])
        if len(blob) != expected:
            raise ValueError("decompressed size mismatch")
        name_offset = blob.index(b"MissionTimer\0")
        pointer = struct.pack("<I", name_offset)
        candidates = []
        for at in range(4, len(blob) - 40, 4):
            if blob[at:at + 4] != pointer:
                continue
            cls = at - 4
            code = struct.unpack_from("<I", blob, cls + 12)[0]
            words = struct.unpack_from("<8I", blob, code + 0x298)
            if words == COUNTDOWN:
                candidates.append((cls, code))
        if len(candidates) != 1:
            raise ValueError("expected one fingerprinted MissionTimer class")
        cls, code = candidates[0]
        return {
            "exe_sha256": EXE_SHA,
            "archive_sha256": hashlib.sha256(data).hexdigest(),
            "member": name,
            "member_sha256": hashlib.sha256(wad).hexdigest(),
            "decompressed_sha256": hashlib.sha256(blob).hexdigest(),
            "class_offset": hex(cls),
            "code_offset": hex(code),
            "countdown_offset": hex(code + 0x298),
            "countdown_words": [f"{word:08X}" for word in COUNTDOWN],
            "replacement_word": "78FF0006",
            "branch_target": hex(code + 0x298 + 4 + 6 * 4),
            "instance_register_pointer_offset": "0x1B4",
            "remaining_float_register": 2,
            "running_int_register": 1,
            "evidence_type": "static artifact inspection; no live execution",
        }
    raise ValueError("9-96.LCS not found")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(r"E:\Games\I-Ninja"))
    args = parser.parse_args()
    print(json.dumps(inspect(args.game), indent=2))
