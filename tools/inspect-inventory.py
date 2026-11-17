"""Read-only inventory HUD anchors: fingerprinted executable + 09.vol/9-96.LCS.

No process access, engine calls, extracted files, or changes to game data.
Requires Capstone, like inspect-ethhhpatch.py.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PATCH = load("patch_inventory_evidence", "inspect-ethhhpatch.py")
DECODER = load("inventory_decoder", "inspect-mission-timer.py")
CALL = (0x15741172, 0x15751672, 0x15761872, 0x80FF7400, 0x00E6000E)


def inspect(game):
    data = (game / "I-Ninja.exe").read_bytes()
    image = PATCH.PEImage(data)  # rejects any other executable digest
    md = PATCH.Cs(PATCH.CS_ARCH_X86, PATCH.CS_MODE_32)

    def rows(start, end):
        at = image.offset(start, end-start, executable=True)
        return {i.address: (i.mnemonic, i.op_str) for i in md.disasm(data[at:at+end-start], start)}

    def expect(instructions, address, mnemonic, operands):
        if instructions.get(address) != (mnemonic, operands):
            raise ValueError(f"instruction contract changed at {address:X}")

    native = rows(0x41DD10, 0x41DD4A)
    expect(native, 0x41DD14, "mov", "ecx, dword ptr [edx]")
    expect(native, 0x41DD20, "cmp", "dword ptr [eax + 0x519a60], ecx")
    expect(native, 0x41DD44, "call", "0x42bef0")
    expect(native, 0x41DD49, "ret", "")
    dispatch = rows(0x426056, 0x42606B)
    expect(dispatch, 0x42605E, "add", "esi, 4")
    expect(dispatch, 0x426061, "call", "eax")
    expect(dispatch, 0x426063, "add", "esp, 8")
    root = rows(0x42BEF0, 0x42BF10)
    expect(root, 0x42BEF8, "mov", "dword ptr [ecx + 0x1c], eax")
    expect(root, 0x42BF05, "jmp", "0x42bc60")
    # Parented sprites refresh their derived coordinates each native UI draw.
    child = rows(0x42BEC1, 0x42BED7)
    expect(child, 0x42BED2, "call", "0x42bc60")
    tick = rows(0x43E0A8, 0x43E11E)
    expect(tick, 0x43E0A8, "call", "0x448450")
    expect(tick, 0x43E119, "call", "0x42b580")
    objects = rows(0x42B580, 0x42B5A0)
    expect(objects, 0x42B594, "call", "dword ptr [eax + 4]")
    reset = rows(0x448450, 0x448478)
    expect(reset, 0x448460, "mov", "dword ptr [eax], 0")
    expect(reset, 0x44846D, "mov", "dword ptr [0x502d98], 0x500d98")
    if struct.unpack_from("<I", data, image.offset(0x4C7E70+0xE6*4, 4))[0] != 0x41DD10:
        raise ValueError("native table E6 differs")
    if struct.unpack_from("<I", data, image.offset(0x4C8D98, 4))[0] != 0x42BD40:
        raise ValueError("sprite object draw vtable differs")
    font = rows(0x40D3A0, 0x40D404)
    expect(font, 0x40D3A9, "mov", "dword ptr [esi + 0x24], eax")
    expect(font, 0x40D3E8, "mov", "dword ptr [esi + 0x28], 0")
    expect(font, 0x40D3EF, "mov", "dword ptr [esi + 0x2c], 0x280")
    queue = rows(0x40D1D0, 0x40D245)
    expect(queue, 0x40D1FB, "mov", "dword ptr [eax], edi")
    expect(queue, 0x40D1FD, "mov", "dword ptr [ecx], eax")
    expect(queue, 0x40D23F, "lea", "edi, [eax + 0x18]")
    renderer = rows(0x448860, 0x44888E)
    expect(renderer, 0x448870, "mov", "esi, dword ptr [ebp]")
    expect(renderer, 0x448882, "mov", "dword ptr [esp + 0x14], esi")
    clip_site = rows(0x448BB8, 0x448BFF)
    expect(clip_site, 0x448BBA, "push", "ebp")
    expect(clip_site, 0x448BE4, "call", "0x4484b0")
    expect(clip_site, 0x448BEF, "add", "esp, 4")
    clip = rows(0x4484B0, 0x448670)
    expect(clip, 0x4484B3, "mov", "eax, dword ptr [esp + 0x10]")
    expect(clip, 0x4484E2, "fild", "dword ptr [eax + 0x34]")
    # Every font-pointer operand is a rectangle read: passing a local copy
    # cannot change the shared font or affect texture/atlas ownership.
    font_operands = [op for _, op in clip.values() if "[eax +" in op]
    if set(font_operands) != {"dword ptr [eax + 0x28]", "dword ptr [eax + 0x2c]",
                             "dword ptr [eax + 0x30]", "dword ptr [eax + 0x34]"}:
        raise ValueError("glyph clip font field coverage differs")

    archive = (game / "Data" / "09.vol").read_bytes()
    if archive[:8] != b"VOLT\x02\0\0\0":
        raise ValueError("unsupported archive")
    count = struct.unpack_from("<I", archive, 8)[0]
    if count > 10000:
        raise ValueError("archive entry limit")
    base = 16+count*12
    for index in range(count):
        at = base+struct.unpack_from("<I", archive, 24+index*12)[0]
        name = archive[at+16:archive.index(b"\0", at+16)].decode("ascii")
        if name != "9-96.LCS":
            continue
        start, _, size, _ = struct.unpack_from("<4I", archive, at)
        wad = archive[start:start+size]
        if hashlib.sha256(wad).hexdigest() != "7db935b142e61e5fb33a5f518140a532dc3376ecb42964cee56f5e44f2e5e8d4":
            raise ValueError("unsupported 9-96.LCS fingerprint")
        expected, _, compressed, _ = struct.unpack_from("<4I", wad, 0x80)
        blob = DECODER.unpack(wad[0x190:0x190+compressed])
        if len(blob) != expected:
            raise ValueError("decompressed size differs")
        cls, code = 0xAA740, 0x9AC50
        name_at = struct.unpack_from("<I", blob, cls+4)[0]
        if blob[name_at:name_at+6] != b"Panel\0" or struct.unpack_from("<I", blob, cls+12)[0] != code:
            raise ValueError("Panel class association differs")
        if struct.unpack_from("<5I", blob, code+0xDB0) != CALL:
            raise ValueError("inventory position call differs")
        for offset, value in ((0x57C, 396.0), (0xD24, 272.0), (0xD48, 256.0), (0xD8C, 416.0)):
            if struct.unpack_from("<f", blob, code+offset)[0] != value:
                raise ValueError(f"inventory anchor differs at {offset:X}")
        if struct.unpack_from("<I", blob, code+0xCE8)[0] != 0xD2FF7204:
            raise ValueError("four-item inventory loop differs")
        return {
            "exe_sha256": PATCH.SHA256, "archive_sha256": hashlib.sha256(archive).hexdigest(),
            "member": name, "member_sha256": hashlib.sha256(wad).hexdigest(),
            "decoded_sha256": hashlib.sha256(blob).hexdigest(),
            "class_offset": hex(cls), "unit_offset": hex(code),
            "inventory_call_offset": hex(code+0xDB0), "call_words": [f"{x:08X}" for x in CALL],
            "native_move": "0x41DD10", "dispatcher_return": "0x426063",
            "text_clip": {"native": "0x4484B0", "caller": "0x448BE9",
                          "string_at_entry_esp": "0x1C", "native_horizontal_bounds": [0, 640],
                          "policy": "inventory-queued strings only; pass a local widened clip copy"},
            "authored_x": {"hidden": 396, "show": 256, "alternate_show": 272, "hide_easing_target": 416},
            "old_1080p_slide_pixels": (396-256)*1080/480,
            "fix": "scale inventory parent X by aspect/(4/3); preserve child sizes/offsets",
            "static_only": True, "live_accepted": False,
        }
    raise ValueError("9-96.LCS not found")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(r"E:\Games\I-Ninja"))
    print(json.dumps(inspect(parser.parse_args().game), indent=2))
