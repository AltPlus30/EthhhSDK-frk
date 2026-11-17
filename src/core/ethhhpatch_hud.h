#pragma once
#include "strat_identity.h"
#include <cmath>
#include <cstring>

namespace eth::patch::hud {

enum class Kind : uint8_t { Inventory, Health, Rage, Unrelated };

struct Position { uint32_t handle = 0; float x = 0, y = 0; };
static_assert(sizeof(Position) == 12, "native handle/X/Y arguments");
struct Root {
    uint32_t self = 0, object = 0, cls = 0;
    Position native{}, installed{};
    Kind kind = Kind::Inventory;
};

// Panel's four inventory parents use centered, 640-wide authored coordinates.
// Keep their anchors (including slide endpoints) at the same screen percentage;
// child offsets and sizes still use the existing uniform HUD scale.
inline float AnchorX(float x, float aspect)
{
    if (!std::isfinite(x) || !std::isfinite(aspect) || aspect <= 4.0f / 3.0f || aspect > 8)
        return x;
    const float result = x * (aspect / (4.0f / 3.0f));
    return std::isfinite(result) ? result : x;
}

// Map an authored point in the group to a screen percentage, while retaining
// child geometry/size. HP uses its left edge; inventory/rage use their origin.
inline float AnchorGroupX(float x, float localAnchor, float aspect)
{
    if (!std::isfinite(x) || !std::isfinite(localAnchor) || !std::isfinite(aspect) ||
        aspect <= 4.f/3 || aspect > 8) return x;
    const float result=(x+localAnchor)*(aspect/(4.f/3))-localAnchor;
    return std::isfinite(result) ? result : x;
}

template<class Read>
bool HealthLeftAnchor(const Root& root, float& anchor, Read read)
{
    uint32_t sprite=0;
    float geometry[8]{}, pivot=0;
    if (!root.object || root.object>UINT32_MAX-0x18 ||
        !read(root.object+0x14,&sprite,4) || !sprite || sprite>UINT32_MAX-0xAC ||
        !read(sprite+0x0C,geometry,sizeof(geometry)) || !read(sprite+0xA8,&pivot,4)) return false;
    // Native sprites store half-extents in sixteenths of authored pixels.
    // Rotation/cropping would need a different bounding-box proof: refuse it.
    if (!std::isfinite(geometry[0]) || geometry[0]<=0 || geometry[0]>65536 ||
        !std::isfinite(geometry[2]) || geometry[2]<=0 || geometry[2]>16 ||
        geometry[6]!=1 || geometry[7]!=0 || pivot!=0) return false;
    anchor=-geometry[0]*geometry[2]/16;
    return std::isfinite(anchor);
}

template<class Read>
bool Identity(uint32_t self, uint32_t handle, uintptr_t table, uintptr_t spriteVtable,
              Root& out, Read read)
{
    if (!self || self > UINT32_MAX - 0x1AC) return false;
    uint32_t cls = 0;
    char name[game::kClassNameCapacity]{};
    if (!read(self + 0x1A8, &cls, 4) || !game::ReadClassName(cls, name, read) ||
        strcmp(name, "Panel")) return false;
    const uintptr_t object = game::ResolveOwnerHandle(table, handle, read);
    uint32_t header[9]{};
    if (!object || object > UINT32_MAX - sizeof(header) ||
        !read(object, header, sizeof(header)) || header[0] != spriteVtable ||
        header[3] != self || header[4] != handle || header[6] != 0) return false;
    out.self = self; out.object = uint32_t(object); out.cls = cls;
    out.installed.handle = handle;
    memcpy(&out.installed.x, &header[7], 8);
    return std::isfinite(out.installed.x) && std::isfinite(out.installed.y);
}

// The VM has already relocated the native pointer and ESI points AFTER it.
// Match verified gameplay-parent updates, not every Panel sprite or an X range.
template<class Read>
Kind PositionCall(uintptr_t nextPC, uintptr_t native, Read read)
{
    uint32_t words[9]{};
    if (nextPC<sizeof(words) || !read(nextPC-sizeof(words),words,sizeof(words)) ||
        words[8]!=uint32_t(native)) return Kind::Unrelated;
    const uint32_t inventory[] = {0x15741172,0x15751672,0x15761872,0x80FF7400};
    const uint32_t inventoryTransition[] = {0x15811180,0x15821680,0x15831880,0x80FF8100};
    const uint32_t health[] = {0x0A790000,0x0A7A000C,0x5A7B0000,0xC3198000,0x80FF7900};
    const uint32_t rage[] = {0x0A560001,0x37595508,0x0D573959,0x5D595500,
        0x42200000,0x5F585900,0x43000000,0x80FF5600};
    if (!memcmp(words+4,inventory,sizeof(inventory)) ||
        !memcmp(words+4,inventoryTransition,sizeof(inventoryTransition))) return Kind::Inventory;
    if (!memcmp(words+3,health,sizeof(health))) return Kind::Health;
    if (!memcmp(words,rage,sizeof(rage))) return Kind::Rage;
    return Kind::Unrelated;
}

template<class Read>
bool InventoryCall(uintptr_t nextPC, uintptr_t native, Read read)
{
    return PositionCall(nextPC,native,read)==Kind::Inventory;
}

class Roots {
public:
    template<class Read>
    static bool AttachedClockOwner(uint32_t self, Read read)
    {
        uint32_t cls=0;
        char name[game::kClassNameCapacity]{};
        return self && self<=UINT32_MAX-0x1AC && read(self+0x1A8,&cls,4) &&
            game::ReadClassName(cls,name,read) &&
            (!strcmp(name,"MissionTimer") || !strcmp(name,"MissionCounter"));
    }
    template<class Read>
    bool Contains(uintptr_t object, uintptr_t table, uintptr_t vtable, Read read) const
    {
        // Text and images share this native UI-object type. Follow only a
        // bounded, generation-checked parent chain back to a tracked root.
        for (unsigned depth=0; object && depth<4; ++depth) {
            uint32_t header[9]{};
            if (object > UINT32_MAX-sizeof(header) || !read(object, header, sizeof(header)) ||
                header[0] != vtable || game::ResolveOwnerHandle(table, header[4], read) != object)
                return false;
            for (const auto& entry : roots_) {
                if (entry.object != object || entry.self != header[3]) continue;
                Root check{};
                return Identity(entry.self, entry.installed.handle, table, vtable, check, read) &&
                    check.object == entry.object && check.cls == entry.cls &&
                    !memcmp(&check.installed, &entry.installed, sizeof(Position));
            }
            const uintptr_t parent = header[6];
            uint32_t parentOwner = 0;
            if (!parent || parent > UINT32_MAX-16 || !read(parent+12, &parentOwner, 4)) return false;
            if (parentOwner != header[3]) {
                // MissionTimer/Counter own their numeric UI objects, but attach
                // their group to Panel's HP parent. Do not treat that supported
                // ownership boundary as unrelated text; don't admit arbitrary
                // foreign owners or other tracked HUD/menu parents either.
                bool healthParent=false;
                for (const auto& entry : roots_)
                    if (entry.kind==Kind::Health && entry.object==parent && entry.self==parentOwner)
                        healthParent=true;
                if (!healthParent || !AttachedClockOwner(header[3],read)) return false;
            }
            object = parent;
        }
        return false;
    }
    // Independent capacities: four inventory parents, one HP group, one rage
    // group. Adding a HUD kind cannot steal an inventory slot.
    template<class Read>
    Root* Slot(const Root& root, uintptr_t table, uintptr_t vtable, Read read)
    {
        Root* free = nullptr;
        if (root.kind!=Kind::Inventory && root.kind!=Kind::Health && root.kind!=Kind::Rage) return nullptr;
        const unsigned begin=root.kind==Kind::Inventory ? 0 : root.kind==Kind::Health ? 4 : 5;
        const unsigned end=root.kind==Kind::Inventory ? 4 : begin+1;
        for (unsigned i=begin;i<end;++i) {
            auto& entry=roots_[i];
            Root check{};
            if (!entry.self || !Identity(entry.self, entry.installed.handle, table, vtable, check, read) ||
                check.object != entry.object || check.cls != entry.cls) {
                if (!free) free = &entry;
            } else if (entry.object == root.object && entry.self == root.self) return &entry;
        }
        return free;
    }
    template<class Read, class Move>
    void Restore(uintptr_t table, uintptr_t vtable, Read read, Move move)
    {
        for (auto& entry : roots_) {
            Root check{};
            if (entry.self && Identity(entry.self, entry.installed.handle, table, vtable, check, read) &&
                check.object == entry.object && check.cls == entry.cls &&
                !memcmp(&check.installed, &entry.installed, sizeof(Position)))
                move(entry.self, entry.native);
            entry = {};
        }
    }
private:
    Root roots_[6]{};
};

struct FontClip {
    uint32_t prefix[10]{};
    int32_t left = 0, right = 0, top = 0, bottom = 0;
};
static_assert(sizeof(FontClip) == 0x38, "native font clip offsets");

inline bool ExpandClip(FontClip& font, float aspect)
{
    if (!std::isfinite(aspect) || aspect <= 4.f/3 || aspect > 8 ||
        font.left != 0 || font.right != 640) return false;
    // Clip is in authored 0..640 coordinates, BEFORE the projection squeeze.
    // Preserve Y and refuse custom subrects rather than erasing menu clipping.
    const float half = 320.f * (aspect / (4.f/3));
    font.left = int32_t(std::floor(320.f-half));
    font.right = int32_t(std::ceil(320.f+half));
    return true;
}

struct ClipBounds {
    int32_t left = 0, right = 640;
    bool valid = false;
    void Configure(float aspect) {
        FontClip font{}; font.right=640;
        valid=ExpandClip(font,aspect);
        left=font.left; right=font.right;
    }
    bool Apply(FontClip& font) const {
        if (!valid || font.left!=0 || font.right!=640) return false;
        font.left=left; font.right=right; return true;
    }
};

struct QueueSnapshot {
    struct Font { uint32_t address = 0, head = 0; } fonts[32]{};
    unsigned count = 0;
    bool valid = false;
    template<class Read>
    void Capture(uintptr_t list, Read read)
    {
        *this = {};
        uint32_t font = 0;
        if (!read(list, &font, 4)) return;
        while (font && count < 32) {
            for (unsigned i=0;i<count;++i) if (fonts[i].address == font) return;
            uint32_t head = 0, next = 0;
            if (font > UINT32_MAX-0x28 || !read(font, &head, 4) || !read(font+0x24, &next, 4)) return;
            fonts[count++] = {font,head};
            font = next;
        }
        valid = font == 0;
    }
};

class QueuedText {
public:
    void Clear() { count_ = 0; memset(index_,0,sizeof(index_)); }
    template<class Read>
    void Collect(const QueueSnapshot& before, Read read)
    {
        if (!before.valid) return;
        const unsigned rollback = count_;
        for (unsigned i=0;i<before.count;++i) {
            uint32_t node = 0;
            if (!read(before.fonts[i].address, &node, 4)) { Rollback(rollback); return; }
            unsigned walked = 0;
            while (node != before.fonts[i].head) {
                Entry entry{};
                entry.font = before.fonts[i].address; entry.node = node;
                if (!node || node > UINT32_MAX-sizeof(entry.header) || walked++ >= 32 || count_ >= 128 ||
                    !read(node, entry.header, sizeof(entry.header))) { Rollback(rollback); return; }
                entries_[count_] = entry;
                Insert(count_++);
                node = entry.header[0];
            }
        }
    }
    template<class Read>
    bool Matches(uintptr_t font, uintptr_t node, Read read) const
    {
        unsigned slot=Hash(font,node);
        for (unsigned probe=0;probe<256;++probe,slot=(slot+1)&255) {
            if (!index_[slot]) return false; // Unrelated text performs NO native read.
            const auto& entry=entries_[index_[slot]-1];
            if (entry.font!=font || entry.node!=node) continue;
            uint32_t check[6]{};
            return read(node, check, sizeof(check)) && !memcmp(check,entry.header,sizeof(check));
        }
        return false;
    }
private:
    static unsigned Hash(uintptr_t font, uintptr_t node) {
        uint32_t key=uint32_t(node)*2654435761u ^ uint32_t(font)*2246822519u;
        key^=key>>16; return key&255;
    }
    void Insert(unsigned i) {
        unsigned slot=Hash(entries_[i].font,entries_[i].node);
        for(unsigned probe=0;probe<256;++probe,slot=(slot+1)&255) {
            if(!index_[slot] || (entries_[index_[slot]-1].font==entries_[i].font &&
                entries_[index_[slot]-1].node==entries_[i].node)) {
                index_[slot]=uint16_t(i+1); return;
            }
        }
    }
    void Rollback(unsigned count) {
        count_=count; memset(index_,0,sizeof(index_));
        for(unsigned i=0;i<count_;++i) Insert(i);
    }
    // Queue header: next, X/Y, scale X/Y, packed colour. Bind provenance to
    // fresh bytes as well as the address; native tick reset removes old leases.
    struct Entry { uint32_t font = 0, node = 0, header[6]{}; };
    Entry entries_[128]{};
    uint16_t index_[256]{};
    unsigned count_ = 0;
};

} // namespace eth::patch::hud
