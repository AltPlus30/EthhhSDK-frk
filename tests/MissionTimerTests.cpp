// Production detour on synthetic script/class objects; no game process access.
#include "../src/core/mission_timer.cpp"
#include "../src/core/trainer_movement.cpp"
namespace {
alignas(4) uint8_t movementCode[0xD320]{}, movementStrat[0x300]{};
uint32_t movementClass[4]{};
char movementName[] = "Ninja";
bool movementPinned = true, hlOwned = false, cameraOwned = false;
}
namespace eth {
size_t safeReads = 0, safeWrites = 0;
uintptr_t GameAddr(uintptr_t a) { return a; }
std::string Sha256FileHex(const std::string&) { return {}; }
bool SafeReadBytes(uintptr_t a, void* out, size_t n) {
    ++safeReads;
    if (!a) return false;
    // Match production SafeRead's VirtualQuery cost for the legacy benchmark.
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi)) ||
        mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (a >= end || n > end - a) return false;
    __try { memcpy(out, reinterpret_cast<void*>(a), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SafeWriteBytes(uintptr_t a, const void* in, size_t n) {
    ++safeWrites;
    if (!a) return false;
    __try { memcpy(reinterpret_cast<void*>(a), in, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
namespace eth::game {
PlayerTracker& PlayerTracker::Get() { static PlayerTracker tracker; return tracker; }
bool PlayerTracker::IsPinnedVmNinja(uintptr_t self) const {
    return movementPinned && self == uintptr_t(movementStrat);
}
}
namespace eth::input { bool NativeMovementKeysOwned() { return hlOwned; } }
namespace eth::patch::runtime { bool ExternalCameraOwned() { return cameraOwned; } }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
alignas(4) uint8_t code[0x400]{}, strat[0x310]{};
uint32_t cls[12]{};
char name[] = "MissionTimer";
bool expectedSkip = false, sawExpected = false, raiseFault = false;
bool expectMovement = true, sawMovement = false, nestedTimer = false;
void MovementFixture() {
    using namespace eth::trainer_movement;
    const auto put = [](size_t offset, uint32_t word) { memcpy(movementCode + offset, &word, 4); };
    put(0, 0x20); put(4, 0x34C8);
    // Recipe details are independently checked in TrainerMovementTests.cpp.
    // Here we test the actual shared detour's call/restoration boundaries.
    for (const auto& site : sites) { put(site.offset - 4, site.instruction); put(site.offset, site.bits); }
    const uint32_t jump[] = {0x0ACF0075,0x0DCE5CCF,0x0B0BCDCE};
    const uint32_t acceleration[] = {0x0B0A0AE9,0x0B0B0BEA,0x0B0C0CEB};
    const uint32_t correction[] = {0x5C797A00,0x3E30A3D7,0x35787902,0x5A790000,0x3DA3D70A};
    const uint32_t result[] = {0x0C747477,0x427810A8,0x0B767478,0x5B757600,0xBDCCCCCB,0x88000000};
    const uint32_t interpolate[] = {0x0C797475,0x0D787679,0x0B777875};
    memcpy(movementCode + 0x2BB4, jump, sizeof(jump));
    memcpy(movementCode + 0x3454, acceleration, sizeof(acceleration));
    memcpy(movementCode + 0x2338, correction, sizeof(correction));
    memcpy(movementCode + 0x2354, result, sizeof(result));
    memcpy(movementCode + 0x2DD8, interpolate, sizeof(interpolate));
    put(0x2C88, 0x0A0B0075);
    movementClass[1] = uintptr_t(movementName);
    movementClass[3] = uintptr_t(movementCode);
    movementClass[2] = movementClass[3] + 0x47D0;
    const uint32_t cls = uintptr_t(movementClass);
    memcpy(movementStrat + 0x1A8, &cls, 4);
}
void __fastcall FakeVm(void* self) {
    using namespace eth::mission_timer;
    if (self == movementStrat) {
        float cap = 0, impulse = 0;
        memcpy(&cap, movementCode + 0x3030, 4);
        memcpy(&impulse, movementCode + 0x2300, 4);
        sawMovement = std::fabs(cap - (expectMovement ? 0.23f : 0.115f)) < 0.000001f &&
            std::fabs(impulse - (expectMovement ? 0.28f : 0.19f)) < 0.000001f;
        if (nestedTimer) VmDetour(strat, nullptr);
        if (raiseFault) RaiseException(0xE0123456, 0, 0, nullptr);
        return;
    }
    uint32_t word = 0;
    memcpy(&word, reinterpret_cast<void*>(cls[3] + kCountdownOffset), 4);
    sawExpected = word == (expectedSkip ? kSkipCountdown : kCountdown[0]);
    if (raiseFault) RaiseException(0xE0123456, 0, 0, nullptr);
}
bool InvokeMovementWithFault() {
    __try { eth::mission_timer::VmDetour(movementStrat, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
bool InvokeWithFault() {
    __try { eth::mission_timer::VmDetour(strat, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
// Keep the old production identification path for an apples-to-apples benchmark.
__declspec(noinline) uintptr_t LegacyProbe(uintptr_t self) {
    using namespace eth;
    uint32_t type = 0, textPtr = 0, bytecode = 0, words[8]{};
    char text[13]{};
    if (!SafeRead(self + 0x1A8, type) || !type ||
        !SafeRead(type + 4, textPtr) || !textPtr ||
        !SafeReadBytes(textPtr, text, sizeof(text)) ||
        memcmp(text, "MissionTimer", sizeof(text)) ||
        !SafeRead(type + 0xC, bytecode) || !bytecode ||
        bytecode > UINT32_MAX - eth::mission_timer::kCountdownOffset - sizeof(words)) return 0;
    const uintptr_t address = bytecode + eth::mission_timer::kCountdownOffset;
    if (!SafeReadBytes(address, words, sizeof(words)) ||
        memcmp(words, eth::mission_timer::kCountdown, sizeof(words))) return 0;
    return address;
}
__declspec(noinline) uintptr_t DirectProbe(uintptr_t self) {
    return eth::mission_timer::CountdownAddress(self);
}
double Benchmark(uintptr_t (*probe)(uintptr_t), size_t count) {
    volatile uintptr_t result = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (size_t i = 0; i < count; ++i) result = probe(reinterpret_cast<uintptr_t>(strat));
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}
}
int main()
{
    using namespace eth::mission_timer;
    static_assert(sizeof(void*) == 4);
    cls[1] = reinterpret_cast<uintptr_t>(name);
    cls[3] = reinterpret_cast<uintptr_t>(code);
    const uint32_t classPtr = reinterpret_cast<uintptr_t>(cls);
    memcpy(strat + 0x1A8, &classPtr, 4);
    memcpy(code + kCountdownOffset, kCountdown, sizeof(kCountdown));
    original = reinterpret_cast<VmFn>(&FakeVm);
    CHECK(!SetStopped("trainer", true)); // Hook unavailable.
    CHECK(!Available());
    installed = true;
    CHECK(Available());
    CHECK(!SetStopped("", true));
    VmDetour(strat, nullptr);
    CHECK(sawExpected); // Disabled path does not touch script.
    CHECK(SetStopped("trainer", true));
    expectedSkip = true;
    VmDetour(strat, nullptr);
    CHECK(sawExpected && !memcmp(code + kCountdownOffset, kCountdown, sizeof(kCountdown)));
    CHECK(kCountdownOffset + 4 + (kSkipCountdown & 0xffff) * 4 == 0x2B4);
    CHECK(SetStopped("second", true));
    Release("trainer");
    CHECK(stopped.load()); // One mod cannot release another mod's request.
    raiseFault = true;
    CHECK(InvokeWithFault());
    CHECK(!memcmp(code + kCountdownOffset, kCountdown, sizeof(kCountdown))); // finally
    raiseFault = false;
    CHECK(eth::safeReads == 0 && eth::safeWrites == 0); // Entire hot path, not only rejection.
    expectedSkip = false;
    name[0] = 'X';
    VmDetour(strat, nullptr);
    CHECK(sawExpected); // Exact class name required.
    name[0] = 'M';
    code[kCountdownOffset + 8] ^= 1;
    VmDetour(strat, nullptr);
    CHECK(sawExpected); // Unknown script refused, even with the right name.
    code[kCountdownOffset + 8] ^= 1;
    Release("second");
    CHECK(!stopped.load());
    VmDetour(strat, nullptr);
    CHECK(sawExpected);
    CHECK(CountdownAddress(0) == 0);
    CHECK(CountdownAddress(1) == 0); // Invalid memory must fail closed under SEH.
    cls[1] = 1;
    CHECK(CountdownAddress(reinterpret_cast<uintptr_t>(strat)) == 0);
    cls[1] = reinterpret_cast<uintptr_t>(name);

    // Same address reused by a different class must not use a stale positive cache.
    name[0] = 'X';
    CHECK(CountdownAddress(reinterpret_cast<uintptr_t>(strat)) == 0);
    name[0] = 'M';
    CHECK(CountdownAddress(reinterpret_cast<uintptr_t>(strat)) != 0);

    auto* readOnly = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(readOnly);
    memcpy(readOnly, code, sizeof(code));
    DWORD old = 0;
    CHECK(VirtualProtect(readOnly, 0x1000, PAGE_READONLY, &old));
    cls[3] = reinterpret_cast<uintptr_t>(readOnly);
    CHECK(SetStopped("trainer", true));
    expectedSkip = false;
    VmDetour(strat, nullptr); // Direct store is refused; native VM still runs.
    CHECK(sawExpected && !memcmp(readOnly + kCountdownOffset, kCountdown, sizeof(kCountdown)));
    Release("trainer");
    cls[3] = reinterpret_cast<uintptr_t>(code);
    CHECK(VirtualFree(readOnly, 0, MEM_RELEASE));
    CHECK(!WriteScriptWord(reinterpret_cast<uintptr_t>(code) + kCountdownOffset, kSkipCountdown, 0));
    CHECK(!memcmp(code + kCountdownOffset, kCountdown, sizeof(kCountdown))); // Foreign word not overwritten.
    CHECK(eth::safeReads == 0 && eth::safeWrites == 0);

    MovementFixture();
    CHECK(eth::trainer_movement::Set("trainer", 2, 2));
    CHECK(SetStopped("trainer", true));
    expectedSkip = true; nestedTimer = true;
    VmDetour(movementStrat, nullptr); // Native jump VM -> nested timer VM.
    CHECK(sawMovement && sawExpected);
    CHECK(!memcmp(code + kCountdownOffset, kCountdown, sizeof(kCountdown)));
    float restored = 0;
    memcpy(&restored, movementCode + 0x3030, 4);
    CHECK(std::fabs(restored - 0.115f) < 0.000001f);
    nestedTimer = false;
    raiseFault = true;
    CHECK(InvokeMovementWithFault() && sawMovement);
    raiseFault = false;
    memcpy(&restored, movementCode + 0x3030, 4);
    CHECK(std::fabs(restored - 0.115f) < 0.000001f);
    expectMovement = false;
    hlOwned = true;
    VmDetour(movementStrat, nullptr); CHECK(sawMovement);
    hlOwned = false; cameraOwned = true;
    VmDetour(movementStrat, nullptr); CHECK(sawMovement);
    cameraOwned = false; movementPinned = false;
    VmDetour(movementStrat, nullptr); CHECK(sawMovement);
    movementPinned = true;
    eth::trainer_movement::Release("trainer");
    Release("trainer");
    expectedSkip = false;

    // Most VM calls are not MissionTimer. No cached negative identities required.
    name[0] = 'X';
    constexpr size_t calls = 200000;
    const double legacyMs = Benchmark(LegacyProbe, calls);
    CHECK(eth::safeReads == 3 * calls);
    eth::safeReads = 0;
    const double directMs = Benchmark(DirectProbe, calls);
    CHECK(eth::safeReads == 0 && eth::safeWrites == 0);
    name[0] = 'M';
    std::printf("Non-timer identification, %zu calls: legacy %.3f ms, direct %.3f ms (%.1fx); hot-path SafeRead/Write calls: 0\n",
                calls, legacyMs, directMs, legacyMs / (std::max)(directMs, 0.000001));
    installed = false; // No real hook was installed.
    Shutdown();
    CHECK(!Available());
    std::puts("Mission timer detour: opt-in, ownership, fingerprint, restoration and SEH checks passed");
}
