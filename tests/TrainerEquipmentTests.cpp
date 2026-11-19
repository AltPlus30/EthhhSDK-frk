// Production queue + native cdecl call boundary; synthetic profile/strat only.
#include "../src/core/trainer_values.cpp"
#include "../src/core/trainer_equipment.cpp"
namespace {
alignas(4) uint8_t profile[0x200]{}, otherProfile[0x200]{}, ninjaMemory[0x300]{};
uint32_t profileRoot = uintptr_t(profile);
uintptr_t nativeEntry = 0;
uint32_t handle = 0x201;
bool valid = true, signatureMatches = true, raiseFault = false;
int stratReads = 0;
std::string className = "Ninja";
struct Notification { uintptr_t sender; std::string name; int event; };
std::vector<Notification> notifications;
void __cdecl FakeNotify(void* sender, const uint32_t* args) {
    if (raiseFault) RaiseException(0xE0123456, 0, 0, nullptr);
    notifications.push_back({uintptr_t(sender), reinterpret_cast<const char*>(args[0]), int(args[1])});
}
}
namespace eth {
uintptr_t GameAddr(uintptr_t va) {
    if (va == 0x51AA88) return uintptr_t(&profileRoot);
    if (va == 0x417380) return uintptr_t(&FakeNotify);
    if (va == 0x4C7E70 + 0x11D * 4) return uintptr_t(&nativeEntry);
    return 0;
}
std::string Sha256FileHex(const std::string&) {
    return "49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0";
}
bool SafeReadBytes(uintptr_t address, void* out, size_t count) {
    if (address == uintptr_t(&FakeNotify) && count == sizeof(trainer::equipment::kNotifyBytes)) {
        memcpy(out, trainer::equipment::kNotifyBytes, count);
        if (!signatureMatches) static_cast<uint8_t*>(out)[0] ^= 1;
        return true;
    }
    if (address == uintptr_t(&nativeEntry) && count == 4) { memcpy(out, &nativeEntry, count); return true; }
    return false;
}
namespace game {
PlayerTracker& PlayerTracker::Get() { static PlayerTracker tracker; return tracker; }
bool PlayerTracker::Valid() const { return valid; }
uintptr_t PlayerTracker::Addr() const { return manual_; }
void PlayerTracker::SetMode(PlayerMode mode) { mode_ = mode; }
void PlayerTracker::Select(uintptr_t address) { manual_ = address; manualValid_ = address != 0; }
bool ReadStrat(uintptr_t address, StratView& out) {
    ++stratReads;
    if (!valid || address != uintptr_t(ninjaMemory)) return false;
    out.addr = address;
    out.selfHandle = handle;
    strcpy_s(out.className, className.c_str());
    return true;
}
}
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main()
{
    using namespace eth::trainer;
    using namespace eth::trainer::equipment;
    static_assert(sizeof(void*) == 4);
    nativeEntry = uintptr_t(&FakeNotify);
    auto& tracker = eth::game::PlayerTracker::Get();
    tracker.SetMode(eth::game::PlayerMode::Manual);
    tracker.Select(uintptr_t(ninjaMemory));
    double value = 0;
    uintptr_t sword = 0, belt = 0, ranks = 0;
    CHECK(Read("sword", value, sword));
    CHECK(Read("belt", value, belt));
    CHECK(Read("ranks", value, ranks));
    CHECK(!Request("", "sword", 2, sword));
    CHECK(!Request("trainer", "health", 2, sword));
    CHECK(!Request("trainer", "sword", 6, sword));
    CHECK(!Request("trainer", "belt", 7, belt));
    CHECK(!Request("trainer", "ranks", 64, ranks));
    CHECK(!Request("trainer", "sword", 2, sword+4));
    valid = false;
    CHECK(!Request("trainer", "sword", 2, sword));
    valid = true;
    className = "NinjaSword";
    CHECK(!Request("trainer", "sword", 2, sword));
    className = "Ninja";
    tracker.SetMode(eth::game::PlayerMode::Auto);
    CHECK(!Request("trainer", "sword", 2, sword));
    tracker.SetMode(eth::game::PlayerMode::Manual);
    signatureMatches = false;
    CHECK(!Request("trainer", "sword", 2, sword));
    signatureMatches = true;
    nativeEntry = 0;
    CHECK(!Request("trainer", "sword", 2, sword));
    nativeEntry = uintptr_t(&FakeNotify);
    CHECK(Request("trainer", "sword", 2, sword));
    CHECK(!Request("trainer", "belt", 3, belt)); // one pending request per owner
    CHECK(Read("sword", value, sword) && value == 0);
    CHECK(notifications.empty()); // GUI queue never calls game code
    OnNativeTickCompleted();
    CHECK(Read("sword", value, sword) && value == 2);
    CHECK(notifications.size() == 2);
    CHECK(notifications[0].sender == uintptr_t(ninjaMemory));
    CHECK(notifications[0].name == "Ninja" && notifications[0].event == 35);
    CHECK(notifications[1].name == "NinjaSword" && notifications[1].event == 54);
    CHECK(Status("trainer").find("applied") != std::string::npos);
    const int idleReads = stratReads;
    OnNativeTickCompleted();
    CHECK(notifications.size() == 2); // consumed exactly once
    CHECK(stratReads == idleReads); // idle atomic-only path does not inspect game state
    CHECK(Request("trainer", "belt", 6, belt));
    OnNativeTickCompleted();
    CHECK(Read("belt", value, belt) && value == 6);
    CHECK(notifications.size() == 2); // no guessed belt-refresh notification
    CHECK(Read("ranks", value, ranks) && value == 0); // no invented rank synchronization
    CHECK(Request("trainer", "ranks", 63, ranks));
    OnNativeTickCompleted();
    CHECK(Read("ranks", value, ranks) && value == 63);
    CHECK(Read("belt", value, belt) && value == 6);
    CHECK(Request("trainer", "sword", 5, sword));
    signatureMatches = false;
    OnNativeTickCompleted();
    signatureMatches = true;
    CHECK(Read("sword", value, sword) && value == 2);
    CHECK(Request("trainer", "sword", 5, sword));
    profileRoot = uintptr_t(otherProfile);
    OnNativeTickCompleted();
    profileRoot = uintptr_t(profile);
    CHECK(Read("sword", value, sword) && value == 2);
    CHECK(notifications.size() == 2);
    CHECK(Request("trainer", "sword", 5, sword));
    ++handle; // same address, new object handle
    OnNativeTickCompleted();
    CHECK(Read("sword", value, sword) && value == 2);
    CHECK(Request("trainer", "sword", 5, sword));
    Release("trainer");
    OnNativeTickCompleted();
    CHECK(Read("sword", value, sword) && value == 2);
    for (int i = 0; i < 8; ++i) CHECK(Request("owner"+std::to_string(i), "ranks", i, ranks));
    CHECK(!Request("overflow", "belt", 0, belt));
    Release("owner0");
    OnNativeTickCompleted();
    CHECK(Read("ranks", value, ranks) && value == 7);
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(page);
    profileRoot = uintptr_t(page);
    uintptr_t readOnlySword = 0;
    CHECK(Read("sword", value, readOnlySword));
    DWORD old = 0;
    CHECK(VirtualProtect(page, 4096, PAGE_READONLY, &old));
    CHECK(Request("trainer", "sword", 5, readOnlySword));
    OnNativeTickCompleted();
    CHECK(Read("sword", value, readOnlySword) && value == 0);
    CHECK(notifications.size() == 2);
    CHECK(VirtualFree(page, 0, MEM_RELEASE));
    profileRoot = uintptr_t(profile);
    CHECK(Request("trainer", "sword", 5, sword));
    raiseFault = true;
    OnNativeTickCompleted();
    raiseFault = false;
    CHECK(Read("sword", value, sword) && value == 5);
    CHECK(Status("trainer").find("refresh faulted") != std::string::npos);
    CHECK(!Request("trainer", "sword", 0, sword)); // fault latch; never auto-retry
    CHECK(notifications.size() == 2);
    Shutdown();
    CHECK(!queued.load() && requests.empty() && statuses.empty());
    std::puts("PASS equipment: bounded tick queue, cdecl notifications, identity, ownership, ranges and fault latch");
}
