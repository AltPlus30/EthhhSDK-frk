#include "mission_timer.h"
#include "game.h"
#include "util.h"
#include "trainer_movement.h"
#include "input.h"
#include "ethhhpatch_runtime.h"
#include <MinHook.h>
#include <set>

namespace eth::mission_timer {
namespace {
constexpr uintptr_t kVm = 0x424BA0;
constexpr uintptr_t kCountdownOffset = 0x298;
// 9-96.LCS MissionTimer: read time scale, subtract, clamp/expire below 60 ticks.
constexpr uint32_t kCountdown[] = {
    0x42251033, 0x0C020225, 0xF4FF0200, 0x42700000,
    0x00000008, 0x86020100, 0x320F0001, 0x7A010012
};
constexpr uint32_t kSkipCountdown = 0x78FF0006; // PC += 4 + 6*4
using VmFn = void(__thiscall*)(void*);
VmFn original = nullptr;
std::atomic<bool> installed{false};
std::mutex mutex;
std::set<std::string> owners;
std::atomic<bool> stopped{false};

uintptr_t CountdownAddress(uintptr_t strat)
{
    // This runs for EVERY strat's interpreter invocation. SafeRead performs a
    // VirtualQuery per read: even rejecting non-timers incurred three system
    // calls. These are native-owned objects on the VM thread, not arbitrary UI
    // addresses. Guard direct access with SEH, without caching heap identities.
    __try {
        if (!strat || strat > UINT32_MAX - 0x1AC) return 0;
        const uintptr_t cls = *reinterpret_cast<const uint32_t*>(strat + 0x1A8);
        if (!cls || cls > UINT32_MAX - 0x10) return 0;
        const auto* name = *reinterpret_cast<const char* const*>(cls + 4);
        if (!name || memcmp(name, "MissionTimer", sizeof("MissionTimer"))) return 0;
        const uintptr_t code = *reinterpret_cast<const uint32_t*>(cls + 0xC);
        if (!code || (code & 3) ||
            code > UINT32_MAX - kCountdownOffset - sizeof(kCountdown)) return 0;
        const uintptr_t address = code + kCountdownOffset;
        if (memcmp(reinterpret_cast<const void*>(address), kCountdown, sizeof(kCountdown))) return 0;
        return address;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool WriteScriptWord(uintptr_t address, uint32_t expected, uint32_t replacement)
{
    // Interpreted ASL data, NOT native executable code. No VirtualProtect or
    // FlushInstructionCache is needed. Read-only/invalid storage fails closed.
    __try {
        auto* word = reinterpret_cast<volatile uint32_t*>(address);
        if (*word != expected) return false;
        *word = replacement;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void __fastcall VmDetour(void* self, void*)
{
    uintptr_t patched = 0;
    trainer_movement::VmPatch movement;
    // Disabled: atomic test only. Never resolve a player or query memory for
    // unrelated VM calls. Fresh pin/registry checks occur only for matching self.
    if (trainer_movement::Active() && !input::NativeMovementKeysOwned() &&
        !patch::runtime::ExternalCameraOwned()) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(self);
        if (game::PlayerTracker::Get().IsPinnedVmNinja(address))
            movement = trainer_movement::BeforeVm(address, address, false);
    }
    if (stopped.load(std::memory_order_relaxed)) {
        patched = CountdownAddress(reinterpret_cast<uintptr_t>(self));
        if (patched && !WriteScriptWord(patched, kCountdown[0], kSkipCountdown)) patched = 0;
    }
    // No saved heap pointers survive this invocation. Streaming/restarts may
    // replace the class; it is resolved and fingerprinted again on every call.
    __try {
        if (original) original(self);
    }
    __finally {
        trainer_movement::AfterVm(movement);
        if (patched) WriteScriptWord(patched, kSkipCountdown, kCountdown[0]);
    }
}
}

bool Install()
{
    if (installed) return true;
    char exe[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!length || length >= MAX_PATH ||
        Sha256FileHex(exe) != "49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0")
        return false;
    // VM prologue, verified separately from the packed script fingerprint.
    const uint8_t expected[] = {0x83, 0xEC, 0x20, 0x57, 0x8B, 0xF9,
                                0x8B, 0x8F, 0x90, 0x01, 0x00, 0x00};
    uint8_t bytes[sizeof(expected)]{};
    if (!SafeReadBytes(game::A(kVm), bytes, sizeof(bytes)) ||
        memcmp(bytes, expected, sizeof(bytes))) return false;
    void* target = reinterpret_cast<void*>(game::A(kVm));
    if (MH_CreateHook(target, reinterpret_cast<void*>(&VmDetour),
                      reinterpret_cast<void**>(&original)) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        original = nullptr;
        return false;
    }
    installed = true;
    return true;
}

bool Available() { return installed.load(std::memory_order_acquire); }

bool SetStopped(const std::string& owner, bool value)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (owner.empty() || (value && !installed)) return false;
    if (value) owners.insert(owner);
    else owners.erase(owner);
    stopped.store(!owners.empty(), std::memory_order_relaxed);
    return true;
}

void Release(const std::string& owner) { SetStopped(owner, false); }

void Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex);
    owners.clear();
    stopped.store(false, std::memory_order_relaxed);
    if (installed) {
        void* target = reinterpret_cast<void*>(game::A(kVm));
        MH_DisableHook(target);
        MH_RemoveHook(target);
        installed = false;
        original = nullptr;
    }
}
}
