#include "../src/core/cosmetic_model_host.cpp"
#include <cstdio>
namespace {
bool pinned = true, onTick = false, installOK = true, immediate = true;
uint32_t generation = 0x202;
int captures = 0, donorSelections = 0, builtinSelections = 0, clears = 0, applied = 0;
int lastTier = -1;
bool wrongThread = false;
uintptr_t table = 0;
void __cdecl FakeBind(void*, const uint32_t*) {}
void EngineCall() { if (!onTick) wrongThread = true; }
}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID* original) { *original=(void*)&FakeBind; return MH_OK; }
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }
}
namespace eth {
uintptr_t GameAddr(uintptr_t va) {
    if (va == 0x416F20) return uintptr_t(&FakeBind);
    if (va == 0x4C7E70+0x29*4) return uintptr_t(&table);
    return va;
}
bool SafeReadBytes(uintptr_t address, void* output, size_t size) {
    if (address == uintptr_t(&table) && size == 4) { memcpy(output,&table,4); return true; }
    if (address == uintptr_t(&FakeBind)) {
        const uint8_t expected[] = {0x8B,0x44,0x24,0x08,0x8B,0x08,0x51,0x8B,0x4C,0x24,0x08,
            0xE8,0x10,0x9D,0xFF,0xFF,0xC3};
        if (size != sizeof(expected)) return false;
        memcpy(output,expected,size); return true;
    }
    if ((address == 0x10128 || address == 0x30128 || address == 0x50128) && size == 4) {
        uint32_t instance=0xABCDEF; memcpy(output,&instance,4); return true;
    }
    return false;
}
namespace trainer {
bool Supported() { return true; }
bool Read(const char* field, double& value, uintptr_t& address) {
    if (strcmp(field,"sword")) return false;
    value=3; address=0xFF00; return true;
}
}
namespace patch::runtime {
bool InstallCosmeticHooks() { EngineCall(); return installOK; }
bool InNativeRender() { return false; }
}
namespace game {
PlayerTracker& PlayerTracker::Get() { static PlayerTracker tracker; return tracker; }
bool PlayerTracker::Valid() const { return pinned; }
uintptr_t PlayerTracker::Addr() const { return manual_; }
void PlayerTracker::SetMode(PlayerMode mode) { mode_=mode; }
void PlayerTracker::Select(uintptr_t address) { manual_=address; manualValid_=address!=0; }
bool ReadStratIdentity(uintptr_t address, StratIdentity& id) {
    if (address!=0x10000 && address!=0x30000 && address!=0x50000) return false;
    id.addr=uint32_t(address); id.pClass=uint32_t(address+0x10000);
    id.selfHandle=address==0x30000 ? generation : uint32_t(address/0x10000+0x200);
    return true;
}
bool ReadStrat(uintptr_t address, StratView& view) {
    if (address!=0x10000) return false;
    view.addr=address; strcpy_s(view.className,"Ninja"); return true;
}
std::vector<StratView> SnapshotStrats(size_t, bool*) {
    EngineCall();
    std::vector<StratView> result;
    for (auto address : {0x10000u,0x30000u,0x50000u}) {
        StratView value; value.addr=address;
        StratIdentity id; ReadStratIdentity(address,id);
        value.pClass=reinterpret_cast<void*>(id.pClass); value.selfHandle=id.selfHandle;
        strcpy_s(value.className,address==0x10000 ? "Ninja" : address==0x30000 ? "NinjaSword" : "Crate");
        result.push_back(value);
    }
    return result;
}
}
namespace cosmetic::models {
bool CaptureActor(uintptr_t address, Actor& out) {
    EngineCall(); ++captures;
    game::StratIdentity id;
    if (!game::ReadStratIdentity(address,id)) return false;
    out={id.addr,id.pClass,id.selfHandle}; return true;
}
bool Select(Channel channel,const Actor& recipient,const Actor&) {
    EngineCall(); ++donorSelections;
    return channel==Channel::Player ? recipient.addr==0x10000 : recipient.addr==0x30000;
}
void Clear(Channel) { EngineCall(); }
void ClearAll() { EngineCall(); ++clears; }
FrameStats Stats() { EngineCall(); return {1,2,3,4}; }
bool SelectBuiltinSword(const Actor&,const Actor& sword,int tier) {
    EngineCall(); ++builtinSelections; lastTier=tier; return sword.addr==0x30000;
}
void ClearBuiltinSword() { EngineCall(); }
bool ApplyBuiltinSword(SwordBindFn) { EngineCall(); ++applied; return immediate; }
bool BindBuiltinSword(void*,const uint32_t*,SwordBindFn) { EngineCall(); return false; }
}
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    using namespace eth::cosmetic::model_host;
    table=uintptr_t(&FakeBind);
    auto& tracker=eth::game::PlayerTracker::Get();
    tracker.SetMode(eth::game::PlayerMode::Manual); tracker.Select(0x10000);
    const auto tick=[] { onTick=true; OnNativeTickCompleted(); onTick=false; };
    CHECK(Inspect(true).candidates.empty());
    CHECK(captures==0 && donorSelections==0);
    tick(); CHECK(Inspect(false).candidates.size()==3);
    CHECK(RequestDonor("mod",false,0,0x50000));
    CHECK(captures==0 && donorSelections==0); // GUI never invokes model/native APIs
    tick(); CHECK(donorSelections==1 && captures==2);
    CHECK(Inspect(false).substituted==2);
    CHECK(!RequestDonor("other",false,0,0x50000));
    CHECK(RequestSword("mod",0x30000,5));
    CHECK(builtinSelections==0);
    tick(); CHECK(builtinSelections==1 && lastTier==5 && applied==1);
    CHECK(!RequestSword("mod",0x50000,4)); // stop before changing recipient
    Release("other"); tick(); CHECK(clears==0);
    Release("mod"); CHECK(clears==0); tick();
    CHECK(clears==1 && lastTier==3 && applied==2); // real profile tier restored, never written
    CHECK(RequestSword("mod",0x30000,2));
    ++generation; tick(); CHECK(builtinSelections==2); // stale queued sword cancelled
    CHECK(Inspect(false).status.find("Cancelled")!=std::string::npos);
    Release("mod"); tick();
    CHECK(!RequestSword("mod",0x30000,1)); // displayed cached generation is stale
    Inspect(true); tick();
    immediate=false;
    CHECK(RequestSword("mod",0x30000,1));
    tick(); CHECK(Inspect(false).status.find("armed")!=std::string::npos);
    pinned=false; tick(); CHECK(Inspect(false).status.find("stopped")!=std::string::npos);
    CHECK(!RequestSword("mod",0x30000,3));
    CHECK(!wrongThread);
    Shutdown();
    puts("PASS cosmetic host: queue-only UI, tick capture, exact generations, ownership, tier-independent apply, real-tier restore and pin-loss stop");
}
