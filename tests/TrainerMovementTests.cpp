// Standalone Win32/MSVC synthetic fixtures; includes the actual production logic.
// No target process, game launch, installed-file writes, or native engine calls.
#include "../src/core/trainer_movement.cpp"
#include <cstdio>
#include <limits>
#include <type_traits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
namespace {
alignas(4) uint8_t code[0xD320]{}, strat[0x300]{};
uint32_t cls[12]{};
char name[] = "Ninja";
void Put(size_t offset, uint32_t word) { std::memcpy(code+offset, &word, 4); }
uint32_t Get(size_t offset) { uint32_t word; std::memcpy(&word, code+offset, 4); return word; }
float Value(size_t offset) { float value; std::memcpy(&value, code+offset, 4); return value; }
void Fixture()
{
    std::memset(code, 0, sizeof(code));
    Put(0, 0x20); Put(4, 0x34C8);
    // Independent artifact-derived op/literal pairs at package-relative sites.
    const uint32_t pairs[][3] = {
        {0x3030,0x5A750000,0x3DEB851F},{0x3038,0x5A740000,0x3F30A3D7},
        {0x30A0,0x5A750000,0x3C8D4FDF},{0x30A8,0x5A740000,0x3D0D4FDF},
        {0x3100,0x5A750000,0x3DEB851F},{0x3108,0x5A740000,0x3E6B851F},
        {0x3134,0x5A750000,0x3B9D4952},{0x313C,0x5A740000,0x3B6BEDFA},
        {0x3170,0x5A750000,0x3DC49BA6},{0x3178,0x5A740000,0x3D9374BC},
        {0x319C,0x5A750000,0x3CC49BA6},{0x31A4,0x5A740000,0x3C9374BC},
        {0x2300,0x5A740000,0x3E428F5C},{0x2314,0x5A740000,0x3E4CCCCD},
        {0x2320,0x5A740000,0x3E6147AE},
    };
    for (const auto& p : pairs) { Put(p[0]-4,p[1]); Put(p[0],p[2]); }
    Put(0x2BB4,0x0ACF0075); Put(0x2BB8,0x0DCE5CCF); Put(0x2BBC,0x0B0BCDCE);
    Put(0x2C88,0x0A0B0075);
    Put(0x3454,0x0B0A0AE9); Put(0x3458,0x0B0B0BEA); Put(0x345C,0x0B0C0CEB);
    Put(0x2338,0x5C797A00); Put(0x233C,0x3E30A3D7); Put(0x2340,0x35787902);
    Put(0x2344,0x5A790000); Put(0x2348,0x3DA3D70A);
    Put(0x2354,0x0C747477); Put(0x2358,0x427810A8); Put(0x235C,0x0B767478);
    Put(0x2360,0x5B757600); Put(0x2364,0xBDCCCCCB); Put(0x2368,0x88000000);
    Put(0x2DD8,0x0C797475); Put(0x2DDC,0x0D787679); Put(0x2DE0,0x0B777875);
    cls[1]=reinterpret_cast<uintptr_t>(name);
    cls[3]=reinterpret_cast<uintptr_t>(code); cls[2]=cls[3]+0x47D0;
    const uint32_t pointer=reinterpret_cast<uintptr_t>(cls);
    std::memcpy(strat+0x1A8,&pointer,4);
}
bool Near(float a,float b) { return std::fabs(a-b)<0.000001f; }
bool FaultFinally(uintptr_t player) {
    auto patch=eth::trainer_movement::BeforeVm(player,player,false);
    __try {
        __try { RaiseException(0xE0123456,0,0,nullptr); }
        __finally { eth::trainer_movement::AfterVm(patch); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return patch.count==0; }
    return false;
}
}
int main()
{
    using namespace eth::trainer_movement;
    static_assert(sizeof(void*)==4,"This fixture models the native x86 VM");
    static_assert(std::is_trivially_destructible<VmPatch>::value,"SEH finally token");
    Fixture();
    const uintptr_t player=reinterpret_cast<uintptr_t>(strat);
    CHECK(!Active());
    CHECK(BeforeVm(1,1,false).count==0); // Disabled: no strat/class memory read.
    CHECK(!Set("",2,2));
    CHECK(!Set("trainer",NAN,1) && !Set("trainer",1,INFINITY));
    CHECK(!Set("trainer",0.49f,1) && !Set("trainer",1,0.49f));
    CHECK(!Set("trainer",5.01f,1) && !Set("trainer",1,5.01f));
    CHECK(Set("trainer",2,2) && Active());
    CHECK(!Set("other",1.5f,1.5f)); // No cumulative or last-writer-owner scaling.
    CHECK(BeforeVm(player,0,false).count==0);
    CHECK(BeforeVm(player,player+4,false).count==0);
    CHECK(BeforeVm(player,player,true).count==0); // Yield to HL ownership.
    CHECK(BeforeVm(1,1,false).count==0); // SEH safety when enabled.
    auto patch=BeforeVm(player,player,false);
    CHECK(patch.count==15 && Near(Value(0x3030),0.23f));
    CHECK(Near(Value(0x319C),0.048f));
    CHECK(Near(Value(0x2300)-0.1f,0.18f)); // Net base impulse doubled, not apex.
    CHECK(BeforeVm(player,player,false).count==0); // Recursive call cannot rescale.
    AfterVm(patch);
    CHECK(patch.count==0 && Get(0x3030)==0x3DEB851F && Get(0x2300)==0x3E428F5C);
    AfterVm(patch); // Idempotent cleanup.
    CHECK(FaultFinally(player));
    CHECK(Get(0x3030)==0x3DEB851F && Get(0x2300)==0x3E428F5C);
    CHECK(Set("trainer",1,1.5f));
    patch=BeforeVm(player,player,false);
    CHECK(patch.count==3 && Get(0x3030)==0x3DEB851F);
    CHECK(Near(Value(0x2300)-0.1f,0.135f));
    AfterVm(patch);
    CHECK(Set("trainer",0.5f,1));
    patch=BeforeVm(player,player,false);
    CHECK(patch.count==12 && Get(0x2300)==0x3E428F5C);
    CHECK(Near(Value(0x3030),0.0575f));
    Release("other"); CHECK(Active());
    Release("trainer"); CHECK(!Active());
    // Releasing a request DURING VM does not lose the stack restoration record.
    AfterVm(patch); CHECK(Get(0x3030)==0x3DEB851F);
    CHECK(Set("trainer",2,2));
    name[0]='X'; CHECK(BeforeVm(player,player,false).count==0); name[0]='N';
    cls[2]+=4; CHECK(BeforeVm(player,player,false).count==0); cls[2]-=4;
    Put(0x3454,0); CHECK(BeforeVm(player,player,false).count==0); Put(0x3454,0x0B0A0AE9);
    Put(0x2300,0); CHECK(BeforeVm(player,player,false).count==0); Put(0x2300,0x3E428F5C);
    CHECK(Get(0x3030)==0x3DEB851F); // Failed fingerprint wrote nothing.
    auto* readonly=static_cast<uint8_t*>(VirtualAlloc(nullptr,sizeof(code),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(readonly);
    std::memcpy(readonly,code,sizeof(code));
    DWORD old=0;
    CHECK(VirtualProtect(readonly,sizeof(code),PAGE_READONLY,&old));
    cls[3]=reinterpret_cast<uintptr_t>(readonly);cls[2]=cls[3]+0x47D0;
    CHECK(BeforeVm(player,player,false).count==0);
    CHECK(!std::memcmp(readonly,code,sizeof(code)));
    cls[3]=reinterpret_cast<uintptr_t>(code);cls[2]=cls[3]+0x47D0;
    CHECK(VirtualFree(readonly,0,MEM_RELEASE));
    patch=BeforeVm(player,player,false);
    CHECK(patch.count==15);
    Put(0x3030,0x12345678); // Never clobber an intervening foreign patch.
    AfterVm(patch);
    CHECK(Get(0x3030)==0x12345678 && Get(0x2300)==0x3E428F5C);
    Fixture(); // Streaming/reused class identity must be re-fingerprinted afresh.
    patch=BeforeVm(player,player,false);CHECK(patch.count==15);AfterVm(patch);
    CHECK(Set("trainer",2.01f,2.01f)); // Previous 2x ceiling was policy, not representation.
    CHECK(Set("trainer",5,5));
    patch=BeforeVm(player,player,false);
    CHECK(patch.count==15);
    CHECK(Near(Value(0x3030),0.575f) && Near(Value(0x3038),3.45f));
    CHECK(Near(Value(0x2300)-0.1f,0.45f) && Near(Value(0x2320)-0.1f,0.6f));
    AfterVm(patch);
    for (const auto& site : sites) CHECK(Get(site.offset)==site.bits);
    Shutdown();CHECK(!Active());
    CHECK(Set("new-owner",1,1));CHECK(!Active());
    std::puts("Native movement: bounds, ownership, pin/HL gating, literals, no accumulation, fingerprints, read-only refusal and SEH restoration passed");
}
