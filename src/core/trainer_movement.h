#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace eth::trainer_movement {
// Native ASL tuning, not HL emulation. Session-only, default off.
// Factors are finite [0.5, 5]. This upper bound is editor policy, not architecture.
// Above 2x awaits live acceptance. A second owner is refused, not multiplied.
bool Set(const std::string& owner, float speedFactor, float jumpFactor);
void Release(const std::string& owner);
void Shutdown();
bool Active() noexcept;

// Trivial stack token: the existing VM hook MUST call AfterVm in __finally.
// Never retain this token, or any contained heap address, past this VM call.
struct VmPatch {
    struct Word { uintptr_t address; uint32_t original, replacement; };
    Word words[15]; // only entries below count are initialized/read
    size_t count = 0;
};
// Supply the fresh, validated exact player pin, and current HL ownership.
// Zero/another strat/HL-owned movement is always a no-op. This installs no hook.
VmPatch BeforeVm(uintptr_t strat, uintptr_t exactPinnedNinja, bool hlOwnsMovement) noexcept;
void AfterVm(VmPatch& patch) noexcept;
}
