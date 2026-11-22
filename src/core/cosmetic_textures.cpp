#include "cosmetic_textures.h"
#include "button_prompt_pixels.h"
#include "game.h"
#include "util.h"
#include <filesystem>
#include <fstream>
#include <bcrypt.h>
#include <algorithm>

namespace eth::cosmetic::textures {
bool Decode(const uint8_t* p, size_t size, Image& output)
{
    if (!p || size < 18 || size > 18 * 1024 * 1024) return false;
    Image next;
    if (p[2] != 10) {
        if (!prompts::DecodeTga(p, size, next.width, next.height, next.pixels)) return false;
    } else {
        auto word = [p](size_t n) { return unsigned(p[n]) | unsigned(p[n + 1]) << 8; };
        next.width = word(12); next.height = word(14);
        if (p[1] || (p[16] != 24 && p[16] != 32) || (p[17] & 0xc0) ||
            !next.width || !next.height || next.width > 2048 || next.height > 2048) return false;
        const size_t total = size_t(next.width) * next.height, stride = p[16] / 8;
        size_t pos = 18 + p[0], written = 0;
        next.pixels.resize(total);
        while (written < total) {
            if (pos >= size) return false;
            const unsigned packet = p[pos++], count = (packet & 127) + 1;
            const size_t bytes = (packet & 128) ? stride : count * stride;
            if (count > total - written || bytes > size - pos) return false;
            for (unsigned i = 0; i < count; ++i) {
                const auto* q = p + pos + ((packet & 128) ? 0 : i * stride);
                const unsigned x = unsigned(written % next.width), y = unsigned(written / next.width);
                const unsigned dx = (p[17] & 16) ? next.width - 1 - x : x;
                const unsigned dy = (p[17] & 32) ? y : next.height - 1 - y;
                next.pixels[size_t(dy) * next.width + dx] = uint32_t(q[0]) |
                    uint32_t(q[1]) << 8 | uint32_t(q[2]) << 16 |
                    uint32_t(stride == 4 ? q[3] : 255) << 24;
                ++written;
            }
            pos += bytes;
        }
    }
    output = std::move(next);
    return true;
}

namespace {
struct Texture {
    uintptr_t vtable;
    unsigned width, height;
    IDirect3DTexture9* handle;
    const uint8_t* source;
    unsigned size, flags, reserved;
};
static_assert(sizeof(Texture) == 32);
static_assert(offsetof(Texture, handle) == 0xC && offsetof(Texture, source) == 0x10);
struct Replacement {
    Image image;
    std::vector<uint8_t> source; // canonical type-2, top-left BGRA TGA
    IDirect3DTexture9* handle = nullptr;
    uint32_t rgb = 0xFFFFFFFF;
};
struct Borrowed {
    uintptr_t mesh = 0;
    Texture* original = nullptr;
    Texture replacement{};
};
std::array<Replacement, 3> replacements;
std::recursive_mutex mutex;
std::string owner;
IDirect3DDevice9* deviceIdentity = nullptr; // identity only; never dereferenced
bool inDraw = false;
uint64_t generation = 1;
PerformanceCounters counters;
struct VerifiedSource {
    const uint8_t* source = nullptr;
    unsigned size = 0, slot = 0;
    Texture descriptor{};
    bool valid = false;
};
std::array<VerifiedSource, 64> verifiedSources{};
size_t nextVerifiedSource = 0;
PreparedBatch prepared;
bool suspended = false;
// Immutable originals admitted by SHA, never pointers into native storage.
// Equality against freshly read bytes is evidence, not a cached lifetime verdict.
std::array<std::vector<uint8_t>, 3> canonicalSources;
void Invalidate()
{
    suspended = false;
    ++generation;
    verifiedSources = {};
    nextVerifiedSource = 0;
    prepared.count = 0;
    prepared.generation = 0;
}
// For already verified native structures: no VirtualQuery per scalar read.
// Never catches exceptions from the draw continuation.
bool GuardedRead(uintptr_t address, void* output, size_t size)
{
    ++counters.guardedReads;
    if (!address || address > UINT32_MAX - size) return false;
    __try { memcpy(output, reinterpret_cast<const void*>(address), size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
template<class T> bool GuardedRead(uintptr_t address, T& output)
{ return GuardedRead(address, &output, sizeof(output)); }
constexpr unsigned widths[] = {256, 256, 128}, heights[] = {256, 128, 128};
constexpr unsigned sourceSizes[] = {66322, 33554, 17170};
constexpr const char* hashes[] = {
    "9d60a2714e673d751a1d74b20bcc5965f976fee3855edfa845d1b7dd64b064c3",
    "4c4b7a0f4c99d737d26044836adb2427c775a5275f252eefee8f29e4e1ad7158",
    "43ae1d9af9e068476b5cc77249d9d63138526d2d02ad946f9c3cf5bb6e0d03c1"
};
bool NativeSource(const Texture& texture, unsigned index, std::vector<uint8_t>& source,
    bool compareCanonical = true)
{
    if (!texture.source || texture.size != sourceSizes[index]) return false;
    const auto& canonical = canonicalSources[index];
    if (compareCanonical && !canonical.empty()) {
        ++counters.sourceComparisons;
        counters.sourceBytes += texture.size;
        std::array<uint8_t, 4096> bytes;
        for (size_t offset = 0; offset < canonical.size();) {
            const size_t count = (std::min)(bytes.size(), canonical.size() - offset);
            if (!GuardedRead(uintptr_t(texture.source) + offset, bytes.data(), count) ||
                memcmp(bytes.data(), canonical.data() + offset, count)) return false;
            offset += count;
        }
        return true;
    }
    ++counters.hashes;
    counters.sourceBytes += texture.size;
    source.resize(texture.size);
    if (!SafeReadBytes(uintptr_t(texture.source), source.data(), source.size())) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0, returned = 0;
    uint8_t digest[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &returned, 0) >= 0 &&
        returned == sizeof(objectSize) && objectSize && objectSize <= 1024 * 1024;
    std::vector<uint8_t> object(ok ? objectSize : 0);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, source.data(), ULONG(source.size()), 0) >= 0 &&
        BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i)
        if (hex[digest[i] >> 4] != hashes[index][i * 2] ||
            hex[digest[i] & 15] != hashes[index][i * 2 + 1]) return false;
    canonicalSources[index] = source;
    return true;
}

void DropGpu()
{
    for (auto& item : replacements) {
        if (item.handle) item.handle->Release();
        item.handle = nullptr;
    }
    deviceIdentity = nullptr;
}
bool ReadNativeHeader(const Binding& binding, Texture& original)
{
    const unsigned index = unsigned(binding.slot);
    if (index >= 3 || binding.rgb > 0xFFFFFF ||
        (index == 2 && binding.rgb != 0xFFFFFF)) return false;
    uintptr_t current = 0, binder = 0;
    if (!GuardedRead(binding.mesh + 0x10, current) || current != binding.nativeTexture ||
        !GuardedRead(current, original) || !original.vtable ||
        original.width != widths[index] || original.height != heights[index] ||
        !GuardedRead(original.vtable, binder) || binder != game::A(0x455B10)) return false;
    // Only the concrete ordinary texture binder is supported, not animated or
    // mod-provided descriptors that could retain SDK source/descriptor pointers.
    return original.source && original.size == sourceSizes[index];
}
bool Compose(const Image& edited, unsigned index, uint32_t rgb, Image& composed)
{
    if (index == 2 && rgb != 0xFFFFFF) return false;
    composed = edited;
    for (size_t i = 0; i < composed.pixels.size(); ++i) {
        const uint32_t color = composed.pixels[i];
        composed.pixels[i] = (color & 0xFF000000) |
            (((color >> 16 & 255) * (rgb >> 16 & 255) / 255) << 16) |
            (((color >> 8 & 255) * (rgb >> 8 & 255) / 255) << 8) |
            ((color & 255) * (rgb & 255) / 255);
    }
    return true;
}
bool Upload(Replacement& item, IDirect3DDevice9* device, unsigned index,
             uint32_t rgb)
{
    if (item.handle && item.rgb == rgb) return true;
    if (item.handle) { item.handle->Release(); item.handle = nullptr; }
    if (FAILED(device->TestCooperativeLevel())) return false;
    Image composed;
    if (!Compose(item.image, index, rgb, composed)) return false;
    memcpy(item.source.data() + 18, composed.pixels.data(), item.source.size() - 18);
    IDirect3DTexture9* handle = nullptr;
    const auto& image = composed;
    if (FAILED(device->CreateTexture(image.width, image.height, 1, 0,
        D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &handle, nullptr))) return false;
    D3DLOCKED_RECT locked{};
    if (FAILED(handle->LockRect(0, &locked, nullptr, 0))) { handle->Release(); return false; }
    const bool valid = locked.pBits && locked.Pitch >= int(image.width * 4);
    if (valid) for (unsigned y = 0; y < image.height; ++y)
        memcpy(static_cast<uint8_t*>(locked.pBits) + size_t(y) * locked.Pitch,
            image.pixels.data() + size_t(y) * image.width, image.width * 4);
    const HRESULT result = handle->UnlockRect(0);
    if (!valid || FAILED(result)) { handle->Release(); return false; }
    item.handle = handle;
    item.rgb = rgb;
    ++counters.uploads;
    return true;
}
// Kept separate from C++ owning locals; restore even during native SEH unwind.
void Borrow(uintptr_t mesh, Texture* replacement, DrawContinuation draw, void* context)
{
    auto** field = reinterpret_cast<Texture**>(mesh + 0x10);
    Texture* original = *field;
    const bool previous = inDraw;
    inDraw = true;
    __try {
        *field = replacement;
        draw(context);
    } __finally { *field = original; inDraw = previous; }
}
// All descriptor/vector allocations finish before entering this routine. Count
// is advanced before the store so a failed write also participates in cleanup.
void BorrowBatch(Borrowed* items, size_t count, DrawContinuation draw, void* context)
{
    size_t applied = 0;
    const bool previous = inDraw;
    inDraw = true;
    __try {
        for (size_t i = 0; i < count; ++i) {
            applied = i + 1;
            *reinterpret_cast<Texture**>(items[i].mesh + 0x10) = &items[i].replacement;
        }
        draw(context);
    } __finally {
        while (applied) {
            auto& item = items[--applied];
            *reinterpret_cast<Texture**>(item.mesh + 0x10) = item.original;
        }
        inDraw = previous;
    }
}
}

bool Load(const std::string& requestedOwner, const std::string& directory,
          const std::vector<std::string>& paths, std::string& error)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    error.clear();
    if (inDraw || requestedOwner.empty() || (!owner.empty() && owner != requestedOwner) ||
        paths.size() != 3) { error = "Busy, invalid owner or expected three texture files"; return false; }
    std::array<Replacement, 3> next;
    try {
        namespace fs = std::filesystem;
        const auto root = fs::canonical(directory);
        auto prefix = root.wstring();
        if (prefix.back() != L'\\') prefix += L'\\';
        for (unsigned i = 0; i < 3; ++i) {
            const fs::path relative(paths[i]);
            if (paths[i].empty() || relative.is_absolute() || relative.has_root_path() ||
                paths[i].find(':') != std::string::npos || paths[i].find('\0') != std::string::npos) {
                error = "Texture path must be relative to the mod directory"; return false;
            }
            const auto path = fs::canonical(root / relative);
            const auto full = path.wstring();
            if (full.size() <= prefix.size() ||
                _wcsnicmp(full.c_str(), prefix.c_str(), prefix.size())) {
                error = "Texture resolves outside the mod directory"; return false;
            }
            const auto size = fs::file_size(path);
            if (size < 18 || size > 18 * 1024 * 1024) { error = "Invalid texture file size"; return false; }
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            std::ifstream file(path, std::ios::binary);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) ||
                !Decode(bytes.data(), bytes.size(), next[i].image) ||
                next[i].image.width != widths[i] || next[i].image.height != heights[i]) {
                error = "Unsupported TGA or dimensions (expected 256x256, 256x128, 128x128)"; return false;
            }
            auto& source = next[i].source;
            source.resize(18 + next[i].image.pixels.size() * 4);
            source[2] = 2; source[12] = uint8_t(widths[i]); source[13] = uint8_t(widths[i] >> 8);
            source[14] = uint8_t(heights[i]); source[15] = uint8_t(heights[i] >> 8);
            source[16] = 32; source[17] = 0x28;
            memcpy(source.data() + 18, next[i].image.pixels.data(), source.size() - 18);
        }
    } catch (const std::exception& e) {
        error = std::string("Cannot load cosmetic textures: ") + e.what(); return false;
    }
    DropGpu();
    replacements = std::move(next);
    Invalidate();
    owner = requestedOwner;
    return true;
}
void Release(const std::string& requestedOwner)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (inDraw || owner != requestedOwner) return;
    DropGpu(); replacements = {}; owner.clear(); Invalidate();
}
void Shutdown()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (inDraw) return; // Never free a borrowed descriptor during reentrant calls.
    DropGpu(); replacements = {}; owner.clear(); Invalidate();
}
void InvalidateBindings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!inDraw) Invalidate();
}
void InvalidatePrepared() { InvalidateBindings(); }
void SuspendPrepared()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (inDraw) return;
    ++counters.suspensions;
    ++generation; // also retire externally held batches
    prepared.generation = generation;
    verifiedSources = {};
    nextVerifiedSource = 0;
    suspended = true;
}
PerformanceCounters GetPerformanceCounters()
{ std::lock_guard<std::recursive_mutex> lock(mutex); return counters; }
void ResetPerformanceCounters()
{ std::lock_guard<std::recursive_mutex> lock(mutex); counters = {}; }
bool Draw(const Binding& binding, DrawContinuation continuation, void* context)
{
    return BatchDraw({binding}, continuation, context);
}
bool VerifyPassHookFingerprints()
{
    const uint8_t opaque[] = {
        0x81,0xEC,0x4C,0x01,0x00,0x00,0x53,0x55,0x56,0x57,0x8B,0xF9};
    const uint8_t transparent[] = {
        0x83,0xEC,0x1C,0x53,0x56,0x8B,0xF1,0x8B,0x86,0xB0,0x07,0x00,0x00};
    uint8_t bytes[sizeof(transparent)]{};
    return SafeReadBytes(game::A(kOpaqueDraw), bytes, sizeof(opaque)) &&
        !memcmp(bytes, opaque, sizeof(opaque)) &&
        SafeReadBytes(game::A(kTransparentDraw), bytes, sizeof(transparent)) &&
        !memcmp(bytes, transparent, sizeof(transparent));
}
bool BatchDraw(const std::vector<Binding>& bindings, DrawContinuation continuation, void* context)
{
    PreparedBatch batch;
    return PrepareBatch(bindings, batch) && DrawPreparedBatch(batch, continuation, context);
}
bool PrepareBatch(const std::vector<Binding>& bindings, PreparedBatch& output)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    ++counters.preparations;
    output.count = output.instanceCount = output.topologyCount = 0;
    output.generation = 0;
    if (inDraw || owner.empty() || bindings.empty() || bindings.size() > kMaxBatchBindings)
        return false;
    PreparedBatch next;
    next.count = bindings.size();
    for (size_t i = 0; i < next.count; ++i) next.entries[i].binding = bindings[i];
    std::sort(next.entries.begin(), next.entries.begin() + next.count,
        [](const auto& a, const auto& b) { return a.binding.mesh < b.binding.mesh; });
    uint32_t colors[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    for (size_t i = 0; i < next.count; ++i) {
        auto& entry = next.entries[i];
        const auto& binding = entry.binding;
        if (!binding.instance || !binding.mesh || !binding.nativeTexture ||
            binding.instance > UINT32_MAX - 0x40 || binding.mesh > UINT32_MAX - 0x48 ||
            (i && next.entries[i - 1].binding.mesh == binding.mesh)) return false;
        size_t instanceIndex = 0;
        while (instanceIndex < next.instanceCount &&
            next.instances[instanceIndex].address != binding.instance) ++instanceIndex;
        if (instanceIndex == next.instanceCount) {
            auto& instance = next.instances[next.instanceCount++];
            instance.address = binding.instance;
            uint32_t flags = 0;
            if (!GuardedRead(instance.address + 0x34, flags) || !(flags & 8) ||
                !GuardedRead(instance.address, instance.heads, sizeof(instance.heads))) return false;
            // Walk each list ONCE per unique instance, rather than twice for
            // every mesh. Sorting permits logarithmic membership lookup.
            std::array<uintptr_t, 1024> meshes{};
            size_t count = 0;
            for (uint32_t head : instance.heads) {
                unsigned n = 0;
                for (; head && n < 512; ++n) {
                    if (head > UINT32_MAX - 0x48) return false;
                    ++counters.chainNodes;
                    meshes[count++] = head;
                    if (next.topologyCount == next.topology.size()) return false;
                    auto& node = next.topology[next.topologyCount++];
                    node.address = head;
                    if (!GuardedRead(head + 0x44, head)) return false;
                    node.next = head;
                }
                if (head) return false; // cyclic/oversized chain: fail closed
            }
            std::sort(meshes.begin(), meshes.begin() + count);
            for (size_t j = 0; j < next.count; ++j) {
                auto& candidate = next.entries[j];
                if (candidate.binding.instance != instance.address) continue;
                if (!std::binary_search(meshes.begin(), meshes.begin() + count,
                    candidate.binding.mesh)) return false;
                candidate.instanceIndex = unsigned(instanceIndex);
            }
        }
        Texture native{};
        if (!ReadNativeHeader(binding, native)) return false;
        const unsigned slot = unsigned(binding.slot);
        const auto found = std::find_if(verifiedSources.begin(), verifiedSources.end(), [&](const auto& item) {
            return item.slot == slot && item.source == native.source && item.size == native.size &&
                !memcmp(&item.descriptor, &native, sizeof(native));
        });
        if (found != verifiedSources.end()) {
            if (!found->valid) return false;
        } else {
            std::vector<uint8_t> source;
            const bool valid = NativeSource(native, slot, source);
            verifiedSources[nextVerifiedSource] = {native.source, native.size, slot, native, valid};
            nextVerifiedSource = (nextVerifiedSource + 1) % verifiedSources.size();
            if (!valid) return false;
        }
        if (colors[slot] != 0xFFFFFFFF && colors[slot] != binding.rgb) return false;
        colors[slot] = binding.rgb;
        memcpy(entry.descriptor.data(), &native, sizeof(native));
        entry.descriptorIndex = unsigned(i);
        for (size_t j = 0; j < i; ++j) {
            if (next.entries[j].binding.nativeTexture != binding.nativeTexture) continue;
            if (next.entries[j].descriptor != entry.descriptor) return false;
            entry.descriptorIndex = next.entries[j].descriptorIndex;
            break;
        }
    }
    next.generation = generation;
    output = next;
    return true;
}
namespace {
bool SnapshotCurrent(const PreparedBatch& batch)
{
    if (!batch.count || batch.generation != generation) return false;
    for (size_t i = 0; i < batch.topologyCount; ++i) {
        const auto& node = batch.topology[i];
        uint32_t next = 0;
        if (!GuardedRead(node.address + 0x44, next) || next != node.next) return false;
    }
    for (size_t i = 0; i < batch.instanceCount; ++i) {
        const auto& instance = batch.instances[i];
        uint32_t flags = 0, heads[2]{};
        if (!GuardedRead(instance.address + 0x34, flags) || !(flags & 8) ||
            !GuardedRead(instance.address, heads, sizeof(heads)) ||
            memcmp(heads, instance.heads, sizeof(heads))) return false;
    }
    for (size_t i = 0; i < batch.count; ++i) {
        const auto& entry = batch.entries[i];
        uintptr_t current = 0;
        if (!GuardedRead(entry.binding.mesh + 0x10, current) ||
            current != entry.binding.nativeTexture) return false;
        if (entry.descriptorIndex != i) continue;
        Texture native{};
        uintptr_t binder = 0;
        if (!GuardedRead(current, native) ||
            memcmp(&native, entry.descriptor.data(), sizeof(native)) ||
            !GuardedRead(native.vtable, binder) || binder != game::A(0x455B10)) return false;
    }
    return true;
}
}
bool PrepareBatch(const std::vector<Binding>& bindings)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (inDraw) return false;
    bool same = prepared.count == bindings.size() && prepared.generation == generation;
    std::array<bool, kMaxBatchBindings> seen{};
    // Stored entries are sorted by mesh; callers need not sort their vectors.
    for (const auto& binding : bindings) {
        if (!same) break;
        const auto end = prepared.entries.begin() + prepared.count;
        const auto found = std::lower_bound(prepared.entries.begin(), end, binding.mesh,
            [](const auto& entry, uintptr_t mesh) { return entry.binding.mesh < mesh; });
        same = found != end && !seen[size_t(found - prepared.entries.begin())] &&
            found->binding.instance == binding.instance &&
            found->binding.mesh == binding.mesh && found->binding.nativeTexture == binding.nativeTexture &&
            found->binding.slot == binding.slot && found->binding.rgb == binding.rgb;
        if (same) seen[size_t(found - prepared.entries.begin())] = true;
    }
    if (same && SnapshotCurrent(prepared)) {
        if (suspended) for (size_t i = 0; i < prepared.count; ++i) {
            const auto& entry = prepared.entries[i];
            if (entry.descriptorIndex != i) continue;
            Texture native{};
            memcpy(&native, entry.descriptor.data(), sizeof(native));
            std::vector<uint8_t> scratch;
            if (!NativeSource(native, unsigned(entry.binding.slot), scratch)) return false;
        }
        suspended = false;
        return true;
    }
    const bool ok = PrepareBatch(bindings, prepared);
    if (ok) suspended = false;
    return ok;
}
bool DrawPrepared(DrawContinuation draw, void* context)
{ return DrawPreparedBatch(prepared, draw, context); }
bool DrawPreparedBatch(const PreparedBatch& batch, DrawContinuation continuation, void* context)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (inDraw || suspended || owner.empty() || !continuation || !batch.count ||
        batch.count > kMaxBatchBindings || batch.instanceCount > kMaxBatchBindings ||
        batch.topologyCount > kMaxPreparedChainNodes ||
        batch.generation != generation) return false;
    std::array<Borrowed, kMaxBatchBindings> borrowed{};
    uint32_t colors[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    for (size_t i = 0; i < batch.count; ++i) {
        const auto& entry = batch.entries[i];
        const unsigned slot = unsigned(entry.binding.slot);
        if (slot >= 3 || entry.instanceIndex >= batch.instanceCount || entry.descriptorIndex > i ||
            entry.binding.rgb > 0xFFFFFF || (slot == 2 && entry.binding.rgb != 0xFFFFFF) ||
            (colors[slot] != 0xFFFFFFFF && colors[slot] != entry.binding.rgb)) return false;
        colors[slot] = entry.binding.rgb;
    }
    IDirect3DDevice9* device = nullptr;
    // Avoid game::Device's general-purpose SafeRead/VirtualQuery on every pass.
    if (!GuardedRead(game::A(game::off::pD3DDevice), device) || !device) return false;
    if (deviceIdentity != device) { DropGpu(); deviceIdentity = device; }
    for (unsigned slot = 0; slot < 3; ++slot) {
        if (colors[slot] == 0xFFFFFFFF) continue;
        auto& replacement = replacements[slot];
        if (replacement.image.pixels.empty() ||
            !Upload(replacement, device, slot, colors[slot])) return false;
    }
    // Recheck AFTER D3D warm-up, once per instance/descriptor, before any writes.
    if (!SnapshotCurrent(batch)) return false;
    for (size_t i = 0; i < batch.count; ++i) {
        const auto& entry = batch.entries[i];
        const auto& binding = entry.binding;
        auto& item = borrowed[i];
        item.mesh = binding.mesh;
        item.original = reinterpret_cast<Texture*>(binding.nativeTexture);
        memcpy(&item.replacement, entry.descriptor.data(), sizeof(Texture));
        const auto& gpu = replacements[unsigned(binding.slot)];
        item.replacement.handle = gpu.handle;
        item.replacement.source = gpu.source.data();
        item.replacement.size = unsigned(gpu.source.size());
    }
    BorrowBatch(borrowed.data(), batch.count, continuation, context);
    return true;
}
}
