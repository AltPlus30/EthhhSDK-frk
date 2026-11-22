// Synthetic render-instance data. Never load the game or installed DLL.
#include <cstdlib>
#include <new>
#include <chrono>
size_t editorAllocations=0;
void* operator new(size_t n) {
    ++editorAllocations;
    if (void* p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
void operator delete[](void* p,size_t) noexcept { std::free(p); }
#include "../src/core/cosmetic_editor.cpp"
#include "cosmetic_texture_stubs.h"
namespace {
alignas(4) uint8_t actor[0x300]{}, model[2][0x50]{}, lookup[2][0x20]{}, mesh[2][2][0x50]{};
alignas(4) uint8_t privateModel[2][0x50]{}, privateLookup[2][0x20]{}, privateMesh[2][2][0x50]{};
char names[2][32] = {"sash white", "blue"};
uint32_t pointers[2][2]{};
uint32_t privatePointers[2][2]{};
bool valid = true, supported = true, signature = true, prepareFault = false;
bool trackerAvailable = true, discover = false, ambiguous = false, discoveryComplete = true;
uint32_t handle = 0x201;
uint32_t registry[1024]{};
int copies = 0, reads = 0;
volatile int tintCalls = 0, releaseCalls = 0;
uintptr_t tableEntry = 0;
uintptr_t heap = 0x1234, package = uintptr_t(&heap), cls = uintptr_t(&package);
template<class T> void Put(void* base, size_t offset, T value) {
    memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(value));
}
uint32_t Color(int slot, int entry) {
    const auto instance = *reinterpret_cast<uintptr_t*>(actor + 0x128 + slot*4);
    const auto table = *reinterpret_cast<uintptr_t*>(instance + 0x3C);
    const auto array = *reinterpret_cast<uintptr_t*>(table + 4);
    const auto address = *reinterpret_cast<uintptr_t*>(array + entry*4);
    return *reinterpret_cast<uint32_t*>(address + 0x14);
}
void PutColor(int slot, int entry, uint32_t color) {
    const auto instance = *reinterpret_cast<uintptr_t*>(actor + 0x128 + slot*4);
    const auto table = *reinterpret_cast<uintptr_t*>(instance + 0x3C);
    const auto array = *reinterpret_cast<uintptr_t*>(table + 4);
    const auto address = *reinterpret_cast<uintptr_t*>(array + entry*4);
    *reinterpret_cast<uint32_t*>(address + 0x14) = color;
}
void __cdecl FakeTint(void*, const uint32_t*) { ++tintCalls; }
void* __cdecl FakeCopy(void*, void* source, int force) {
    ++copies;
    if (prepareFault) RaiseException(0xE0123456, 0, 0, nullptr);
    if (force != 1) return nullptr;
    const int s = source == model[0] ? 0 : 1;
    memcpy(privateModel[s], source, sizeof(model[s]));
    memcpy(privateLookup[s], lookup[s], sizeof(lookup[s]));
    memcpy(privateMesh[s], mesh[s], sizeof(mesh[s]));
    Put(privateModel[s], 0x34, uint32_t(0xC));
    Put(privateModel[s], 0x3C, uintptr_t(privateLookup[s]));
    Put(privateLookup[s], 4, uintptr_t(privatePointers[s]));
    for (int e = 0; e < 2; ++e) privatePointers[s][e] = uintptr_t(privateMesh[s][e]);
    if (*reinterpret_cast<uintptr_t*>(model[s])==uintptr_t(mesh[s][0])) {
        Put(privateModel[s],0,uintptr_t(privateMesh[s][0]));
        Put(privateMesh[s][0],0x44,uintptr_t(privateMesh[s][1]));
    }
    return privateModel[s];
}
void __cdecl FakeRelease(void*) { ++releaseCalls; }
void Reset() {
    eth::cosmetic::Shutdown();
    valid = true; supported = true; signature = true; prepareFault = false;
    trackerAvailable=true; discover=false; ambiguous=false; discoveryComplete=true;
    handle = 0x201; copies = 0;
    Put(actor, 0x1A8, uintptr_t(&cls));
    for (int s = 0; s < 2; ++s) {
        Put(actor, 0x128+s*4, uintptr_t(model[s]));
        Put(model[s], 0x34, uint32_t(4)); // initially shared
        Put(model[s], 0x3C, uintptr_t(lookup[s]));
        Put(lookup[s], 0, uint16_t(2));
        Put(lookup[s], 4, uintptr_t(pointers[s]));
        Put(lookup[s], 0x10, uintptr_t(names));
        for (int e = 0; e < 2; ++e) {
            pointers[s][e] = uintptr_t(mesh[s][e]);
            Put(mesh[s][e], 0x14, uint32_t((0x80+s)*0x1000000u + 0x102030u + e));
        }
    }
}
}
namespace eth {
uintptr_t GameAddr(uintptr_t va) {
    if (va == 0x458330) return uintptr_t(&FakeTint);
    if (va == 0x454240) return uintptr_t(&FakeCopy);
    if (va == 0x454150) return uintptr_t(&FakeRelease);
    if (va == 0x4C7E70+0x184*4) return uintptr_t(&tableEntry);
    if (va == 0x51019C) return uintptr_t(registry);
    return 0;
}
bool SafeReadBytes(uintptr_t addr, void* out, size_t size) {
    ++reads;
    if (addr == uintptr_t(&FakeCopy)) {
        const uint8_t bytes[] = {0x55,0x8B,0x6C,0x24,0x0C,0x8B,0x45,0x34,0xA8,0x04};
        if (size != sizeof(bytes)) return false;
        memcpy(out, bytes, size); return true;
    }
    if (addr == uintptr_t(&FakeRelease)) {
        const uint8_t bytes[] = {0x56,0x8B,0x74,0x24,0x08,0x85,0xF6,0x74,0x3D};
        if (size != sizeof(bytes)) return false;
        memcpy(out, bytes, size); return true;
    }
    if (addr == uintptr_t(&FakeTint)) {
        const uint8_t bytes[] = {0x83,0xEC,0x48,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x5C};
        if (size != sizeof(bytes)) return false;
        memcpy(out, bytes, size);
        if (!signature) static_cast<uint8_t*>(out)[0] ^= 1;
        return true;
    }
    if (!addr) return false;
    __try { memcpy(out, reinterpret_cast<void*>(addr), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
namespace trainer { bool Supported() { return supported; } }
namespace game {
std::vector<StratView> SnapshotStrats(size_t, bool* complete) {
    if (complete) *complete = discoveryComplete;
    if (!discover || !valid) return {};
    StratView strat; strat.addr=uintptr_t(actor); strat.selfHandle=handle;
    strat.pClass=reinterpret_cast<void*>(0x123400); strcpy_s(strat.className,"Ninja");
    std::vector<StratView> result{strat};
    if (ambiguous) { strat.addr+=0x10000; ++strat.selfHandle; result.push_back(strat); }
    return result;
}
PlayerTracker& PlayerTracker::Get() { static PlayerTracker tracker; return tracker; }
bool PlayerTracker::Valid() const { return valid && trackerAvailable; }
uintptr_t PlayerTracker::Addr() const { return manual_; }
void PlayerTracker::SetMode(PlayerMode mode) { mode_ = mode; }
void PlayerTracker::Select(uintptr_t address) { manual_ = address; manualValid_ = address != 0; }
bool ReadStratIdentity(uintptr_t address, StratIdentity& result) {
    if (!valid || address != uintptr_t(actor)) return false;
    result.addr = address; result.pClass = 0x123400; result.selfHandle = handle;
    result.role = StratRole::Ninja;
    return true;
}
}
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    using namespace eth::cosmetic;
    static_assert(sizeof(void*) == 4);
    CHECK(TextureRgb(0x0000FF)==0xFF0000 && TextureRgb(0xFF0000)==0x0000FF);
    tableEntry = uintptr_t(&FakeTint);
    auto& tracker = eth::game::PlayerTracker::Get();
    tracker.SetMode(eth::game::PlayerMode::Manual);
    tracker.Select(uintptr_t(actor));
    Reset();
    const int idle = reads;
    OnNativeTickCompleted();
    CHECK(reads == idle);
    CHECK(view.lastTickUs==0 && view.maxTickUs==0 && view.lastFullTickUs==0); // gated idle is not sampled
    Inspect(); OnNativeTickCompleted();
    CHECK(view.lastTickUs>=0 && view.maxTickUs>=view.lastTickUs && view.lastFullTickUs==view.lastTickUs);
    const double idleLastUs=view.lastTickUs, idleMaxUs=view.maxTickUs;
    nextInspection=GetTickCount64()+100000;
    const auto idleInspections=Inspect().fullInspections;
    const int idleReads=reads;
    for (unsigned i=0;i<1000;++i) { Inspect(); OnNativeTickCompleted(); }
    CHECK(reads==idleReads && Inspect().fullInspections==idleInspections);
    CHECK(view.lastTickUs==idleLastUs && view.maxTickUs==idleMaxUs); // cadence gate is not sampled
    CHECK(!Set("", {{"blue", 0xFFFFFF}}));
    CHECK(!Set("a", {{"blue", 0x1000000}}));
    CHECK(!Set("a", {{std::string(32, 'a'), 1}}));
    CHECK(!Set("a", {{"blue",1,"enemy"}}));
    supported = false; CHECK(!Set("a", {{"blue", 1}})); supported = true;
    signature = false; CHECK(!Set("a", {{"blue", 1}})); signature = true;
    valid = false; CHECK(!Set("a", {{"blue", 1}})); valid = true;
    CHECK(Set("a", {{"sash white", 0xFF00AA}}));
    CHECK(!Set("b", {{"blue", 1}})); // exclusive ownership
    CHECK(copies == 0 && Color(0, 0) == 0x80102030); // queue only
    OnNativeTickCompleted();
    CHECK(copies == 2);
    CHECK(Color(0, 0) == 0x80FF00AA && Color(1, 0) == 0x81FF00AA);
    CHECK(*reinterpret_cast<uint32_t*>(mesh[0][0] + 0x14) == 0x80102030); // shared/NPC source untouched
    CHECK(Color(0, 1) == 0x80102031);
    CHECK(Inspect().active && Inspect().materials.size() == 2);
    OnNativeTickCompleted(); CHECK(copies == 2); // never clone every tick
    Release("b"); OnNativeTickCompleted(); CHECK(Inspect().active);
    // Script color refresh becomes the new restoration baseline; alpha survives.
    PutColor(0, 0, 0x42123456);
    OnNativeTickCompleted();
    CHECK(Color(0, 0) == 0x42FF00AA);
    CHECK(Set("a", {{"blue", 0xABCDEF}}));
    OnNativeTickCompleted();
    CHECK(Color(0, 0) == 0x42123456); // removed draft restored individually
    CHECK(Color(0, 1) == 0x80ABCDEF);
    Release("a");
    CHECK(Color(0, 1) == 0x80ABCDEF); // release never stores on GUI thread
    OnNativeTickCompleted();
    CHECK(Color(0, 1) == 0x80102031);
    CHECK(!Inspect().active);
    Reset();
    // Native refresh after our last tick must win over restoration.
    CHECK(Set("a", {{"blue", 0x123456}}));
    OnNativeTickCompleted();
    PutColor(0, 1, 0x42111111);
    Release("a");
    OnNativeTickCompleted();
    CHECK(Color(0, 1) == 0x42111111);
    CHECK(Color(1, 1) == 0x81102031);
    Reset();
    CHECK(Set("a", {{"blue", 0x112233}}));
    OnNativeTickCompleted();
    ++handle; // same address, new actor: retire old snapshots, reapply SESSION palette
    for (int s=0;s<2;++s) {
        Put(actor,0x128+s*4,uintptr_t(model[s]));
        Put(model[s],0x34,uint32_t(4));
    }
    OnNativeTickCompleted();
    CHECK(Inspect().active && Color(0, 1) == 0x80112233 && copies == 4);
    valid=false; OnNativeTickCompleted();
    CHECK(!Inspect().active && Inspect().waiting);
    valid=true; OnNativeTickCompleted();
    CHECK(Inspect().active);
    Release("a"); OnNativeTickCompleted();
    CHECK(!Inspect().active && !Inspect().waiting);
    Reset();
    CHECK(Set("a", {{"belt",0x445566,"group"}}));
    OnNativeTickCompleted();
    CHECK(Color(0,0)==0x80445566 && Color(0,1)==0x80102031);
    trackerAvailable=false; discover=true; nextDiscovery=0;
    OnNativeTickCompleted();
    CHECK(Inspect().active && !tracker.Valid()); // cosmetic reacquire never repins movement
    ambiguous=true; nextDiscovery=0;
    OnNativeTickCompleted();
    CHECK(!Inspect().active && Inspect().waiting && Color(0,0)==0x80102030);
    ambiguous=false; discoveryComplete=false; nextDiscovery=0;
    OnNativeTickCompleted();
    CHECK(Inspect().waiting);
    discoveryComplete=true; nextDiscovery=0;
    OnNativeTickCompleted();
    CHECK(Inspect().active && Color(0,0)==0x80445566);
    Release("a"); OnNativeTickCompleted();
    CHECK(!Inspect().active && Color(0,0)==0x80102030);
    Reset();
    CHECK(Set("a", {{"absent material", 0xFFFFFF}}));
    OnNativeTickCompleted();
    CHECK(copies == 0 && !Inspect().active);
    Release("a"); OnNativeTickCompleted();
    Reset();
    // Private does not imply writable. Refuse an actual read-only data page.
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(page);
    Put(page, 0x14, uint32_t(0xFFAABBCC));
    pointers[0][0] = uintptr_t(page);
    Put(model[0], 0x34, uint32_t(0xC));
    Put(model[1], 0x34, uint32_t(0xC));
    DWORD oldProtection = 0;
    CHECK(VirtualProtect(page, 4096, PAGE_READONLY, &oldProtection));
    CHECK(Set("a", {{"sash white", 0x112233}}));
    OnNativeTickCompleted();
    CHECK(Color(0, 0) == 0xFFAABBCC);
    Release("a"); OnNativeTickCompleted();
    CHECK(VirtualFree(page, 0, MEM_RELEASE));
    Reset();
    uint32_t blueTexture[8]={0,256,256,0,0,0,0,0};
    for (int s=0;s<2;++s) {
        Put(model[s],0,uintptr_t(mesh[s][0]));
        Put(mesh[s][0],0x44,uintptr_t(mesh[s][1]));
        Put(mesh[s][1],0x10,uintptr_t(blueTexture));
    }
    CHECK(Set("a",{{"costume",0x008000,"group"}}));
    OnNativeTickCompleted();
    texture_fixture::allowLoad=texture_fixture::allowPass=true;
    std::string error;
    CHECK(LoadTextures("a","mod",{"a.tga","b.tga","c.tga"},error));
    CHECK(!LoadTextures("b","mod",{"a.tga","b.tga","c.tga"},error));
    CHECK(texture_fixture::installs==0); // UI pack load never installs native hooks
    OnNativeTickCompleted(); OnNativeTickCompleted();
    CHECK(texture_fixture::installs==1 && Inspect().textures);
    Put(actor,0x1A0,handle);
    Put(actor,0x1A8,uint32_t(0x123400));
    registry[(handle&0x1FF)*2]=handle;
    registry[(handle&0x1FF)*2+1]=uintptr_t(actor);
    unsigned nativeDraws=0;
    const auto draw=+[](void* context) { ++*static_cast<unsigned*>(context); };
    CHECK(DrawTextures(draw,&nativeDraws) && nativeDraws==1);
    const int warmReads=reads;
    for (int i=0;i<1000;++i) CHECK(DrawTextures(draw,&nativeDraws));
    CHECK(reads==warmReads && nativeDraws==1001); // no SafeRead/list walk at draw time
    PutColor(0,1,0x80102031); // native script refresh on the cheap tick path
    nextInspection=GetTickCount64()+100000;
    BeforeNativeTick(); OnNativeTickCompleted();
    CHECK(Color(0,1)==0x80008000 && DrawTextures(draw,&nativeDraws));
    // Freeze optional inspection deadline: GUI spam is not a native command.
    nextInspection=GetTickCount64()+100000;
    const auto inspections=Inspect().fullInspections;
    const int tickReads=reads;
    const auto tickAllocations=editorAllocations;
    const auto tickBegin=std::chrono::steady_clock::now();
    double previousMaxUs=view.maxTickUs;
    const double previousFullUs=view.lastFullTickUs;
    for (unsigned i=0;i<1000;++i) {
        BeforeNativeTick();
        auto ui=Inspect();
        OnNativeTickCompleted();
        CHECK(view.lastTickUs>=0 && view.maxTickUs>=view.lastTickUs && view.maxTickUs>=previousMaxUs);
        previousMaxUs=view.maxTickUs;
        CHECK(DrawTextures(draw,&nativeDraws));
    }
    const auto tickEnd=std::chrono::steady_clock::now();
    const auto tickAllocationDelta=editorAllocations-tickAllocations;
    CHECK(Inspect().fullInspections==inspections && Inspect().fastTicks>=1000);
    CHECK(reads==tickReads); // includes pre/post tick, GUI polling and draw
    CHECK(view.lastFullTickUs==previousFullUs); // fast samples do not replace the last full sample
    printf("Synthetic editor 1000 ticks + Inspect + draw: %.3f ms, SafeRead=0, "
        "allocations=%zu (View copies + texture stub), full inspections=0\n",
        std::chrono::duration<double,std::milli>(tickEnd-tickBegin).count(),
        tickAllocationDelta);
    const auto fullReads=reads;
    const auto fullAllocations=editorAllocations;
    const auto fullBegin=std::chrono::steady_clock::now();
    for (unsigned i=0;i<1000;++i) {
        nextInspection=0; // former editor rebuilt inspection every tick
        BeforeNativeTick();
        auto ui=Inspect();
        OnNativeTickCompleted();
        CHECK(view.lastFullTickUs==view.lastTickUs && view.maxTickUs>=view.lastTickUs &&
            view.maxTickUs>=previousMaxUs);
        previousMaxUs=view.maxTickUs;
        CHECK(DrawTextures(draw,&nativeDraws));
    }
    const auto fullEnd=std::chrono::steady_clock::now();
    printf("Synthetic forced-full editor 1000 ticks + Inspect + draw: %.3f ms, SafeRead=%d, "
        "allocations=%zu\n",
        std::chrono::duration<double,std::milli>(fullEnd-fullBegin).count(),
        reads-fullReads,editorAllocations-fullAllocations);
    nextInspection=GetTickCount64()+100000;
    const auto commandInspections=Inspect().fullInspections;
    CHECK(Set("a",{{"costume",0x008001,"group"}}));
    OnNativeTickCompleted(); // commands ignore the optional deadline
    CHECK(Color(0,1)==0x80008001 && Inspect().fullInspections==commandInspections+1);
    CHECK(Set("a",{{"costume",0x008000,"group"}}));
    OnNativeTickCompleted();
    CHECK(Color(0,1)==0x80008000);
    const auto drawsAfterTicks=nativeDraws;
    Put(actor,0x1A0,handle+0x200);
    CHECK(!DrawTextures(draw,&nativeDraws));
    Put(actor,0x1A0,handle);
    CHECK(texture_fixture::observed.size()==2);
    CHECK(texture_fixture::observed[0].rgb==0xFFFFFF); // no double multiplication
    CHECK(Color(0,1)==0x80008000);
    BeforeNativeTick();
    CHECK(!DrawTextures(draw,&nativeDraws) && nativeDraws==drawsAfterTicks);
    OnNativeTickCompleted();
    CHECK(DrawTextures(draw,&nativeDraws)); // no permanent invalidation gap
    const auto drawsBeforeStop=nativeDraws;
    ClearTextures("a");
    CHECK(!DrawTextures(draw,&nativeDraws) && nativeDraws==drawsBeforeStop);
    CHECK(!Inspect().textures && Inspect().active); // texture stop retains tint
    Release("a"); OnNativeTickCompleted();
    CHECK(Color(0,1)==0x80102031);
    Reset();
    texture_fixture::allowLoad=true; texture_fixture::installOK=false;
    CHECK(LoadTextures("a","mod",{"a.tga","b.tga","c.tga"},error));
    OnNativeTickCompleted(); OnNativeTickCompleted();
    CHECK(texture_fixture::installs==1 && copies==0 && !Inspect().textures);
    texture_fixture::installOK=true;
    CHECK(LoadTextures("a","mod",{"a.tga","b.tga","c.tga"},error));
    OnNativeTickCompleted();
    CHECK(texture_fixture::installs==2 && Inspect().textures);
    Release("a"); OnNativeTickCompleted();
    Reset();
    CHECK(Set("a",{{"blue",0xABCDEF}}));
    OnNativeTickCompleted();
    Put(actor,0x1A0,handle);
    Put(actor,0x1A8,uint32_t(0x123400));
    registry[(handle&0x1FF)*2]=handle;
    registry[(handle&0x1FF)*2+1]=uintptr_t(actor);
    nextInspection=GetTickCount64()+100000;
    PutColor(0,1,0x42112233);
    const auto beforeRefresh=Inspect().fastTicks;
    BeforeNativeTick(); OnNativeTickCompleted();
    CHECK(Inspect().fastTicks==beforeRefresh+1 && Color(0,1)==0x42ABCDEF);
    Release("a"); OnNativeTickCompleted();
    CHECK(Color(0,1)==0x42112233 && !Inspect().active); // latest native baseline, immediate Stop
    Reset();
    // Invalid primary instance must not reach the native copy helper.
    Put(actor, 0x128, uintptr_t(0));
    CHECK(Set("a", {{"blue", 1}}));
    OnNativeTickCompleted();
    CHECK(copies == 0);
    Shutdown();
    puts("PASS cosmetic tint: tick-only stores, COW gating, both instances, RGB/alpha, restoration, native refresh and actor generation");
}
