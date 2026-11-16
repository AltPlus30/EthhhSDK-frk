#include "mission_timer.h"
#include "game.h"
#include "util.h"
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
bool installed = false;
std::mutex mutex;
std::set<std::string> owners;
std::atomic<bool> stopped{false};

uintptr_t CountdownAddress(uintptr_t strat)
{
    uint32_t cls = 0, name = 0, code = 0;
    char text[13]{};
    uint32_t words[std::size(kCountdown)]{};
    if (!SafeRead(strat + 0x1A8, cls) || !cls ||
        !SafeRead(cls + 4, name) || !name ||
        !SafeReadBytes(name, text, sizeof(text)) ||
        memcmp(text, "MissionTimer", sizeof(text)) ||
        !SafeRead(cls + 0xC, code) || !code ||
        code > UINT32_MAX - kCountdownOffset - sizeof(words)) return 0;
    const uintptr_t address = code + kCountdownOffset;
    if (!SafeReadBytes(address, words, sizeof(words)) ||
        memcmp(words, kCountdown, sizeof(words))) return 0;
    return address;
}

void __fastcall VmDetour(void* self, void*)
{
    uintptr_t patched = 0;
    if (stopped.load(std::memory_order_relaxed)) {
        patched = CountdownAddress(reinterpret_cast<uintptr_t>(self));
        if (patched && !SafeWrite(patched, kSkipCountdown)) patched = 0;
    }
    // No saved heap pointers survive this invocation. Streaming/restarts may
    // replace the class; it is resolved and fingerprinted again on every call.
    __try {
        if (original) original(self);
    }
    __finally {
        uint32_t current = 0;
        if (patched && SafeRead(patched, current) && current == kSkipCountdown)
            SafeWrite(patched, kCountdown[0]);
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
