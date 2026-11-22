#include "cosmetic_blood_fx.h"
#include <limits>
#include <cstring>

namespace eth::cosmetic::blood::fx {
namespace {
struct Header {
    uint32_t vtable, width, height, count, records;
};
struct Texture {
    uint32_t vtable, width, height, handle, source, bytes, flags, reserved;
};
static_assert(sizeof(Header) == 20 && sizeof(Texture) == 32);
bool Fits(uintptr_t address, size_t size)
{
    return address && size <= std::numeric_limits<uintptr_t>::max() - address;
}
struct Scope {
    const PreparedAnimated* prepared;
    Read read;
    void* context;
};
thread_local const Scope* activeScope = nullptr;
bool SameChild(const PreparedAnimated& p, size_t index, Read read, void* context)
{
    std::array<uint32_t, 8> descriptor{};
    const auto address = p.children.items[index].descriptor;
    uint32_t binder = 0, getter = 0;
    return read(address, descriptor.data(), sizeof(descriptor), context) &&
        descriptor == p.descriptors[index] &&
        Fits(descriptor[0], 0x1C) &&
        read(descriptor[0], &binder, 4, context) &&
        read(uintptr_t(descriptor[0])+0x18, &getter, 4, context) &&
        binder == p.ordinaryBinder && getter == p.gpuGetter;
}
bool Current(uintptr_t parent, const PreparedAnimated& p, Read read, void* context)
{
    if (!read || parent != p.parent || !p.parent ||
        !p.children.count || p.children.count > 8 ||
        !p.header[3] || p.header[3] > 8) return false;
    std::array<uint32_t, 5> header{};
    uint32_t binder = 0;
    if (!read(parent, header.data(), sizeof(header), context) || header != p.header ||
        !read(header[0], &binder, 4, context) ||
        binder != p.animatedBinder)
        return false;
    for (uint32_t stage = 0; stage < header[3]; ++stage) {
        std::array<uint8_t, 0x40> record{};
        if (!read(uintptr_t(header[4])+size_t(stage)*0x40,
                  record.data(), record.size(), context) || record != p.records[stage])
            return false;
    }
    for (size_t i = 0; i < p.children.count; ++i)
        if (!p.replacementHandles[i] || !SameChild(p, i, read, context))
            return false;
    return true;
}
}
bool CollectAnimated(uintptr_t descriptor, uintptr_t animatedBinder,
                     uintptr_t ordinaryBinder, Read read, void* context,
                     Children& output)
{
    if (!read || !animatedBinder || !ordinaryBinder ||
        !Fits(descriptor, sizeof(Header))) return false;
    Header header{};
    uint32_t binder = 0;
    if (!read(descriptor, &header, sizeof(header), context) ||
        !Fits(header.vtable, 4) || !read(header.vtable, &binder, 4, context) ||
        binder != animatedBinder || !header.count || header.count > 8 ||
        !Fits(header.records, size_t(header.count) * 0x40)) return false;
    Children next{};
    for (uint32_t stage = 0; stage < header.count; ++stage) {
        uint32_t child = 0;
        if (!read(uintptr_t(header.records) + size_t(stage) * 0x40,
                  &child, 4, context)) return false;
        if (!child) continue; // Native animated binder also skips null stages.
        Texture texture{};
        if (!Fits(child, sizeof(texture)) ||
            !read(child, &texture, sizeof(texture), context) ||
            !Fits(texture.vtable, 4) ||
            !read(texture.vtable, &binder, 4, context) ||
            binder != ordinaryBinder || !texture.handle || !texture.source ||
            !texture.width || !texture.height || !texture.bytes)
            return false;
        next.items[next.count++] = {child, texture.handle, texture.source,
            texture.width, texture.height, texture.bytes, stage};
    }
    output = next;
    return true;
}
bool PrepareAnimated(uintptr_t parent, uintptr_t animatedBinder,
                     uintptr_t ordinaryBinder, uintptr_t gpuGetter,
                     Read read, void* readContext, Authorize authorize,
                     void* authorizeContext, PreparedAnimated& output)
{
    if (!authorize || !gpuGetter || !read || !animatedBinder || !ordinaryBinder)
        return false;
    PreparedAnimated next{};
    next.parent = parent;
    next.animatedBinder = animatedBinder;
    next.ordinaryBinder = ordinaryBinder;
    next.gpuGetter = gpuGetter;
    if (!CollectAnimated(parent, animatedBinder, ordinaryBinder,
                         read, readContext, next.children) || !next.children.count ||
        !read(parent, next.header.data(), sizeof(next.header), readContext) ||
        !next.header[3] || next.header[3] > 8 ||
        !Fits(next.header[4], size_t(next.header[3])*0x40))
        return false;
    for (uint32_t stage = 0; stage < next.header[3]; ++stage)
        if (!read(uintptr_t(next.header[4])+size_t(stage)*0x40,
                  next.records[stage].data(), 0x40, readContext)) return false;
    for (size_t i = 0; i < next.children.count; ++i)
        if (!read(next.children.items[i].descriptor, next.descriptors[i].data(),
                  sizeof(next.descriptors[i]), readContext) ||
            !SameChild(next, i, read, readContext)) return false;
    if (!authorize(parent, next.children, next.replacementHandles, authorizeContext) ||
        !Current(parent, next, read, readContext)) return false;
    output = next;
    return true;
}
void RunAnimated(uintptr_t parent, const PreparedAnimated* prepared,
                 Read read, void* readContext, Continue continuation,
                 void* continuationContext)
{
    if (!continuation) return;
    Scope scope{prepared && Current(parent, *prepared, read, readContext) ?
                    prepared : nullptr, read, readContext};
    const Scope* previous = activeScope;
    activeScope = &scope;
#ifdef _MSC_VER
    __try { continuation(continuationContext); }
    __finally { activeScope = previous; }
#else
    try { continuation(continuationContext); }
    catch (...) { activeScope = previous; throw; }
    activeScope = previous;
#endif
}
void RunMesh(uintptr_t mesh, const PreparedAnimated* prepared,
             Read read, void* readContext, Continue continuation,
             void* continuationContext)
{
    uint32_t parent = 0;
    const bool valid = prepared && read && Fits(mesh, 0x14) &&
        read(mesh+0x10, &parent, 4, readContext) && parent == prepared->parent;
    RunAnimated(parent, valid ? prepared : nullptr, read, readContext,
                continuation, continuationContext);
}
const uint32_t* ScopedGpuHandle(uintptr_t childDescriptor)
{
    const auto* scope = activeScope;
    if (!scope || !scope->prepared || !scope->read) return nullptr;
    const auto& p = *scope->prepared;
    for (size_t i = 0; i < p.children.count; ++i)
        if (p.children.items[i].descriptor == childDescriptor)
            return SameChild(p, i, scope->read, scope->context) ?
                &p.replacementHandles[i] : nullptr;
    return nullptr;
}
} // namespace eth::cosmetic::blood::fx
