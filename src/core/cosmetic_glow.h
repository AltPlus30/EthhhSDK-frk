#pragma once
#include <cstdint>
#include "cosmetic_blood_fx.h"

namespace eth::cosmetic::blood::glow {
// Exact shared-asset policy explicitly approved by the user. No actor/level
// heuristics and no edits to native meshes, vertices, indices or descriptors.
void Configure(uint32_t textureRgb, bool enabled);
void BeforeNativeTick();
void Tick();
void OnDeviceReset();
void Shutdown();
bool Enabled();
using MeshSetupFn = void(__thiscall*)(void*);
using PrimitiveFn = void(__thiscall*)(void*);
void MeshSetup(void* mesh, MeshSetupFn original);
void Primitive(void* primitive, PrimitiveFn original);
void EndPass();
struct Stats {
    uint64_t draws=0, refused=0, uploads=0, hashes=0;
    uint32_t colorOp=0,arg1=0,arg2=0;
    bool enabled=false,faulted=false;
};
Stats GetStats();
}
