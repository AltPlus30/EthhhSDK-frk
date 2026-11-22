// Win32 synthetic fixture; include the REAL implementation, never the game.
// Build as a standalone executable (C++17, /EHa), with the usual core/ImGui
// include directories, like CosmeticEditorTests.cpp. No game DLL is linked.
#include "../src/core/cosmetic_models.cpp"

namespace cm = eth::cosmetic::models;
namespace {
static_assert(sizeof(void*) == 4, "The production engine ABI is Win32");
constexpr uint32_t unit = 0x4B5F20;
constexpr uint32_t operandA[6] = {
    0x4B86A8, 0x4B86B8, 0x4B86C8, 0x4B86D8, 0x4B86E8, 0x4B86F8
};
constexpr uint32_t descriptorA[6] = {
    0x4B3CC0, 0x4B3CD0, 0x4B3CE0, 0x4B3CF0, 0x4B3D00, 0x4B3D10
};
enum { Ninja = 0, Sword = 1, Donor = 2, OtherSword = 3 };
alignas(16) uint8_t actors[4][0x310]{}, classes[4][0x40]{};
alignas(16) uint8_t builtin[6][0x50]{}, builtinMeshes[6][0x50]{};
alignas(16) uint8_t models[4][0x50]{}, meshes[4][2][0x50]{};
alignas(16) uint8_t renderer[0x5D000]{};
alignas(16) uint8_t controllers[4][0x30]{};
uint32_t skeletons[4]{}, registry[1024]{};
char classNames[4][64] = {"Ninja", "NinjaSword", "Ranx", "NinjaSword"};
struct alignas(16) Nodes {
    uint32_t header[4];
    cm::Matrix pose[2];
};
Nodes nodeBlocks[4]{};
cm::Actor ids[4];
uint8_t* pool = nullptr;
uintptr_t code = 0;
uint32_t descriptors[6]{};
uint8_t profile[0x200]{}, profileBefore[0x200]{};
uint32_t profilePointer = 0, linkedGetter = 0x414F80;
uint32_t tick = 100, frame = 200;
bool supported = true, memoryFault = false;
int bindCalls = 0, forwarded = 0, synchronousNodes = 0;
uint32_t lastDescriptor = 0;
uintptr_t lastBindActor = 0, lastArgs = 0;
uintptr_t lastSubmittedInstance = 0, lastInputWorld = 0;
cm::Matrix lastCopiedMatrix;

template<class T> void Put(void* base, size_t offset, T value)
{
    memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(value));
}
template<class T = uint32_t> T Get(const void* base, size_t offset)
{
    T value;
    memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(value));
    return value;
}
uint32_t Ptr(const void* p) { return uint32_t(uintptr_t(p)); }
cm::Matrix Identity(float x = 0)
{
    cm::Matrix m;
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1;
    m.m[12] = x;
    return m;
}
cm::Matrix Multiply(const cm::Matrix& a, const cm::Matrix& b)
{
    cm::Matrix out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k)
                out.m[r*4+c] += a.m[r*4+k] * b.m[k*4+c];
    return out;
}
uintptr_t Operand(int tier) { return code + operandA[tier] - unit; }
uint32_t Instance(int actor) { return Get(actors[actor], 0x128); }
void __cdecl FakeBind(void* actor, const uint32_t* args)
{
    ++bindCalls;
    lastDescriptor = args[0]; lastBindActor = uintptr_t(actor); lastArgs = uintptr_t(args);
    const uint32_t instance = Get(reinterpret_cast<void*>(uintptr_t(args[0])), 0);
    Put(actor, 0x128, instance);
    Put(actor, 0x12C, uint32_t(0));
    Put(actor, 0x144, uint32_t(0));
    memcpy(static_cast<uint8_t*>(actor) + 0xD0,
        reinterpret_cast<void*>(uintptr_t(instance) + 0x10), 16);
}
void DispatchBind(int actor, uint32_t descriptor)
{
    const uint32_t args[] = {descriptor};
    if (!cm::BindBuiltinSword(actors[actor], args, &FakeBind)) {
        ++forwarded;
        FakeBind(actors[actor], args);
    }
}
// Free-function stand-ins for thiscall trampolines use fastcall's dummy EDX,
// the same detour convention as production. ECX and the stack arguments match.
void __fastcall FakeNodes(void*, void*, void* mesh, const cm::Matrix* world,
    const cm::Matrix* nodes)
{
    ++synchronousNodes;
    uint32_t at = Ptr(mesh);
    while (at) {
        const uint16_t index = Get<uint16_t>(reinterpret_cast<void*>(uintptr_t(at)), 0x3C);
        const cm::Matrix copied = index == 0xFFFF ? *world : Multiply(nodes[index], *world);
        const uint32_t n = Get(renderer, 0x45F70);
        memcpy(renderer + 0xDFB0 + n * sizeof(cm::Matrix), &copied, sizeof(copied));
        Put(renderer, 0x45F70, n + 1);
        lastCopiedMatrix = copied;
        at = Get(reinterpret_cast<void*>(uintptr_t(at)), 0x44);
    }
}
cm::NodeSubmitFn NodeOriginal()
{
    return reinterpret_cast<cm::NodeSubmitFn>(&FakeNodes);
}
void NodeDetour(void* self, void* mesh, const cm::Matrix* world, const cm::Matrix* nodes)
{
    if (!cm::SubmitNodes(self, mesh, world, nodes, NodeOriginal()))
        NodeOriginal()(self, mesh, world, nodes);
}
void __fastcall FakeEntity(void* self, void*, void* actor, void* instance,
    const cm::Matrix* world)
{
    lastSubmittedInstance = uintptr_t(instance); lastInputWorld = uintptr_t(world);
    const uint32_t q = Get(renderer, 0xAFAC);
    auto* record = renderer + 0x7C8 + q * 0x1C;
    const uint32_t n = Get(renderer, 0x45F70);
    Put(renderer, 0xAFAC, q + 1);
    Put(record, 0, Ptr(actor)); Put(record, 4, Ptr(instance));
    // Matches 0x44A503: renderer matrix storage, NEVER the world argument.
    Put(record, 8, Ptr(renderer + 0xDFB0 + n * sizeof(cm::Matrix)));
    Put(instance, 0x30, frame);
    const auto* nodes = reinterpret_cast<const cm::Matrix*>(
        uintptr_t(Get(actor, 0x13C)));
    NodeDetour(self, reinterpret_cast<void*>(uintptr_t(Get(instance, 0))), world, nodes);
    Put(record, 0xC, Get(renderer, 0x45F70) - n);
    const uint32_t second = Get(instance, 4);
    if (second) {
        NodeDetour(self, reinterpret_cast<void*>(uintptr_t(second)), world, nodes);
        Put(renderer, 0x50758, Get(renderer, 0x50758) + 1);
        Put(renderer, 0x5275C, Get(renderer, 0x5275C) + 1);
    }
}
cm::EntitySubmitFn EntityOriginal()
{
    return reinterpret_cast<cm::EntitySubmitFn>(&FakeEntity);
}
void Continuation(void* self, void* actor, void* instance, const cm::Matrix* world)
{
    ++forwarded;
    EntityOriginal()(self, actor, instance, world);
}
void Submit(int actor, const cm::Matrix& world)
{
    cm::SubmitEntity(renderer, actors[actor], reinterpret_cast<void*>(uintptr_t(Instance(actor))),
        &world, &Continuation, EntityOriginal());
}
bool StartFrame()
{
    memset(renderer, 0, sizeof(renderer)); // native queue init is after BeginFrame
    if (!cm::BeginFrame(renderer, true)) return false;
    ++frame; // 0x43E170 increments before any native submissions
    return true;
}
void Reset()
{
    cm::EndFrame(); cm::ClearAll();
    memset(actors, 0, sizeof(actors)); memset(classes, 0, sizeof(classes));
    memset(builtin, 0, sizeof(builtin)); memset(builtinMeshes, 0, sizeof(builtinMeshes));
    memset(models, 0, sizeof(models)); memset(meshes, 0, sizeof(meshes));
    memset(renderer, 0, sizeof(renderer)); memset(registry, 0, sizeof(registry));
    memset(controllers, 0, sizeof(controllers)); memset(pool, 0, 0x10000);
    memcpy(classNames[0], "Ninja", sizeof("Ninja"));
    memcpy(classNames[1], "NinjaSword", sizeof("NinjaSword"));
    memcpy(classNames[2], "Ranx", sizeof("Ranx"));
    memcpy(classNames[3], "NinjaSword", sizeof("NinjaSword"));
    memoryFault = false; supported = true;
    bindCalls = forwarded = synchronousNodes = 0;
    lastDescriptor = 0; lastBindActor = lastArgs = 0;
    lastSubmittedInstance = lastInputWorld = 0;
    linkedGetter = 0x414F80;
    for (int i = 0; i < 4; ++i) {
        ids[i] = {Ptr(actors[i]), Ptr(classes[i]), uint32_t(0x200 + i)};
        Put(actors[i], 0x1A0, ids[i].handle);
        Put(actors[i], 0x1A8, ids[i].pClass);
        Put(classes[i], 4, Ptr(classNames[i]));
        registry[i*2] = ids[i].handle; registry[i*2+1] = ids[i].addr;
        nodeBlocks[i].header[0] = 1;
        nodeBlocks[i].header[1] = Ptr(nodeBlocks[i].pose + 2);
        nodeBlocks[i].header[2] = 0x12345678;
        nodeBlocks[i].header[3] = 0;
        nodeBlocks[i].pose[0] = Identity(float(i + 1));
        nodeBlocks[i].pose[1] = Identity();
        skeletons[i] = 1;
        Put(actors[i], 0x13C, Ptr(nodeBlocks[i].pose));
        Put(actors[i], 0x140, uint32_t(1));
        Put(actors[i], 0x268, Ptr(&skeletons[i]));
        Put(actors[i], 0x26C, Ptr(controllers[i]));
        Put(actors[i], 0x2C0, uint32_t(0x55667788)); // NOT sword global 97
        Put(controllers[i], 0x24, uint32_t(1));
        for (int pass = 0; pass < 2; ++pass)
            Put(meshes[i][pass], 0x3C, uint16_t(0));
        Put(models[i], 0, Ptr(meshes[i][0]));
        Put(models[i], 4, Ptr(meshes[i][1]));
        Put(actors[i], 0x128, Ptr(models[i]));
        const cm::Matrix root = Identity(float(i * 10));
        memcpy(actors[i] + 0x10, &root, sizeof(root));
    }
    Put(classes[Ninja], 0xC, uint32_t(code));
    Put(classes[Ninja], 8, uint32_t(code + 0x4BA6F0 - unit));
    Put(reinterpret_cast<void*>(code), 0, uint32_t(0x20));
    Put(reinterpret_cast<void*>(code), 4, uint32_t(0x34C8));
    Put(reinterpret_cast<void*>(code + 0x4B8670 - unit), 0, uint32_t(0x32740011));
    Put(reinterpret_cast<void*>(code + 0x4B8670 - unit), 4, uint32_t(0x81747400));
    Put(reinterpret_cast<void*>(code + 0x4B8670 - unit), 8, linkedGetter);
    for (int i = 0; i < 6; ++i) {
        descriptors[i] = uint32_t(code - (unit - descriptorA[i]));
        Put(reinterpret_cast<void*>(Operand(i)), size_t(0), descriptors[i]);
        Put(reinterpret_cast<void*>(Operand(i) - 4), 0, uint32_t(0x5A740000));
        Put(reinterpret_cast<void*>(Operand(i) + 4), 0, uint32_t(0x43747061));
        Put(reinterpret_cast<void*>(uintptr_t(descriptors[i])), 0, Ptr(builtin[i]));
        Put(builtin[i], 0, Ptr(builtinMeshes[i]));
        Put(builtinMeshes[i], 0x3C, uint16_t(0));
        for (int j = 0; j < 4; ++j)
            Put(builtin[i], 0x10 + j*4, float(100 + i*10 + j));
    }
    Put(actors[Sword], 0x128, Ptr(builtin[0]));
    Put(actors[OtherSword], 0x128, Ptr(builtin[1]));
    memcpy(actors[Sword] + 0xD0, builtin[0] + 0x10, 16);
    for (unsigned i = 0; i < sizeof(profile); ++i) profile[i] = uint8_t(i ^ 0xA5);
    Put(profile, 0x78, int32_t(3)); // real tier
    memcpy(profileBefore, profile, sizeof(profile));
    profilePointer = Ptr(profile);
}
bool ProfileUnchanged() { return !memcmp(profile, profileBefore, sizeof(profile)); }
bool BoundsEqual(const void* a, const void* b)
{
    return !memcmp(static_cast<const uint8_t*>(a) + 0xD0,
        static_cast<const uint8_t*>(b) + 0x10, 16);
}
struct WorkerResult { bool ok = false; };
DWORD WINAPI Worker(void* output)
{
    cm::Actor capture;
    bool ok = !cm::CaptureActor(uintptr_t(actors[Ninja]), capture);
    ok &= !cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 2);
    ok &= !cm::ApplyBuiltinSword(&FakeBind);
    const uint32_t args[] = {descriptors[0]};
    ok &= !cm::BindBuiltinSword(actors[Sword], args, &FakeBind);
    ok &= !cm::Select(cm::Channel::Player, ids[Ninja], ids[Donor]);
    ok &= !cm::BeginFrame(renderer, true);
    cm::BeforeFlush(renderer); cm::EndFrame(); cm::ClearAll();
    static_cast<WorkerResult*>(output)->ok = ok;
    return 0;
}
} // namespace

