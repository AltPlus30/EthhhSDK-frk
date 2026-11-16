// Production detour on synthetic script/class objects; no game process access.
#include "../src/core/mission_timer.cpp"
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
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
alignas(4) uint8_t code[0x400]{}, strat[0x310]{};
uint32_t cls[12]{};
char name[] = "MissionTimer";
bool expectedSkip = false, sawExpected = false, raiseFault = false;
void __fastcall FakeVm(void*) {
    using namespace eth::mission_timer;
    uint32_t word = 0;
    memcpy(&word, reinterpret_cast<void*>(cls[3] + kCountdownOffset), 4);
    sawExpected = word == (expectedSkip ? kSkipCountdown : kCountdown[0]);
    if (raiseFault) RaiseException(0xE0123456, 0, 0, nullptr);
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
    installed = true;
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
    std::puts("Mission timer detour: opt-in, ownership, fingerprint, restoration and SEH checks passed");
}
