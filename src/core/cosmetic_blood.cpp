#include "cosmetic_blood.h"
#include "cosmetic_textures.h"
#include "cosmetic_glow.h"
#include "game.h"
#include "util.h"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>

namespace eth::cosmetic::blood {
namespace {
using textures::Image;
struct Descriptor {
    uintptr_t vtable;
    unsigned width, height;
    IDirect3DTexture9* handle;
    const uint8_t* source;
    unsigned size, flags, reserved;
};
static_assert(sizeof(Descriptor) == 32 && offsetof(Descriptor, handle) == 12);
constexpr unsigned kAuthoredCount=5, kTextureCount=8;
constexpr unsigned sides[] = {32,32,32,32,64,64,64,128};
constexpr unsigned sizes[] = {1050,1050,1050,1050,5138,5138,5138,17170};
constexpr const char* hashes[] = {
    "db74cc596032f738f79efacff7d7458c7f74f8bedcbf788d4793b5209b77907d",
    "ea28e3a9dd00917e68adc5dc5ee5d188b9a87cec1f4b4213e729368b402b1481",
    "f68aee3ba871deab29fc94333e3253fe059e772a319c217b59cf784317d2fe04",
    "136d3d094796f53cea81e94b2fcb38c1d4d498cd36d1128043eedca2ce794dbb",
    "faa3da9594ae2e9d503b78c9a9163b279ef6e7e1494a90387e919a4006d2f9dd",
    "e64c12bd4c83d000d8812401834835a413c854769201f95376880d5d035e2d2c",
    "c671fe748913aa55a3134305c5ba42ff784dbe948692bd0871b15660913b6a96",
    "b940a1d4a0afda2e49e019c1c6224af1b1c5da732c97207007a8a9ed751c17b8"
};
// SHA-256 of decoded top-left alpha bytes, independently compared to originals.
constexpr const char* alphaHashes[] = {
    "4aa9b5055317640dce366ab411aabbcd44e7dfcc1e2ce59271d7a8b5f77b4192",
    "9de81e8d6f29eb82017ddfee24c71e9960f4f05655a7b71a58473d10887d8b1d",
    "55d76999be39d79d39efdd2b25d7f7320892eefda8aacaef230f65ded5b9e73b",
    "f6f3c37b3aba251a9bb46988c3c4f69bd622fe6bce78fb0f6ad2dc3aa466804f",
    "eab5b57892444f9a2cef76b3a931052040fc7f1f1c2faa95b6d24c83cdf86195"
};
struct Replacement { Image image; IDirect3DTexture9* handle = nullptr; };
enum class State { Empty, Pending, Negative, Positive, Revalidate, Tombstone };
struct Identity {
    uintptr_t address = 0;
    Descriptor descriptor{};
    State state = State::Empty;
    unsigned slot = 0;
    uint64_t lastSeen = 0;
};
// Fixed, non-evicting table: saturation fails native until invalidation.
// No unbounded candidate queue and no repeated hash of a negative identity.
std::array<Identity, 128> identities{};
std::array<Replacement, kTextureCount> replacements;
uint32_t selectedRgb=0xFFFFFF;
std::string owner;
IDirect3DDevice9* deviceIdentity = nullptr;
bool inBind = false;
PerformanceCounters counters{};
size_t pendingCursor = 0;
uint64_t nativeEpoch = 1;
struct SourceVerdict {
    unsigned width=0, height=0;
    int slot=-1;
    std::vector<uint8_t> bytes;
};
// Pin positively attributed byte patterns; unrelated same-size
// textures must not evict blood identity proof and make it flicker green.
std::array<SourceVerdict,kTextureCount+32> sourceVerdicts;
size_t nextVerdict = 0;
struct TickTimer {
    LARGE_INTEGER begin{};
    TickTimer() { QueryPerformanceCounter(&begin); }
    ~TickTimer() {
        static const LONGLONG frequency=[] {
            LARGE_INTEGER value{};
            return QueryPerformanceFrequency(&value) ? value.QuadPart : LONGLONG(0);
        }();
        LARGE_INTEGER end{};
        if (frequency<=0 || !begin.QuadPart || !QueryPerformanceCounter(&end) ||
            end.QuadPart<begin.QuadPart) return;
        counters.lastTickUs=uint64_t((end.QuadPart-begin.QuadPart)*1000000/frequency);
        counters.maxTickUs=(std::max)(counters.maxTickUs,counters.lastTickUs);
    }
};

// Used only for constant-size snapshots on the hook. Unlike SafeReadBytes this
// does not VirtualQuery every material every draw. Native lifetime is serialized.
bool Snapshot(uintptr_t address, void* output, size_t size)
{
    if (!address || size > UINTPTR_MAX - address) return false;
    __try { memcpy(output, reinterpret_cast<void*>(address), size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
bool Hash(const uint8_t* data, size_t size, uint8_t (&digest)[32])
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(data), ULONG(size), 0) >= 0 &&
        BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}
bool DigestMatches(const uint8_t (&digest)[32], const char* expected)
{
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i=0; i<32; ++i)
        if (hex[digest[i]>>4] != expected[i*2] || hex[digest[i]&15] != expected[i*2+1]) return false;
    return true;
}
bool HashMatches(const uint8_t* data, size_t size, const char* expected)
{
    uint8_t digest[32]{};
    return Hash(data,size,digest) && DigestMatches(digest,expected);
}
bool ValidateImage(const Image& image, unsigned slot)
{
    if (slot >= kAuthoredCount || image.width != sides[slot] || image.height != sides[slot] ||
        image.pixels.size() != size_t(sides[slot])*sides[slot]) return false;
    std::vector<uint8_t> alpha(image.pixels.size());
    for (size_t i=0; i<alpha.size(); ++i) {
        const auto p = image.pixels[i];
        if ((p&255) != (p>>8&255) || (p&255) != (p>>16&255)) return false;
        alpha[i] = uint8_t(p>>24);
    }
    return HashMatches(alpha.data(), alpha.size(), alphaHashes[slot]);
}
void DropGpu()
{
    for (auto& item : replacements) {
        if (item.handle) item.handle->Release();
        item.handle = nullptr;
    }
    deviceIdentity = nullptr;
}
bool Upload(Replacement& item, IDirect3DDevice9* device)
{
    if (item.handle) return true;
    IDirect3DTexture9* handle = nullptr;
    if (FAILED(device->CreateTexture(item.image.width,item.image.height,1,0,
        D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&handle,nullptr))) return false;
    D3DLOCKED_RECT locked{};
    if (FAILED(handle->LockRect(0,&locked,nullptr,0))) { handle->Release(); return false; }
    const bool valid = locked.pBits && locked.Pitch >= int(item.image.width*4);
    if (valid) for (unsigned y=0; y<item.image.height; ++y)
        memcpy(static_cast<uint8_t*>(locked.pBits)+size_t(y)*locked.Pitch,
               item.image.pixels.data()+size_t(y)*item.image.width,item.image.width*4);
    const auto result = handle->UnlockRect(0);
    if (!valid || FAILED(result)) { handle->Release(); return false; }
    item.handle = handle; ++counters.uploads;
    return true;
}
// No owning locals: restores the reentrancy guard even on native SEH unwind.
long Call(void* descriptor, BindFn original)
{
    const bool previous = inBind;
    inBind = true;
    long result = 0;
    __try { result = original(descriptor); }
    __finally { inBind = previous; }
    return result;
}
bool Same(const Descriptor& a, const Descriptor& b) { return !memcmp(&a,&b,sizeof(a)); }
bool PrepareDerived(unsigned slot)
{
    if (slot<kAuthoredCount || slot>=kTextureCount || !replacements[slot].image.pixels.empty()) return true;
    // Two WHOLE pool textures and the separate anatomical cut-cap atlas are
    // positively attributed. Never convert mixed armor/skin/body atlases.
    const auto& source=sourceVerdicts[slot].bytes;
    if (source.empty()) return false;
    Image image;
    if (!textures::Decode(source.data(),source.size(),image) ||
        image.width!=sides[slot] || image.height!=sides[slot]) return false;
    for (auto& pixel : image.pixels) {
        // Channel maximum retains the original brightest intensity/shape.
        // Alpha is copied bit-for-bit, including partially transparent edges.
        const auto value=(std::max)({pixel&255,(pixel>>8)&255,(pixel>>16)&255});
        pixel=(pixel&0xFF000000) | ((value*(selectedRgb>>16&255)/255)<<16) |
            ((value*(selectedRgb>>8&255)/255)<<8) | (value*(selectedRgb&255)/255);
    }
    replacements[slot].image=std::move(image);
    return true;
}
}

bool VerifyHookFingerprint()
{
    // Full 0x34-byte function. Absolute globals relocate; normalize that operand.
    const uint8_t expected[] = {
        0xA1,0,0x55,0x52,0,0x56,0x8B,0xB0,0xB0,7,0,0,0x57,0x6A,0,0x6A,
        0x18,0x8B,0xF9,0x8B,0x8E,0xA8,0,0,0,0x6A,0,0xE8,0xC0,0x10,0xFF,0xFF,
        0x8B,0x57,0x0C,0x8B,0x36,0x8B,0x0E,0x52,0x6A,0,0x56,0xFF,0x91,4,1,0,0,0x5F,0x5E,0xC3
    };
    uint8_t bytes[sizeof(expected)]{};
    if (!SafeReadBytes(game::A(kTextureBinder),bytes,sizeof(bytes))) return false;
    uintptr_t global = 0;
    memcpy(&global,bytes+1,4);
    if (global != game::A(0x525500)) return false;
    memcpy(bytes+1,expected+1,4);
    return !memcmp(bytes,expected,sizeof(bytes));
}
bool Configure(const std::string& requestedOwner, const std::string& directory,
               const std::vector<std::string>& paths, uint32_t rgb, std::string& error,bool sharedGlow)
{
    error.clear();
    if (inBind || requestedOwner.empty() || (!owner.empty() && owner != requestedOwner) ||
        paths.size()!=5 || rgb>0xFFFFFF) { error="Busy, invalid owner/RGB or expected five blood TGAs"; return false; }
    std::array<Replacement,kTextureCount> next;
    try {
        namespace fs = std::filesystem;
        auto prefix=fs::canonical(directory).wstring();
        if (prefix.back()!=L'\\') prefix+=L'\\';
        for (unsigned i=0;i<kAuthoredCount;++i) {
            const fs::path relative(paths[i]);
            if (paths[i].empty() || relative.has_root_path() || relative.is_absolute() ||
                paths[i].find(':')!=std::string::npos || paths[i].find('\0')!=std::string::npos) {
                error="Blood texture path must be mod-relative"; return false;
            }
            const auto path=fs::canonical(fs::path(directory)/relative);
            const auto full=path.wstring();
            if (full.size()<=prefix.size() || _wcsnicmp(full.c_str(),prefix.c_str(),prefix.size())) {
                error="Blood texture escapes mod directory"; return false;
            }
            const auto size=fs::file_size(path);
            if (size<18 || size>128*1024) { error="Invalid blood TGA size"; return false; }
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            std::ifstream file(path,std::ios::binary);
            if (!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) ||
                !textures::Decode(bytes.data(),bytes.size(),next[i].image) || !ValidateImage(next[i].image,i)) {
                error="Blood TGA must be monochrome with verified original dimensions and alpha"; return false;
            }
            for (auto& pixel : next[i].image.pixels) {
                const uint32_t value=pixel&255;
                pixel=(pixel&0xFF000000) | ((value*(rgb>>16&255)/255)<<16) |
                    ((value*(rgb>>8&255)/255)<<8) | (value*(rgb&255)/255);
            }
        }
        // Finish potentially allocating owner assignment before destroying old state.
        auto nextOwner=requestedOwner;
        DropGpu(); replacements=std::move(next); owner.swap(nextOwner);
        selectedRgb=rgb;
        glow::Configure(rgb,sharedGlow);
        InvalidateBindings();
        return true;
    } catch (const std::exception&) { error="Cannot read blood TGAs"; return false; }
}
void Tick()
{
    if (inBind) return;
    if (owner.empty()) { glow::Tick(); return; }
    TickTimer timer;
    glow::Tick();
    struct SourceCheck {
        const uint8_t* source=nullptr;
        unsigned size=0, width=0, height=0;
        int verdict=-2;
    };
    std::array<SourceCheck,128> checked{};
    size_t checkedCount=0;
    unsigned hashBudget=4;
    auto verify=[&](Identity& entry) {
        entry.state=State::Negative;
        Descriptor current{};
        if (!Snapshot(entry.address,&current,sizeof(current)) || !Same(current,entry.descriptor)) {
            entry.state=State::Tombstone; return;
        }
        int verdict=-2;
        const auto found=std::find_if(checked.begin(),checked.begin()+checkedCount,[&](const SourceCheck& item) {
            return item.source==current.source && item.size==current.size &&
                item.width==current.width && item.height==current.height;
        });
        if (found!=checked.begin()+checkedCount) verdict=found->verdict;
        else {
            std::array<uint8_t,17170> bytes{};
            if (current.size>bytes.size() ||
                !Snapshot(uintptr_t(current.source),bytes.data(),current.size)) {
                entry.state=State::Tombstone; return;
            }
            counters.sourceBytes+=current.size;
            for (const auto& cached : sourceVerdicts) {
                if (cached.width==current.width && cached.height==current.height &&
                    cached.bytes.size()==current.size &&
                    !memcmp(cached.bytes.data(),bytes.data(),current.size)) {
                    verdict=cached.slot; ++counters.byteCacheHits; break;
                }
            }
            if (verdict==-2 && hashBudget) {
                --hashBudget; ++counters.hashes;
                uint8_t digest[32]{};
                if (Hash(bytes.data(),current.size,digest)) {
                    verdict=-1;
                    for (unsigned i=0;i<kTextureCount;++i)
                        if (current.width==sides[i] && current.height==sides[i] &&
                            current.size==sizes[i] && DigestMatches(digest,hashes[i])) {
                            verdict=int(i); break;
                        }
                    auto& cached=sourceVerdicts[verdict>=0 ? size_t(verdict) : kTextureCount+nextVerdict++%32];
                    cached.width=current.width; cached.height=current.height; cached.slot=verdict;
                    cached.bytes.assign(bytes.begin(),bytes.begin()+current.size);
                }
            }
            checked[checkedCount++]={current.source,current.size,current.width,current.height,verdict};
        }
        if (verdict>=0 && PrepareDerived(unsigned(verdict))) {
            entry.slot=unsigned(verdict); entry.state=State::Positive;
        }
        else if (verdict==-2) { entry.state=State::Pending; ++counters.deferred; }
    };
    // No eight-alias cap: revalidate ALL active descriptors, sharing one source
    // read across aliases. Unknown inputs alone consume the bounded hash budget.
    for (size_t n=0;n<identities.size();++n) {
        auto& entry=identities[pendingCursor++%identities.size()];
        if (entry.state==State::Pending || entry.state==State::Revalidate) verify(entry);
    }
    pendingCursor=(pendingCursor+1)%identities.size();
    auto* device=static_cast<IDirect3DDevice9*>(game::Device());
    if (device!=deviceIdentity) { DropGpu(); deviceIdentity=device; }
    if (!device || FAILED(device->TestCooperativeLevel())) return;
    for (auto& item : replacements)
        if (!item.image.pixels.empty() && !Upload(item,device)) break;
}
void BeforeNativeTick()
{
    if (inBind || owner.empty()) return;
    ++nativeEpoch;
    glow::BeforeNativeTick();
    for (auto& entry : identities) {
        if (entry.state==State::Positive || entry.state==State::Negative) entry.state=State::Revalidate;
    }
}
long Bind(void* nativeTexture, BindFn original)
{
    if (!original) return E_FAIL;
    if (inBind || owner.empty() || !nativeTexture) return original(nativeTexture);
    Descriptor current{};
    const auto address=uintptr_t(nativeTexture);
    if (!Snapshot(address,&current,sizeof(current)) || !current.source ||
        !((current.width==32 && current.height==32 && current.size==1050) ||
          (current.width==64 && current.height==64 && current.size==5138) ||
          (current.width==128 && current.height==128 && current.size==17170)))
        return Call(nativeTexture,original);
    uintptr_t binder=0;
    if (!Snapshot(current.vtable,&binder,sizeof(binder)) || binder!=game::A(kTextureBinder))
        return Call(nativeTexture,original);
    // Bounded open addressing with tombstones; eight colliding descriptors must
    // not silently lose the rest of the particles or strand entries after reuse.
    const auto start=(address>>5)%identities.size();
    Identity* available=nullptr;
    Identity* oldest=nullptr;
    for (size_t probe=0;probe<identities.size();++probe) {
        auto& entry=identities[(start+probe)%identities.size()];
        if (entry.state==State::Tombstone) { if (!available) available=&entry; continue; }
        if (entry.state==State::Empty) {
            if (!available) available=&entry;
            break;
        }
        if (entry.lastSeen<nativeEpoch && (!oldest || entry.lastSeen<oldest->lastSeen)) oldest=&entry;
        if (entry.address!=address) continue;
        entry.lastSeen=nativeEpoch;
        if (!Same(entry.descriptor,current)) {
            entry.descriptor=current; entry.state=State::Pending;
            return Call(nativeTexture,original);
        }
        if (entry.state!=State::Positive || !replacements[entry.slot].handle)
            return Call(nativeTexture,original);
        // game::Device() uses VirtualQuery-backed SafeRead: do NOT call it on
        // this draw path. The pointer-sized global snapshot has the same guard.
        IDirect3DDevice9* currentDevice = nullptr;
        if (!Snapshot(game::A(game::off::pD3DDevice),&currentDevice,sizeof(currentDevice)) ||
            currentDevice!=deviceIdentity) return Call(nativeTexture,original);
        // The verified native binder reads only +0xC, synchronously. Nothing is
        // written into native descriptors, sprite registry, meshes or palettes.
        current.handle=replacements[entry.slot].handle;
        ++counters.substitutions;
        ++counters.kinds[entry.slot];
        return Call(&current,original);
    }
    // Keep proven stable resources warm across quiet scenes. Only pressure
    // evicts an older identity; expiry made the first splat of each idle hit
    // flash native green unnecessarily. Identity entries own no GPU resources.
    if (!available) available=oldest;
    if (available) {
        available->address=address; available->descriptor=current; available->state=State::Pending;
        available->lastSeen=nativeEpoch;
    } else ++counters.saturated;
    return Call(nativeTexture,original);
}
void InvalidateBindings() { if (!inBind) identities={}; }
void OnDeviceReset() { if (!inBind) { DropGpu(); glow::OnDeviceReset(); } }
bool SetGlowEnabled(const std::string& requestedOwner,bool enabled) {
    if (inBind || owner!=requestedOwner || owner.empty()) return false;
    glow::Configure(selectedRgb,enabled); return true;
}
void Release(const std::string& requestedOwner) { if (!inBind && requestedOwner==owner) Shutdown(); }
void Shutdown()
{
    if (inBind) return;
    DropGpu(); replacements={}; identities={}; sourceVerdicts={}; owner.clear();
    glow::Shutdown();
    pendingCursor=nextVerdict=0;
    nativeEpoch=1;
    selectedRgb=0xFFFFFF;
}
PerformanceCounters GetPerformanceCounters() { return counters; }
void ResetPerformanceCounters() { counters={}; }
}
