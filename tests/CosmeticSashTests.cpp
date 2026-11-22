// Synthetic x86 allocations only. Never loads/attaches to the installed game.
#include "../src/core/cosmetic_sash.cpp"

namespace {
alignas(4) uint8_t ninja[0x300]{}, head[0x300]{}, cloth[0x295C]{};
uint32_t registers[8]{}, registry[512][2]{};
uint32_t stratRegistry[512][2]{}, ninjaClass[2]{}, headClass[2]{};
char ninjaName[64]="Ninja", headName[64]="NinjaHead";
uint32_t nativeEntry = 0x41DB90, ninjaHandle = 0x201, headHandle = 0x202;
bool supported = true, signature = true, complete = true, present = true;
bool wrongClass = false, ambiguous = false, unreadable = false;
unsigned calls = 0, restores = 0;
unsigned snapshots = 0, identityReads = 0;
template<class T> void Put(void* p,size_t offset,T value) {
    memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));
}
uint32_t Color() {
    uint32_t result = 0; memcpy(&result,cloth+0x2898,4); return result;
}
void __fastcall NativeTint(void* object,void*,uint32_t rgb) {
    ++calls; Put(object,0x2898,rgb|0xFF000000);
}
void Init() {
    eth::cosmetic::sash::Invalidate();
    memset(ninja,0,sizeof(ninja)); memset(head,0,sizeof(head));
    memset(cloth,0,sizeof(cloth)); memset(registry,0,sizeof(registry));
    memset(stratRegistry,0,sizeof(stratRegistry));
    strcpy_s(ninjaName,"Ninja"); strcpy_s(headName,"NinjaHead");
    ninjaClass[1]=uint32_t(uintptr_t(ninjaName));
    headClass[1]=uint32_t(uintptr_t(headName));
    ninjaHandle=0x201; headHandle=0x202; registers[2]=0x203;
    supported=signature=complete=present=true;
    wrongClass=ambiguous=unreadable=false; calls=restores=snapshots=identityReads=0;
    Put(ninja,0x1A0,ninjaHandle); Put(head,0x1A0,headHandle);
    Put(ninja,0x1A8,uint32_t(uintptr_t(ninjaClass)));
    Put(head,0x1A8,uint32_t(uintptr_t(headClass)));
    stratRegistry[1][0]=ninjaHandle; stratRegistry[1][1]=uint32_t(uintptr_t(ninja));
    stratRegistry[2][0]=headHandle; stratRegistry[2][1]=uint32_t(uintptr_t(head));
    Put(head,0x238,ninjaHandle);
    Put(head,0x1B4,uint32_t(uintptr_t(registers)));
    Put(cloth,0,uint32_t(0x4C8CF8));
    Put(cloth,0x0C,uint32_t(uintptr_t(head)));
    Put(cloth,0x10,uint32_t(0x203));
    Put(cloth,0x2898,uint32_t(0x7FC8C8C8));
    registry[3][0]=0x203; registry[3][1]=uint32_t(uintptr_t(cloth));
}
eth::game::StratIdentity Ninja() {
    eth::game::StratIdentity result{};
    eth::game::ReadStratIdentity(uintptr_t(ninja),result);
    return result;
}
}
namespace eth {
uintptr_t GameAddr(uintptr_t va) {
    if (va==0x45A900) return uintptr_t(&NativeTint);
    if (va==0x4C7E70+0x1D8*4) return uintptr_t(&nativeEntry);
    if (va==0x519A60) return uintptr_t(registry);
    if (va==0x51019C) return uintptr_t(stratRegistry);
    return va;
}
bool SafeReadBytes(uintptr_t addr,void* out,size_t size) {
    const uint8_t expected[] = {
        0x8B,0x44,0x24,0x04,0x0D,0,0,0,0xFF,
        0x89,0x81,0x98,0x28,0,0,0xC2,0x04,0
    };
    if (addr==uintptr_t(&NativeTint) && size==sizeof(expected)) {
        memcpy(out,expected,size); if (!signature) static_cast<uint8_t*>(out)[0]=0;
        return true;
    }
    if (unreadable && addr==uintptr_t(registers)+8) return false;
    __try { memcpy(out,reinterpret_cast<void*>(addr),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SafeWriteBytes(uintptr_t addr,const void* data,size_t size) {
    if (addr!=uintptr_t(cloth)+0x2898 || size!=4) return false;
    ++restores; memcpy(reinterpret_cast<void*>(addr),data,size); return true;
}
namespace trainer { bool Supported() { return supported; } }
namespace game {
bool ReadStratIdentity(uintptr_t addr,StratIdentity& out) {
    ++identityReads;
    if (!present || (addr!=uintptr_t(ninja) && addr!=uintptr_t(head))) return false;
    out={}; out.addr=uint32_t(addr);
    out.pClass=uint32_t(addr==uintptr_t(ninja)?uintptr_t(ninjaClass):uintptr_t(headClass));
    out.selfHandle=addr==uintptr_t(ninja)?ninjaHandle:headHandle;
    out.role=addr==uintptr_t(ninja)?StratRole::Ninja:StratRole::Attachment;
    if (addr==uintptr_t(head)) {
        out.ownerFieldKnown=1;
        memcpy(&out.rawOwnerHandle,head+0x238,4);
        if (out.rawOwnerHandle==ninjaHandle) out.ownerAddr=uint32_t(uintptr_t(ninja));
    }
    return true;
}
std::vector<StratView> SnapshotStrats(size_t,bool* result) {
    ++snapshots;
    *result=complete;
    if (!present) return {};
    std::vector<StratView> rows;
    for (auto* bytes : {ninja,head}) {
        StratIdentity id{}; ReadStratIdentity(uintptr_t(bytes),id);
        StratView row{}; row.addr=id.addr; row.selfHandle=id.selfHandle;
        row.pClass=reinterpret_cast<void*>(id.pClass);
        strcpy_s(row.className,bytes==ninja?"Ninja":wrongClass?"NotNinjaHead":"NinjaHead");
        rows.push_back(row);
    }
    if (ambiguous) rows.push_back(rows.back());
    return rows;
}
}
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main() {
    static_assert(sizeof(void*)==4);
    using namespace eth::cosmetic::sash;
    Init(); auto identity=Ninja();
    CHECK(Apply(identity,0x123456)==Result::Applied);
    CHECK(Color()==0xFF123456 && calls==1);
    CHECK(Apply(identity,0x123456)==Result::Applied && calls==1);
    CHECK(Apply(identity,0x654321)==Result::Applied && calls==2);
    Restore(); CHECK(Color()==0x7FC8C8C8 && restores==0);
    Restore(); CHECK(restores==0); // restoration must never use SafeWriteBytes
    // Native tier/event refresh replaces the restoration baseline.
    CHECK(Apply(identity,0x123456)==Result::Applied);
    Put(cloth,0x2898,uint32_t(0xFF147882));
    CHECK(Apply(identity,0x123456)==Result::Applied);
    Restore(); CHECK(Color()==0xFF147882);
    CHECK(Apply(identity,0x123456)==Result::Applied);
    Put(cloth,0x2898,uint32_t(0xFFAABBCC));
    Restore(); CHECK(Color()==0xFFAABBCC); // never clobber another writer
    for (int failure=0;failure<12;++failure) {
        Init(); identity=Ninja();
        switch (failure) {
        case 0: registry[3][0]=0x403; break; // same slot, stale effects generation
        case 1: Put(cloth,0x10,uint32_t(0x403)); break;
        case 2: Put(cloth,0x0C,uint32_t(uintptr_t(ninja))); break;
        case 3: Put(cloth,0,uint32_t(0x4C7E44)); break;
        case 4: Put(head,0x238,uint32_t(0x401)); break; // stale owner generation
        case 5: wrongClass=true; break;
        case 6: complete=false; break;
        case 7: ambiguous=true; break;
        case 8: unreadable=true; break;
        case 9: registers[2]=0; break;
        case 10: ++ninjaHandle; break;
        case 11: present=false; break;
        }
        CHECK(Apply(identity,0x123456)==Result::Waiting);
        CHECK(calls==0 && Color()==0x7FC8C8C8);
    }
    for (int failure=0;failure<5;++failure) {
        Init(); identity=Ninja(); CHECK(Apply(identity,0x123456)==Result::Applied);
        switch (failure) {
        case 0: ++ninjaHandle; break;
        case 1: ++headHandle; break;
        case 2: registry[3][0]=0x403; break;
        case 3: Put(head,0x238,uint32_t(0x401)); break;
        case 4: Invalidate(); break; // session boundary, even address reuse
        }
        Restore(); CHECK(restores==0 && Color()==0xFF123456);
    }
    Init(); identity=Ninja(); supported=false;
    CHECK(Apply(identity,0)==Result::Refused && calls==0);
    supported=true; signature=false;
    CHECK(Apply(identity,0)==Result::Refused && calls==0);
    signature=true;
    CHECK(Apply(identity,0x1000000)==Result::Refused && calls==0);
    // Hot path uses fresh caller membership evidence, then independently
    // rechecks every identity using direct bounded SEH reads.
    Init(); identity=Ninja();
    eth::game::StratIdentity headIdentity{};
    CHECK(eth::game::ReadStratIdentity(uintptr_t(head),headIdentity));
    std::vector<eth::game::StratIdentity> heads{headIdentity};
    snapshots=identityReads=0;
    CHECK(ApplyKnown(identity,0x123456,heads)==Result::Applied);
    CHECK(Color()==0xFF123456 && calls==1);
    CHECK(ApplyKnown(identity,0x123456,heads)==Result::Applied && calls==1);
    CHECK(ApplyKnown(identity,0x654321,heads)==Result::Applied && calls==2);
    RestoreKnown(identity,heads);
    CHECK(Color()==0x7FC8C8C8 && restores==0 && snapshots==0 && identityReads==0);
    CHECK(ApplyKnown(identity,0x123456,heads)==Result::Applied);
    Put(cloth,0x2898,uint32_t(0xFF147882));
    CHECK(ApplyKnown(identity,0x123456,heads)==Result::Applied);
    RestoreKnown(identity,heads); CHECK(Color()==0xFF147882);
    CHECK(ApplyKnown(identity,0x123456,heads)==Result::Applied);
    Put(cloth,0x2898,uint32_t(0xFFAABBCC));
    RestoreKnown(identity,heads); CHECK(Color()==0xFFAABBCC);
    CHECK(snapshots==0 && identityReads==0 && restores==0);
    for (int failure=0;failure<19;++failure) {
        Init(); identity=Ninja();
        CHECK(eth::game::ReadStratIdentity(uintptr_t(head),headIdentity));
        heads={headIdentity};
        switch (failure) {
        case 0: stratRegistry[1][0]=0x401; break;
        case 1: stratRegistry[2][0]=0x402; break;
        case 2: stratRegistry[2][1]=uint32_t(uintptr_t(ninja)); break;
        case 3: Put(ninja,0x1A0,uint32_t(0x401)); break;
        case 4: Put(head,0x1A0,uint32_t(0x402)); break;
        case 5: Put(head,0x1A8,uint32_t(uintptr_t(ninjaClass))); break;
        case 6: strcpy_s(ninjaName,"NinjaSword"); break;
        case 7: strcpy_s(headName,"Ninja"); break;
        case 8: heads[0].role=eth::game::StratRole::Other; break;
        case 9: heads[0].rawOwnerHandle=0x401; break;
        case 10: Put(head,0x238,uint32_t(0x401)); break;
        case 11: registry[3][0]=0x403; break;
        case 12: Put(cloth,0x0C,uint32_t(uintptr_t(ninja))); break;
        case 13: Put(cloth,0,uint32_t(0x4C7E44)); break;
        case 14: Put(cloth,0x10,uint32_t(0x403)); break;
        case 15: Put(head,0x1B4,uint32_t(1)); break; // real guarded SEH failure
        case 16: heads.push_back(headIdentity); break;
        case 17: heads.clear(); break;
        case 18: heads[0].ownerAddr=uint32_t(uintptr_t(head)); break;
        }
        snapshots=identityReads=0;
        CHECK(ApplyKnown(identity,0x123456,heads)==Result::Waiting);
        RestoreKnown(identity,heads);
        CHECK(calls==0 && Color()==0x7FC8C8C8 && restores==0);
        CHECK(snapshots==0 && identityReads==0);
    }
    for (int failure=0;failure<6;++failure) {
        Init(); identity=Ninja();
        CHECK(eth::game::ReadStratIdentity(uintptr_t(head),headIdentity));
        heads={headIdentity}; snapshots=identityReads=0;
        CHECK(ApplyKnown(identity,0x123456,heads)==Result::Applied);
        switch (failure) {
        case 0: stratRegistry[2][0]=0x402; break;
        case 1: Put(head,0x238,uint32_t(0x401)); break;
        case 2: registry[3][0]=0x403; break;
        case 3: heads.clear(); break;
        case 4: Invalidate(); break;
        case 5: ++identity.selfHandle; break;
        }
        RestoreKnown(identity,heads);
        CHECK(Color()==0xFF123456 && restores==0);
        CHECK(snapshots==0 && identityReads==0);
    }
    // A changed root must not secretly invoke the legacy snapshot restore.
    Init(); identity=Ninja();
    CHECK(eth::game::ReadStratIdentity(uintptr_t(head),headIdentity));
    heads={headIdentity}; snapshots=identityReads=0;
    CHECK(ApplyKnown(identity,0x123456,heads)==Result::Applied);
    ++identity.selfHandle;
    CHECK(ApplyKnown(identity,0x654321,heads)==Result::Waiting);
    Restore();
    CHECK(Color()==0xFF123456 && snapshots==0 && identityReads==0 && restores==0);
    puts("PASS wavy sash: exact head/effect ownership, generation, reversible native tint, refresh and teardown");
}
