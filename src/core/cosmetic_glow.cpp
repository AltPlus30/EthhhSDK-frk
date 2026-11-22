#include "precomp.h"
#include "cosmetic_glow.h"
#include "cosmetic_textures.h"
#include "game.h"
#include <bcrypt.h>
#include <memory>

namespace eth::cosmetic::blood::fx {
namespace {
constexpr unsigned kSourceBytes=5138, kMaxPrimitives=8;
constexpr size_t kMaxVertexBytes=512*1024, kMaxIndexBytes=128*1024;
constexpr char kGreenHash[]="fbde93538ad88d9648c1994fb3c78b43f448a2ef94e745ee9fa31c0619f77185";
unsigned drawDepth=0;
unsigned prepareDepth=0;
struct Preparing { Preparing() { ++prepareDepth; } ~Preparing() { --prepareDepth; } };
std::array<uint8_t,kSourceBytes> canonical{};
bool haveCanonical=false;
uint64_t hashes=0, uploads=0, draws=0, refusals=0;
bool restoreFault=false;
DWORD lastColorOp=0,lastArg1=0,lastArg2=0;

bool ReadNative(uintptr_t address,void* out,size_t size,void*)
{
    if (!address || size>UINT32_MAX-address) return false;
    __try { memcpy(out,reinterpret_cast<void*>(address),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool HashGreen(const uint8_t* bytes)
{
    ++hashes;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    uint8_t digest[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if (ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if (ok) ok=BCryptHashData(hash,const_cast<PUCHAR>(bytes),kSourceBytes,0)>=0 &&
        BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    constexpr char hex[]="0123456789abcdef";
    for (unsigned i=0;ok && i<32;++i)
        ok=hex[digest[i]>>4]==kGreenHash[i*2] && hex[digest[i]&15]==kGreenHash[i*2+1];
    return ok;
}
struct Key {
    std::array<uint32_t,18> mesh{};
    std::array<uint32_t,5> parent{};
    std::array<uint8_t,64> stage{};
    std::array<uint32_t,8> child{};
    std::array<uint32_t,9> buffer{};
    std::array<std::array<uint32_t,10>,kMaxPrimitives> primitives{};
};
bool Capture(uintptr_t mesh,Read read,void* context,Key& key)
{
    if (!read || !read(mesh,key.mesh.data(),sizeof(key.mesh),context) ||
        key.mesh[1]!=0x142 || (key.mesh[5]&255)!=(key.mesh[5]>>8&255) ||
        (key.mesh[5]&255)!=(key.mesh[5]>>16&255) ||
        key.mesh[15]!=0xFFFF ||
        !key.mesh[3] || !key.mesh[4] || !key.mesh[13] || key.mesh[13]>kMaxPrimitives ||
        !key.mesh[14]) return false;
    uint32_t binder=0,child=0,getter=0;
    if (!read(key.mesh[4],key.parent.data(),sizeof(key.parent),context) ||
        key.parent[3]!=1 || !key.parent[4] ||
        !read(key.parent[0],&binder,4,context) || binder!=game::A(kAnimatedBinder) ||
        !read(key.parent[4],key.stage.data(),key.stage.size(),context)) return false;
    memcpy(&child,key.stage.data(),4);
    if (!child || !read(child,key.child.data(),sizeof(key.child),context) ||
        key.child[1]!=64 || key.child[2]!=64 || key.child[5]!=kSourceBytes ||
        !key.child[3] || !key.child[4] ||
        !read(key.child[0],&binder,4,context) || binder!=game::A(kOrdinaryBinder) ||
        !read(uintptr_t(key.child[0])+0x18,&getter,4,context) ||
        getter!=game::A(kOrdinaryGpuGetter) ||
        !read(key.mesh[3],key.buffer.data(),sizeof(key.buffer),context) ||
        (key.buffer[0]!=game::A(0x4C9B8C) && key.buffer[0]!=game::A(0x4C9B94))) return false;
    // Shader UV animation, opacity, cached render-state arrays, linked-list
    // links and buffer reference counts are NOT geometry identity. They remain
    // native and must not cause GPU uploads on every fading/animated frame.
    for (unsigned i : {2u,6u,7u,8u,9u,10u,11u,12u,16u,17u}) key.mesh[i]=0;
    key.mesh[5]=0;
    key.mesh[15]&=0xFFFF;
    key.buffer[1]=0;
    std::fill(key.stage.begin()+4,key.stage.end(),0);
    const auto& b=key.buffer;
    if (b[2]!=24 || !b[3] || b[3]>kMaxVertexBytes/24 ||
        b[5]<3 || b[5]>kMaxIndexBytes/2 || !b[4] || !b[6] || !b[7] || !b[8]) return false;
    for (unsigned i=0;i<key.mesh[13];++i) {
        auto& p=key.primitives[i];
        if (!read(uintptr_t(key.mesh[14])+i*40,p.data(),40,context) ||
            p[0]!=D3DPT_TRIANGLESTRIP || !p[1] || !p[2] ||
            p[2]>b[5]-2 || p[9]>b[5]-(p[2]+2) ||
            p[8]>=b[3] || p[1]>b[3]-p[8]) return false;
        for (unsigned j=3;j<8;++j) p[j]=0;
    }
    return true;
}
bool Equal(const Key& a,const Key& b) { return !memcmp(&a,&b,sizeof(a)); }
bool EqualBytes(uintptr_t source,const uint8_t* expected,size_t count,Read read,void* context)
{
    std::array<uint8_t,4096> buffer{};
    while (count) {
        const auto n=(std::min)(count,buffer.size());
        if (!read(source,buffer.data(),n,context) || memcmp(buffer.data(),expected,n)) return false;
        source+=n; expected+=n; count-=n;
    }
    return true;
}
}
struct GreenDraw {
    uintptr_t mesh=0;
    IDirect3DDevice9* device=nullptr;
    IDirect3DVertexBuffer9* vertices=nullptr;
    IDirect3DVertexBuffer9* tintedVertices=nullptr;
    IDirect3DTexture9* texture=nullptr;
    Key key{};
    Read read=nullptr;
    void* readContext=nullptr;
    EffectProof proof=nullptr;
    void* proofContext=nullptr;
    std::vector<uint8_t> originalVertices,originalIndices;
    bool healthy=true;
    ~GreenDraw() {
        if (vertices) vertices->Release();
        if (tintedVertices) tintedVertices->Release();
        if (texture) texture->Release();
    }
};
namespace {
thread_local GreenDraw* activeGreen=nullptr;
bool Current(const GreenDraw& p)
{
    Key current{};
    return p.healthy && !restoreFault && p.proof && p.proof(p.mesh,p.proofContext) &&
        Capture(p.mesh,p.read,p.readContext,current) && Equal(current,p.key);
}
bool Revalidate(GreenDraw& p)
{
    return Current(p) &&
        EqualBytes(p.key.child[4],canonical.data(),canonical.size(),p.read,p.readContext) &&
        EqualBytes(p.key.buffer[4],p.originalVertices.data(),p.originalVertices.size(),p.read,p.readContext) &&
        EqualBytes(p.key.buffer[6],p.originalIndices.data(),p.originalIndices.size(),p.read,p.readContext);
}
}
GreenDraw* CreateGreenDraw(uintptr_t mesh,void* device,uint32_t rgb,Read read,void* context,
    EffectProof proof,void* proofContext)
{
    if (drawDepth || !device || rgb>0xFFFFFF || !proof || !proof(mesh,proofContext)) return nullptr;
    Preparing preparing;
    auto p=std::make_unique<GreenDraw>();
    p->mesh=mesh; p->device=static_cast<IDirect3DDevice9*>(device);
    p->read=read; p->readContext=context; p->proof=proof; p->proofContext=proofContext;
    if (!Capture(mesh,read,context,p->key)) return nullptr;
    std::array<uint8_t,kSourceBytes> source{};
    if (!read(p->key.child[4],source.data(),source.size(),context)) return nullptr;
    if (haveCanonical) {
        if (source!=canonical) return nullptr;
    } else {
        if (!HashGreen(source.data())) return nullptr;
        canonical=source; haveCanonical=true;
    }
    textures::Image image;
    if (!textures::Decode(source.data(),source.size(),image) || image.width!=64 || image.height!=64)
        return nullptr;
    for (auto& pixel : image.pixels) {
        const auto value=(std::max)({pixel&255,pixel>>8&255,pixel>>16&255});
        pixel=(pixel&0xFF000000) | ((value*(rgb>>16&255)/255)<<16) |
            ((value*(rgb>>8&255)/255)<<8) | (value*(rgb&255)/255);
    }
    const auto& b=p->key.buffer;
    p->originalVertices.resize(size_t(b[3])*24);
    p->originalIndices.resize(size_t(b[5])*2);
    if (!read(b[4],p->originalVertices.data(),p->originalVertices.size(),context) ||
        !read(b[6],p->originalIndices.data(),p->originalIndices.size(),context)) return nullptr;
    auto gray=p->originalVertices;
    // Native indices are absolute in the shared stream (BaseVertexIndex=0).
    // Only vertices referenced by THIS admitted primitive set are neutralized.
    for (unsigned n=0;n<p->key.mesh[13];++n) {
        const auto& primitive=p->key.primitives[n];
        for (unsigned i=0;i<primitive[2]+2;++i) {
            uint16_t vertex=0;
            memcpy(&vertex,p->originalIndices.data()+size_t(primitive[9]+i)*2,2);
            if (vertex<primitive[8] || vertex-primitive[8]>=primitive[1] || vertex>=b[3]) return nullptr;
            uint32_t color=0;
            memcpy(&color,gray.data()+size_t(vertex)*24+12,4);
            const auto value=(std::max)({color&255,color>>8&255,color>>16&255});
            color=(color&0xFF000000)|value|(value<<8)|(value<<16);
            memcpy(gray.data()+size_t(vertex)*24+12,&color,4);
        }
    }
    auto tinted=gray;
    for (unsigned n=0;n<p->key.mesh[13];++n) for (unsigned i=0;i<p->key.primitives[n][2]+2;++i) {
        uint16_t vertex=0;
        memcpy(&vertex,p->originalIndices.data()+size_t(p->key.primitives[n][9]+i)*2,2);
        uint32_t pixel=0;
        memcpy(&pixel,gray.data()+size_t(vertex)*24+12,4);
        const auto value=pixel&255;
        pixel=(pixel&0xFF000000) | ((value*(rgb>>16&255)/255)<<16) |
            ((value*(rgb>>8&255)/255)<<8) | (value*(rgb&255)/255);
        memcpy(tinted.data()+size_t(vertex)*24+12,&pixel,4);
    }
    const auto uploadVertices=[&](const std::vector<uint8_t>& bytes,IDirect3DVertexBuffer9** output) {
        if (FAILED(p->device->CreateVertexBuffer(UINT(bytes.size()),D3DUSAGE_WRITEONLY,0x142,
            D3DPOOL_MANAGED,output,nullptr))) return false;
        void* destination=nullptr;
        if (FAILED((*output)->Lock(0,UINT(bytes.size()),&destination,0))) return false;
        if (destination) memcpy(destination,bytes.data(),bytes.size());
        return SUCCEEDED((*output)->Unlock()) && destination;
    };
    if (!uploadVertices(gray,&p->vertices) || !uploadVertices(tinted,&p->tintedVertices)) return nullptr;
    if (FAILED(p->device->CreateTexture(64,64,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,
        &p->texture,nullptr))) return nullptr;
    D3DLOCKED_RECT locked{};
    if (FAILED(p->texture->LockRect(0,&locked,nullptr,0))) return nullptr;
    const bool textureMemory=locked.pBits && locked.Pitch>=256;
    if (textureMemory) for (unsigned row=0;row<64;++row)
        memcpy(static_cast<uint8_t*>(locked.pBits)+size_t(row)*locked.Pitch,image.pixels.data()+row*64,256);
    if (FAILED(p->texture->UnlockRect(0)) || !textureMemory || !Revalidate(*p)) return nullptr;
    uploads+=3;
    return p.release();
}
bool ReleaseGreenDraw(GreenDraw* p) { if (drawDepth || prepareDepth) return false; delete p; return true; }
bool GreenDrawHealthy(const GreenDraw* p) { return p && Current(*p); }
void RunGreenMesh(uintptr_t mesh,GreenDraw* p,Continue continuation,void* context)
{
    if (!continuation) return;
    GreenDraw* previous=activeGreen;
    activeGreen=p && mesh==p->mesh && Current(*p) ? p : nullptr;
    ++drawDepth;
    __try { continuation(context); }
    __finally { --drawDepth; activeGreen=previous; }
}
void RunGreenPrimitive(uintptr_t primitive,Continue continuation,void* context)
{
    if (!continuation) return;
    auto* p=activeGreen;
    bool match=false;
    if (p && Current(*p)) for (unsigned i=0;i<p->key.mesh[13];++i)
        if (primitive==uintptr_t(p->key.mesh[14])+i*40) match=true;
    if (!match) { continuation(context); return; }
    IDirect3DVertexBuffer9* original=nullptr;
    IDirect3DIndexBuffer9* indices=nullptr;
    IDirect3DBaseTexture9* texture=nullptr;
    IDirect3DVertexShader9* vertexShader=nullptr;
    IDirect3DPixelShader9* pixelShader=nullptr;
    UINT offset=0,stride=0;
    DWORD fvf=0;
    DWORD colorOp=0,arg1=0,arg2=0;
    bool admitted=SUCCEEDED(p->device->GetStreamSource(0,&original,&offset,&stride)) &&
        SUCCEEDED(p->device->GetIndices(&indices)) &&
        SUCCEEDED(p->device->GetTexture(0,&texture)) &&
        SUCCEEDED(p->device->GetFVF(&fvf)) &&
        SUCCEEDED(p->device->GetVertexShader(&vertexShader)) &&
        SUCCEEDED(p->device->GetPixelShader(&pixelShader)) &&
        SUCCEEDED(p->device->GetTextureStageState(0,D3DTSS_COLOROP,&colorOp)) &&
        SUCCEEDED(p->device->GetTextureStageState(0,D3DTSS_COLORARG1,&arg1)) &&
        SUCCEEDED(p->device->GetTextureStageState(0,D3DTSS_COLORARG2,&arg2)) &&
        !vertexShader && !pixelShader && offset==0 && stride==24 && fvf==0x142 &&
        uintptr_t(original)==p->key.buffer[7] && uintptr_t(indices)==p->key.buffer[8] &&
        uintptr_t(texture)==p->key.child[3];
    lastColorOp=colorOp; lastArg1=arg1; lastArg2=arg2;
    const bool diffuse1=arg1==D3DTA_DIFFUSE || arg1==D3DTA_CURRENT;
    const bool diffuse2=arg2==D3DTA_DIFFUSE || arg2==D3DTA_CURRENT;
    const bool multiply=(colorOp==D3DTOP_MODULATE || colorOp==D3DTOP_MODULATE2X ||
        colorOp==D3DTOP_MODULATE4X) &&
        ((arg1==D3DTA_TEXTURE && diffuse2) || (arg2==D3DTA_TEXTURE && diffuse1));
    const bool textureOnly=(colorOp==D3DTOP_SELECTARG1 && arg1==D3DTA_TEXTURE) ||
        (colorOp==D3DTOP_SELECTARG2 && arg2==D3DTA_TEXTURE);
    const bool vertexOnly=(colorOp==D3DTOP_SELECTARG1 && diffuse1) ||
        (colorOp==D3DTOP_SELECTARG2 && diffuse2);
    const bool linear=(colorOp==D3DTOP_ADD || colorOp==D3DTOP_SUBTRACT ||
        colorOp==D3DTOP_BLENDDIFFUSEALPHA || colorOp==D3DTOP_BLENDTEXTUREALPHA ||
        colorOp==D3DTOP_BLENDFACTORALPHA || colorOp==D3DTOP_BLENDTEXTUREALPHAPM) &&
        ((arg1==D3DTA_TEXTURE && diffuse2) || (arg2==D3DTA_TEXTURE && diffuse1));
    // Unknown nonlinear/constant/complement recipes stay native rather than turning a
    // green additive layer white/black or tinting the requested color twice.
    admitted &= multiply || textureOnly || vertexOnly || linear;
    ++drawDepth;
    __try {
        if (admitted) {
            // Multiplication needs the requested RGB only in the texture.
            // Linear add/lerp needs it in BOTH inputs, not a white diffuse term.
            auto* stream=(linear || vertexOnly) ? p->tintedVertices : p->vertices;
            admitted=SUCCEEDED(p->device->SetStreamSource(0,stream,0,24)) &&
                SUCCEEDED(p->device->SetTexture(0,p->texture));
            if (!admitted) {
                const auto a=p->device->SetTexture(0,texture);
                const auto b=p->device->SetStreamSource(0,original,offset,stride);
                if (FAILED(a) || FAILED(b)) { p->healthy=false; restoreFault=true; }
            }
        }
        if (admitted) ++draws; else ++refusals;
        continuation(context);
    } __finally {
        if (admitted) {
            const auto a=p->device->SetTexture(0,texture);
            const auto b=p->device->SetStreamSource(0,original,offset,stride);
            if (FAILED(a) || FAILED(b)) { p->healthy=false; restoreFault=true; }
        }
        if (pixelShader) pixelShader->Release();
        if (vertexShader) vertexShader->Release();
        if (texture) texture->Release();
        if (indices) indices->Release();
        if (original) original->Release();
        --drawDepth;
    }
}
}

namespace eth::cosmetic::blood::glow {
namespace {
struct Entry { uintptr_t mesh=0; fx::GreenDraw* draw=nullptr; uint64_t seen=0; bool ready=false; uint64_t retry=0; };
std::array<Entry,32> entries;
bool enabled=false;
uint32_t color=0xFF0000;
uint64_t epoch=1;
IDirect3DDevice9* deviceIdentity=nullptr;
thread_local fx::GreenDraw* current=nullptr;
bool pending=false,pendingEnabled=false;
uint32_t pendingColor=0;
bool Consent(uintptr_t,void*) { return enabled && !fx::restoreFault; }
void Clear()
{
    current=nullptr;
    for (auto& e : entries) {
        if (e.draw && !fx::ReleaseGreenDraw(e.draw)) { e.ready=false; continue; }
        e={};
    }
}
struct Call { void* object; void(__thiscall* original)(void*); };
void Continue(void* context) {
    auto& call=*static_cast<Call*>(context);
    if (call.original) call.original(call.object);
}
}
void Configure(uint32_t rgb,bool on) {
    if (fx::drawDepth || fx::prepareDepth) {
        enabled=false; pending=true; pendingEnabled=on; pendingColor=rgb;
        return;
    }
    pending=false; enabled=false; Clear(); fx::restoreFault=false; color=rgb; enabled=on;
}
bool Enabled() { return enabled && !fx::restoreFault; }
void BeforeNativeTick()
{
    ++epoch; current=nullptr;
    for (auto& e : entries) e.ready=false;
}
void Tick()
{
    if (fx::drawDepth || fx::prepareDepth) return;
    if (pending) Configure(pendingColor,pendingEnabled);
    if (!enabled || fx::restoreFault) { Clear(); return; }
    auto* device=static_cast<IDirect3DDevice9*>(game::Device());
    if (device!=deviceIdentity) { Clear(); deviceIdentity=device; }
    if (!device || FAILED(device->TestCooperativeLevel())) return;
    unsigned coldBudget=4;
    for (auto& e : entries) {
        if (!e.mesh) continue;
        if (epoch-e.seen>120) {
            fx::ReleaseGreenDraw(e.draw); e={}; continue;
        }
        if (e.draw && fx::Revalidate(*e.draw)) { e.ready=true; continue; }
        if (e.draw) { fx::ReleaseGreenDraw(e.draw); e.draw=nullptr; }
        if (coldBudget && epoch>=e.retry) {
            --coldBudget;
            e.draw=fx::CreateGreenDraw(e.mesh,device,color,fx::ReadNative,nullptr,Consent,nullptr);
            e.ready=e.draw!=nullptr;
            e.retry=e.ready ? 0 : epoch+30;
        }
    }
}
void MeshSetup(void* mesh,MeshSetupFn original)
{
    current=nullptr;
    fx::GreenDraw* prepared=nullptr;
    uint32_t fvf=0;
    if (Enabled() && fx::ReadNative(uintptr_t(mesh)+4,&fvf,4,nullptr) && fvf==0x142) {
        Entry* available=nullptr;
        for (auto& e : entries) {
            if (e.mesh==uintptr_t(mesh)) {
                e.seen=epoch;
                if (e.ready && fx::GreenDrawHealthy(e.draw)) prepared=e.draw;
                available=nullptr; break;
            }
            if (!e.mesh && !available) available=&e;
        }
        if (available) {
            fx::Key key{};
            if (fx::Capture(uintptr_t(mesh),fx::ReadNative,nullptr,key))
                *available={uintptr_t(mesh),nullptr,epoch,false};
        }
    }
    // 4545D0 only sets native state; no geometry is drawn inside it. The exact
    // following primitive record must match before any SDK state is borrowed.
    if (original) original(mesh);
    current=prepared;
}
void Primitive(void* primitive,PrimitiveFn original)
{
    if (!Enabled() || !current) { if (original) original(primitive); return; }
    IDirect3DDevice9* live=nullptr;
    if (!fx::ReadNative(game::A(game::off::pD3DDevice),&live,sizeof(live),nullptr) ||
        live!=current->device) {
        current=nullptr;
        if (original) original(primitive);
        return;
    }
    Call call{primitive,original};
    // A plain continuation keeps SEH restoration out of C++ owning contexts.
    struct ScopeCall { fx::GreenDraw* draw; uintptr_t primitive; Call* call; } scope{current,uintptr_t(primitive),&call};
    const auto run=+[](void* context) {
        auto& scope=*static_cast<ScopeCall*>(context);
        fx::RunGreenPrimitive(scope.primitive,Continue,scope.call);
    };
    fx::RunGreenMesh(enabled && current ? current->mesh : 0,enabled ? current : nullptr,
        run,&scope);
}
void EndPass() { current=nullptr; }
void OnDeviceReset() {
    if (fx::drawDepth || fx::prepareDepth) { Configure(color,enabled); return; }
    Clear(); deviceIdentity=nullptr;
}
void Shutdown() { Configure(color,false); if (!fx::drawDepth) deviceIdentity=nullptr; }
Stats GetStats() { return {fx::draws,fx::refusals,fx::uploads,fx::hashes,
    fx::lastColorOp,fx::lastArg1,fx::lastArg2,Enabled(),fx::restoreFault}; }
}
