// Isolated production loader/decoder/borrow checks. No process launch or hooks.
#include <cstdlib>
#include <new>
#include <chrono>
size_t heapAllocations = 0;
void* operator new(size_t n) {
    ++heapAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
#include "../src/core/cosmetic_textures.cpp"
size_t queries = 0;
void* testDevice = nullptr;
uint8_t opaqueEntry[] = {0x81,0xEC,0x4C,0x01,0,0,0x53,0x55,0x56,0x57,0x8B,0xF9};
uint8_t transparentEntry[] = {0x83,0xEC,0x1C,0x53,0x56,0x8B,0xF1,0x8B,0x86,0xB0,0x07,0,0};
namespace eth {
uintptr_t GameAddr(uintptr_t address) {
    return address == game::off::pD3DDevice ? uintptr_t(&testDevice) : address;
}
bool SafeReadBytes(uintptr_t address, void* out, size_t size)
{
    if (address == 0x44D890) {
        if (size > sizeof(opaqueEntry)) return false;
        memcpy(out, opaqueEntry, size); return true;
    }
    if (address == 0x44C5C0) {
        if (size > sizeof(transparentEntry)) return false;
        memcpy(out, transparentEntry, size); return true;
    }
    // Same query-per-region policy as the production safe reader.
    uintptr_t cursor = address, end = address + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        ++queries;
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const uintptr_t next = uintptr_t(region.BaseAddress) + region.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    __try { memcpy(out, reinterpret_cast<void*>(address), size); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
}
namespace eth::game { void* Device() { return testDevice; } }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
using namespace eth::cosmetic::textures;
struct Context { uintptr_t mesh; Texture* replacement; unsigned calls; bool correct; };
void __cdecl CheckBorrow(void* value)
{
    auto& c = *static_cast<Context*>(value);
    ++c.calls;
    c.correct = *reinterpret_cast<Texture**>(c.mesh + 0x10) == c.replacement;
    // Reentrant release/shutdown MUST NOT release a live descriptor.
    const bool previous = inDraw;
    inDraw = true;
    Release("a"); Shutdown();
    c.correct = c.correct && owner == "a";
    inDraw = previous;
}
void __cdecl Fault(void*) { RaiseException(0xE0000001, 0, 0, nullptr); }
bool FaultBorrow(uintptr_t mesh, Texture* replacement)
{
    __try { Borrow(mesh, replacement, Fault, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
// Minimal ABI fixtures, NOT a real D3D device: only methods production uses.
unsigned creates = 0, releases = 0;
bool failCreate = false;
void* textureVtable[22]{};
struct FakeTexture {
    void** vtable = textureVtable;
    std::vector<uint8_t> bytes;
    unsigned pitch;
    FakeTexture(unsigned width, unsigned height) : bytes((width * 4 + 16) * height), pitch(width * 4 + 16) {}
};
ULONG WINAPI FakeRelease(FakeTexture* texture)
{ ++releases; delete texture; return 0; }
HRESULT WINAPI FakeLock(FakeTexture* texture, UINT, D3DLOCKED_RECT* locked, const RECT*, DWORD)
{ locked->pBits = texture->bytes.data(); locked->Pitch = texture->pitch; return S_OK; }
HRESULT WINAPI FakeUnlock(FakeTexture*, UINT) { return S_OK; }
HRESULT WINAPI FakeCooperative(void*) { return S_OK; }
HRESULT WINAPI FakeCreate(void*, UINT width, UINT height, UINT levels, DWORD usage,
    D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** output, HANDLE*)
{
    if (failCreate || levels != 1 || usage || format != D3DFMT_A8R8G8B8 || pool != D3DPOOL_MANAGED)
        return E_FAIL;
    ++creates; *output = reinterpret_cast<IDirect3DTexture9*>(new FakeTexture(width, height));
    return S_OK;
}
struct BatchContext {
    const std::vector<Binding>* bindings;
    unsigned calls = 0;
    bool correct = false;
};
void __cdecl CheckBatch(void* value)
{
    auto& context = *static_cast<BatchContext*>(value);
    ++context.calls; context.correct = true;
    Texture* first = nullptr;
    for (const auto& binding : *context.bindings) {
        auto* texture = *reinterpret_cast<Texture**>(binding.mesh + 0x10);
        context.correct &= uintptr_t(texture) != binding.nativeTexture &&
            texture->handle && texture->size == 18 + texture->width * texture->height * 4;
        context.correct &= *reinterpret_cast<uint32_t*>(binding.mesh + 0x14) == 0x7A112233;
        if (first) context.correct &= texture != first && texture->handle == first->handle;
        first = texture;
    }
    context.correct &= !BatchDraw(*context.bindings, CheckBatch, value); // no nested substitutions
    Release("a"); Shutdown();
    context.correct &= owner == "a";
}
bool FaultBatch(const std::vector<Binding>& bindings)
{
    __try { BatchDraw(bindings, Fault, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
// Frozen pre-optimization validation/setup cost model: two PrivateMesh walks
// per binding/pass, SafeRead/VirtualQuery per field, per-pass source SHA and
// owning scratch vectors. No GPU or native draw included in either timing.
bool OldPrivateMesh(const Binding& binding)
{
    uint32_t flags = 0, heads[2]{};
    if (!eth::SafeRead(binding.instance + 0x34, flags) || !(flags & 8) ||
        !eth::SafeReadBytes(binding.instance, heads, sizeof(heads))) return false;
    for (auto head : heads) {
        for (unsigned n = 0; head && n < 512; ++n) {
            if (head == binding.mesh) return true;
            ++counters.chainNodes;
            if (!eth::SafeRead(head + 0x44, head)) return false;
        }
    }
    return false;
}
bool OldValidation(const std::vector<Binding>& bindings)
{
    std::vector<Borrowed> scratch(bindings.size());
    struct Source { unsigned slot; const uint8_t* source; unsigned size; };
    std::vector<Source> verified;
    std::array<std::vector<uint8_t>, 3> sources;
    for (size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        for (size_t j = 0; j < i; ++j) if (bindings[j].mesh == binding.mesh) return false;
        Texture native{};
        uintptr_t current = 0, binder = 0;
        if (!OldPrivateMesh(binding) || !eth::SafeRead(binding.mesh + 0x10, current) ||
            current != binding.nativeTexture || !eth::SafeRead(current, native) ||
            !eth::SafeRead(native.vtable, binder) || binder != 0x455B10) return false;
        const auto found = std::find_if(verified.begin(), verified.end(), [&](const auto& item) {
            return item.source == native.source && item.size == native.size &&
                item.slot == unsigned(binding.slot);
        });
        if (found == verified.end()) {
            std::vector<uint8_t> source;
            if (!NativeSource(native, unsigned(binding.slot), source, false)) return false;
            verified.push_back({unsigned(binding.slot), native.source, native.size});
            if (sources[unsigned(binding.slot)].empty()) sources[unsigned(binding.slot)] = std::move(source);
        }
    }
    for (const auto& binding : bindings) if (!OldPrivateMesh(binding)) return false;
    return true;
}
void __cdecl CountDraw(void* value) { ++*static_cast<unsigned*>(value); }
int Benchmark(Texture& original)
{
    constexpr unsigned meshCount = 64, passes = 1000;
    std::array<std::array<uint8_t, 0x48>, meshCount> meshes{};
    alignas(4) uint8_t instance[0x40]{};
    *reinterpret_cast<uintptr_t*>(instance) = uintptr_t(meshes[0].data());
    *reinterpret_cast<uint32_t*>(instance + 0x34) = 8;
    std::vector<Binding> bindings;
    bindings.reserve(meshCount);
    for (unsigned i = 0; i < meshCount; ++i) {
        *reinterpret_cast<Texture**>(meshes[i].data() + 0x10) = &original;
        if (i + 1 < meshCount)
            *reinterpret_cast<uintptr_t*>(meshes[i].data() + 0x44) = uintptr_t(meshes[i + 1].data());
        bindings.push_back({uintptr_t(instance), uintptr_t(meshes[i].data()), uintptr_t(&original), Slot::Blue});
    }
    InvalidatePrepared();
    canonicalSources[0].clear(); // measure cold SHA admission independently
    ResetPerformanceCounters();
    CHECK(PrepareBatch(bindings));
    CHECK(GetPerformanceCounters().chainNodes == meshCount && GetPerformanceCounters().hashes == 1);
    unsigned draws = 0;
    CHECK(DrawPrepared(CountDraw, &draws)); // warm GPU
    const auto initialAllocations = heapAllocations, initialQueries = queries;
    ResetPerformanceCounters();
    CHECK(PrepareBatch(bindings)); // tick no-op also alloc/hash/chain/query-free
    CHECK(heapAllocations == initialAllocations && queries == initialQueries);
    CHECK(!GetPerformanceCounters().chainNodes && !GetPerformanceCounters().hashes);
    const auto begin = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < passes; ++i) CHECK(DrawPrepared(CountDraw, &draws));
    const auto preparedEnd = std::chrono::steady_clock::now();
    const auto warm = GetPerformanceCounters();
    CHECK(draws == passes + 1 && !warm.hashes && !warm.chainNodes);
    CHECK(heapAllocations == initialAllocations && queries == initialQueries);
    CHECK(warm.guardedReads == uint64_t(passes) * (2 * meshCount + 5) + (2 * meshCount + 4));
    // v0.4 host model: discard everything across each native tick, SHA and
    // rebuild once post-tick, then use the already optimized prepared draws.
    const auto v04Allocations=heapAllocations, v04Queries=queries;
    ResetPerformanceCounters();
    const auto v04Begin=std::chrono::steady_clock::now();
    for (unsigned i=0;i<passes;++i) {
        InvalidatePrepared();
        canonicalSources[0].clear(); // former code had no retained originals
        CHECK(PrepareBatch(bindings));
        CHECK(DrawPrepared(CountDraw,&draws));
        CHECK(DrawPrepared(CountDraw,&draws));
    }
    const auto v04End=std::chrono::steady_clock::now();
    const auto v04=GetPerformanceCounters();
    CHECK(v04.hashes==passes && v04.chainNodes==uint64_t(passes)*meshCount);
    std::printf("Synthetic v0.4 %u ticks + 2 draws/tick: %.3f ms, SHA=%llu, "
        "VirtualQuery=%zu, C++ allocations=%zu, list traversals=%llu\n",passes,
        std::chrono::duration<double,std::milli>(v04End-v04Begin).count(),
        static_cast<unsigned long long>(v04.hashes),queries-v04Queries,
        heapAllocations-v04Allocations,static_cast<unsigned long long>(v04.chainNodes));
    // End-to-end tick boundary + fresh source proof + two real prepared draws.
    // Unlike the former standalone draw benchmark, lifetime invalidation is
    // included. Immutable originals survive; native source trust does not.
    const auto tickAllocations=heapAllocations, tickQueries=queries;
    ResetPerformanceCounters();
    const auto tickBegin=std::chrono::steady_clock::now();
    for (unsigned i=0;i<passes;++i) {
        SuspendPrepared();
        CHECK(!DrawPrepared(CountDraw,&draws));
        CHECK(PrepareBatch(bindings));
        CHECK(DrawPrepared(CountDraw,&draws));
        CHECK(DrawPrepared(CountDraw,&draws));
    }
    const auto tickEnd=std::chrono::steady_clock::now();
    const auto ticks=GetPerformanceCounters();
    CHECK(!ticks.hashes && ticks.sourceComparisons==passes && !ticks.chainNodes);
    CHECK(ticks.suspensions==passes && !ticks.preparations && !ticks.uploads);
    CHECK(heapAllocations==tickAllocations && queries==tickQueries);
    std::printf("Synthetic %u ticks + 2 draws/tick: %.3f ms, SHA=0, fresh comparisons=%llu, "
        "VirtualQuery=0, C++ allocations=0, list traversals=0\n",passes,
        std::chrono::duration<double,std::milli>(tickEnd-tickBegin).count(),
        static_cast<unsigned long long>(ticks.sourceComparisons));
    auto* mutableSource=const_cast<uint8_t*>(original.source);
    SuspendPrepared();
    mutableSource[original.size-1]^=1;
    CHECK(!PrepareBatch(bindings) && !DrawPrepared(CountDraw,&draws));
    mutableSource[original.size-1]^=1;
    CHECK(PrepareBatch(bindings) && DrawPrepared(CountDraw,&draws));
    auto* nativeCopy=static_cast<uint8_t*>(VirtualAlloc(nullptr,original.size,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(nativeCopy);
    memcpy(nativeCopy,original.source,original.size);
    const auto* oldSource=original.source;
    original.source=nativeCopy;
    SuspendPrepared(); CHECK(PrepareBatch(bindings));
    DWORD previousProtection=0;
    CHECK(VirtualProtect(nativeCopy,original.size,PAGE_NOACCESS,&previousProtection));
    SuspendPrepared();
    CHECK(!PrepareBatch(bindings) && !DrawPrepared(CountDraw,&draws));
    original.source=oldSource;
    CHECK(VirtualFree(nativeCopy,0,MEM_RELEASE));
    InvalidatePrepared();
    CHECK(PrepareBatch(bindings) && DrawPrepared(CountDraw,&draws));
    const auto oldAllocations = heapAllocations, oldQueries = queries;
    ResetPerformanceCounters();
    const auto oldBegin = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < passes; ++i) CHECK(OldValidation(bindings));
    const auto oldEnd = std::chrono::steady_clock::now();
    const auto old = GetPerformanceCounters();
    CHECK(old.hashes == passes && old.chainNodes == uint64_t(passes) * meshCount * (meshCount - 1));
    CHECK(queries > oldQueries && heapAllocations > oldAllocations);
    std::printf("Synthetic %u meshes x %u passes: old validation %.3f ms, SHA=%llu, bytes=%llu, "
        "VirtualQuery=%zu, C++ allocations=%zu, chain links=%llu; prepared %.3f ms, "
        "SHA=0, VirtualQuery=0 (fake device), C++ allocations=0, chain links=0\n",
        meshCount, passes, std::chrono::duration<double, std::milli>(oldEnd - oldBegin).count(),
        static_cast<unsigned long long>(old.hashes), static_cast<unsigned long long>(old.sourceBytes),
        queries - oldQueries, heapAllocations - oldAllocations,
        static_cast<unsigned long long>(old.chainNodes),
        std::chrono::duration<double, std::milli>(preparedEnd - begin).count());
    // Fail closed before continuation on current descriptor/pointer/head/flag changes.
    original.flags ^= 1;
    CHECK(!DrawPrepared(CountDraw, &draws));
    CHECK(PrepareBatch(bindings)); // changed descriptor observed even with same bindings
    CHECK(DrawPrepared(CountDraw, &draws));
    original.flags ^= 1;
    CHECK(!DrawPrepared(CountDraw, &draws));
    CHECK(PrepareBatch(bindings));
    *reinterpret_cast<uint32_t*>(instance + 0x34) = 0;
    CHECK(!DrawPrepared(CountDraw, &draws));
    *reinterpret_cast<uint32_t*>(instance + 0x34) = 8;
    *reinterpret_cast<uintptr_t*>(instance) = uintptr_t(meshes[1].data());
    CHECK(!DrawPrepared(CountDraw, &draws));
    *reinterpret_cast<uintptr_t*>(instance) = uintptr_t(meshes[0].data());
    const auto oldNext = *reinterpret_cast<uint32_t*>(meshes[20].data() + 0x44);
    *reinterpret_cast<uint32_t*>(meshes[20].data() + 0x44) = 0;
    CHECK(!DrawPrepared(CountDraw, &draws)); // unchanged heads, detached middle/tail
    CHECK(!PrepareBatch(bindings)); // unreachable requested meshes, fail closed
    *reinterpret_cast<uint32_t*>(meshes[20].data() + 0x44) = oldNext;
    CHECK(PrepareBatch(bindings));
    *reinterpret_cast<Texture**>(meshes[0].data() + 0x10) = nullptr;
    CHECK(!DrawPrepared(CountDraw, &draws));
    *reinterpret_cast<Texture**>(meshes[0].data() + 0x10) = &original;
    auto duplicate = bindings; duplicate[1] = duplicate[0];
    CHECK(!PrepareBatch(duplicate)); // no-op must not accept duplicate keys
    CHECK(!DrawPrepared(CountDraw, &draws));
    CHECK(PrepareBatch(bindings));
    InvalidatePrepared();
    CHECK(!DrawPrepared(CountDraw, &draws));
    ResetPerformanceCounters();
    auto* source = const_cast<uint8_t*>(original.source);
    source[original.size - 1] ^= 1;
    CHECK(!PrepareBatch(bindings));
    CHECK(!PrepareBatch(bindings));
    CHECK(GetPerformanceCounters().sourceComparisons == 1); // negative source cache
    source[original.size - 1] ^= 1;
    InvalidatePrepared(); // source byte mutation/reuse MUST invalidate
    CHECK(PrepareBatch(bindings));
    CHECK(GetPerformanceCounters().sourceComparisons == 2);
    // Unmapped descriptor reads are caught; no VirtualQuery needed.
    auto* protectedPage = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    CHECK(protectedPage);
    Texture ignored{};
    CHECK(!GuardedRead(uintptr_t(protectedPage), ignored));
    CHECK(VirtualFree(protectedPage, 0, MEM_RELEASE));
    InvalidatePrepared();
    return 0;
}
int main(int argc, char** argv)
{
    CHECK(argc == 3); // supplied pack directory, extracted originals directory
    CHECK(VerifyPassHookFingerprints());
    opaqueEntry[0] ^= 1; CHECK(!VerifyPassHookFingerprints()); opaqueEntry[0] ^= 1;
    transparentEntry[0] ^= 1; CHECK(!VerifyPassHookFingerprints()); transparentEntry[0] ^= 1;
    const std::vector<std::string> paths{
        "ninja-blue-original-A00063740.tga",
        "ninja-body-secondary-original-A00073a80.tga",
        "ninja-head-original-A001b0720.tga"};
    Image image, unchanged; unchanged.width = 123;
    uint8_t bad[18]{};
    CHECK(!Decode(bad, sizeof(bad), unchanged) && unchanged.width == 123);
    // One raw RLE packet followed by one repeat packet; bottom-right origin.
    std::vector<uint8_t> tga(18);
    tga[2] = 10; tga[12] = 2; tga[14] = 2; tga[16] = 24; tga[17] = 16;
    const uint8_t packets[] = {1, 1,2,3, 4,5,6, 129, 7,8,9};
    tga.insert(tga.end(), std::begin(packets), std::end(packets));
    CHECK(Decode(tga.data(), tga.size(), image));
    CHECK(image.pixels == std::vector<uint32_t>({0xFF090807,0xFF090807,0xFF060504,0xFF030201}));
    for (size_t n = 18; n < tga.size(); ++n) CHECK(!Decode(tga.data(), n, unchanged));
    tga[18] = 127; // packet overruns the image
    CHECK(!Decode(tga.data(), tga.size(), unchanged));
    std::string error;
    CHECK(Load("a", argv[1], paths, error));
    CHECK(owner == "a" && replacements[0].image.width == 256);
    CHECK(!Load("b", argv[1], paths, error));
    Release("b"); CHECK(owner == "a");
    auto invalid = paths;
    invalid[0] = std::filesystem::canonical(std::filesystem::path(argv[1]) / paths[0]).string();
    CHECK(!Load("a", argv[1], invalid, error));
    invalid[0] = paths[0] + ":stream";
    CHECK(!Load("a", argv[1], invalid, error));
    invalid[0] = "..\\outside.tga";
    CHECK(!Load("a", argv[1], invalid, error));
    CHECK(replacements[0].image.width == 256 && owner == "a"); // transactional
    for (unsigned i = 0; i < 3; ++i) {
        const auto path = std::filesystem::path(argv[2]) / paths[i];
        std::ifstream file(path, std::ios::binary);
        std::vector<uint8_t> bytes(sourceSizes[i]);
        CHECK(bool(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())));
        Texture original{}; original.source = bytes.data(); original.size = unsigned(bytes.size());
        std::vector<uint8_t> observed;
        CHECK(NativeSource(original, i, observed));
        CHECK(Decode(observed.data(), observed.size(), image));
        if (i == 2) {
            Image authored = replacements[2].image;
            authored.pixels[0] = 0x12345678; // replacement alpha, not native alpha
            authored.pixels.back() = 0x9AABCDEF; // entire head, not a partial selection
            Image composed;
            CHECK(Compose(authored, 2, 0xFFFFFF, composed));
            CHECK(composed.pixels == authored.pixels); // full image AND authored alpha
            CHECK(!Compose(replacements[2].image, 2, 0xFF0000, composed));
        }
        bytes.back() ^= 1;
        CHECK(!NativeSource(original, i, observed));
        original.size--;
        CHECK(!NativeSource(original, i, observed));
    }
    alignas(4) uint8_t mesh[0x48]{}, instance[0x40]{};
    Texture original{}, replacement{};
    *reinterpret_cast<Texture**>(mesh + 0x10) = &original;
    *reinterpret_cast<uintptr_t*>(instance) = uintptr_t(mesh);
    *reinterpret_cast<uint32_t*>(instance + 0x34) = 8;
    Binding binding{uintptr_t(instance), uintptr_t(mesh), uintptr_t(&original), Slot::Blue};
    PreparedBatch preparedFixture;
    *reinterpret_cast<uint32_t*>(instance + 0x34) = 0;
    CHECK(!PrepareBatch({binding}, preparedFixture));
    *reinterpret_cast<uint32_t*>(instance + 0x34) = 8;
    Context context{uintptr_t(mesh), &replacement, 0, false};
    Borrow(uintptr_t(mesh), &replacement, CheckBorrow, &context);
    CHECK(context.correct && context.calls == 1);
    CHECK(*reinterpret_cast<Texture**>(mesh + 0x10) == &original);
    CHECK(FaultBorrow(uintptr_t(mesh), &replacement));
    CHECK(*reinterpret_cast<Texture**>(mesh + 0x10) == &original);
    CHECK(!Draw(binding, CheckBorrow, &context) && context.calls == 1);
    textureVtable[2] = reinterpret_cast<void*>(FakeRelease);
    textureVtable[19] = reinterpret_cast<void*>(FakeLock);
    textureVtable[20] = reinterpret_cast<void*>(FakeUnlock);
    void* deviceVtable[119]{};
    deviceVtable[3] = reinterpret_cast<void*>(FakeCooperative);
    deviceVtable[23] = reinterpret_cast<void*>(FakeCreate);
    void** deviceObject = deviceVtable;
    testDevice = &deviceObject;
    uint32_t binder = 0x455B10;
    std::vector<uint8_t> originalBytes(sourceSizes[0]);
    std::ifstream originalFile(std::filesystem::path(argv[2]) / paths[0], std::ios::binary);
    CHECK(bool(originalFile.read(reinterpret_cast<char*>(originalBytes.data()), originalBytes.size())));
    original.vtable = uintptr_t(&binder); original.width = 256; original.height = 256;
    original.source = originalBytes.data(); original.size = unsigned(originalBytes.size());
    alignas(4) uint8_t mesh2[0x48]{};
    *reinterpret_cast<Texture**>(mesh2 + 0x10) = &original;
    *reinterpret_cast<uint32_t*>(mesh + 0x14) = 0x7A112233;
    *reinterpret_cast<uint32_t*>(mesh2 + 0x14) = 0x7A112233;
    *reinterpret_cast<uintptr_t*>(mesh + 0x44) = uintptr_t(mesh2);
    std::vector<Binding> batch{binding, {uintptr_t(instance), uintptr_t(mesh2),
        uintptr_t(&original), Slot::Blue}};
    BatchContext batchContext{&batch};
    auto invalidBatch = batch; invalidBatch[1].mesh = invalidBatch[0].mesh;
    CHECK(!BatchDraw(invalidBatch, CheckBatch, &batchContext) && !creates);
    invalidBatch = batch; invalidBatch[1].rgb = 0x123456;
    CHECK(!BatchDraw(invalidBatch, CheckBatch, &batchContext) && !creates);
    invalidBatch = batch; invalidBatch[1].nativeTexture = uintptr_t(&replacement);
    CHECK(!BatchDraw(invalidBatch, CheckBatch, &batchContext) && !creates);
    failCreate = true;
    CHECK(!BatchDraw(batch, CheckBatch, &batchContext) && batchContext.calls == 0);
    CHECK(*reinterpret_cast<Texture**>(mesh + 0x10) == &original);
    failCreate = false;
    CHECK(BatchDraw(batch, CheckBatch, &batchContext));
    CHECK(batchContext.correct && batchContext.calls == 1 && creates == 1 && !inDraw);
    CHECK(*reinterpret_cast<Texture**>(mesh + 0x10) == &original);
    CHECK(*reinterpret_cast<Texture**>(mesh2 + 0x10) == &original);
    CHECK(*reinterpret_cast<uint32_t*>(mesh + 0x14) == 0x7A112233);
    CHECK(*reinterpret_cast<uint32_t*>(mesh2 + 0x14) == 0x7A112233);
    CHECK(FaultBatch(batch));
    CHECK(!inDraw && *reinterpret_cast<Texture**>(mesh + 0x10) == &original);
    CHECK(*reinterpret_cast<Texture**>(mesh2 + 0x10) == &original);
    CHECK(*reinterpret_cast<uint32_t*>(mesh + 0x14) == 0x7A112233);
    CHECK(BatchDraw(batch, CheckBatch, &batchContext) && creates == 1); // managed cache
    std::vector<uint8_t> headBytes(sourceSizes[2]);
    std::ifstream headFile(std::filesystem::path(argv[2]) / paths[2], std::ios::binary);
    CHECK(bool(headFile.read(reinterpret_cast<char*>(headBytes.data()), headBytes.size())));
    Texture headOriginal = original;
    headOriginal.width = headOriginal.height = 128;
    headOriginal.source = headBytes.data(); headOriginal.size = unsigned(headBytes.size());
    alignas(4) uint8_t headMesh[0x48]{}, headInstance[0x40]{};
    *reinterpret_cast<uintptr_t*>(headInstance) = uintptr_t(headMesh);
    *reinterpret_cast<uint32_t*>(headInstance + 0x34) = 8;
    *reinterpret_cast<Texture**>(headMesh + 0x10) = &headOriginal;
    *reinterpret_cast<uint32_t*>(headMesh + 0x14) = 0x7A112233;
    std::vector<Binding> headBatch{{uintptr_t(headInstance), uintptr_t(headMesh),
        uintptr_t(&headOriginal), Slot::Head, 0xFFFFFF}};
    BatchContext headContext{&headBatch};
    auto coloredHead = headBatch; coloredHead[0].rgb = 0xCC2244;
    CHECK(!BatchDraw(coloredHead, CheckBatch, &headContext) && creates == 1);
    CHECK(BatchDraw(headBatch, CheckBatch, &headContext));
    CHECK(headContext.calls == 1 && headContext.correct && creates == 2);
    CHECK(*reinterpret_cast<uint32_t*>(headMesh + 0x14) == 0x7A112233);
    CHECK(FaultBatch(headBatch));
    CHECK(*reinterpret_cast<Texture**>(headMesh + 0x10) == &headOriginal);
    CHECK(*reinterpret_cast<uint32_t*>(headMesh + 0x14) == 0x7A112233);
    CHECK(Benchmark(original) == 0);
    Release("a");
    CHECK(owner.empty() && replacements[0].source.empty());
    CHECK(releases == 2);
    std::puts("Cosmetic texture production loader/RLE/hash/full-head/batch/fake-D3D/scoped-SEH checks passed");
}
