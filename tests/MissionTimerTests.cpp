// Production detour on synthetic script/class objects; no game process access.
#include "../src/core/mission_timer.cpp"
namespace eth {
uintptr_t GameAddr(uintptr_t a) { return a; }
std::string Sha256FileHex(const std::string&) { return {}; }
bool SafeReadBytes(uintptr_t a, void* out, size_t n) {
    if (!a) return false;
    __try { memcpy(out, reinterpret_cast<void*>(a), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SafeWriteBytes(uintptr_t a, const void* in, size_t n) {
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
    memcpy(&word, code + kCountdownOffset, 4);
    sawExpected = word == (expectedSkip ? kSkipCountdown : kCountdown[0]);
    if (raiseFault) RaiseException(0xE0123456, 0, 0, nullptr);
}
bool InvokeWithFault() {
    __try { eth::mission_timer::VmDetour(strat, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
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
    installed = false; // No real hook was installed.
    Shutdown();
    std::puts("Mission timer detour: opt-in, ownership, fingerprint, restoration and SEH checks passed");
}
