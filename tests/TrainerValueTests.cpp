// Production data resolver/writer on synthetic Win32 memory, never the game.
#include "../src/core/trainer_values.cpp"
#include <limits>
namespace {
uint32_t profileRoot = 0, multiplierRoot = 0;
bool validHash = true;
int hashCalls = 0;
alignas(4) uint8_t profile[0x200]{}, replacement[0x200]{}, nodes[7][0x200]{};
void Put(void* address, uint32_t value) { memcpy(address, &value, 4); }
}
namespace eth {
uintptr_t GameAddr(uintptr_t va) {
    return va == 0x51AA88 ? uintptr_t(&profileRoot) :
           va == 0x4ED8A0 ? uintptr_t(&multiplierRoot) : 0;
}
std::string Sha256FileHex(const std::string&) {
    ++hashCalls;
    return validHash ? "49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0" : "";
}
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char**)
{
    using namespace eth::trainer;
    static_assert(sizeof(void*) == 4);
    double value = 0;
    uintptr_t address = 0;
    validHash = argc == 1;
    if (!validHash) {
        CHECK(!Read("health", value, address));
        CHECK(!Write("coins", 100, uintptr_t(profile) + 0x94));
        CHECK(hashCalls == 1);
        std::puts("PASS unsupported executable refuses reads/writes");
        return 0;
    }
    profileRoot = uintptr_t(profile);
    float fractional = 3.5f;
    memcpy(profile + 0x14, &fractional, 4);
    CHECK(Read("health", value, address) && value == 3.5);
    CHECK(Write("health", 4.25, address));
    CHECK(Read("health", value, address) && value == 4.25);
    fractional = std::numeric_limits<float>::quiet_NaN();
    memcpy(profile + 0x14, &fractional, 4);
    CHECK(!Read("health", value, address) && !address);
    CHECK(!Write("health", 4, uintptr_t(profile) + 0x14));
    for (const auto& field : fields) {
        if (!strcmp(field.name, "multiplier")) continue;
        memset(profile, 0x5A, sizeof(profile));
        if (field.floating) {
            float health = 4;
            memcpy(profile + field.offset, &health, 4);
        } else Put(profile + field.offset, 20);
        CHECK(Read(field.name, value, address));
        CHECK(value == (field.floating ? 4 : 20) && address == uintptr_t(profile) + field.offset);
        uint8_t before[sizeof(profile)];
        memcpy(before, profile, sizeof(profile));
        const double target = (std::min)(7.0, field.maximum);
        CHECK(Write(field.name, target, address));
        CHECK(Read(field.name, value, address) && value == target);
        memcpy(before + field.offset, profile + field.offset, 4);
        CHECK(!memcmp(before, profile, sizeof(profile))); // exactly four bytes
        CHECK(!Write(field.name, -1, address));
        CHECK(!Write(field.name, field.maximum + 1, address));
        CHECK(!Write(field.name, 3, address + 4));
        CHECK(!Write(field.name, 3, 0));
        CHECK(!Write(field.name, std::numeric_limits<double>::infinity(), address));
        CHECK(!Write(field.name, std::numeric_limits<double>::quiet_NaN(), address));
        if (!field.floating) CHECK(!Write(field.name, 3.5, address));
    }
    CHECK(!Read("not_a_field", value, address) && !address);
    CHECK(!Write("unknown", 0, uintptr_t(profile)));
    CHECK(!Read(nullptr, value, address));
    CHECK(Read("health", value, address));
    const auto stale = address;
    profileRoot = uintptr_t(replacement);
    CHECK(!Write("health", 5, stale)); // new allocation refuses old identity token
    profileRoot = 0;
    CHECK(!Read("health", value, address) && !address);
    profileRoot = 1;
    CHECK(!Read("health", value, address));
    profileRoot = 0xFFFFFFFC;
    CHECK(!Read("health", value, address));
    profileRoot = uintptr_t(profile);
    const uintptr_t offsets[] = {0, 4, 0x24, 0x44, 0, 0xD8, 0x138};
    multiplierRoot = uintptr_t(nodes[0]);
    for (int i = 0; i < 6; ++i) Put(nodes[i] + offsets[i], uintptr_t(nodes[i+1]));
    Put(nodes[6] + offsets[6], 3);
    CHECK(Read("multiplier", value, address) && value == 3);
    CHECK(address == uintptr_t(nodes[6]) + 0x138); // CE order, final offset not dereferenced
    CHECK(Write("multiplier", 10, address));
    CHECK(!Write("multiplier", 0, address) && !Write("multiplier", 11, address));
    for (int i = 0; i < 6; ++i) {
        Put(nodes[i] + offsets[i], 0);
        CHECK(!Read("multiplier", value, address));
        CHECK(!Write("multiplier", 2, uintptr_t(nodes[6]) + 0x138));
        Put(nodes[i] + offsets[i], 0xFFFFFFFC);
        CHECK(!Read("multiplier", value, address)); // overflow or inaccessible top of address space
        Put(nodes[i] + offsets[i], uintptr_t(nodes[i+1]));
    }
    multiplierRoot = 0;
    CHECK(!Read("multiplier", value, address));
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(page);
    profileRoot = uintptr_t(page);
    CHECK(Read("coins", value, address));
    DWORD old = 0;
    CHECK(VirtualProtect(page, 4096, PAGE_READONLY, &old));
    CHECK(!Write("coins", 100, address)); // no protection bypass
    CHECK(VirtualFree(page, 0, MEM_RELEASE));
    CHECK(!Read("coins", value, address)); // freed allocation
    CHECK(hashCalls == 1); // no filesystem or memory-query work per tick
    std::puts("PASS trainer values: CE/equipment fields, CE order, ranges, identity, SEH and read-only refusal");
}
