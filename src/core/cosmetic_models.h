#pragma once
#include "ethhhpatch_entity_interpolation.h"

namespace eth::cosmetic::models {
using Matrix = patch::entity::Matrix;
using EntitySubmitFn = void(__thiscall*)(void*, void*, void*, const Matrix*);
using NodeSubmitFn = void(__thiscall*)(void*, void*, const Matrix*, const Matrix*);
using EntityContinuation = void(*)(void*, void*, void*, const Matrix*);

enum class Channel { Player = 0, Sword = 1 };
struct Actor {
    uint32_t addr = 0, pClass = 0, handle = 0;
};
struct FrameStats {
    uint32_t captured = 0, substituted = 0, waitingForDonor = 0, refused = 0;
};

// EXPERIMENTAL: donor-pose appearance, NOT animation retargeting or equipment.
// All calls must run on the same engine/render thread. UI must marshal requests.
// Selections are exact actor generations, never saved/reacquired on respawn.
// CaptureActor accepts only a live handle-manager/class-consistent actor.
bool CaptureActor(uintptr_t address, Actor& out);
bool Select(Channel channel, const Actor& recipient, const Actor& donor);
void Clear(Channel channel);
void ClearAll();

// Mandatory integration/lifetime contract:
//  * BeginFrame immediately before the OUTERMOST original AppRender (0x43E170).
//  * flushFenceInstalled may be true ONLY after successfully hooking 0x44A340.
//  * BeforeFlush at entry to 0x44A340, BEFORE calling its original. This disables
//    swapping/capture for the remainder of AppRender, including its second scene.
//  * EndFrame in AppRender's __finally, after all original drawing/Present.
// Native queue record+8 points to renderer-owned COPIED matrices, not world;
// record+0xC is their count. The scoped world/nodes inputs need only survive
// the synchronous submission. EndFrame clears all captured engine addresses.
// No native resource is retained, cloned, released, rebound or persisted here.
// A donor is usable ONLY AFTER its unmodified native instance has actually been
// queued earlier in this frame, with that exact native queue record still live.
// Invisible donors and donors submitted after their recipient therefore do not
// work. Resource destruction/rebinding from another thread or render callback is
// unsupported, just as it would invalidate the engine's own pending donor draw.
bool BeginFrame(void* renderer, bool flushFenceInstalled);
void BeforeFlush(void* renderer);
void EndFrame();
FrameStats Stats(); // engine-thread only; counters for current/last frame

// Wrap EVERY EntitySubmitDetour entry, including nested and fallback entries.
// continuation is the old detour BODY (cdecl, no dummy EDX), original is the
// existing native trampoline. Never pass the new detour as continuation.
// Native/unsupported submissions use continuation unchanged. A cosmetic draw
// bypasses recipient interpolation and uses frozen donor nodes for this frame.
void SubmitEntity(void* renderer, void* owner, void* instance,
    const Matrix* world, EntityContinuation continuation, EntitySubmitFn original);

// Call FIRST in NodeSubmitDetour; return immediately if true. False means run
// the existing interpolation/node-detour body. Do not hook these sites twice.
bool SubmitNodes(void* renderer, void* mesh, const Matrix* world,
    const Matrix* nodes, NodeSubmitFn original);

// Six built-in swords, independent of donor swapping and profile integer 17.
// Native hook is 0/0x29 (VA 0x416F20), NOT 0/0x5B. Install only on the verified
// executable; its ABI is cdecl(strat,args), args[0] = descriptor.
// The exact Ninja supplies the live/fingerprinted resource table, not ownership
// of the exact NinjaSword (that relationship is deliberately not inferred).
using SwordBindFn = void(__cdecl*)(void*, const uint32_t*);
bool SelectBuiltinSword(const Actor& resourceNinja, const Actor& exactSword, int tier);
// Stops future overrides; does not rebind the existing appearance. To restore
// immediately, select the REAL tier, ApplyBuiltinSword if eligible, then clear;
// otherwise the next normal game refresh restores it. Never modify the profile.
void ClearBuiltinSword();
// Call first in the 0x416F20 detour. True means original has been called with
// overridden args; false means the caller must forward the ORIGINAL args.
// Only overrides a normal six-tier binding already requested by the game.
bool BindBuiltinSword(void* strat, const uint32_t* args, SwordBindFn original);
// Optional tick-thread immediate refresh, outside rendering. Refuses unless the
// current primary instance is exactly one of the six canonical resource models.
// In particular this can refuse after tint COW; do not weaken that guard. The
// binding hook still works on the next native refresh. No event is broadcast.
bool ApplyBuiltinSword(SwordBindFn original);
} // namespace eth::cosmetic::models
