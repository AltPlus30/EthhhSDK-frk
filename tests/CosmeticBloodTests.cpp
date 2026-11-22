// Isolated real implementation + fake D3D ABI. Never launches/attaches the game.
#include <cstdlib>
#include <new>
size_t allocations=0;
void* operator new(size_t size) {
    ++allocations;
    if (auto* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
void operator delete[](void* p,size_t) noexcept { std::free(p); }
#include "../src/core/cosmetic_textures.cpp"
#include "../src/core/cosmetic_blood.cpp"

void* testDevice=nullptr;
size_t safeReads=0,deviceReads=0;
uint8_t binderBytes[]={
    0xA1,0,0x55,0x52,0,0x56,0x8B,0xB0,0xB0,7,0,0,0x57,0x6A,0,0x6A,
    0x18,0x8B,0xF9,0x8B,0x8E,0xA8,0,0,0,0x6A,0,0xE8,0xC0,0x10,0xFF,0xFF,
    0x8B,0x57,0x0C,0x8B,0x36,0x8B,0x0E,0x52,0x6A,0,0x56,0xFF,0x91,4,1,0,0,0x5F,0x5E,0xC3
};
namespace eth {
uintptr_t GameAddr(uintptr_t address) {
    return address==game::off::pD3DDevice?uintptr_t(&testDevice):address;
}
bool SafeReadBytes(uintptr_t address,void* output,size_t size) {
    ++safeReads;
    if (address==cosmetic::blood::kTextureBinder) {
        if (size>sizeof(binderBytes)) return false;
        memcpy(output,binderBytes,size); return true;
    }
    if (!address) return false;
    __try { memcpy(output,reinterpret_cast<void*>(address),size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
}
namespace eth::game { void* Device() { ++deviceReads; return testDevice; } }
namespace b=eth::cosmetic::blood;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
void* textureVtable[22]{};
unsigned creates=0,releases=0,calls=0;
bool failCreate=false,failLock=false,failUnlock=false,lost=false;
struct FakeTexture {
    void** vtable=textureVtable;
    std::vector<uint8_t> bytes;
    unsigned pitch;
    FakeTexture(unsigned w,unsigned h):bytes((w*4+16)*h),pitch(w*4+16) {}
};
ULONG WINAPI FakeRelease(FakeTexture* p) { ++releases; delete p; return 0; }
HRESULT WINAPI FakeLock(FakeTexture* p,UINT,D3DLOCKED_RECT* out,const RECT*,DWORD) {
    if (failLock) return E_FAIL;
    out->pBits=p->bytes.data(); out->Pitch=p->pitch; return S_OK;
}
HRESULT WINAPI FakeUnlock(FakeTexture*,UINT) { return failUnlock?E_FAIL:S_OK; }
HRESULT WINAPI FakeCooperative(void*) { return lost?D3DERR_DEVICELOST:S_OK; }
HRESULT WINAPI FakeCreate(void*,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,
                         D3DPOOL pool,IDirect3DTexture9** out,HANDLE*) {
    if (failCreate || levels!=1 || usage || format!=D3DFMT_A8R8G8B8 || pool!=D3DPOOL_MANAGED) return E_FAIL;
    ++creates; *out=reinterpret_cast<IDirect3DTexture9*>(new FakeTexture(w,h)); return S_OK;
}
void* observed=nullptr;
IDirect3DTexture9* observedHandle=nullptr;
// __fastcall fixture consumes ECX exactly as the real thiscall binder does.
long __fastcall Observe(void* pointer,void*) {
    ++calls; observed=pointer;
    observedHandle=static_cast<b::Descriptor*>(pointer)->handle;
    // Reentrant unload must not free a borrowed GPU resource.
    b::Shutdown(); b::Release("test"); b::OnDeviceReset(); b::InvalidateBindings();
    return 123;
}
long __fastcall Fault(void*,void*) {
    RaiseException(0xE0000001,0,0,nullptr); return 0;
}
bool FaultBind(void* descriptor) {
    __try { b::Bind(descriptor,reinterpret_cast<b::BindFn>(Fault)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
std::vector<uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main(int argc,char** argv)
{
    CHECK(argc>=3 && argc<=5); // baseline, optional v0.5 pools and anatomical cap
    CHECK(b::VerifyHookFingerprint());
    for (size_t i=0;i<sizeof(binderBytes);++i) {
        binderBytes[i]^=1; CHECK(!b::VerifyHookFingerprint()); binderBytes[i]^=1;
    }
    const std::vector<std::string> paths={
        "blood-splatter-0-original-A001e0060.tga","blood-splatter-1-original-A001dfc20.tga",
        "blood-splatter-2-original-A001df7e0.tga","blood-splatter-3-original-A001df3a0.tga",
        "green-blood-pool-original-A001c7de0.tga"};
    textureVtable[2]=reinterpret_cast<void*>(FakeRelease);
    textureVtable[19]=reinterpret_cast<void*>(FakeLock);
    textureVtable[20]=reinterpret_cast<void*>(FakeUnlock);
    void* deviceVtable[120]{};
    deviceVtable[3]=reinterpret_cast<void*>(FakeCooperative);
    deviceVtable[23]=reinterpret_cast<void*>(FakeCreate);
    struct { void** vtable; } fakeDevice{deviceVtable}, otherDevice{deviceVtable};
    testDevice=&fakeDevice;
    std::string error;
    CHECK(b::Configure("test",argv[1],paths,0xC02040,error));
    CHECK(!b::Configure("other",argv[1],paths,0,error));
    CHECK(!b::Configure("test",argv[1],paths,0x1000000,error));
    auto badPaths=paths; badPaths[0]="../"+paths[0];
    CHECK(!b::Configure("test",argv[1],badPaths,0,error));
    CHECK(b::owner=="test");
    std::array<std::vector<uint8_t>,5> sources;
    std::array<b::Descriptor,5> descriptors{};
    uintptr_t vtable=b::kTextureBinder;
    for (unsigned i=0;i<5;++i) {
        sources[i]=Read(std::filesystem::path(argv[2])/paths[i]);
        CHECK(sources[i].size()==b::sizes[i]);
        CHECK(b::HashMatches(sources[i].data(),sources[i].size(),b::hashes[i]));
        eth::cosmetic::textures::Image original,edited;
        CHECK(eth::cosmetic::textures::Decode(sources[i].data(),sources[i].size(),original));
        auto bytes=Read(std::filesystem::path(argv[1])/paths[i]);
        CHECK(eth::cosmetic::textures::Decode(bytes.data(),bytes.size(),edited));
        CHECK(b::ValidateImage(edited,i));
        for (size_t n=0;n<edited.pixels.size();++n) {
            CHECK((original.pixels[n]>>24)==(edited.pixels[n]>>24));
            CHECK((b::replacements[i].image.pixels[n]&0xFF000000)==(edited.pixels[n]&0xFF000000));
        }
        edited.pixels[0]^=0x01000000; CHECK(!b::ValidateImage(edited,i));
        edited.pixels[0]^=0x01000000; edited.pixels[0]^=1; CHECK(!b::ValidateImage(edited,i));
        descriptors[i]={uintptr_t(&vtable),b::sides[i],b::sides[i],nullptr,
                        sources[i].data(),unsigned(sources[i].size()),1,0};
    }
    auto fn=reinterpret_cast<b::BindFn>(Observe);
    for (auto& d:descriptors) {
        CHECK(b::Bind(&d,fn)==123 && observed==&d);
    }
    CHECK(calls==5 && b::GetPerformanceCounters().hashes==0 && creates==0);
    b::Tick(); CHECK(b::GetPerformanceCounters().hashes==4 && creates==5);
    b::Tick(); CHECK(b::GetPerformanceCounters().hashes==5 && creates==5);
    for (unsigned i=0;i<5;++i) {
        const auto original=descriptors[i];
        CHECK(b::Bind(&descriptors[i],fn)==123 && observed!=&descriptors[i]);
        CHECK(observedHandle==b::replacements[i].handle);
        CHECK(!memcmp(&original,&descriptors[i],sizeof(original)));
        const auto* gpu=reinterpret_cast<FakeTexture*>(b::replacements[i].handle);
        for (unsigned y=0;y<b::sides[i];++y)
            CHECK(!memcmp(gpu->bytes.data()+size_t(y)*gpu->pitch,
                b::replacements[i].image.pixels.data()+size_t(y)*b::sides[i],b::sides[i]*4));
    }
    const auto before=b::GetPerformanceCounters();
    const auto beforeAlloc=allocations;
    const auto beforeReads=safeReads,beforeDeviceReads=deviceReads;
    for (unsigned n=0;n<100000;++n) b::Bind(&descriptors[n%5],fn);
    CHECK(allocations==beforeAlloc && creates==5);
    CHECK(safeReads==beforeReads && deviceReads==beforeDeviceReads);
    CHECK(b::GetPerformanceCounters().hashes==before.hashes);
    CHECK(b::GetPerformanceCounters().sourceBytes==before.sourceBytes);
    CHECK(b::GetPerformanceCounters().substitutions==before.substitutions+100000);
    CHECK(FaultBind(&descriptors[1]) && !b::inBind);
    // Same header/pointer after native resource reuse must not retain trust.
    b::BeforeNativeTick();
    b::Bind(&descriptors[0],fn); CHECK(observed==&descriptors[0]);
    sources[0].back()^=1;
    b::Tick();
    b::Bind(&descriptors[0],fn); CHECK(observed==&descriptors[0]);
    b::Bind(&descriptors[1],fn); CHECK(observed!=&descriptors[1]);
    sources[0].back()^=1;
    b::BeforeNativeTick(); b::Tick();
    b::Bind(&descriptors[0],fn); CHECK(observed!=&descriptors[0]);
    // >8 aliases of the same positively identified blood sources. v0.4's
    // revalidation quota left later aliases native (green) every frame.
    std::array<b::Descriptor,32> aliases{};
    for (size_t n=0;n<aliases.size();++n) {
        aliases[n]=descriptors[n%5];
        b::Bind(&aliases[n],fn);
    }
    b::Tick();
    b::BeforeNativeTick(); b::Tick();
    for (size_t n=0;n<aliases.size();++n) {
        b::Bind(&aliases[n],fn); CHECK(observed!=&aliases[n]);
        CHECK(observedHandle==b::replacements[n%5].handle);
    }
    const auto warmTick=b::GetPerformanceCounters();
    const auto warmAllocations=allocations;
    LARGE_INTEGER frequency{},oldStart{},oldEnd{},newStart{},newEnd{};
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&oldStart);
    for (unsigned tick=0;tick<1000;++tick)
        for (unsigned i=0;i<5;++i)
            CHECK(b::HashMatches(sources[i].data(),sources[i].size(),b::hashes[i]));
    QueryPerformanceCounter(&oldEnd); QueryPerformanceCounter(&newStart);
    for (unsigned tick=0;tick<1000;++tick) {
        b::BeforeNativeTick(); b::Tick();
        for (auto& descriptor : descriptors) b::Bind(&descriptor,fn);
        for (auto& descriptor : aliases) b::Bind(&descriptor,fn);
    }
    QueryPerformanceCounter(&newEnd);
    CHECK(b::GetPerformanceCounters().hashes==warmTick.hashes);
    CHECK(allocations==warmAllocations && creates==5);
    CHECK(b::GetPerformanceCounters().sourceBytes-warmTick.sourceBytes==
        1000u*(1050u*4u+5138u)); // source copies deduplicated across all aliases
    std::printf("Blood 1000 ticks, 37 descriptors/5 sources: old hash-only %.3f ms (5000 SHA); "
        "new full validation + binds %.3f ms (0 SHA/allocations/uploads).\n",
        1000.0*(oldEnd.QuadPart-oldStart.QuadPart)/frequency.QuadPart,
        1000.0*(newEnd.QuadPart-newStart.QuadPart)/frequency.QuadPart);
    struct alignas(32) Collision {
        b::Descriptor value;
        uint8_t pad[4096-sizeof(b::Descriptor)];
    };
    std::array<Collision,12> collisions{};
    for (auto& item : collisions) { item.value=descriptors[0]; b::Bind(&item.value,fn); }
    b::Tick();
    for (auto& item : collisions) { b::Bind(&item.value,fn); CHECK(observed!=&item.value); }
    // Recycled descriptors leave tombstones rather than breaking later probes.
    collisions[0].value.reserved^=1;
    b::BeforeNativeTick(); b::Tick();
    for (size_t i=1;i<collisions.size();++i) {
        b::Bind(&collisions[i].value,fn); CHECK(observed!=&collisions[i].value);
    }
    // Known-size impostor: one bounded verification, then permanent negative.
    auto corrupted=sources[0]; corrupted.back()^=1;
    b::Descriptor impostor=descriptors[0]; impostor.source=corrupted.data();
    b::Bind(&impostor,fn); b::Tick();
    CHECK(observed==&impostor);
    const auto negatives=b::GetPerformanceCounters().hashes;
    for (unsigned n=0;n<1000;++n) b::Bind(&impostor,fn);
    b::Tick(); CHECK(b::GetPerformanceCounters().hashes==negatives);
    // Header changes and level changes cannot borrow stale positive identities.
    auto changed=descriptors[0]; changed.source=corrupted.data();
    descriptors[0]=changed; b::Bind(&descriptors[0],fn); CHECK(observed==&descriptors[0]);
    b::InvalidateBindings();
    b::Bind(&descriptors[1],fn); CHECK(observed==&descriptors[1]);
    b::Tick(); b::Bind(&descriptors[1],fn); CHECK(observed!=&descriptors[1]);
    // Device transition/reset: no old GPU handle is ever borrowed.
    testDevice=&otherDevice; b::Bind(&descriptors[1],fn); CHECK(observed==&descriptors[1]);
    b::Tick(); CHECK(releases==5);
    b::OnDeviceReset(); CHECK(releases==10);
    lost=true; b::Tick(); CHECK(creates==10);
    b::Bind(&descriptors[1],fn); CHECK(observed==&descriptors[1]);
    lost=false; failCreate=true; b::Tick(); CHECK(creates==10);
    failCreate=false; failLock=true; b::Tick(); CHECK(creates==11 && releases==11);
    failLock=false; failUnlock=true; b::Tick(); CHECK(creates==12 && releases==12);
    failUnlock=false; b::Tick(); CHECK(creates==17);
    b::Release("other"); CHECK(b::owner=="test");
    b::Release("test"); CHECK(b::owner.empty() && releases==17);
    CHECK(b::Bind(&descriptors[1],fn)==123 && observed==&descriptors[1]);
    if (argc>=4) {
        CHECK(b::Configure("test",argv[1],paths,0xE03090,error));
        const char* extraNames[]={
            "rax-pool-0-original-A000d25c0.tga","rax-pool-2-original-A000cfd40.tga"};
        std::array<std::vector<uint8_t>,2> poolSources;
        std::array<b::Descriptor,2> pools{};
        for (unsigned i=0;i<2;++i) {
            poolSources[i]=Read(std::filesystem::path(argv[3])/extraNames[i]);
            CHECK(poolSources[i].size()==5138);
            pools[i]={uintptr_t(&vtable),64,64,nullptr,poolSources[i].data(),5138,1,0};
            b::Bind(&pools[i],fn); CHECK(observed==&pools[i]);
        }
        b::Tick();
        for (unsigned i=0;i<2;++i) {
            b::Bind(&pools[i],fn); CHECK(observed!=&pools[i]);
            CHECK(observedHandle==b::replacements[5+i].handle);
            eth::cosmetic::textures::Image original;
            CHECK(eth::cosmetic::textures::Decode(poolSources[i].data(),poolSources[i].size(),original));
            const auto& edited=b::replacements[5+i].image;
            CHECK(edited.pixels.size()==original.pixels.size());
            for (size_t n=0;n<original.pixels.size();++n) {
                const auto pixel=original.pixels[n];
                const auto value=(std::max)({pixel&255,pixel>>8&255,pixel>>16&255});
                const auto expected=(pixel&0xFF000000) | ((value*0xE0/255)<<16) |
                    ((value*0x30/255)<<8) | (value*0x90/255);
                CHECK(edited.pixels[n]==expected);
            }
        }
        poolSources[0].back()^=1;
        b::BeforeNativeTick(); b::Tick();
        b::Bind(&pools[0],fn); CHECK(observed==&pools[0]);
        poolSources[0].back()^=1;
        b::BeforeNativeTick(); b::Tick();
        b::Bind(&pools[0],fn); CHECK(observed!=&pools[0]);
        b::Release("test");
        CHECK(creates==releases);
        std::puts("PASS two additional attributed Ranx pool textures: full-source admission, RGB/alpha and reused-memory refusal.");
    } else std::puts("SKIP extra Ranx pool fixtures: supply build/blood-v05-originals as the third directory.");
    if (argc==5) {
        CHECK(b::Configure("test",argv[1],paths,0xD02080,error));
        auto capSource=Read(std::filesystem::path(argv[4])/"split-interior-cap-original-A000c3540.tga");
        CHECK(capSource.size()==17170);
        b::Descriptor cap={uintptr_t(&vtable),128,128,nullptr,capSource.data(),17170,1,0};
        const auto unchanged=cap;
        b::Bind(&cap,fn); CHECK(observed==&cap);
        b::Tick();
        b::Bind(&cap,fn); CHECK(observed!=&cap && observedHandle==b::replacements[7].handle);
        CHECK(!memcmp(&cap,&unchanged,sizeof(cap)));
        eth::cosmetic::textures::Image original;
        CHECK(eth::cosmetic::textures::Decode(capSource.data(),capSource.size(),original));
        const auto& edited=b::replacements[7].image;
        CHECK(edited.width==128 && edited.height==128);
        for (size_t n=0;n<original.pixels.size();++n) {
            const auto pixel=original.pixels[n];
            const auto value=(std::max)({pixel&255,pixel>>8&255,pixel>>16&255});
            const auto expected=(pixel&0xFF000000) | ((value*0xD0/255)<<16) |
                ((value*0x20/255)<<8) | (value*0x80/255);
            CHECK(edited.pixels[n]==expected && (edited.pixels[n]>>24)==255);
        }
        capSource.back()^=1; // Size/dimensions alone never authorize another atlas.
        b::BeforeNativeTick(); b::Tick();
        b::Bind(&cap,fn); CHECK(observed==&cap);
        capSource.back()^=1;
        b::BeforeNativeTick(); b::Tick();
        b::Bind(&cap,fn); CHECK(observed!=&cap);
        b::Release("test"); CHECK(creates==releases);
        std::puts("PASS isolated split-body anatomical atlas: RGB/opaque alpha, unchanged descriptors and source mutation refusal.");
    } else std::puts("SKIP anatomical cap fixture: supply build/blood-v05-split-verified as the fourth directory.");
    CHECK(b::Configure("test",argv[1],paths,0xFF0000,error));
    b::Bind(&descriptors[1],fn); b::Tick();
    b::Bind(&descriptors[1],fn); CHECK(observed!=&descriptors[1]);
    for (unsigned tick=0;tick<121;++tick) b::BeforeNativeTick();
    b::Tick();
    b::Bind(&descriptors[1],fn); CHECK(observed!=&descriptors[1]);
    sources[1].back()^=1;
    b::BeforeNativeTick(); b::Tick();
    b::Bind(&descriptors[1],fn); CHECK(observed==&descriptors[1]);
    sources[1].back()^=1;
    b::BeforeNativeTick(); b::Tick();
    b::Bind(&descriptors[1],fn); CHECK(observed!=&descriptors[1]);
    b::Release("test"); CHECK(creates==releases);
    std::puts("Cosmetic blood tests passed (100000 warmed binds; no hook allocations/SHA/upload).");
    return 0;
}
