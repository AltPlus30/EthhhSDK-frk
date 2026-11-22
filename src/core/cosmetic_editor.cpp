#include "cosmetic_editor.h"
#include "game.h"
#include "trainer_values.h"
#include "ethhhpatch_runtime.h"
#include "cosmetic_sash.h"

namespace eth::cosmetic {
namespace {
struct Entry {
    uintptr_t instance = 0, lookup = 0, address = 0;
    std::string name;
    uint32_t color = 0;
    game::StratIdentity actor{};
    std::string part = "body";
    uintptr_t names = 0, pointers = 0;
    uint16_t count = 0, index = 0;
};
struct Saved {
    Entry entry;
    uint32_t original = 0, applied = 0;
};
std::mutex mutex;
std::string owner;
std::map<std::string, uint32_t> desired;
std::vector<Saved> saved;
game::StratIdentity target{};
View view;
bool inspectRequested = false;
bool workRequested = false;
ULONGLONG nextInspection = 0;
std::vector<Entry> cachedEntries;
std::atomic<bool> faulted{false};
game::StratIdentity reacquired{};
ULONGLONG nextDiscovery = 0;
game::StratIdentity headOwner{};
std::vector<game::StratIdentity> headCandidates;
ULONGLONG nextHeadDiscovery = 0;
bool textureWanted = false, textureReady = false;
bool textureInstallAttempted = false;
bool secondaryEnabled = false;
struct RenderActor {
    game::StratIdentity identity{};
    uint32_t mode = 0, instances[2]{}, heads[2][2]{}, flags[2]{};
    bool head = false;
};
std::vector<RenderActor> renderActors;
std::vector<RenderActor> materialActors;
std::vector<textures::Binding> renderBindings;
bool texturePrepared = false;
std::vector<game::StratIdentity> lastHeads;

// Allocation-free SDK-only elapsed timing, not CPU utilization or GPU time.
// The scope is inside the editor lock and outside original native tick/draw.
struct TickTimer {
    View& output;
    LARGE_INTEGER started{};
    bool full = false, valid = false;
    static LONGLONG Frequency() noexcept
    {
        static const LONGLONG frequency=[]() noexcept -> LONGLONG {
            LARGE_INTEGER value{};
            return QueryPerformanceFrequency(&value) && value.QuadPart>0 ? value.QuadPart : 0;
        }();
        return frequency;
    }
    explicit TickTimer(View& result) noexcept : output(result)
    {
        valid=Frequency()>0 && QueryPerformanceCounter(&started) && started.QuadPart>=0;
    }
    ~TickTimer() noexcept
    {
        LARGE_INTEGER ended{};
        if (!valid || !QueryPerformanceCounter(&ended) || ended.QuadPart<started.QuadPart) return;
        const double elapsed=double(ended.QuadPart-started.QuadPart)*1000000.0/double(Frequency());
        output.lastTickUs=elapsed;
        if (elapsed>output.maxTickUs) output.maxTickUs=elapsed;
        if (full) output.lastFullTickUs=elapsed;
    }
};

bool ReadFast(uintptr_t address, void* output, size_t size) noexcept
{
    if (!address || address > UINT32_MAX-size) return false;
    __try { memcpy(output,reinterpret_cast<const void*>(address),size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool ReadFast(uintptr_t address, T& output)
{ return ReadFast(address,&output,sizeof(output)); }

bool Same(const game::StratIdentity& a, const game::StratIdentity& b)
{
    return a.addr && a.addr == b.addr && a.pClass == b.pClass &&
        a.selfHandle == b.selfHandle;
}
bool SameAppearance(const game::StratIdentity& a, const game::StratIdentity& b)
{
    return Same(a, b) && a.model == b.model && a.animation == b.animation;
}
void InvalidateRender()
{
    texturePrepared=false;
    renderActors.clear();
    renderBindings.clear();
    textures::InvalidatePrepared();
}
bool SameRender(const RenderActor& a, const RenderActor& b)
{
    return SameAppearance(a.identity,b.identity) && a.head==b.head && a.mode==b.mode &&
        !memcmp(a.instances,b.instances,sizeof(a.instances)) && !memcmp(a.heads,b.heads,sizeof(a.heads)) &&
        !memcmp(a.flags,b.flags,sizeof(a.flags));
}
bool CaptureRender(const game::StratIdentity& actor, bool head, RenderActor& key, bool requirePrivate=true)
{
    key.identity=actor; key.head=head;
    if (!SafeRead(actor.addr+0x144,key.mode) ||
        !SafeReadBytes(actor.addr+0x128,key.instances,sizeof(key.instances))) return false;
    if (key.instances[0]>key.instances[1]) std::swap(key.instances[0],key.instances[1]);
    for (unsigned slot=0;slot<2;++slot) if (key.instances[slot]) {
        if (!SafeRead(key.instances[slot]+0x34,key.flags[slot]) || (requirePrivate && !(key.flags[slot]&8)) ||
            !SafeReadBytes(key.instances[slot],key.heads[slot],sizeof(key.heads[slot]))) return false;
    }
    return true;
}
// Render-only validation of identities already admitted by the tick's full
// registry/live-list/class checks. No traversal, allocation or VirtualQuery.
bool RenderLive(const RenderActor& key, const game::StratIdentity& ninja) noexcept
{
    __try {
        const uintptr_t actor=key.identity.addr;
        const auto read=[](uintptr_t address) { return *reinterpret_cast<const uint32_t*>(address); };
        if (read(actor+0x1A0)!=key.identity.selfHandle || read(actor+0x1A8)!=key.identity.pClass ||
            read(actor+0x268)!=key.identity.model || read(actor+0x26C)!=key.identity.animation ||
            read(actor+0x144)!=key.mode) return false;
        const uintptr_t registry=game::A(game::off::StratMgrObject)+(key.identity.selfHandle&0x1FF)*8;
        if (read(registry)!=key.identity.selfHandle || read(registry+4)!=actor ||
            (key.head && read(actor+0x238)!=ninja.selfHandle)) return false;
        uint32_t first=read(actor+0x128), second=read(actor+0x12C);
        if (first>second) std::swap(first,second);
        if (first!=key.instances[0] || second!=key.instances[1]) return false;
        for (unsigned slot=0;slot<2;++slot) if (key.instances[slot]) {
            const auto instance=key.instances[slot];
            if (read(instance+0x34)!=key.flags[slot] || read(instance)!=key.heads[slot][0] ||
                read(instance+4)!=key.heads[slot][1]) return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool RefreshCached()
{
    if (materialActors.empty() || cachedEntries.empty()) return false;
    for (const auto& actor : materialActors) if (!RenderLive(actor,target)) return false;
    for (auto& entry : cachedEntries) {
        uintptr_t lookup=0, names=0, pointers=0, address=0;
        uint16_t count=0;
        char name[32]{};
        if (!ReadFast(entry.instance+0x3C,lookup) || lookup!=entry.lookup ||
            !ReadFast(lookup,count) || count!=entry.count ||
            !ReadFast(lookup+4,pointers) || pointers!=entry.pointers ||
            !ReadFast(lookup+0x10,names) || names!=entry.names ||
            !ReadFast(pointers+entry.index*4,address) || address!=entry.address ||
            !ReadFast(names+entry.index*32,name,sizeof(name)) ||
            memcmp(name,entry.name.c_str(),entry.name.size()+1) ||
            !ReadFast(address+0x14,entry.color)) return false;
    }
    return true;
}
void PrepareRender(const game::StratIdentity& ninja, const std::vector<game::StratIdentity>& heads,
    bool discover=true)
{
    if (!textureWanted || !textureReady) {
        if (texturePrepared || !renderActors.empty()) InvalidateRender();
        return;
    }
    if (!discover && renderActors.size()==heads.size()+1 && !renderBindings.empty()) {
        bool live=SameAppearance(renderActors[0].identity,ninja);
        for (size_t i=0;i<heads.size();++i)
            live &= SameAppearance(renderActors[i+1].identity,heads[i]);
        for (const auto& actor : renderActors) if (!RenderLive(actor,ninja)) { live=false; break; }
        if (live && textures::PrepareBatch(renderBindings)) {
            texturePrepared=true;
            return;
        }
    }
    std::vector<RenderActor> actors;
    RenderActor body;
    if (!CaptureRender(ninja,false,body)) { InvalidateRender(); return; }
    actors.push_back(body);
    for (const auto& head : heads) {
        RenderActor key;
        if (!CaptureRender(head,true,key)) { InvalidateRender(); return; }
        actors.push_back(key);
    }
    bool same=actors.size()==renderActors.size();
    if (same) for (size_t i=0;i<actors.size();++i) if (!SameRender(actors[i],renderActors[i])) { same=false; break; }
    if (!same) InvalidateRender();
    renderActors=std::move(actors);
    renderBindings.clear();
    renderBindings.reserve(128);
    for (const auto& actor : renderActors) for (unsigned slot=0;slot<2;++slot) {
        if (!actor.instances[slot]) continue;
        for (uint32_t mesh : actor.heads[slot]) for (unsigned n=0;mesh && n<512;++n) {
            if (mesh>UINT32_MAX-0x48) { InvalidateRender(); return; }
            uintptr_t nativeTexture=0; uint32_t dimensions[2]{}, next=0;
            if (!SafeRead(mesh+0x10,nativeTexture) || !SafeRead(mesh+0x44,next)) { InvalidateRender(); return; }
            if (nativeTexture && SafeReadBytes(nativeTexture+4,dimensions,sizeof(dimensions))) {
                textures::Slot type;
                bool candidate=true;
                if (dimensions[0]==256 && dimensions[1]==256) type=textures::Slot::Blue;
                else if (dimensions[0]==256 && dimensions[1]==128 && secondaryEnabled) type=textures::Slot::Secondary;
                else if (dimensions[0]==128 && dimensions[1]==128 && actor.head) type=textures::Slot::Head;
                else candidate=false;
                if (candidate && std::none_of(renderBindings.begin(),renderBindings.end(),
                    [mesh](const textures::Binding& entry) { return entry.mesh==mesh; })) {
                    if (renderBindings.size()==128) { InvalidateRender(); return; }
                    renderBindings.push_back({actor.instances[slot],mesh,nativeTexture,type,0xFFFFFF});
                }
            }
            mesh=next;
        }
    }
    texturePrepared=textures::PrepareBatch(renderBindings);
    view.textureStatus=texturePrepared ? "Texture bindings prepared on tick; draw passes use the guarded cache." :
        "Texture preparation refused: source/binder/private-mesh/device validation did not match.";
}
bool Current(game::StratIdentity& identity, bool follow = false)
{
    auto& tracker = game::PlayerTracker::Get();
    if (tracker.Pinned() && tracker.Valid() &&
        game::ReadStratIdentity(tracker.Addr(), identity) &&
        identity.role == game::StratRole::Ninja) {
        reacquired = {}; nextDiscovery = 0;
        return true;
    }
    if (!follow) return false;
    // Cosmetic consent is session-local and independent of movement/HL pinning.
    // Never change PlayerTracker's selection or activate a movement adapter.
    const auto now = GetTickCount64();
    if (now >= nextDiscovery) {
        nextDiscovery = now + 250;
        reacquired = {};
        bool complete = false;
        const auto snapshot = game::SnapshotStrats(8192, &complete);
        const auto exact = complete ? game::UniqueNinja(snapshot) : 0;
        if (exact) game::ReadStratIdentity(exact, reacquired);
    }
    if (!reacquired.addr || !game::ReadStratIdentity(reacquired.addr, identity) ||
        !Same(reacquired, identity) || identity.role != game::StratRole::Ninja) return false;
    return true;
}
std::string Key(const std::string& part, const std::string& name) { return part + "/" + name; }
uint32_t TextureRgb(uint32_t native)
{
    return ((native & 255) << 16) | (native & 0xFF00) | ((native >> 16) & 255);
}
std::vector<game::StratIdentity> LinkedHeads(const game::StratIdentity& ninja)
{
    const auto now = GetTickCount64();
    if (!Same(headOwner,ninja) || now >= nextHeadDiscovery) {
        nextHeadDiscovery = now + 250;
        headOwner = ninja;
        headCandidates.clear();
        bool complete = false;
        const auto snapshot = game::SnapshotStrats(8192,&complete);
        if (complete) for (const auto& strat : snapshot) {
            if (strcmp(strat.className,"NinjaHead")) continue;
            game::StratIdentity head{};
            if (game::ReadStratIdentity(strat.addr,head) &&
                head.pClass == uint32_t(uintptr_t(strat.pClass)) && head.selfHandle == strat.selfHandle &&
                head.ownerFieldKnown && head.rawOwnerHandle == ninja.selfHandle && head.ownerAddr == ninja.addr)
                headCandidates.push_back(head);
            if (headCandidates.size()>16) { headCandidates.clear(); break; }
        }
    }
    std::vector<game::StratIdentity> result;
    for (const auto& expected : headCandidates) {
        game::StratIdentity head{};
        if (game::ReadStratIdentity(expected.addr,head) && Same(expected,head) &&
            head.ownerFieldKnown && head.rawOwnerHandle == ninja.selfHandle && head.ownerAddr == ninja.addr)
            result.push_back(head);
    }
    return result;
}
const uint32_t* Wanted(const Entry& entry)
{
    for (const auto& key : {Key(entry.part,entry.name),Key("all",entry.name)}) {
        const auto found = desired.find(key);
        if (found != desired.end()) return &found->second;
    }
    const char* group = nullptr;
    if (entry.name == "sash white" || entry.name == "sash knot white") group = "belt";
    else if (entry.name == "blue" || entry.name == "blue_double") group = "costume";
    else if (entry.part == "head" && entry.name == "head") group = "head";
    if (!group) return nullptr;
    const auto found = desired.find(Key("group",group));
    return found == desired.end() ? nullptr : &found->second;
}
bool Available()
{
    uintptr_t entry = 0;
    const uint8_t expected[] = {0x83,0xEC,0x48,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x5C};
    const uint8_t copyBytes[] = {0x55,0x8B,0x6C,0x24,0x0C,0x8B,0x45,0x34,0xA8,0x04};
    const uint8_t releaseBytes[] = {0x56,0x8B,0x74,0x24,0x08,0x85,0xF6,0x74,0x3D};
    uint8_t bytes[sizeof(expected)]{};
    uint8_t copied[sizeof(copyBytes)]{}, released[sizeof(releaseBytes)]{};
    return !faulted && trainer::Supported() &&
        SafeRead(game::A(0x4C7E70 + 0x184 * 4), entry) &&
        entry == game::A(0x458330) &&
        SafeReadBytes(entry, bytes, sizeof(bytes)) &&
        !memcmp(bytes, expected, sizeof(bytes)) &&
        SafeReadBytes(game::A(0x454240), copied, sizeof(copied)) &&
        !memcmp(copied, copyBytes, sizeof(copied)) &&
        SafeReadBytes(game::A(0x454150), released, sizeof(released)) &&
        !memcmp(released, releaseBytes, sizeof(released));
}
bool Gather(uintptr_t strat, std::vector<Entry>& entries)
{
    entries.clear();
    for (uintptr_t slot : {0x128u, 0x12Cu}) {
        uintptr_t instance = 0, lookup = 0, names = 0, pointers = 0;
        uint16_t count = 0;
        if (!SafeRead(strat + slot, instance)) return false;
        if (!instance) continue;
        if (!SafeRead(instance + 0x3C, lookup) || !lookup ||
            !SafeRead(lookup, count) || count > 512 ||
            !SafeRead(lookup + 4, pointers) || !SafeRead(lookup + 0x10, names) ||
            (count && (!names || !pointers))) return false;
        for (unsigned i = 0; i < count; ++i) {
            char name[33]{};
            uintptr_t address = 0;
            uint32_t color = 0;
            if (!SafeReadBytes(names + i * 32, name, 32) ||
                !SafeRead(pointers + i * 4, address) || !address ||
                !SafeRead(address + 0x14, color)) return false;
            size_t length = 0;
            while (length < 32 && name[length]) {
                if (static_cast<unsigned char>(name[length]) < 32 ||
                    static_cast<unsigned char>(name[length]) > 126) return false;
                ++length;
            }
            // Only names accepted by the native bounded string wrapper.
            if (!length || length == 32) continue;
            entries.push_back({instance, lookup, address, name, color});
            auto& entry=entries.back();
            entry.names=names; entry.pointers=pointers; entry.count=count; entry.index=uint16_t(i);
        }
    }
    return !entries.empty();
}
bool Prepare(uintptr_t strat)
{
    // Reproduce 0x457BF0's copy/release recipe, but check EACH slot's flag.
    // The shipped helper tests the primary slot even on its second iteration.
    // We refuse to tint the secondary resource until it is independently private.
    using Copy = void*(__cdecl*)(void*, void*, int);
    using Release = void(__cdecl*)(void*);
    __try {
        if (!*reinterpret_cast<const uintptr_t*>(strat + 0x128)) return false;
        const uintptr_t cls = *reinterpret_cast<const uintptr_t*>(strat + 0x1A8);
        if (!cls) return false;
        const uintptr_t package = *reinterpret_cast<const uintptr_t*>(cls);
        if (!package) return false;
        void* heap = *reinterpret_cast<void* const*>(package);
        if (!heap) return false;
        for (uintptr_t offset : {0x128u, 0x12Cu}) {
            auto* slot = reinterpret_cast<void**>(strat + offset);
            void* instance = *slot;
            if (!instance || (*reinterpret_cast<const uint32_t*>(
                    reinterpret_cast<uintptr_t>(instance) + 0x34) & 8)) continue;
            void* copy = reinterpret_cast<Copy>(game::A(0x454240))(heap, instance, 1);
            if (!copy) return false;
            *slot = copy;
            reinterpret_cast<Release>(game::A(0x454150))(instance);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Private(const Entry& entry)
{
    uint32_t flags = 0;
    return SafeRead(entry.instance + 0x34, flags) && (flags & 8);
}
void Tag(std::vector<Entry>& entries, const game::StratIdentity& actor, const std::string& part);
bool GatherPrepared(const game::StratIdentity& actor, const std::string& part,
    std::vector<Entry>& entries, bool apply)
{
    if (!Gather(actor.addr,entries)) return false;
    Tag(entries,actor,part);
    if (!apply) return true;
    bool needsCopy = false;
    std::vector<uintptr_t> shared;
    for (const auto& entry : entries) if (!Private(entry)) {
        shared.push_back(entry.address);
        needsCopy |= (textureWanted && textureReady) || Wanted(entry) != nullptr;
    }
    if (!needsCopy) return true;
    if (!Available() || !Prepare(actor.addr) || !Gather(actor.addr,entries)) {
        faulted = true;
        view.status = "Native instance preparation failed; tint disabled until restart.";
        return false;
    }
    Tag(entries,actor,part);
    for (const auto& entry : entries) if (((textureWanted && textureReady) || Wanted(entry)) &&
        std::find(shared.begin(),shared.end(),entry.address) != shared.end()) {
        faulted = true;
        view.status = "Copy retained a shared material entry; tint refused until restart.";
        return false;
    }
    return true;
}
bool Store(uintptr_t entry, uint32_t expected, uint32_t rgb)
{
    // Writable data only: no VirtualProtect or instruction cache operations.
    __try {
        auto* color = reinterpret_cast<volatile uint32_t*>(entry + 0x14);
        if (*color != expected) return false;
        *color = (expected & 0xFF000000) | (rgb & 0xFFFFFF);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Matches(const Entry& a, const Entry& b)
{
    return a.instance == b.instance && a.lookup == b.lookup &&
        a.address == b.address && a.name == b.name && a.part == b.part && Same(a.actor,b.actor);
}
void Tag(std::vector<Entry>& entries, const game::StratIdentity& actor, const std::string& part)
{
    for (auto& entry : entries) { entry.actor = actor; entry.part = part; }
}
void RestoreOne(const Saved& old)
{
    game::StratIdentity current{};
    std::vector<Entry> entries;
    if (!game::ReadStratIdentity(old.entry.actor.addr,current) ||
        !Same(current,old.entry.actor) || current.model != old.entry.actor.model ||
        !Gather(current.addr,entries)) return;
    Tag(entries,current,old.entry.part);
    for (const auto& entry : entries) {
        if (Matches(entry,old.entry) && Private(entry) &&
            (entry.color & 0xFFFFFF) == old.applied)
            Store(entry.address,entry.color,old.original);
    }
}
void RestoreAll()
{
    for (const auto& old : saved) RestoreOne(old);
    saved.clear();
}
}

bool Set(const std::string& requestedOwner, const std::vector<Material>& colors)
{
    if (requestedOwner.empty() || colors.size() > 128 || !Available()) return false;
    std::map<std::string, uint32_t> next;
    for (const auto& color : colors) {
        if (color.name.empty() || color.name.size() > 31 || color.rgb > 0xFFFFFF) return false;
        for (unsigned char c : color.name) if (c < 32 || c > 126) return false;
        if (color.part != "body" && color.part != "head" && color.part != "all" && color.part != "group")
            return false;
        if (color.part == "group" && color.name != "belt" && color.name != "costume" && color.name != "head")
            return false;
        next[Key(color.part,color.name)] = color.rgb;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner.empty() && owner != requestedOwner) return false;
    game::StratIdentity identity{};
    if (!Current(identity,owner == requestedOwner)) return false;
    owner = next.empty() && !textureWanted ? "" : requestedOwner;
    // Actual actor transitions and restoration are handled only on the tick thread.
    desired = std::move(next);
    inspectRequested = true;
    workRequested = true;
    view.status = "Queued. RGB tint multiplies the original texture; gameplay stats are unchanged.";
    return true;
}
View Inspect()
{
    std::lock_guard<std::mutex> lock(mutex);
    inspectRequested = true;
    return view;
}
bool LoadTextures(const std::string& requestedOwner, const std::string& directory,
    const std::vector<std::string>& paths, std::string& error, bool secondary)
{
    std::lock_guard<std::mutex> lock(mutex);
    game::StratIdentity current{};
    if (requestedOwner.empty() || !Available() || (!owner.empty() && owner != requestedOwner) ||
        !Current(current,owner == requestedOwner)) {
        error = "Load a level and pin the exact Ninja before arming a texture pack.";
        return false;
    }
    if (!textures::Load(requestedOwner,directory,paths,error)) return false;
    InvalidateRender();
    owner = requestedOwner;
    textureWanted = true; textureReady = false;
    secondaryEnabled = secondary;
    textureInstallAttempted = false;
    view.textureStatus = "Pack validated; native draw hooks/private instances will prepare on the next tick.";
    inspectRequested = true;
    workRequested = true;
    return true;
}
void ClearTextures(const std::string& requestedOwner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (owner != requestedOwner) return;
    textures::Release(owner);
    InvalidateRender();
    textureWanted = textureReady = false;
    textureInstallAttempted = false;
    view.textures = false;
    view.textureStatus = "Original textures restored; material tint requests are unchanged.";
    if (desired.empty()) owner.clear();
    workRequested = true;
}
bool DrawTextures(textures::DrawContinuation continuation, void* context)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!textureWanted || !textureReady || !texturePrepared || owner.empty() || faulted ||
        renderActors.empty() || !target.addr) return false;
    for (const auto& actor : renderActors) if (!RenderLive(actor,target)) return false;
    const bool drawn = textures::DrawPrepared(continuation,context);
    if (drawn) {
        ++view.texturePasses;
    } else {
        ++view.textureRefusals;
    }
    return drawn;
}
void Release(const std::string& requestedOwner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (owner != requestedOwner) return;
    textures::Release(owner);
    InvalidateRender();
    textureWanted = textureReady = false;
    view.textures = false;
    desired.clear();
    owner.clear();
    workRequested = true;
    view.active = false;
    view.textureStatus = "Texture pack stopped; native textures in use.";
    view.status = "Restoration queued for the next game tick.";
}
void BeforeNativeTick()
{
    std::lock_guard<std::mutex> lock(mutex);
    // Native simulation can unload/reuse resource memory, including identical
    // descriptor headers. No source verdict spans that unobserved boundary.
    if (textureWanted) {
        texturePrepared=false;
        textures::SuspendPrepared();
    }
}
void OnNativeTickCompleted()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!inspectRequested && owner.empty() && saved.empty() && !target.addr) return;
    const auto now=GetTickCount64();
    const bool full=workRequested || now>=nextInspection;
    // An inspection reader is not a command. Polling the GUI must not turn
    // optional discovery/material formatting into per-frame native traversal.
    if (!full && owner.empty() && saved.empty() && !target.addr) return;
    TickTimer timer(view);
    const bool fast=!full && target.addr && !owner.empty() && trainer::Supported() && RefreshCached();
    if (fast) {
        ++view.fastTicks;
        const auto belt=desired.find(Key("group","belt"));
        if (belt!=desired.end()) sash::ApplyKnown(target,belt->second,lastHeads);
        // Native scripts may overwrite colors every tick. Reassert immediately
        // and adopt their newest value as the restoration baseline.
        // saved is a subset of the last full cachedEntries snapshot. The
        // complete table proof above establishes those same named addresses;
        // no quadratic saved x material search is needed on intervening ticks.
        for (auto& old : saved) {
            uint32_t color=0;
            if (!ReadFast(old.entry.address+0x14,color)) { workRequested=true; continue; }
            const uint32_t rgb=color&0xFFFFFF;
            if (rgb!=old.applied) {
                if (Store(old.entry.address,color,old.applied)) old.original=rgb;
            }
        }
        if (textureWanted && textureReady) PrepareRender(target,lastHeads,false);
        return;
    }
    timer.full=true;
    ++view.fullInspections;
    nextInspection=now+150;
    workRequested=false;
    inspectRequested = false;
    game::StratIdentity current{};
    if (textureWanted && !textureReady && !textureInstallAttempted) {
        textureInstallAttempted = true;
        textureReady = patch::runtime::InstallCosmeticTextureHooks();
        if (!textureReady) view.textureStatus="Native texture draw-hook verification failed; no GPU override active.";
    }
    const bool requested = !owner.empty() && (!desired.empty() || textureWanted);
    const bool haveCurrent = Current(current,requested);
    if (target.addr && (!haveCurrent || !SameAppearance(target, current) || !requested)) {
        if (haveCurrent) sash::RestoreKnown(target,lastHeads);
        else sash::Invalidate();
        lastHeads.clear();
        InvalidateRender();
        RestoreAll();
        target = {};
        cachedEntries.clear(); materialActors.clear();
        view.active = false;
    }
    view.materials.clear();
    view.heads = 0;
    view.textures = textureWanted && textureReady;
    view.waiting = requested && !haveCurrent;
    if (!haveCurrent || !trainer::Supported()) {
        cachedEntries.clear(); materialActors.clear();
        sash::Invalidate();
        InvalidateRender();
        view.status = requested ? "Palette retained; waiting for a uniquely identified Ninja after loading." :
            "Load a level and pin the exact Ninja (Auto+ or Manual).";
        return;
    }
    if (requested) target = current;
    const auto heads=LinkedHeads(current);
    lastHeads=heads;
    const auto belt=desired.find(Key("group","belt"));
    if (requested && belt!=desired.end()) {
        const auto result=sash::ApplyKnown(current,belt->second,heads);
        view.sashStatus=result==sash::Result::Applied ? "Wavy sash override active." :
            result==sash::Result::Waiting ? "Waiting for the linked wavy sash." :
            "Wavy sash refused: native signature/ownership did not match.";
    } else {
        sash::RestoreKnown(current,heads);
        view.sashStatus="Native wavy sash color.";
    }
    std::vector<Entry> entries;
    if (!GatherPrepared(current,"body",entries,requested)) {
        cachedEntries.clear(); materialActors.clear();
        view.active = false;
        view.waiting = requested && !faulted;
        if (faulted) {
            textures::Release(owner); textureWanted=textureReady=false;
            desired.clear(); owner.clear();
        }
        else view.status = "Render entries are not available yet; palette retained.";
        return;
    }
    for (const auto& head : heads) {
        std::vector<Entry> headEntries;
        if (GatherPrepared(head,"head",headEntries,requested)) {
            ++view.heads;
            entries.insert(entries.end(),headEntries.begin(),headEntries.end());
        }
        if (faulted) {
            textures::Release(owner); textureWanted=textureReady=false;
            desired.clear(); owner.clear(); view.active=false;
            return;
        }
    }
    if (requested) {
        for (const auto& old : saved) {
            const bool present = std::any_of(entries.begin(),entries.end(),
                [&](const Entry& entry) { return Matches(entry,old.entry); });
            if (!present) RestoreOne(old);
        }
        std::vector<Saved> next;
        next.reserve(entries.size());
        size_t matched = 0, applied = 0;
        for (auto& entry : entries) {
            const auto wanted = Wanted(entry);
            const Saved* previous = nullptr;
            for (const auto& old : saved) if (Matches(entry, old.entry)) { previous = &old; break; }
            if (!wanted) {
                if (previous && Private(entry) && (entry.color & 0xFFFFFF) == previous->applied &&
                    Store(entry.address, entry.color, previous->original))
                    entry.color = (entry.color & 0xFF000000) | previous->original;
                continue;
            }
            ++matched;
            if (!Private(entry)) continue; // no writes to a shared resource
            const uint32_t rgb = entry.color & 0xFFFFFF;
            // Adopt new native baseline when a script refresh overwrites our tint.
            const uint32_t original = previous && rgb == previous->applied ? previous->original : rgb;
            if (rgb == *wanted || Store(entry.address, entry.color, *wanted)) {
                next.push_back({entry, original, *wanted});
                entry.color = (entry.color & 0xFF000000) | *wanted;
                ++applied;
            } else if (previous) {
                // Retain restoration metadata if a requested update failed.
                next.push_back(*previous);
            }
        }
        saved = std::move(next);
        view.active = !saved.empty() || (textureWanted && textureReady);
        view.status = matched && applied < matched ?
            "Partial/refused tint: some matching entries were not private or writable. Stop before retrying." :
            view.active ? "Cosmetic tint active. Save/profile fields are untouched." :
            "No requested named entries matched this model.";
    }
    std::map<std::string, Material> unique;
    for (const auto& entry : entries)
        unique.emplace(Key(entry.part,entry.name),Material{entry.name,entry.color & 0xFFFFFF,entry.part});
    for (const auto& entry : unique) view.materials.push_back(entry.second);
    if (!requested) view.status = "No palette armed. Apply explicitly; requests survive level changes until stopped.";
    if (faulted) view.status = "Native preparation fault/refusal latched; restart required. No automatic retry.";
    cachedEntries=entries;
    materialActors.clear();
    RenderActor body;
    if (requested && CaptureRender(current,false,body,false)) {
        materialActors.push_back(body);
        for (const auto& head : heads) {
            RenderActor key;
            if (!CaptureRender(head,true,key,false)) { materialActors.clear(); break; }
            materialActors.push_back(key);
        }
    }
    PrepareRender(current,heads);
}
void Shutdown()
{
    // Shutdown may run off the engine thread: never dereference saved heap pointers.
    std::lock_guard<std::mutex> lock(mutex);
    desired.clear(); saved.clear(); owner.clear(); target = {}; view = {};
    textures::Shutdown();
    InvalidateRender();
    sash::Invalidate(); lastHeads.clear();
    textureWanted = textureReady = false;
    textureInstallAttempted = false;
    reacquired = {}; nextDiscovery = 0;
    headOwner = {}; headCandidates.clear(); nextHeadDiscovery = 0;
    inspectRequested = false;
    workRequested=false; nextInspection=0;
    cachedEntries.clear(); materialActors.clear();
}
}
