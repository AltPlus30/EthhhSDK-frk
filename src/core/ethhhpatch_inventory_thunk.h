#pragma once

// Native 0xE6 is cdecl(self, handle/X/Y). Capture the VM's ESI before a
// compiler prologue can reuse it. Handler is cdecl(self, args, caller, nextPC).
// Leave the caller's original two arguments and cdecl cleanup unchanged.
#define ETH_INVENTORY_THUNK(name, handler) \
    __declspec(naked) void name() { \
        __asm { push esi } \
        __asm { push dword ptr [esp + 4] } \
        __asm { push dword ptr [esp + 10h] } \
        __asm { push dword ptr [esp + 10h] } \
        __asm { call handler } \
        __asm { add esp, 10h } \
        __asm { ret } \
    }

// 0x448BE4's glyph clip call has the current queued string at entry ESP+0x1C.
// Capture it before the clipper reuses ESI for the glyph vertex buffer.
#define ETH_INVENTORY_CLIP_THUNK(name, handler) \
    __declspec(naked) uint32_t name() { \
        __asm { push dword ptr [esp + 1Ch] } \
        __asm { push dword ptr [esp + 4] } \
        __asm { push dword ptr [esp + 0Ch] } \
        __asm { call handler } \
        __asm { add esp, 0Ch } \
        __asm { ret } \
    }
