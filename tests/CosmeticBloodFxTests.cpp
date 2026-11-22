#include "../src/core/cosmetic_blood_fx.h"
#include <cassert>
#include <cstring>
#include <vector>
#ifdef _MSC_VER
#include <windows.h>
#endif

namespace fx = eth::cosmetic::blood::fx;
namespace {
bool Read(uintptr_t address, void* output, size_t bytes, void* context)
{
    auto& memory = *static_cast<std::vector<uint8_t>*>(context);
    if (!address || address > memory.size() || bytes > memory.size()-address)
        return false;
    std::memcpy(output, memory.data()+address, bytes);
    return true;
}
void Put(std::vector<uint8_t>& memory, size_t address, uint32_t value)
{
    std::memcpy(memory.data()+address, &value, 4);
}
bool Authorize(uintptr_t parent, const fx::Children& children,
               std::array<uint32_t, 8>& handles, void* context)
{
    if (!*static_cast<bool*>(context) || parent != 64 || children.count != 2)
        return false;
    handles[0] = 900;
    handles[1] = 901;
    return true;
}
struct Draw {
    std::vector<uint8_t>* memory;
    const fx::PreparedAnimated* prepared;
    int calls = 0;
    bool expect = true;
    bool nested = false;
    bool fault = false;
    bool seh = false;
};
void Continue(void* context)
{
    auto& draw = *static_cast<Draw*>(context);
    ++draw.calls;
    const auto* handle = fx::ScopedGpuHandle(512);
    assert(bool(handle) == draw.expect);
    if (handle) assert(*handle == 900);
    assert(!fx::ScopedGpuHandle(999));
    if (draw.nested) {
        Draw inner{draw.memory, nullptr};
        inner.expect = false;
        fx::RunAnimated(65, nullptr, Read, draw.memory, Continue, &inner);
        assert(inner.calls == 1);
        assert(fx::ScopedGpuHandle(512) && *fx::ScopedGpuHandle(512) == 900);
    }
    if (draw.fault) throw 5;
#ifdef _MSC_VER
    if (draw.seh) RaiseException(0xE0424242, 0, 0, nullptr);
#endif
}
#ifdef _MSC_VER
bool ExpectSeh(Draw* draw)
{
    __try {
        fx::RunAnimated(64, draw->prepared, Read, draw->memory, Continue, draw);
    }
    __except (GetExceptionCode() == 0xE0424242 ?
              EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}
#endif
}
int main()
{
    std::vector<uint8_t> memory(2048);
    Put(memory, 64, 128); Put(memory, 128, fx::kAnimatedBinder);
    Put(memory, 76, 2); Put(memory, 80, 256);
    Put(memory, 256, 512); Put(memory, 320, 640);
    Put(memory, 160, fx::kOrdinaryBinder);
    Put(memory, 184, fx::kOrdinaryGpuGetter);
    for (auto child : {512u, 640u}) {
        Put(memory, child, 160); Put(memory, child+4, 128);
        Put(memory, child+8, 32); Put(memory, child+12, 777);
        Put(memory, child+16, 1024); Put(memory, child+20, 5138);
    }
    fx::Children result{};
    auto collect = [&] {
        return fx::CollectAnimated(64, fx::kAnimatedBinder, fx::kOrdinaryBinder,
                                  Read, &memory, result);
    };
    assert(collect() && result.count == 2);
    assert(result.items[1].descriptor == 640 && result.items[1].stage == 1);
    assert(result.items[0].gpuHandle == 777 && result.items[0].sourceBytes == 5138);
    Put(memory, 640, 192); Put(memory, 192, 0x1234);
    assert(!collect() && result.count == 2); // Entire result stays unchanged.
    Put(memory, 640, 160); Put(memory, 76, 9);
    assert(!collect());
    Put(memory, 76, 2); Put(memory, 320, 0);
    assert(collect() && result.count == 1);
    Put(memory, 80, 2040);
    assert(!collect() && result.count == 1);
    assert(!fx::CollectAnimated(64, 0, fx::kOrdinaryBinder, Read, &memory, result));
    Put(memory, 80, 256); Put(memory, 320, 640);
    fx::PreparedAnimated prepared{};
    bool authorized = false;
    auto prepare = [&] {
        return fx::PrepareAnimated(64, fx::kAnimatedBinder, fx::kOrdinaryBinder,
            fx::kOrdinaryGpuGetter, Read, &memory, Authorize, &authorized, prepared);
    };
    assert(!prepare() && prepared.parent == 0);
    authorized = true;
    assert(prepare());
    assert(!fx::ScopedGpuHandle(512));
    const auto originalMemory = memory;
    Draw draw{&memory, &prepared};
    draw.nested = true;
    fx::RunAnimated(64, &prepared, Read, &memory, Continue, &draw);
    assert(draw.calls == 1 && memory == originalMemory && !fx::ScopedGpuHandle(512));
    draw.nested = false; draw.expect = false;
    Put(memory, 524, 778); // Native handle changed: no stale SDK substitution.
    fx::RunAnimated(64, &prepared, Read, &memory, Continue, &draw);
    assert(draw.calls == 2);
    Put(memory, 524, 777); draw.expect = true; draw.fault = true;
    try { fx::RunAnimated(64, &prepared, Read, &memory, Continue, &draw); }
    catch (int value) { assert(value == 5); }
    assert(draw.calls == 3 && !fx::ScopedGpuHandle(512));
#ifdef _MSC_VER
    draw.fault = false; draw.seh = true;
    assert(ExpectSeh(&draw));
    assert(draw.calls == 4 && !fx::ScopedGpuHandle(512));
#endif
    Put(memory, 184, 0x1234);
    assert(!prepare()); // Unknown ordinary getter never called or accepted.
    assert(prepared.gpuGetter == fx::kOrdinaryGpuGetter);
    Put(memory, 184, fx::kOrdinaryGpuGetter);
    Put(memory, 1536+0x10, 64);
    draw.expect = true; draw.fault = draw.seh = false;
    const auto beforeMesh = memory;
    const int calls = draw.calls;
    fx::RunMesh(1536, &prepared, Read, &memory, Continue, &draw);
    assert(draw.calls == calls+1 && memory == beforeMesh && !fx::ScopedGpuHandle(512));
    Put(memory, 1536+0x10, 65); draw.expect = false;
    fx::RunMesh(1536, &prepared, Read, &memory, Continue, &draw);
    assert(draw.calls == calls+2 && !fx::ScopedGpuHandle(512));
    return 0;
}
