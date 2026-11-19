#include "trainer_movement.h"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace eth::trainer_movement {
namespace {
constexpr uintptr_t kUnit = 0x4B5F20;
struct Site { uint32_t offset, instruction, bits; bool jump; };
// Package A offsets, not VAs. Only literal operands are modified.
constexpr Site sites[] = {
    {0x4B8F50-kUnit, 0x5A750000, 0x3DEB851F, false}, // cap .115
    {0x4B8F58-kUnit, 0x5A740000, 0x3F30A3D7, false}, // cap .69
    {0x4B8FC0-kUnit, 0x5A750000, 0x3C8D4FDF, false}, // cap .01725
    {0x4B8FC8-kUnit, 0x5A740000, 0x3D0D4FDF, false}, // cap .0345
    {0x4B9020-kUnit, 0x5A750000, 0x3DEB851F, false}, // cap .115
    {0x4B9028-kUnit, 0x5A740000, 0x3E6B851F, false}, // cap .23
    {0x4B9054-kUnit, 0x5A750000, 0x3B9D4952, false}, // acceleration .0048
    {0x4B905C-kUnit, 0x5A740000, 0x3B6BEDFA, false}, // .0036
    {0x4B9090-kUnit, 0x5A750000, 0x3DC49BA6, false}, // .096
    {0x4B9098-kUnit, 0x5A740000, 0x3D9374BC, false}, // .072
    {0x4B90BC-kUnit, 0x5A750000, 0x3CC49BA6, false}, // .024
    {0x4B90C4-kUnit, 0x5A740000, 0x3C9374BC, false}, // .018
    {0x4B8220-kUnit, 0x5A740000, 0x3E428F5C, true},  // launch .19
    {0x4B8234-kUnit, 0x5A740000, 0x3E4CCCCD, true},  // launch .20
    {0x4B8240-kUnit, 0x5A740000, 0x3E6147AE, true},  // launch .22
};
std::mutex mutex;
std::string owner;
// Pack both factors into one atomic snapshot, no lock or allocation in VM.
std::atomic<uint64_t> factors{0};
uint32_t Bits(float value) noexcept {
    uint32_t result; std::memcpy(&result, &value, 4); return result;
}
float Float(uint32_t value) noexcept {
    float result; std::memcpy(&result, &value, 4); return result;
}
bool Valid(float factor) noexcept {
    return std::isfinite(factor) && factor >= 0.5f && factor <= 5.0f;
}
bool Writable(uintptr_t address) noexcept {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) ||
        mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD mode = mbi.Protect & 0xff;
    // No VirtualProtect; refuse read-only storage instead of making it writable.
    if (mode != PAGE_READWRITE && mode != PAGE_WRITECOPY &&
        mode != PAGE_EXECUTE_READWRITE && mode != PAGE_EXECUTE_WRITECOPY) return false;
    const auto start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    return address >= start && address - start <= mbi.RegionSize &&
        sizeof(uint32_t) <= mbi.RegionSize - (address - start);
}
bool Replace(uintptr_t address, uint32_t expected, uint32_t replacement) noexcept {
    __try {
        auto* word = reinterpret_cast<volatile uint32_t*>(address);
        if (*word != expected) return false;
        *word = replacement;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uintptr_t Resolve(uintptr_t strat) noexcept {
    __try {
        if (!strat || strat > UINT32_MAX - 0x1AC) return 0;
        const uintptr_t cls = *reinterpret_cast<const uint32_t*>(strat + 0x1A8);
        if (!cls || cls > UINT32_MAX - 0x10) return 0;
        const auto* name = *reinterpret_cast<const char* const*>(cls + 4);
        if (!name || std::memcmp(name, "Ninja", sizeof("Ninja"))) return 0;
        const uintptr_t code = *reinterpret_cast<const uint32_t*>(cls + 0xC);
        if (!code || (code & 3) || code > UINT32_MAX - 0xD320) return 0;
        if (*reinterpret_cast<const uint32_t*>(cls + 8) != code + (0x4BA6F0-kUnit)) return 0;
        const auto* words = reinterpret_cast<const uint32_t*>(code);
        if (words[0] != 0x20 || words[1] != 0x34C8) return 0;
        // Consumers: shared base jump result and horizontal acceleration.
        const uint32_t jumpConsumer[] = {0x0ACF0075, 0x0DCE5CCF, 0x0B0BCDCE};
        const uint32_t acceleration[] = {0x0B0A0AE9, 0x0B0B0BEA, 0x0B0C0CEB};
        const uint32_t jumpCorrection[] = {
            0x5C797A00, 0x3E30A3D7, 0x35787902, 0x5A790000, 0x3DA3D70A
        };
        const uint32_t jumpResult[] = {
            0x0C747477, 0x427810A8, 0x0B767478, 0x5B757600, 0xBDCCCCCB, 0x88000000
        };
        const uint32_t interpolate[] = {0x0C797475, 0x0D787679, 0x0B777875};
        if (std::memcmp(reinterpret_cast<const void*>(code + 0x4B8AD4-kUnit),
                        jumpConsumer, sizeof(jumpConsumer)) ||
            std::memcmp(reinterpret_cast<const void*>(code + 0x4B9374-kUnit),
                        acceleration, sizeof(acceleration)) ||
            std::memcmp(reinterpret_cast<const void*>(code + 0x4B8258-kUnit),
                        jumpCorrection, sizeof(jumpCorrection)) ||
            std::memcmp(reinterpret_cast<const void*>(code + 0x4B8274-kUnit),
                        jumpResult, sizeof(jumpResult)) ||
            std::memcmp(reinterpret_cast<const void*>(code + 0x4B8CF8-kUnit),
                        interpolate, sizeof(interpolate)) ||
            *reinterpret_cast<const uint32_t*>(code + 0x4B8BA8-kUnit) != 0x0A0B0075)
            return 0;
        for (const auto& site : sites) {
            const auto* at = reinterpret_cast<const uint32_t*>(code + site.offset);
            if (at[-1] != site.instruction || at[0] != site.bits) return 0;
        }
        return code;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}

bool Set(const std::string& requestedOwner, float speed, float jump)
{
    if (requestedOwner.empty() || !Valid(speed) || !Valid(jump)) return false;
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner.empty() && owner != requestedOwner) return false;
    if (speed == 1.0f && jump == 1.0f) {
        factors.store(0, std::memory_order_relaxed);
        owner.clear();
    } else {
        owner = requestedOwner;
        factors.store(uint64_t(Bits(speed)) | (uint64_t(Bits(jump)) << 32),
                      std::memory_order_relaxed);
    }
    return true;
}
void Release(const std::string& requestedOwner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (owner != requestedOwner) return;
    factors.store(0, std::memory_order_relaxed);
    owner.clear();
}
void Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex);
    factors.store(0, std::memory_order_relaxed);
    owner.clear();
}
bool Active() noexcept { return factors.load(std::memory_order_relaxed) != 0; }

