#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace eth::cosmetic::blood::fx {
// Preferred-image addresses; relocate through game::A before use.
inline constexpr uintptr_t kAnimatedBinder = 0x455D10;
inline constexpr uintptr_t kOrdinaryBinder = 0x455B10;
inline constexpr uintptr_t kOrdinaryGpuGetter = 0x446870;
inline constexpr uintptr_t kMeshSetup = 0x4545D0;
// Normalize absolute operand at bytes 2..5 to relocated game::A(0x525500).
inline constexpr std::array<uint8_t, 11> kMeshSetupFingerprint = {
    0x51,0xA1,0x00,0x55,0x52,0x00,0x53,0x56,0x57,0x8B,0xF9
};
inline constexpr std::array<uint8_t, 16> kGpuGetterFingerprint = {
    0x8D,0x41,0x0C,0xC3,0x90,0x90,0x90,0x90,
    0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90
};
#ifdef _MSC_VER
using AnimatedBindFn = void (__thiscall*)(void*);
using GpuGetterFn = void* (__thiscall*)(void*);
using MeshSetupFn = void (__thiscall*)(void*);
#endif
inline constexpr std::array<uint8_t, 19> kAnimatedPrologue = {
    0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x81,0xEC,0xB4,0x01,0,0,
    0x53,0x56,0x57,0x89,0x4C,0x24,0x3C
};
using Read = bool (*)(uintptr_t address, void* output, size_t bytes, void* context);
struct Child {
    uint32_t descriptor = 0, gpuHandle = 0, source = 0;
    uint32_t width = 0, height = 0, sourceBytes = 0, stage = 0;
};
struct Children {
    std::array<Child, 8> items{};
    size_t count = 0;
};
// Read-only, transactional schema inspection, NOT effect attribution.
// Run on bounded discovery ticks (>=250ms), not each draw. The exact native
// binder hook can enqueue its `this` pointer without traversing resources.
// Caller supplies guarded reads and relocated binder addresses. Hash admission,
// source lifetime revalidation, GPU ownership and substitution remain host-owned.
// Unknown/nested texture kinds fail closed; no native virtual methods are called.
bool CollectAnimated(uintptr_t descriptor, uintptr_t animatedBinder,
                     uintptr_t ordinaryBinder, Read read, void* context,
                     Children& output);

// Cold-tick admission. This callback MUST establish an exact attributed parent
// recipe AND its effect/draw provenance, validate all child source hashes, and
// supply live SDK-owned GPU handles for every nonnull stage. The module does not
// infer attribution from dimensions, green pixels, class-name substrings or
// shared child hashes. Returning false leaves the previous prepared key intact.
using Authorize = bool (*)(uintptr_t parent, const Children& children,
                          std::array<uint32_t, 8>& replacementHandles,
                          void* context);
struct PreparedAnimated {
    uintptr_t parent = 0;
    std::array<uint32_t, 5> header{};
    std::array<std::array<uint8_t, 0x40>, 8> records{};
    std::array<std::array<uint32_t, 8>, 8> descriptors{};
    Children children{};
    std::array<uint32_t, 8> replacementHandles{};
    uintptr_t animatedBinder = 0, ordinaryBinder = 0, gpuGetter = 0;
};
bool PrepareAnimated(uintptr_t parent, uintptr_t animatedBinder,
                     uintptr_t ordinaryBinder, uintptr_t gpuGetter,
                     Read read, void* readContext, Authorize authorize,
                     void* authorizeContext, PreparedAnimated& output);
using Continue = void (*)(void*);
// ALWAYS invokes continuation exactly once, with TLS scope suppressed on any
// invalid/missing key. Never retry native rendering based on this function.
// A nested unadmitted parent suppresses an outer scope until it returns.
// Caller revalidates its exact actor/model admission before passing prepared.
// Prepared storage and SDK handles must stay alive and immutable until return.
void RunAnimated(uintptr_t parent, const PreparedAnimated* prepared,
                 Read read, void* readContext, Continue continuation,
                 void* continuationContext);
// Preferred when a parent texture is shared: host selects prepared only for an
// independently validated PRIVATE mesh belonging to the exact admitted effect.
// Wrap original 4545D0(mesh), not geometry submission or an entire frame.
// Rechecks mesh+0x10 == prepared.parent; host retains actor/generation/private-
// clone/model proof. No discovery, hashing, resource writes or virtual calls.
void RunMesh(uintptr_t mesh, const PreparedAnimated* prepared,
             Read read, void* readContext, Continue continuation,
             void* continuationContext);
// Getter detour contract: return this pointer when nonnull, otherwise call the
// original getter. Returns only an SDK handle's ADDRESS, never a copied parent
// descriptor; no native texture, vertex color, alpha or render state is written.
const uint32_t* ScopedGpuHandle(uintptr_t childDescriptor);

#ifdef _WIN32
// Exact green glow layer. Caller authorizes either explicit shared-asset
// recoloring (user approved), or a stricter effect/owner policy. Native memory
// is never modified; original GPU stream/texture state is restored per primitive.
// Called both at cold preparation and immediately before each mesh draw.
// Not thread-safe: serialize preparation/draw/release on native render thread.
using EffectProof = bool (*)(uintptr_t privateMesh, void* context);
struct GreenDraw;
struct IDirect3DDevice9Tag;
// device is IDirect3DDevice9*. Opaque here to avoid Windows headers in public API.
// Returns nullptr on any unproved recipe, source/hash/schema or upload failure.
GreenDraw* CreateGreenDraw(uintptr_t privateMesh, void* device, uint32_t rgb,
                          Read read, void* readContext,
                          EffectProof proof, void* proofContext);
// Refuses while ANY scoped mesh/primitive continuation is active.
bool ReleaseGreenDraw(GreenDraw* prepared);
bool GreenDrawHealthy(const GreenDraw* prepared);
// Both wrappers always run a nonnull continuation EXACTLY ONCE. A missing,
// stale, unproved or unsupported candidate draws natively.
void RunGreenMesh(uintptr_t mesh, GreenDraw* prepared,
                  Continue continuation, void* context);
void RunGreenPrimitive(uintptr_t primitive, Continue continuation, void* context);
inline constexpr uintptr_t kOpaqueMeshDraw = 0x454760;
inline constexpr uintptr_t kTransparentMeshDraw = 0x454470;
inline constexpr uintptr_t kPrimitiveDraw = 0x453E60;
#ifdef _MSC_VER
using OpaqueMeshDrawFn = void (__thiscall*)(void*, uintptr_t, uintptr_t);
using TransparentMeshDrawFn = void (__thiscall*)(void*);
using PrimitiveDrawFn = void (__thiscall*)(void*);
#endif
inline constexpr std::array<uint8_t, 9> kOpaqueMeshPrologue = {
    0x56,0x57,0x8B,0xF1,0xE8,0x67,0xFE,0xFF,0xFF
};
inline constexpr std::array<uint8_t, 9> kTransparentMeshPrologue = {
    0x56,0x8B,0xF1,0x8B,0x46,0x34,0x57,0x33,0xFF
};
#endif
} // namespace eth::cosmetic::blood::fx
