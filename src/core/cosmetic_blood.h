#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eth::cosmetic::blood {
constexpr uintptr_t kTextureBinder = 0x455B10;
// ECX=ordinary descriptor, no stack args, EAX=SetTexture HRESULT.
using BindFn = long(__thiscall*)(void* descriptor);
bool VerifyHookFingerprint(); // BEFORE hooking, after executable SHA gate
// Five monochrome TGAs in splatter 0/1/2/3/pool order; CPU-only, transactional.
// rgb is baked into SDK-owned textures, not written into native sprite corners.
bool Configure(const std::string& owner, const std::string& modDirectory,
               const std::vector<std::string>& paths, uint32_t rgb, std::string& error,
               bool sharedGlow=false);
bool SetGlowEnabled(const std::string& owner,bool enabled);
// All APIs are same engine/render thread ONLY, never concurrent with level unload.
// Tick: exact source-byte revalidation, at most four NEW hashes + GPU warm-up.
// Bind: exactly one synchronous original call; no hashing, allocation or upload.
void Tick();
// Revoke source trust before native code can unload/reuse resources. Tick
// compares fresh bytes on the safe side of the tick; identical reused headers
// alone are never trusted. SDK-owned byte/hash verdicts can outlive allocations.
void BeforeNativeTick();
long Bind(void* nativeTexture, BindFn original);
// BEFORE resource mutation/reuse/unload, including reuse at the same address.
void InvalidateBindings();
// Call before device Reset/destruction. Tick recreates managed GPU textures.
void OnDeviceReset();
void Release(const std::string& owner);
void Shutdown();
struct PerformanceCounters {
    uint64_t hashes = 0, sourceBytes = 0, uploads = 0, substitutions = 0;
    uint64_t byteCacheHits = 0, deferred = 0, saturated = 0;
    uint64_t lastTickUs = 0, maxTickUs = 0; // SDK Tick wall time, including cold uploads
    // Four splatters, three pool variants, then the separate anatomical cut cap.
    std::array<uint64_t,8> kinds{};
};
PerformanceCounters GetPerformanceCounters();
void ResetPerformanceCounters();
}
