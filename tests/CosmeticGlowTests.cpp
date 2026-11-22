// Production GPU substitution on isolated Win32 COM fixtures. Never runs I-Ninja.
#include <cstdlib>
#include <new>
static size_t allocations=0;
void* operator new(size_t n) { ++allocations; if (auto* p=std::malloc(n?n:1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
void operator delete[](void* p,size_t) noexcept { std::free(p); }
#include "../src/core/cosmetic_textures.cpp"
#include "../src/core/cosmetic_glow.cpp"
#include <stdexcept>

namespace fx=eth::cosmetic::blood::fx;
namespace glow=eth::cosmetic::blood::glow;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
void* devicePointer=nullptr;
namespace eth {
uintptr_t GameAddr(uintptr_t va) { return va==game::off::pD3DDevice?uintptr_t(&devicePointer):va; }
bool SafeReadBytes(uintptr_t p,void* out,size_t n) { return fx::ReadNative(p,out,n,nullptr); }
}
namespace eth::game { void* Device() { return devicePointer; } }

void* vbTable[14]{},*texTable[22]{},*simpleTable[4]{},*deviceTable[120]{};
unsigned creates=0,destroys=0;
struct Com { void** table; ULONG refs=1; unsigned kind; Com(void** t,unsigned k):table(t),kind(k) {} };
struct VB:Com { std::vector<uint8_t> bytes; explicit VB(size_t n):Com(vbTable,1),bytes(n) {} };
struct Tex:Com { std::vector<uint8_t> bytes; unsigned pitch; Tex(unsigned w,unsigned h):Com(texTable,2),bytes(w*h*4),pitch(w*4) {} };
ULONG WINAPI AddRef(Com* p) { return ++p->refs; }
ULONG WINAPI Release(Com* p) {
    CHECK(p->refs);
    if (--p->refs) return p->refs;
    ++destroys;
    if (p->kind==1) delete static_cast<VB*>(p);
    else if (p->kind==2) delete static_cast<Tex*>(p);
    else delete p;
    return 0;
}
bool failCreate=false,failLock=false,failGet=false,failPrivateBind=false,failRestore=false,lost=false;
HRESULT WINAPI LockVB(VB* p,UINT offset,UINT size,void** out,DWORD) {
    if (failLock || offset>p->bytes.size() || size>p->bytes.size()-offset) return E_FAIL;
    *out=p->bytes.data()+offset; return S_OK;
}
HRESULT WINAPI UnlockVB(VB*) { return S_OK; }
HRESULT WINAPI LockTex(Tex* p,UINT,D3DLOCKED_RECT* out,const RECT*,DWORD) {
    if (failLock) return E_FAIL;
    out->pBits=p->bytes.data(); out->Pitch=p->pitch; return S_OK;
}
HRESULT WINAPI UnlockTex(Tex*,UINT) { return S_OK; }
struct Device {
    void** table=deviceTable;
    VB* stream=nullptr;
    Com* indices=nullptr;
    Tex* texture=nullptr;
    VB* nativeStream=nullptr;
    DWORD op=D3DTOP_MODULATE,arg1=D3DTA_TEXTURE,arg2=D3DTA_DIFFUSE;
    DWORD fvf=0x142;
} device;
HRESULT WINAPI Cooperative(Device*) { return lost?D3DERR_DEVICELOST:S_OK; }
HRESULT WINAPI CreateVB(Device*,UINT n,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE*) {
    if (failCreate || usage!=D3DUSAGE_WRITEONLY || fvf!=0x142 || pool!=D3DPOOL_MANAGED) return E_FAIL;
    ++creates; *out=reinterpret_cast<IDirect3DVertexBuffer9*>(new VB(n)); return S_OK;
}
HRESULT WINAPI CreateTex(Device*,UINT w,UINT h,UINT levels,DWORD,D3DFORMAT format,D3DPOOL pool,
    IDirect3DTexture9** out,HANDLE*) {
    if (failCreate || levels!=1 || format!=D3DFMT_A8R8G8B8 || pool!=D3DPOOL_MANAGED) return E_FAIL;
    ++creates; *out=reinterpret_cast<IDirect3DTexture9*>(new Tex(w,h)); return S_OK;
}
HRESULT WINAPI GetStream(Device* d,UINT,IDirect3DVertexBuffer9** out,UINT* offset,UINT* stride) {
    if (failGet) return E_FAIL;
    *out=reinterpret_cast<IDirect3DVertexBuffer9*>(d->stream); AddRef(d->stream);
    *offset=0; *stride=24; return S_OK;
}
HRESULT WINAPI SetStream(Device* d,UINT,IDirect3DVertexBuffer9* p,UINT,UINT) {
    auto* next=reinterpret_cast<VB*>(p);
    if ((failPrivateBind && next!=d->nativeStream) || (failRestore && next==d->nativeStream)) return E_FAIL;
    if (next) AddRef(next);
    if (d->stream) Release(d->stream);
    d->stream=next; return S_OK;
}
HRESULT WINAPI GetIndices(Device* d,IDirect3DIndexBuffer9** out) {
    *out=reinterpret_cast<IDirect3DIndexBuffer9*>(d->indices); AddRef(d->indices); return S_OK;
}
HRESULT WINAPI GetTex(Device* d,DWORD,IDirect3DBaseTexture9** out) {
    *out=reinterpret_cast<IDirect3DBaseTexture9*>(d->texture); AddRef(d->texture); return S_OK;
}
HRESULT WINAPI SetTex(Device* d,DWORD,IDirect3DBaseTexture9* value) {
    auto* next=reinterpret_cast<Tex*>(value);
    if (next) AddRef(next);
    if (d->texture) Release(d->texture);
    d->texture=next; return S_OK;
}
HRESULT WINAPI GetFVF(Device* d,DWORD* out) { *out=d->fvf; return S_OK; }
HRESULT WINAPI GetVS(Device*,IDirect3DVertexShader9** out) { *out=nullptr; return S_OK; }
HRESULT WINAPI GetPS(Device*,IDirect3DPixelShader9** out) { *out=nullptr; return S_OK; }
HRESULT WINAPI GetStage(Device* d,DWORD,D3DTEXTURESTAGESTATETYPE state,DWORD* out) {
    *out=state==D3DTSS_COLOROP?d->op:state==D3DTSS_COLORARG1?d->arg1:d->arg2;
    return S_OK;
}
struct Vertex { float xyz[3]; uint32_t color; float uv[2]; };
Vertex originalVertices[5]={
    {{0,0,0},0xFF52FF00,{0,0}},{{1,0,0},0x801E8400,{1,0}},
    {{0,1,0},0x3F000000,{0,1}},{{1,1,0},0x3FC0E2FF,{1,1}},
    {{9,9,9},0x11223344,{9,9}} // Unreferenced, must NEVER be neutralized.
};
uint16_t originalIndices[]={0,1,2,3};
uint32_t childVtable[7]={fx::kOrdinaryBinder,0,0,0,0,0,fx::kOrdinaryGpuGetter};
uint32_t parentVtable=fx::kAnimatedBinder;
uint32_t mesh[18]{},parent[5]{},stage[16]{},child[8]{},buffer[9]{},primitive[10]{};
bool consent=true;
unsigned calls=0;
fx::GreenDraw* prepared=nullptr;
Tex* originalTexture=nullptr;
Com* originalIB=nullptr;
size_t sourceReads=0;
bool Read(uintptr_t p,void* out,size_t n,void*) {
    if (p==uintptr_t(originalVertices) || p==uintptr_t(originalIndices) || p==child[4]) ++sourceReads;
    return fx::ReadNative(p,out,n,nullptr);
}
bool Authorize(uintptr_t,void*) { return consent; }
void Native(void*) { ++calls; CHECK(device.stream==device.nativeStream && device.texture==originalTexture); }
bool tinted=false;
void Paint(void*) {
    ++calls;
    CHECK(device.stream!=device.nativeStream && device.texture!=originalTexture);
    for (size_t n=0;n<5;++n) {
        Vertex value{}; memcpy(&value,device.stream->bytes.data()+n*24,24);
        CHECK(!memcmp(value.xyz,originalVertices[n].xyz,12));
        CHECK(!memcmp(value.uv,originalVertices[n].uv,8));
        if (n==4) CHECK(value.color==originalVertices[n].color);
        else {
            const auto old=originalVertices[n].color;
            const auto v=(std::max)({old&255,old>>8&255,old>>16&255});
            const auto expected=tinted ? ((v*0xD0/255)<<16)|((v*0x20/255)<<8)|(v*0x80/255) : v|(v<<8)|(v<<16);
            CHECK(value.color==((old&0xFF000000)|expected));
        }
    }
    CHECK(!fx::ReleaseGreenDraw(prepared)); // No resource destruction in scope.
}
void RunPaint(void*) { fx::RunGreenPrimitive(uintptr_t(primitive),Paint,nullptr); }
void RunNative(void*) { fx::RunGreenPrimitive(uintptr_t(primitive),Native,nullptr); }
void Throw(void*) { ++calls; RaiseException(0xE0012345,0,0,nullptr); }
void RunFault(void*) { fx::RunGreenPrimitive(uintptr_t(primitive),Throw,nullptr); }
void __fastcall NativeSetup(void*,void*) {}
void __fastcall NativePrimitive(void*,void*) { Native(nullptr); }
void __fastcall PaintedPrimitive(void*,void*) { Paint(nullptr); }
bool Fault() {
    __try { fx::RunGreenMesh(uintptr_t(mesh),prepared,RunFault,nullptr); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
int main(int argc,char** argv) {
    CHECK(argc==2);
    static_assert(sizeof(void*)==4 && sizeof(Vertex)==24);
    vbTable[1]=texTable[1]=simpleTable[1]=(void*)AddRef;
    vbTable[2]=texTable[2]=simpleTable[2]=(void*)Release;
    vbTable[11]=(void*)LockVB; vbTable[12]=(void*)UnlockVB;
    texTable[19]=(void*)LockTex; texTable[20]=(void*)UnlockTex;
    deviceTable[3]=(void*)Cooperative;
    deviceTable[23]=(void*)CreateTex; deviceTable[26]=(void*)CreateVB;
    deviceTable[101]=(void*)GetStream; deviceTable[100]=(void*)SetStream;
    deviceTable[105]=(void*)GetIndices; deviceTable[64]=(void*)GetTex; deviceTable[65]=(void*)SetTex;
    deviceTable[90]=(void*)GetFVF; deviceTable[93]=(void*)GetVS; deviceTable[108]=(void*)GetPS;
    deviceTable[66]=(void*)GetStage;
    devicePointer=&device;
    device.nativeStream=new VB(sizeof(originalVertices)); ++creates;
    memcpy(device.nativeStream->bytes.data(),originalVertices,sizeof(originalVertices));
    originalIB=new Com(simpleTable,0); ++creates;
    originalTexture=new Tex(64,64); ++creates;
    device.indices=originalIB; AddRef(originalIB);
    SetStream(&device,0,reinterpret_cast<IDirect3DVertexBuffer9*>(device.nativeStream),0,24);
    SetTex(&device,0,reinterpret_cast<IDirect3DBaseTexture9*>(originalTexture));
    std::ifstream file(std::filesystem::path(argv[1])/"split-plane-green-glow-original-A000af8a0.tga",std::ios::binary);
    std::vector<uint8_t> source((std::istreambuf_iterator<char>(file)),{});
    CHECK(source.size()==5138);
    parent[0]=uintptr_t(&parentVtable); parent[1]=parent[2]=64; parent[3]=1; parent[4]=uintptr_t(stage);
    stage[0]=uintptr_t(child);
    child[0]=uintptr_t(childVtable); child[1]=child[2]=64;
    child[3]=uintptr_t(originalTexture); child[4]=uintptr_t(source.data()); child[5]=5138; child[6]=1;
    buffer[0]=0x4C9B8C; buffer[1]=19; buffer[2]=24; buffer[3]=5;
    buffer[4]=uintptr_t(originalVertices); buffer[5]=4; buffer[6]=uintptr_t(originalIndices);
    buffer[7]=uintptr_t(device.nativeStream); buffer[8]=uintptr_t(originalIB);
    mesh[1]=0x142; mesh[3]=uintptr_t(buffer); mesh[4]=uintptr_t(parent); mesh[5]=0xFFFFFFFF;
    mesh[13]=1; mesh[14]=uintptr_t(primitive); mesh[15]=0xFFFF;
    primitive[0]=5; primitive[1]=4; primitive[2]=2;
    const auto untouchedVertices=std::vector<uint8_t>(reinterpret_cast<uint8_t*>(originalVertices),
        reinterpret_cast<uint8_t*>(originalVertices)+sizeof(originalVertices));
    const auto untouchedSource=source;
    consent=false;
    CHECK(!fx::CreateGreenDraw(uintptr_t(mesh),&device,0xD02080,Read,nullptr,Authorize,nullptr));
    consent=true;
    failCreate=true;
    CHECK(!fx::CreateGreenDraw(uintptr_t(mesh),&device,0xD02080,Read,nullptr,Authorize,nullptr));
    failCreate=false; failLock=true;
    CHECK(!fx::CreateGreenDraw(uintptr_t(mesh),&device,0xD02080,Read,nullptr,Authorize,nullptr));
    failLock=false;
    prepared=fx::CreateGreenDraw(uintptr_t(mesh),&device,0xD02080,Read,nullptr,Authorize,nullptr);
    CHECK(prepared && fx::GreenDrawHealthy(prepared));
    eth::cosmetic::textures::Image original;
    CHECK(eth::cosmetic::textures::Decode(source.data(),source.size(),original));
    const auto* pixels=reinterpret_cast<const uint32_t*>(static_cast<Tex*>(reinterpret_cast<void*>(prepared->texture))->bytes.data());
    for (size_t n=0;n<original.pixels.size();++n) {
        const auto p=original.pixels[n],v=(std::max)({p&255,p>>8&255,p>>16&255});
        CHECK(pixels[n]==((p&0xFF000000)|((v*0xD0/255)<<16)|((v*0x20/255)<<8)|(v*0x80/255)));
    }
    const auto beforeAlloc=allocations,beforeReads=sourceReads;
    const auto beforeUploads=glow::GetStats().uploads;
    for (unsigned n=0;n<10000;++n) fx::RunGreenMesh(uintptr_t(mesh),prepared,RunPaint,nullptr);
    CHECK(calls==10000 && allocations==beforeAlloc && sourceReads==beforeReads);
    CHECK(glow::GetStats().uploads==beforeUploads);
    CHECK(device.stream==device.nativeStream && device.texture==originalTexture);
    CHECK(device.nativeStream->refs==2 && originalIB->refs==2 && originalTexture->refs==2);
    CHECK(!memcmp(originalVertices,untouchedVertices.data(),untouchedVertices.size()) && source==untouchedSource);
    device.op=D3DTOP_ADD; tinted=true;
    fx::RunGreenMesh(uintptr_t(mesh),prepared,RunPaint,nullptr);
    device.op=D3DTOP_ADDSMOOTH;
    fx::RunGreenMesh(uintptr_t(mesh),prepared,RunNative,nullptr);
    device.op=D3DTOP_MODULATE; tinted=false;
    failGet=true;
    fx::RunGreenMesh(uintptr_t(mesh),prepared,RunNative,nullptr);
    failGet=false; failPrivateBind=true;
    fx::RunGreenMesh(uintptr_t(mesh),prepared,RunNative,nullptr);
    failPrivateBind=false;
    mesh[5]=0x11FFFFFF; stage[4]=0x12345678; buffer[1]=40; primitive[3]=0x3F800000;
    CHECK(fx::Revalidate(*prepared)); // Opacity/UV/refcount/bounds are not upload triggers.
    CHECK(Fault() && device.stream==device.nativeStream && device.texture==originalTexture);
    fx::RunGreenPrimitive(uintptr_t(primitive),Native,nullptr); // TLS must be empty after fault.
    source.back()^=1; CHECK(!fx::Revalidate(*prepared)); source.back()^=1;
    CHECK(fx::ReleaseGreenDraw(prepared)); prepared=nullptr;
    glow::Configure(0xD02080,true); glow::Tick();
    // Main controller discovers on setup and performs cold work on Tick only.
    auto nativeSetup=reinterpret_cast<glow::MeshSetupFn>(NativeSetup);
    glow::MeshSetup(mesh,nativeSetup); glow::Tick();
    CHECK(glow::GetStats().uploads>beforeUploads);
    const auto warmUploads=glow::GetStats().uploads;
    const auto warmHashes=glow::GetStats().hashes;
    const auto warmAllocations=allocations;
    for (unsigned frame=0;frame<1000;++frame) {
        mesh[5]=((frame%255)<<24)|0xFFFFFF; // Native opacity animation
        stage[4]=frame; buffer[1]=frame+1; primitive[3]=frame;
        glow::BeforeNativeTick(); glow::Tick();
        glow::MeshSetup(mesh,nativeSetup);
        glow::Primitive(primitive,reinterpret_cast<glow::PrimitiveFn>(PaintedPrimitive));
        glow::EndPass();
    }
    CHECK(allocations==warmAllocations && glow::GetStats().uploads==warmUploads);
    CHECK(glow::GetStats().hashes==warmHashes);
    glow::Configure(0xD02080,false);
    glow::Primitive(primitive,reinterpret_cast<glow::PrimitiveFn>(NativePrimitive));
    CHECK(device.stream==device.nativeStream && device.texture==originalTexture);
    prepared=fx::CreateGreenDraw(uintptr_t(mesh),&device,0xD02080,Read,nullptr,Authorize,nullptr);
    CHECK(prepared);
    failRestore=true;
    fx::RunGreenMesh(uintptr_t(mesh),prepared,RunPaint,nullptr);
    CHECK(glow::GetStats().faulted && !fx::GreenDrawHealthy(prepared));
    failRestore=false;
    SetStream(&device,0,reinterpret_cast<IDirect3DVertexBuffer9*>(device.nativeStream),0,24);
    fx::RunGreenMesh(uintptr_t(mesh),prepared,RunNative,nullptr);
    CHECK(fx::ReleaseGreenDraw(prepared));
    glow::Shutdown();
    Release(device.stream); device.stream=nullptr;
    Release(device.indices); device.indices=nullptr;
    Release(device.texture); device.texture=nullptr;
    Release(device.nativeStream); Release(originalIB); Release(originalTexture);
    CHECK(creates==destroys);
    puts("PASS shared glow: exact asset, RGB/alpha, per-primitive GPU restoration/SEH, 10000 allocation/source-read/upload-free draws, fallback and independent disable.");
}
