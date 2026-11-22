#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace eth::cosmetic::textures {
enum class Slot : unsigned { Blue, Secondary, Head };
struct Image {
    unsigned width = 0, height = 0;
    std::vector<uint32_t> pixels; // top-left ARGB
};
// Bounded uncompressed/indexed and truecolour RLE TGA decoder. Atomic failure.
bool Decode(const uint8_t* bytes, size_t size, Image& output);
// Exactly three files, ordered Blue/Secondary/Head. CPU-only and transactional.
bool Load(const std::string& owner, const std::string& modDirectory,
          const std::vector<std::string>& relativePaths, std::string& error);
void Release(const std::string& owner);
void Shutdown();
struct Binding {
    uintptr_t instance = 0, mesh = 0, nativeTexture = 0;
    Slot slot = Slot::Blue;
    uint32_t rgb = 0xFFFFFF; // Head MUST be white; native material supplies head tint.
};
using DrawContinuation = void(__cdecl*)(void* context);
// Engine/render thread ONLY. Caller must validate exact actor generation/class,
// appearance, and that instance/mesh are its PRIVATE clones before each call.
// This is for a synchronous actual mesh draw, NOT queue submission/AppRender.
// The continuation must neither destroy the mesh nor escape its texture pointer.
// No native hook is installed by this module: see cosmetic-texture-binding.md.
// True: continuation ran (exactly once); false: caller must draw natively.
bool Draw(const Binding& binding, DrawContinuation continuation, void* context);
// Supported-build synchronous native PASS boundaries, not queue builders:
// opaque 0x44D890: void __thiscall(void* renderer)
// transparent 0x44C5C0: void __thiscall(void* renderer, int pass)
using OpaqueDrawFn = void(__thiscall*)(void* renderer);
using TransparentDrawFn = void(__thiscall*)(void* renderer, int pass);
constexpr uintptr_t kOpaqueDraw = 0x44D890, kTransparentDraw = 0x44C5C0;
// Call BEFORE installing either hook, after the executable hash gate.
bool VerifyPassHookFingerprints();
// Same true/false continuation contract as Draw. No native field is changed
// until every requested binding validates and all uploads are complete.
// Duplicate meshes/conflicting RGB values for one slot are refused atomically.
bool BatchDraw(const std::vector<Binding>& bindings,
                DrawContinuation continuation, void* context);

// Tick/render thread only; no concurrent destruction of native resources.
// Treat prepared storage as opaque, do not mutate/copy entries manually.
constexpr size_t kMaxBatchBindings = 128;
constexpr size_t kMaxPreparedChainNodes = 2048;
struct PreparedBatch {
    struct Entry {
        Binding binding{};
        std::array<uint8_t, 32> descriptor{};
        unsigned instanceIndex = 0, descriptorIndex = 0;
    };
    struct Instance { uintptr_t address = 0; uint32_t heads[2]{}; };
    struct ChainNode { uintptr_t address = 0; uint32_t next = 0; };
    std::array<Entry, kMaxBatchBindings> entries{};
    std::array<Instance, kMaxBatchBindings> instances{};
    std::array<ChainNode, kMaxPreparedChainNodes> topology{};
    size_t count = 0, instanceCount = 0, topologyCount = 0;
    uint64_t generation = 0;
};
// Prepare ON TICK after verifying actor identity/generation/appearance.
// Walks each private instance's two lists once, hashes each native source only
// on bounded cache misses. Does not touch D3D. Failure clears output.
bool PrepareBatch(const std::vector<Binding>& bindings, PreparedBatch& output);
// Single caller convenience cache. A matching binding/descriptor/head snapshot
// makes repeated preparation a guarded-read-only no-op. After SuspendPrepared,
// also proves fresh source bytes equal the immutable SHA-admitted originals.
bool PrepareBatch(const std::vector<Binding>& bindings);
bool DrawPrepared(DrawContinuation draw, void* context);
void InvalidatePrepared();
// Block drawing across a native tick, retaining only structural hints. Resume
// requires fresh guarded source-byte equality and full structural validation.
void SuspendPrepared();
// Actual synchronous draw pass only. No list walking, SHA, VirtualQuery or heap
// allocation after GPU warm-up. Rechecks flags/list heads, mesh pointers and
// complete current descriptors and flat cached node.next fields with guarded
// reads before borrowing anything (including middle-chain changes).
bool DrawPreparedBatch(const PreparedBatch& batch, DrawContinuation draw, void* context);
// Call BEFORE scene/resource generation changes, model clone destruction,
// topology changes or source mutation/reuse (even at the same address).
// Also discard caller's prepared batch whenever actor identity/appearance changes.
// Load/Release/Shutdown invalidate automatically. Not callable within a draw.
void InvalidateBindings();
struct PerformanceCounters {
    uint64_t hashes = 0, sourceBytes = 0, guardedReads = 0, chainNodes = 0;
    uint64_t sourceComparisons = 0;
    uint64_t uploads = 0, preparations = 0, suspensions = 0;
};
PerformanceCounters GetPerformanceCounters();
void ResetPerformanceCounters();
} 