namespace eth {
uintptr_t GameAddr(uintptr_t va)
{
    if (va == game::off::StratMgrObject) return uintptr_t(registry);
    if (va == 0x4C87C8 + 0x3E * 4) return uintptr_t(&linkedGetter);
    if (va == game::off::pProfile) return uintptr_t(&profilePointer);
    return 0;
}
bool ValidateGameImage() { return supported; }
bool SafeReadBytes(uintptr_t address, void* output, size_t size)
{
    if (memoryFault || !address || !output || !size ||
        address > UINT32_MAX || size > UINT32_MAX - address) return false;
    __try { memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SafeWriteBytes(uintptr_t address, const void* input, size_t size)
{
    if (memoryFault || !address || !input || !size ||
        address > UINT32_MAX || size > UINT32_MAX - address) return false;
    __try { memcpy(reinterpret_cast<void*>(address), input, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
namespace game {
uint32_t TickCount() { return tick; }
uint32_t FrameCount() { return frame; }
void* Renderer() { return renderer; }
}
} // namespace eth

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main()
{
    // Honor the current production resolver's admitted address range. A random
    // VirtualAlloc address may fall below `unit` on this Windows installation;
    // that is a fixture allocation failure, not an observed game-pool address.
    for (uintptr_t address=0x10000000;!pool && address<0x30000000;address+=0x100000)
        pool = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(address),0x10000,
            MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE));
    CHECK(pool);
    code = uintptr_t(pool) + 0x3000;
    CHECK(code >= unit); // synthetic live pool, NEVER executable VAs/A offsets
    Reset();
    cm::Actor captured;
    CHECK(cm::CaptureActor(uintptr_t(actors[Ninja]), captured));
    CHECK(captured.addr == ids[Ninja].addr && captured.pClass == ids[Ninja].pClass &&
        captured.handle == ids[Ninja].handle);
    CHECK(!cm::CaptureActor(0, captured) && !captured.addr);
    CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], -1));
    CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 6));
    CHECK(!cm::SelectBuiltinSword(ids[Donor], ids[Sword], 1));
    CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Ninja], 1));
    CHECK(!cm::Select(static_cast<cm::Channel>(99), ids[Ninja], ids[Donor]));
    CHECK(!cm::Select(cm::Channel::Player, ids[Sword], ids[Donor]));
    CHECK(!cm::Select(cm::Channel::Sword, ids[Ninja], ids[Donor]));
    CHECK(!cm::Select(cm::Channel::Player, ids[Ninja], ids[Ninja]));

    // Every appearance and real-tier request: exact args, unchanged profile,
    // untouched skeleton/controller/global-lookalike, real requested bounds.
    for (int appearance = 0; appearance < 6; ++appearance) {
        CHECK(cm::SelectBuiltinSword(ids[Ninja], ids[Sword], appearance));
        const uint32_t skeleton = Get(actors[Sword], 0x268);
        const uint32_t animation = Get(actors[Sword], 0x26C);
        for (int real = 0; real < 6; ++real) {
            const int before = bindCalls;
            DispatchBind(Sword, descriptors[real]);
            CHECK(bindCalls == before + 1 && lastDescriptor == descriptors[appearance]);
            CHECK(lastBindActor == uintptr_t(actors[Sword]) && forwarded == 0);
            CHECK(Instance(Sword) == Ptr(builtin[appearance]));
            CHECK(BoundsEqual(actors[Sword], builtin[real]));
            CHECK(Get(actors[Sword], 0x268) == skeleton);
            CHECK(Get(actors[Sword], 0x26C) == animation);
            CHECK(Get(actors[Sword], 0x2C0) == 0x55667788 && ProfileUnchanged());
        }
    }
    cm::ClearBuiltinSword();
    DispatchBind(Sword, descriptors[3]);
    CHECK(lastDescriptor == descriptors[3] && forwarded == 1 && ProfileUnchanged());

    Reset();
    CHECK(cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 5));
    float bounds[4] = {1, 2, 3, 4};
    memcpy(actors[Sword] + 0xD0, bounds, sizeof(bounds));
    CHECK(cm::ApplyBuiltinSword(&FakeBind));
    CHECK(lastDescriptor == descriptors[5] && !memcmp(actors[Sword] + 0xD0, bounds, 16));
    CHECK(ProfileUnchanged());
    const uint32_t realArgs[] = {descriptors[2]};
    const int calls = bindCalls;
    CHECK(!cm::BindBuiltinSword(actors[OtherSword], realArgs, &FakeBind));
    CHECK(bindCalls == calls); // explicit recipient, not a guessed owner relationship
    const uint32_t hide[] = {0};
    CHECK(!cm::BindBuiltinSword(actors[Sword], hide, &FakeBind));
    CHECK(!cm::BindBuiltinSword(actors[Sword], nullptr, &FakeBind));
    CHECK(!cm::BindBuiltinSword(actors[Sword], realArgs, nullptr));
    CHECK(!cm::ApplyBuiltinSword(nullptr));

    // Live code/descriptors must be canonical, relocated and fingerprinted.
    for (int i = 0; i < 6; ++i) {
        Reset();
        Put(reinterpret_cast<void*>(Operand(i)), 0, descriptorA[i]); // serialized offset
        CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], i));
        Reset();
        Put(reinterpret_cast<void*>(Operand(i) - 4), 0, uint32_t(0x5A750000));
        CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], i));
        Reset();
        Put(reinterpret_cast<void*>(Operand(i) + 4), 0, uint32_t(0x43741061));
        CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], i));
        for (int word = 0; word < 4; ++word) {
            Reset();
            Put(reinterpret_cast<void*>(uintptr_t(descriptors[i])), word*4,
                word == 0 ? uint32_t(0) : uint32_t(1));
            CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], i));
        }
        Reset();
        Put(builtinMeshes[i], 0x3C, uint16_t(1)); // count==1, including nonselected tier
        CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], (i + 1) % 6));
    }
    Reset(); Put(classes[Ninja], 8, uint32_t(code));
    CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 0));
    Reset(); Put(reinterpret_cast<void*>(code), 4, uint32_t(0x34C9));
    CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 0));
    Reset(); ++linkedGetter;
    CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 0));
    Reset(); memoryFault = true;
    CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 0));
    memoryFault = false;

    Reset();
    CHECK(cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 4));
    Put(actors[Sword], 0x144, uint32_t(1));
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));
    Put(actors[Sword], 0x144, uint32_t(0));
    Put(actors[Sword], 0x2FC, uint32_t(0x123456));
    CHECK(!cm::ApplyBuiltinSword(&FakeBind)); // never destroy auxiliary physics
    Put(actors[Sword], 0x2FC, uint32_t(0));
    Put(builtin[4], 0x34, uint32_t(0x100));
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));
    Put(builtin[4], 0x34, uint32_t(0));
    Put(actors[Sword], 0x128, Ptr(models[Sword])); // tint/private/noncanonical instance
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));
    CHECK(cm::BindBuiltinSword(actors[Sword], realArgs, &FakeBind)); // normal refresh still works
    CHECK(lastDescriptor == descriptors[4] && ProfileUnchanged());

    // Generation failure disarms permanently; repairing bytes does not re-arm.
    Reset();
    CHECK(cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 2));
    ++registry[Sword*2];
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));
    --registry[Sword*2];
    CHECK(!cm::ApplyBuiltinSword(&FakeBind) && bindCalls == 0);
    CHECK(cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 2));
    Put(actors[Ninja], 0x1A0, ids[Ninja].handle + 0x200);
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));
    Put(actors[Ninja], 0x1A0, ids[Ninja].handle);
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));
    Reset();
    CHECK(cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 2));
    Put(actors[Sword], 0x1A8, Ptr(classes[OtherSword]));
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));
    Put(actors[Sword], 0x1A8, Ptr(classes[Sword]));
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));

    Reset();
    CHECK(!cm::BeginFrame(renderer, false));
    supported = false; CHECK(!cm::BeginFrame(renderer, true)); supported = true;
    CHECK(!cm::BeginFrame(actors[Ninja], true)); // wrong renderer
    CHECK(cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 1));
    CHECK(cm::Select(cm::Channel::Player, ids[Ninja], ids[Donor]));
    CHECK(StartFrame());
    CHECK(!cm::BeginFrame(renderer, true));
    CHECK(!cm::ApplyBuiltinSword(&FakeBind));
    CHECK(!cm::SelectBuiltinSword(ids[Ninja], ids[Sword], 2));
    // Wrong-thread operations must not disable the in-progress main-thread frame.
    WorkerResult workerResult;
    HANDLE worker = CreateThread(nullptr, 0, &Worker, &workerResult, 0, nullptr);
    CHECK(worker);
    const DWORD wait = WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    CHECK(wait == WAIT_OBJECT_0 && workerResult.ok && bindCalls == 0);

    const cm::Matrix root = Identity(100);
    Submit(Ninja, root); // source not submitted yet: MUST use native recipient
    CHECK(lastSubmittedInstance == Ptr(models[Ninja]));
    CHECK(cm::Stats().waitingForDonor == 1 && cm::Stats().substituted == 0);
    nodeBlocks[Donor].pose[0] = Identity(7);
    Submit(Donor, root);
    CHECK(cm::Stats().captured == 1);
    nodeBlocks[Donor].pose[0] = Identity(99); // copied/frozen donor pose for this frame
    Submit(Ninja, root);
    CHECK(lastSubmittedInstance == Ptr(models[Donor]) && cm::Stats().substituted == 1);
    CHECK(lastCopiedMatrix.m[12] == 107 && root.m[12] == 100);
    CHECK(cm::g_scope == nullptr); // synchronous nested context always restored
    const uint32_t q = Get(renderer, 0xAFAC) - 1;
    auto* record = renderer + 0x7C8 + q * 0x1C;
    CHECK(Get(record, 8) >= Ptr(renderer + 0xDFB0));
    CHECK(Get(record, 8) < Ptr(renderer + 0x45F70));
    CHECK(Get(record, 8) != lastInputWorld && Get(record, 0xC) == 1);
    const auto* queuedWorld = reinterpret_cast<const cm::Matrix*>(uintptr_t(Get(record, 8)));
    CHECK(queuedWorld->m[12] == 107); // readable AFTER SubmitEntity's scope returned
    CHECK(Get(actors[Ninja], 0x128) == Ptr(models[Ninja])); // actor never rebound
    CHECK(ProfileUnchanged());
    cm::BeforeFlush(actors[Ninja]); // wrong renderer: no fence
    Submit(Ninja, root);
    CHECK(cm::Stats().substituted == 2);
    cm::BeforeFlush(renderer);
    Submit(Ninja, root);
    CHECK(lastSubmittedInstance == Ptr(models[Ninja]) && cm::Stats().substituted == 2);
    cm::EndFrame();
    CHECK(cm::g_lanes[0].snapshot.instance == 0 && cm::g_lanes[0].snapshot.nodes == 0 &&
        cm::g_lanes[0].snapshot.mesh[0] == 0 && cm::g_lanes[0].record[0] == 0);
    CHECK(StartFrame());
    Submit(Ninja, root);
    CHECK(cm::Stats().substituted == 0 && cm::Stats().waitingForDonor == 1);
    cm::EndFrame(); // no donor cache crosses frames

    // Independent recipient/donor channels.
    Reset();
    CHECK(cm::Select(cm::Channel::Player, ids[Ninja], ids[Donor]));
    CHECK(cm::Select(cm::Channel::Sword, ids[Sword], ids[OtherSword]));
    CHECK(StartFrame());
    Submit(Donor, root); Submit(OtherSword, root);
    Submit(Ninja, root); CHECK(lastSubmittedInstance == Ptr(models[Donor]));
    Submit(Sword, root); CHECK(lastSubmittedInstance == Ptr(builtin[1]));
    CHECK(cm::Stats().captured == 2 && cm::Stats().substituted == 2);
    cm::EndFrame();

    // Both chains, signed node indices, modes, pending identity and capacity.
    for (int defect = 0; defect < 7; ++defect) {
        Reset();
        CHECK(cm::Select(cm::Channel::Player, ids[Ninja], ids[Donor]));
        CHECK(StartFrame());
        Submit(Donor, root);
        switch (defect) {
        case 0: Put(meshes[Donor][1], 0x3C, uint16_t(1)); break;
        case 1: Put(meshes[Donor][0], 0x3C, uint16_t(0x8000)); break;
        case 2: Put(actors[Donor], 0x144, uint32_t(1)); break;
        case 3: Put(actors[Ninja], 0x144, uint32_t(1)); break;
        case 4: Put(renderer, 0x7C8 + 4, Ptr(models[Ninja])); break;
        case 5: Put(renderer, 0x45F70, uint32_t(cm::kMatrixCapacity)); break;
        case 6: ++tick; break;
        }
        // For capacity defect don't let synthetic vanilla rendering itself
        // overflow; a refusal is observable without enqueuing a fallback draw.
        if (defect == 5) {
            cm::SubmitEntity(renderer, actors[Ninja], models[Ninja], &root, nullptr, EntityOriginal());
            CHECK(cm::Stats().substituted == 0);
        } else {
            Submit(Ninja, root);
            CHECK(lastSubmittedInstance == Ptr(models[Ninja]) && cm::Stats().substituted == 0);
        }
        cm::EndFrame();
    }
    Reset();
    CHECK(cm::Select(cm::Channel::Player, ids[Ninja], ids[Donor]));
    CHECK(StartFrame()); Submit(Donor, root);
    Put(actors[Donor], 0x1A0, ids[Donor].handle + 0x200);
    Submit(Ninja, root); CHECK(cm::Stats().substituted == 0);
    Put(actors[Donor], 0x1A0, ids[Donor].handle);
    cm::EndFrame();
    CHECK(!StartFrame()); Submit(Donor, root); Submit(Ninja, root);
    // No active lane means no new cosmetic frame: telemetry retains the last
    // active frame, but the repaired donor must not re-arm or substitute.
    CHECK(cm::Stats().captured == 1 && cm::Stats().substituted == 0);
    cm::EndFrame(); cm::ClearAll();
    CHECK(ProfileUnchanged());
    CHECK(VirtualFree(pool, 0, MEM_RELEASE));
    puts("PASS cosmetic models: six canonical swords, args/bounds/profile, identity/thread guards, donor order, copied matrices, frame/flush fences");
    return 0;
}
