#include "cosmetic_sash.h"
#include "game.h"
#include "trainer_values.h"

namespace eth::cosmetic::sash {
namespace {
struct Binding {
    game::StratIdentity ninja{}, head{};
    uint32_t registers = 0, object = 0, handle = 0;
};
struct Saved {
    Binding binding{};
    uint32_t original = 0, applied = 0;
    bool active = false;
};
Saved saved;

bool ReadData(uintptr_t address, void* out, size_t size)
{
    __try {
        memcpy(out,reinterpret_cast<const void*>(address),size);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool ReadValue(uintptr_t address, T& out, bool fast)
{
    return fast ? ReadData(address,&out,sizeof(out)) : SafeRead(address,out);
}
bool RestoreWord(uint32_t object, uint32_t expected, uint32_t original)
{
    // Data only: never VirtualProtect an allocation or make readonly memory
    // writable. Comparison and store stay in one guarded, tick-thread scope.
    __try {
        auto* color = reinterpret_cast<volatile uint32_t*>(object+0x2898);
        if (*color != expected) return false;
        *color = original;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Same(const game::StratIdentity& a, const game::StratIdentity& b)
{
    return a.addr && a.addr == b.addr && a.pClass == b.pClass &&
        a.selfHandle && a.selfHandle == b.selfHandle;
}
bool Same(const Binding& a, const Binding& b)
{
    return Same(a.ninja,b.ninja) && Same(a.head,b.head) &&
        a.registers == b.registers && a.object == b.object && a.handle == b.handle;
}
bool Available()
{
    // The setter is thiscall(object,packedRGB), ret 4, forces opaque alpha.
    const uint8_t expected[] = {
        0x8B,0x44,0x24,0x04,0x0D,0,0,0,0xFF,
        0x89,0x81,0x98,0x28,0,0,0xC2,0x04,0
    };
    uint8_t actual[sizeof(expected)]{};
    uint32_t wrapper = 0;
    return trainer::Supported() &&
        SafeRead(game::A(0x4C7E70 + 0x1D8*4),wrapper) &&
        wrapper == game::A(0x41DB90) &&
        SafeReadBytes(game::A(0x45A900),actual,sizeof(actual)) &&
        !memcmp(actual,expected,sizeof(expected));
}
bool Live(const game::StratView& row, game::StratIdentity& identity)
{
    return game::ReadStratIdentity(row.addr,identity) &&
        identity.addr == row.addr && identity.selfHandle &&
        identity.selfHandle == row.selfHandle &&
        identity.pClass == uint32_t(uintptr_t(row.pClass));
}
bool Effect(Binding& b, bool fast = false)
{
    uint32_t handle = 0, registryHandle = 0, object = 0, vtable = 0;
    uint32_t creator = 0, selfHandle = 0;
    if (!ReadValue(b.head.addr + 0x1B4,b.registers,fast) || !b.registers ||
        b.registers > UINT32_MAX - 12 ||
        !ReadValue(b.registers + 2*4,handle,fast) || !handle) return false;
    const auto slot = game::A(0x519A60) + (handle & 0x1FF)*8;
    if (!ReadValue(slot,registryHandle,fast) || registryHandle != handle ||
        !ReadValue(slot+4,object,fast) || !object || object > UINT32_MAX - 0x295C ||
        !ReadValue(object,vtable,fast) || vtable != game::A(0x4C8CF8) ||
        !ReadValue(object+0x0C,creator,fast) || creator != b.head.addr ||
        !ReadValue(object+0x10,selfHandle,fast) || selfHandle != handle) return false;
    b.object = object;
    b.handle = handle;
    return true;
}
bool LiveKnown(const game::StratIdentity& identity, const char* exactName,
    size_t nameBytes, game::StratRole role)
{
    if (!identity.addr || identity.addr > UINT32_MAX-0x300 ||
        !identity.pClass || identity.pClass > UINT32_MAX-8 ||
        !identity.selfHandle || identity.role != role) return false;
    uint32_t pair[2]{}, self = 0, pClass = 0, namePointer = 0;
    char name[10]{}; // "NinjaHead" including terminator; never truncate
    const auto slot = game::A(0x51019C) + (identity.selfHandle & 0x1FF)*8;
    return nameBytes <= sizeof(name) &&
        ReadData(slot,pair,sizeof(pair)) &&
        pair[0] == identity.selfHandle && pair[1] == identity.addr &&
        ReadData(identity.addr+0x1A0,&self,4) && self == identity.selfHandle &&
        ReadData(identity.addr+0x1A8,&pClass,4) && pClass == identity.pClass &&
        ReadData(pClass+4,&namePointer,4) && namePointer &&
        namePointer <= UINT32_MAX-nameBytes &&
        ReadData(namePointer,name,nameBytes) && !memcmp(name,exactName,nameBytes);
}
bool FindKnown(const game::StratIdentity& ninja,
    const std::vector<game::StratIdentity>& heads, Binding& out)
{
    // Membership is supplied by the caller's fresh complete snapshot. We do
    // not treat that evidence as permission to trust cached identity fields.
    if (heads.size() > 16 ||
        !LiveKnown(ninja,"Ninja",sizeof("Ninja"),game::StratRole::Ninja)) return false;
    unsigned matches = 0;
    for (const auto& head : heads) {
        if (!head.ownerFieldKnown || head.rawOwnerHandle != ninja.selfHandle ||
            head.ownerAddr != ninja.addr ||
            !LiveKnown(head,"NinjaHead",sizeof("NinjaHead"),game::StratRole::Attachment))
            continue;
        uint32_t owner = 0;
        if (!ReadData(head.addr+0x238,&owner,4) || owner != ninja.selfHandle) continue;
        Binding candidate{};
        candidate.ninja = ninja; candidate.head = head;
        if (!Effect(candidate,true)) continue;
        out = candidate;
        if (++matches > 1) return false;
    }
    return matches == 1;
}
bool Find(const game::StratIdentity& ninja, Binding& out)
{
    if (!ninja.addr || !ninja.selfHandle || ninja.role != game::StratRole::Ninja)
        return false;
    bool complete = false, foundNinja = false;
    const auto rows = game::SnapshotStrats(8192,&complete);
    if (!complete) return false;
    for (const auto& row : rows) {
        if (row.addr != ninja.addr || strcmp(row.className,"Ninja")) continue;
        game::StratIdentity current{};
        if (!Live(row,current) || !Same(current,ninja)) return false;
        foundNinja = true;
    }
    if (!foundNinja) return false;
    unsigned matches = 0;
    for (const auto& row : rows) {
        if (strcmp(row.className,"NinjaHead")) continue;
        Binding candidate{};
        candidate.ninja = ninja;
        if (!Live(row,candidate.head) || !candidate.head.ownerFieldKnown ||
            candidate.head.rawOwnerHandle != ninja.selfHandle ||
            candidate.head.ownerAddr != ninja.addr) continue;
        // Never fall back to scanning all effects, material/resource pointers,
        // a same-slot handle, or an arbitrary object owned by the Ninja.
        if (!Effect(candidate)) continue;
        out = candidate;
        if (++matches > 1) return false; // ambiguous heads: no guessed winner
    }
    return matches == 1;
}
bool Tint(uint32_t object, uint32_t rgb)
{
    __try {
        using Fn = void(__thiscall*)(void*,uint32_t);
        reinterpret_cast<Fn>(game::A(0x45A900))(reinterpret_cast<void*>(object),rgb);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
Result ApplyBinding(const Binding& current, uint32_t rgb, bool fast)
{
    if (saved.active && !Same(saved.binding,current)) saved = {};
    uint32_t color = 0;
    if (!ReadValue(current.object+0x2898,color,fast)) return Result::Waiting;
    const uint32_t applied = rgb | 0xFF000000;
    // A normal tier/event refresh owns a newly observed baseline.
    if (!saved.active || color != saved.applied) {
        saved.binding = current;
        saved.original = color;
    }
    if (color != applied && !Tint(current.object,rgb)) {
        saved = {};
        return Result::Refused;
    }
    saved.applied = applied;
    saved.active = true;
    return Result::Applied;
}
}

void Invalidate() { saved = {}; }

void Restore()
{
    if (!saved.active) return;
    Binding current{};
    if (Available() && Find(saved.binding.ninja,current) &&
        Same(current,saved.binding)) {
        // The native setter forces alpha FF; restore the exact saved dword,
        // including alpha, only on the revalidated private allocation.
        RestoreWord(current.object,saved.applied,saved.original);
    }
    Invalidate(); // even when stale/unreadable/reparented/externally recolored
}

Result Apply(const game::StratIdentity& ninja, uint32_t rgb)
{
    if (rgb > 0xFFFFFF || !Available()) return Result::Refused;
    if (saved.active && !Same(saved.binding.ninja,ninja)) Restore();
    Binding current{};
    if (!Find(ninja,current)) {
        // Keep the old baseline for transient unreadability, but do not touch it.
        return Result::Waiting;
    }
    return ApplyBinding(current,rgb,false);
}

Result ApplyKnown(const game::StratIdentity& ninja, uint32_t rgb,
    const std::vector<game::StratIdentity>& verifiedHeads)
{
    if (rgb > 0xFFFFFF || !Available()) return Result::Refused;
    // No implicit audit restore here: even a root change must stay O(heads).
    // Caller restores before changing selection, or deliberately invalidates.
    if (saved.active && !Same(saved.binding.ninja,ninja)) Invalidate();
    Binding current{};
    if (!FindKnown(ninja,verifiedHeads,current)) return Result::Waiting;
    return ApplyBinding(current,rgb,true);
}

void RestoreKnown(const game::StratIdentity& ninja,
    const std::vector<game::StratIdentity>& verifiedHeads)
{
    if (saved.active && Same(saved.binding.ninja,ninja) && Available()) {
        Binding current{};
        if (FindKnown(ninja,verifiedHeads,current) && Same(current,saved.binding))
            RestoreWord(current.object,saved.applied,saved.original);
    }
    Invalidate();
}
}
