// Exact head ownership + production tint logic, synthetic Win32 objects only.
#include "../src/core/cosmetic_editor.cpp"
#include "cosmetic_texture_stubs.h"
namespace {
struct Model {
    alignas(4) uint8_t bytes[0x50]{}, table[0x20]{}, mesh[3][0x50]{};
    uint32_t pointers[3]{};
};
struct Actor {
    alignas(4) uint8_t bytes[0x300]{};
    Model original[2], copy[2];
    char names[3][32]{};
    uint32_t handle = 0, cls = 0;
    int count = 0;
};
Actor body, head, foreign;
bool present = true, complete = true;
int copies = 0, stores = 0;
uint32_t tableEntry = 0;
uintptr_t heap = 0x1234, package = uintptr_t(&heap), clsStorage = uintptr_t(&package);
template<class T> void Put(void* p,size_t offset,T value) {
    memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));
}
void __cdecl Tint(void*,const uint32_t*) { ++stores; }
void __cdecl ReleaseInstance(void*) { --stores; }
void* __cdecl CopyInstance(void*,void* source,int) {
    ++copies;
    for (auto* actor : {&body,&head,&foreign}) for (int slot=0;slot<2;++slot) {
        if (source!=actor->original[slot].bytes) continue;
        auto& copy=actor->copy[slot];
        memcpy(&copy,&actor->original[slot],sizeof(copy));
        Put(copy.bytes,0x34,uint32_t(0xC));
        Put(copy.bytes,0x3C,uintptr_t(copy.table));
        Put(copy.table,4,uintptr_t(copy.pointers));
        for (int i=0;i<actor->count;++i) copy.pointers[i]=uintptr_t(copy.mesh[i]);
        return copy.bytes;
    }
    return nullptr;
}
uint32_t Color(Actor& actor,int slot,int item) {
    auto instance=*reinterpret_cast<uintptr_t*>(actor.bytes+0x128+slot*4);
    auto table=*reinterpret_cast<uintptr_t*>(instance+0x3C);
    auto pointers=*reinterpret_cast<uintptr_t*>(table+4);
    auto mesh=*reinterpret_cast<uintptr_t*>(pointers+item*4);
    return *reinterpret_cast<uint32_t*>(mesh+0x14);
}
void Init(Actor& actor,uint32_t handle,bool isHead) {
    actor.handle=handle; actor.cls=0x123400+handle*4; actor.count=isHead?3:2;
    strcpy_s(actor.names[0],isHead?"head":"blue");
    strcpy_s(actor.names[1],"sash white");
    if (isHead) strcpy_s(actor.names[2],"sash knot white");
    Put(actor.bytes,0x1A8,uintptr_t(&clsStorage));
    Put(actor.bytes,0x238,body.handle);
    for (int slot=0;slot<2;++slot) {
        auto& model=actor.original[slot];
        Put(actor.bytes,0x128+slot*4,uintptr_t(model.bytes));
        Put(model.bytes,0x34,uint32_t(4));
        Put(model.bytes,0x3C,uintptr_t(model.table));
        Put(model.table,0,uint16_t(actor.count));
        Put(model.table,4,uintptr_t(model.pointers));
        Put(model.table,0x10,uintptr_t(actor.names));
        for (int i=0;i<actor.count;++i) {
            model.pointers[i]=uintptr_t(model.mesh[i]);
            Put(model.mesh[i],0x14,uint32_t((0x80+slot)*0x1000000u+0xFFFFFF));
        }
    }
}
}
namespace eth {
uintptr_t GameAddr(uintptr_t va) {
    if (va==0x458330) return uintptr_t(&Tint);
    if (va==0x454240) return uintptr_t(&CopyInstance);
    if (va==0x454150) return uintptr_t(&ReleaseInstance);
    if (va==0x4C7E70+0x184*4) return uintptr_t(&tableEntry);
    return 0;
}
bool SafeReadBytes(uintptr_t addr,void* out,size_t size) {
    const uint8_t tint[]={0x83,0xEC,0x48,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x5C};
    const uint8_t copy[]={0x55,0x8B,0x6C,0x24,0x0C,0x8B,0x45,0x34,0xA8,0x04};
    const uint8_t release[]={0x56,0x8B,0x74,0x24,0x08,0x85,0xF6,0x74,0x3D};
    if (addr==uintptr_t(&Tint) && size==sizeof(tint)) { memcpy(out,tint,size); return true; }
    if (addr==uintptr_t(&CopyInstance) && size==sizeof(copy)) { memcpy(out,copy,size); return true; }
    if (addr==uintptr_t(&ReleaseInstance) && size==sizeof(release)) { memcpy(out,release,size); return true; }
    if (!addr) return false;
    __try { memcpy(out,reinterpret_cast<void*>(addr),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
namespace trainer { bool Supported() { return true; } }
namespace game {
PlayerTracker& PlayerTracker::Get() { static PlayerTracker tracker; return tracker; }
bool PlayerTracker::Valid() const { return present; }
uintptr_t PlayerTracker::Addr() const { return manual_; }
void PlayerTracker::SetMode(PlayerMode mode) { mode_=mode; }
void PlayerTracker::Select(uintptr_t addr) { manual_=addr; manualValid_=addr!=0; }
std::vector<StratView> SnapshotStrats(size_t,bool* result) {
    if (result) *result=complete;
    if (!present) return {};
    std::vector<StratView> values;
    for (auto* actor : {&body,&head,&foreign}) {
        StratView value; value.addr=uintptr_t(actor->bytes);
        value.selfHandle=actor->handle; value.pClass=reinterpret_cast<void*>(actor->cls);
        strcpy_s(value.className,actor==&body?"Ninja":"NinjaHead");
        values.push_back(value);
    }
    return values;
}
bool ReadStratIdentity(uintptr_t addr,StratIdentity& result) {
    if (!present) return false;
    for (auto* actor : {&body,&head,&foreign}) if (addr==uintptr_t(actor->bytes)) {
        result.addr=uint32_t(addr); result.pClass=actor->cls; result.selfHandle=actor->handle;
        result.role=actor==&body?StratRole::Ninja:StratRole::Attachment;
        if (actor!=&body) {
            result.ownerFieldKnown=1;
            memcpy(&result.rawOwnerHandle,actor->bytes+0x238,4);
            result.ownerAddr=result.rawOwnerHandle==body.handle ? uint32_t(uintptr_t(body.bytes)) : 0;
        }
        return true;
    }
    return false;
}
}
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    using namespace eth::cosmetic;
    static_assert(sizeof(void*)==4);
    Init(body,0x201,false); Init(head,0x202,true); Init(foreign,0x203,true);
    Put(foreign.bytes,0x238,uint32_t(0x401)); // same low handle slot, different generation
    tableEntry=uintptr_t(&Tint);
    auto& tracker=eth::game::PlayerTracker::Get();
    tracker.SetMode(eth::game::PlayerMode::Manual); tracker.Select(uintptr_t(body.bytes));
    CHECK(Set("editor",{{"belt",0x123456,"group"},{"costume",0x654321,"group"}}));
    OnNativeTickCompleted();
    CHECK(Inspect().heads==1 && Inspect().active);
    CHECK(copies==4); // body + linked head, independently cloned double buffers
    for (int slot=0;slot<2;++slot) {
        const uint32_t alpha=(0x80+slot)*0x1000000u;
        CHECK(Color(body,slot,0)==(alpha|0x654321));
        CHECK(Color(body,slot,1)==(alpha|0x123456));
        CHECK(Color(head,slot,0)==(alpha|0xFFFFFF)); // never claim mask-only material tint
        CHECK(Color(head,slot,1)==(alpha|0x123456) && Color(head,slot,2)==(alpha|0x123456));
        CHECK(Color(foreign,slot,1)==(alpha|0xFFFFFF));
    }
    CHECK(Set("editor",{{"head",0x0A0B0C,"head"},{"belt",0x123456,"group"}}));
    OnNativeTickCompleted();
    CHECK(Color(head,0,0)==0x800A0B0C && Color(body,0,0)==0x80FFFFFF);
    Put(head.bytes,0x238,uint32_t(0x401)); // reparent away: restore surviving private entries
    OnNativeTickCompleted();
    CHECK(Inspect().heads==0 && Color(head,0,0)==0x80FFFFFF && Color(head,0,1)==0x80FFFFFF);
    Put(head.bytes,0x238,body.handle); nextHeadDiscovery=0;
    OnNativeTickCompleted(); CHECK(Inspect().heads==1 && Color(head,0,0)==0x800A0B0C);
    ++head.handle; nextHeadDiscovery=0;
    OnNativeTickCompleted(); CHECK(Inspect().heads==1); // fresh generation, no old snapshot writes
    Release("editor"); OnNativeTickCompleted();
    CHECK(!Inspect().active && Color(body,0,1)==0x80FFFFFF);
    CHECK(eth::game::HasKnownOwnerField("NinjaHead") && !eth::game::HasKnownOwnerField("NinjaSword"));
    puts("PASS head cosmetics: exact owner generation, grouped body/head sash, independent private buffers, face exclusion and safe reparent/restore");
}