VmPatch BeforeVm(uintptr_t strat, uintptr_t exactPinnedNinja, bool hlOwnsMovement) noexcept
{
    VmPatch patch{};
    const uint64_t snapshot = factors.load(std::memory_order_relaxed);
    if (!snapshot || !strat || strat != exactPinnedNinja || hlOwnsMovement) return patch;
    const uintptr_t code = Resolve(strat);
    if (!code) return patch;
    const float speed = Float(uint32_t(snapshot)), jump = Float(uint32_t(snapshot >> 32));
    // Preflight EVERY changed word before the first store (read-only refusal).
    for (const auto& site : sites) {
        const float factor = site.jump ? jump : speed;
        if (factor == 1.0f) continue;
        if (!Writable(code + site.offset)) return {};
    }
    for (const auto& site : sites) {
        const float factor = site.jump ? jump : speed;
        if (factor == 1.0f) continue;
        const float base = Float(site.bits);
        // The jump helper subtracts .1 after adding support/speed corrections.
        // Scale its base net launch impulse, not unrelated gravity/r11 paths.
        const uint32_t replacement = Bits(site.jump ?
            0.1f + (base - 0.1f) * factor : base * factor);
        const uintptr_t at = code + site.offset;
        if (!Replace(at, site.bits, replacement)) {
            AfterVm(patch); return {};
        }
        patch.words[patch.count++] = {at, site.bits, replacement};
    }
    return patch;
}
void AfterVm(VmPatch& patch) noexcept
{
    while (patch.count) {
        const auto word = patch.words[--patch.count];
        Replace(word.address, word.replacement, word.original);
        patch.words[patch.count] = {};
    }
}
}
