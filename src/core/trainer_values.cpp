#include "trainer_values.h"
#include <cmath>

namespace eth::trainer {
namespace {
// User-supplied I-Ninja.CT. These are profile fields, NOT profile int-table indices.
constexpr Field fields[] = {
    {"health",     0x14, true,  0, 64},
    {"rage",       0x98, false, 0, 65536},
    {"coins",      0x94, false, 0, INT32_MAX},
    {"score",      0xAC, false, 0, INT32_MAX},
    {"multiplier", 0,    false, 1, 10},
    {"lives",      0xB4, false, 0, INT32_MAX},
    {"darts",      0x8C, false, 0, INT32_MAX},
    {"shurikens",  0x84, false, 0, INT32_MAX},
    {"ranx",       0xA0, false, 0, INT32_MAX},
};

bool Pointer(uintptr_t address, uintptr_t offset, uintptr_t& result)
{
    if (!address || (address & 3) || address > UINT32_MAX - 3) return false;
    const uint32_t pointer = *reinterpret_cast<const volatile uint32_t*>(address);
    if (pointer < 0x10000 || (pointer & 3) || pointer > UINT32_MAX - offset - 3)
        return false;
    result = pointer + offset;
    return true;
}

uintptr_t Resolve(const Field& field)
{
    uintptr_t address = 0;
    if (strcmp(field.name, "multiplier")) {
        return Pointer(GameAddr(0x51AA88), field.offset, address) ? address : 0;
    }
    // CE stores offsets outermost first. Dereference from root in reverse order:
    // [exe+ED8A0] +0 -> +4 -> +24 -> +44 -> +0 -> +D8 -> +138.
    address = GameAddr(0x4ED8A0);
    for (uintptr_t offset : {0u, 4u, 0x24u, 0x44u, 0u, 0xD8u, 0x138u})
        if (!Pointer(address, offset, address)) return 0;
    return address;
}

bool ReadAt(const Field& field, uintptr_t address, double& value)
{
    if (!address) return false;
    value = field.floating ? double(*reinterpret_cast<const volatile float*>(address))
                           : double(*reinterpret_cast<const volatile int32_t*>(address));
    return std::isfinite(value);
}
}

const Field* FindField(const char* name)
{
    if (name) for (const auto& field : fields)
        if (!strcmp(name, field.name)) return &field;
    return nullptr;
}

bool Supported()
{
    // Hash once, never on each freeze tick. The executable on disk must match.
    static const bool supported = [] {
        char exe[MAX_PATH]{};
        const DWORD length = GetModuleFileNameA(nullptr, exe, MAX_PATH);
        return length && length < MAX_PATH &&
            Sha256FileHex(exe) == "49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0";
    }();
    return supported;
}

bool Read(const char* name, double& value, uintptr_t& address)
{
    address = 0;
    const auto* field = FindField(name);
    if (!field || !Supported()) return false;
    __try {
        const uintptr_t resolved = Resolve(*field);
        if (!ReadAt(*field, resolved, value)) return false;
        address = resolved;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Write(const char* name, double value, uintptr_t expectedAddress)
{
    const auto* field = FindField(name);
    if (!field || !expectedAddress || !Supported() || !std::isfinite(value) ||
        value < field->minimum || value > field->maximum ||
        (!field->floating && std::floor(value) != value)) return false;
    __try {
        const uintptr_t address = Resolve(*field);
        double current = 0;
        if (address != expectedAddress || !ReadAt(*field, address, current)) return false;
        // Data-only store: no VirtualProtect, no instruction-cache flush and no
        // cached heap pointer. Read-only/unmapped memory fails closed under SEH.
        if (field->floating) *reinterpret_cast<volatile float*>(address) = float(value);
        else *reinterpret_cast<volatile int32_t*>(address) = int32_t(value);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
