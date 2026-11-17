"""Read-only HP/rage/inventory/movie widescreen contracts for the supported game.

No process access, engine calls, extraction, or game/SDK file writes.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

SPEC = importlib.util.spec_from_file_location("widescreen_inventory", Path(__file__).with_name("inspect-inventory.py"))
INV = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INV)


def panel(game):
    data = (game / "Data/09.vol").read_bytes()
    count = struct.unpack_from("<I", data, 8)[0]
    if data[:8] != b"VOLT\x02\0\0\0" or count > 10000:
        raise ValueError("unsupported archive")
    base = 16 + count*12
    for i in range(count):
        at = base + struct.unpack_from("<I", data, 24+i*12)[0]
        if data[at+16:data.index(b"\0", at+16)] != b"9-96.LCS":
            continue
        start, _, size, _ = struct.unpack_from("<4I", data, at)
        member = data[start:start+size]
        if hashlib.sha256(member).hexdigest() != "7db935b142e61e5fb33a5f518140a532dc3376ecb42964cee56f5e44f2e5e8d4":
            raise ValueError("unsupported Panel member fingerprint")
        expected, _, compressed, _ = struct.unpack_from("<4I", member, 0x80)
        blob = INV.DECODER.unpack(member[0x190:0x190+compressed])
        if len(blob) != expected:
            raise ValueError("decoded size differs")
        return blob
    raise ValueError("Panel package not found")


def inspect(game):
    previous = INV.inspect(game)
    blob = panel(game)
    start = 0x9AC50
    end = start + struct.unpack_from("<I", blob, start+4)[0]*4
    contracts = {
        "inventory": (0x15741172,0x15751672,0x15761872,0x80FF7400,0x00E6000E),
        "inventory_transition": (0x15811180,0x15821680,0x15831880,0x80FF8100,0x00E60009),
        "health": (0x0A790000,0x0A7A000C,0x5A7B0000,0xC3198000,0x80FF7900,0x00E60006),
        "rage": (0x0A560001,0x37595508,0x0D573959,0x5D595500,
                 0x42200000,0x5F585900,0x43000000,0x80FF5600,0x00E6000B),
    }
    sites = {}
    for name, words in contracts.items():
        pattern = struct.pack("<"+"I"*len(words), *words)
        found = [at for at in range(start+8, end-len(pattern)+1, 4) if blob[at:at+len(pattern)] == pattern]
        if len(found) != 1:
            raise ValueError(f"expected exactly one {name} call in Panel unit")
        sites[name] = {"package_offset": hex(found[0]), "native_word": hex(found[0]+len(pattern)-4),
                       "prefix_words": [f"{word:08X}" for word in words[:-1]]}
    hp = struct.unpack_from("<8I", blob, 0x8E830)
    rage = struct.unpack_from("<8I", blob, 0x23D60)
    if hp[1:3] != (256,32) or rage[1:3] != (32,32):
        raise ValueError("HP/rage texture dimensions differ")
    constructors = {}
    for name, words in (
        ("health",(0x5A640000,0x0008E830,0x5A650000,0xC3250000,0x5A660000,0xC3198000)),
        ("rage",(0x5AA40000,0x00023D60,0x5AA50000,0xC3C80000,0x5AA60000,0x43000000)),
    ):
        pattern=struct.pack("<"+"I"*len(words),*words)
        found=[at for at in range(start+8,end-len(pattern)+1,4) if blob[at:at+len(pattern)]==pattern]
        if len(found)!=1:
            raise ValueError(f"expected exactly one {name} constructor")
        constructors[name]=hex(found[0])
    # The timer/counter roots export the HP handle through Panel export AD.
    # They own their own children: clipping provenance must cross this one
    # supported script-owner boundary, not reject them as foreign menu text.
    if struct.unpack_from("<II",blob,0xADF48)!=(0xAD,0x10000):
        raise ValueError("Panel HP-root export differs")
    clock_calls = {}
    for name, offset, words in (
        ("timer_parent",0x9FC7C,(0x421B702B,0x421BF0AD,0x81091800,0x10FA0009)),
        ("timer_digit",0x9FCC4,(0x0A1B0009,0x81131800,0x113C0008)),
        ("counter_parent",0xA00C0,(0x420D702B,0x420DF0AD,0x81060A00,0x10FA0002)),
    ):
        if struct.unpack_from("<"+"I"*len(words),blob,offset)!=words:
            raise ValueError(f"attached clock contract differs: {name}")
        clock_calls[name]=hex(offset)
    data = (game / "I-Ninja.exe").read_bytes()
    image = INV.PATCH.PEImage(data)
    md = INV.PATCH.Cs(INV.PATCH.CS_ARCH_X86,INV.PATCH.CS_MODE_32)

    def rows(a,b):
        off=image.offset(a,b-a,executable=True)
        return {i.address:(i.mnemonic,i.op_str) for i in md.disasm(data[off:off+b-a],a)}

    def expect(rows, at, mnemonic, operands):
        if rows.get(at) != (mnemonic,operands):
            raise ValueError(f"native contract differs at {at:X}")

    draw = rows(0x497D40,0x497D5E)
    expect(draw,0x497D53,"jmp","0x497b10")
    expect(draw,0x497D58,"jmp","0x4977d0")
    call = rows(0x49A3AD,0x49A413)
    expect(call,0x49A3C6,"push","0x3f800000")  # alpha
    expect(call,0x49A3CB,"push","eax")         # scaleY
    expect(call,0x49A3CC,"push","ecx")         # scaleX
    expect(call,0x49A403,"push","ebx")         # X (0)
    expect(call,0x49A404,"push","edx")         # surface
    expect(call,0x49A405,"call","0x497d40")
    expect(call,0x49A410,"add","esp, 0x18")    # six arguments, no height
    sprite = rows(0x446D40,0x446DE0)
    expect(sprite,0x446DCA,"shl","eax, 4")
    expect(sprite,0x446DD5,"shl","ecx, 4")
    for index, target in ((0xFA,0x41DBD0),(0x13C,0x41DCC0)):
        if struct.unpack_from("<I",data,image.offset(0x4C87C8+index*4,4))[0]!=target:
            raise ValueError("return-native clock creation table differs")
    parent = rows(0x41DBD0,0x41DC18)
    expect(parent,0x41DBD4,"mov","edx, dword ptr [ecx + 0xc]")
    expect(parent,0x41DBE1,"cmp","dword ptr [eax + 0x519a60], edx")
    expect(parent,0x41DC09,"call","0x42b7c0")
    return {
        "exe_sha256": previous["exe_sha256"], "panel_sha256": previous["member_sha256"],
        "call_sites": sites, "constructors": constructors, "attached_clock_calls": clock_calls,
        "health": {"texture": "0x8E830", "pixels": list(hp[1:3]), "shown_x": -165,
                   "left_local_anchor": -128, "anchor_source": "live unrotated sprite half-width / 16",
                   "old_1080p_logical_left_difference": 219.75},
        "rage": {"texture": "0x23D60", "pixels": list(rage[1:3]), "anchor": "group origin",
                 "centered_selection_stays_centered": True},
        "movie": {"dispatch": "0x497D40", "return": "0x49A40A",
                  "arguments": ["surface","x","y","scaleX","scaleY","alpha"],
                  "caller_cleanup_bytes": 24,
                  "fit_1920x1080": {"x":240,"y":0,"scaleX":2.25,"scaleY":2.25}},
        "static_only": True, "followup_timer_reset_live_accepted": False,
        "prior_combined_build_feedback": "user reports looks good and plays well; timer/grade/restore follow-ups reported",
        "prior_inventory_performance": "user reports fixed after scoped-read/index optimization",
    }


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game",type=Path,default=Path(r"E:\Games\I-Ninja"))
    print(json.dumps(inspect(parser.parse_args().game),indent=2))
