#include "precomp.h"
#include "cosmetic_models.h"
#include "game.h"
#include "util.h"

// Static evidence / supported build:
// I-Ninja.exe SHA256 49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0
// REA ev_2046b695d993cf140a87f1b33263f69427b408bc2a651f93fd8e0e219d4c69b6:
// 0x43D8F0 pumps messages, completes ticks, then calls 0x43E170 synchronously.
// ev_9b02f4c69e1ecacbd05d2519f78f025f476d9857d056910ee7130114aa9dc348:
// AppRender initializes queues (0x449E00), submits, then calls 0x44A340.
// ev_be50c918e1698de81185acaffd0de82a4dc3c342eb5cf9a2acecfd3d8186b033,
// ev_31626eeeb56ea96899e99446ebbbddcba07ee011bbe899fa1cd4f22ac23e3c27:
// 0x44A340 consumes queues and later 0x44EB80 RESETS queues for another scene.
// ev_2046... also covers opaque/transparency consumers 0x44D890/0x44C5C0.
// Virtual render/device calls prevent a universal proof against arbitrary mods.
// Instead of assuming every loaded actor survives AppRender, this prototype
// borrows ONLY an instance already pending in the native scene queue. Its meshes
// must already survive that same scene's native draws. It adds no cross-tick,
// cross-scene or resource-pool lifetime. This is a conditional native-lifetime
// argument, NOT observed runtime validation or an independent resource pin.
// See docs/trainer-cosmetic-research.md for queue/matrix layouts and copy ABI.
//
// No scratch strat/native clone: 0x43E5D0 merely binds a borrowed pointer;
// 0x454240 also retains resource references whose pool lifetime is unresolved.
// The six serialized sword descriptors are NOT process-addressable instances.

namespace eth::cosmetic::models {
namespace {
constexpr uint32_t kMaxNodes = patch::entity::kMaxNodes;
constexpr uintptr_t kQueue = 0x7C8, kQueueCount = 0xAFAC;
constexpr uintptr_t kMatrixCount = 0x45F70;
constexpr uint32_t kMatrixCapacity = (0x45F70 - 0xDFB0) / sizeof(Matrix);

struct Snapshot {
    Actor actor;
    uint32_t instance = 0, nodes = 0, count = 0;
    uint32_t model = 0, animation = 0, mesh[2]{};
    uint32_t links = 0;
    alignas(16) Matrix pose[kMaxNodes]{};
};
struct Lane {
    bool selected = false, captured = false;
    Actor recipient, donor;
    Snapshot snapshot;
    uint32_t queueIndex = 0, record[4]{};
};
struct Scope {
    Snapshot* donor = nullptr;
    void* renderer = nullptr;
    // Stack lifetime is intentional: 0x44A503 stores a renderer-owned matrix
    // queue pointer at primary-record+8, NOT this input world pointer.
    // 0x44A537 stores matrix count at +0xC. Both calls to 0x449EB0 synchronously
    // copy computed matrices (0x449FEB..0x449FF0). The remaining world reads
    // (including 0x44A6F1..0x44A725) compute sorting inputs before returning.
    Matrix world;
};
Lane g_lanes[2];
std::atomic<DWORD> g_thread{0};
std::atomic<bool> g_anyDonor{false};
bool g_frame = false, g_building = false;
void* g_renderer = nullptr;
uint32_t g_tick = 0, g_nativeFrame = 0;
Scope* g_scope = nullptr;
FrameStats g_stats;
struct BuiltinSword {
    bool selected = false;
    Actor ninja, sword;
    int tier = 0;
};
BuiltinSword g_builtin;

void DropCaptured(Lane& lane)
{
    lane.captured = false;
    lane.snapshot.actor = {};
    lane.snapshot.instance = lane.snapshot.nodes = lane.snapshot.count = 0;
    lane.snapshot.model = lane.snapshot.animation = lane.snapshot.links = 0;
    lane.snapshot.mesh[0] = lane.snapshot.mesh[1] = 0;
    lane.queueIndex = 0;
    memset(lane.record, 0, sizeof(lane.record));
    // pose contains copied floats only; no engine pointers or allocation.
}

bool Thread()
{
    const DWORD current = GetCurrentThreadId();
    DWORD empty = 0;
    g_thread.compare_exchange_strong(empty, current);
    return g_thread.load() == current;
}

Lane* GetLane(Channel channel)
{
    if (channel == Channel::Player) return &g_lanes[0];
    if (channel == Channel::Sword) return &g_lanes[1];
    return nullptr;
}

bool Equal(const Actor& a, const Actor& b)
{
    return a.addr == b.addr && a.pClass == b.pClass && a.handle == b.handle;
}

bool Live(const Actor& a, const char* exactClass = nullptr)
{
    if (!a.addr || a.addr > UINT32_MAX - 0x300 || !a.pClass || !a.handle)
        return false;
    uint32_t handle = 0, pClass = 0;
    char name[game::kClassNameCapacity]{};
    return SafeRead(a.addr + 0x1A0, handle) && handle == a.handle &&
        SafeRead(a.addr + 0x1A8, pClass) && pClass == a.pClass &&
        game::ResolveOwnerHandle(game::A(game::off::StratMgrObject),
            a.handle, SafeReadBytes) == a.addr &&
        game::ReadClassName(a.pClass, name, SafeReadBytes) &&
        (!exactClass || !strcmp(name, exactClass));
}

bool Building(void* renderer)
{
    return g_frame && g_building && renderer == g_renderer &&
        game::TickCount() == g_tick && game::FrameCount() == g_nativeFrame;
}

bool Meshes(Snapshot& s)
{
    s.links = 0;
    if (!s.mesh[0]) return false;
    for (uint32_t head : s.mesh) {
        uint32_t mesh = head;
        unsigned length = 0;
        while (mesh) {
            if (mesh > UINT32_MAX - 0x48 || length++ == 512) return false;
            uint32_t fields[3]{};
            if (!SafeReadBytes(mesh + 0x3C, fields, sizeof(fields))) return false;
            const uint16_t index = uint16_t(fields[0]);
            if (index != 0xFFFF && (index >= s.count || index >= 0x8000))
                return false;
            mesh = fields[2];
            ++s.links;
        }
    }
    return true;
}

bool ReadSnapshot(const Actor& a, uint32_t instance, Snapshot& s, bool copyPose)
{
    if (!Live(a) || !instance || instance > UINT32_MAX - 0x50) return false;
    uint32_t slots[8]{}, resources[2]{};
    if (!SafeReadBytes(a.addr + 0x128, slots, sizeof(slots)) ||
        slots[0] != instance || slots[7] != 0 ||
        !SafeReadBytes(a.addr + 0x268, resources, sizeof(resources)) ||
        !SafeReadBytes(instance, s.mesh, sizeof(s.mesh))) return false;
    s.actor = a; s.instance = instance; s.nodes = slots[5]; s.count = slots[6];
    s.model = resources[0]; s.animation = resources[1];
    if (s.count > kMaxNodes || !Meshes(s)) return false;
    if (s.count) {
        patch::entity::Key key;
        if (!patch::entity::ReadKey(a.addr, game::A(game::off::StratMgrObject),
            key, SafeReadBytes) || key.instanceMode || key.instance != instance)
            return false;
        if (copyPose) {
            if (!SafeReadBytes(s.nodes, s.pose, s.count * sizeof(Matrix))) return false;
            patch::entity::Affine check;
            for (uint32_t i = 0; i < s.count; ++i)
                if (!patch::entity::Decompose(s.pose[i], check)) return false;
        }
    }
    // World-only static models can have no skeleton/controller. Indexed meshes
    // have already been refused when count==0. No guessed bind-pose synthesis.
    uint32_t finalSlots[8]{}, finalResources[2]{};
    return SafeReadBytes(a.addr + 0x128, finalSlots, sizeof(finalSlots)) &&
        !memcmp(slots, finalSlots, sizeof(slots)) &&
        SafeReadBytes(a.addr + 0x268, finalResources, sizeof(finalResources)) &&
        !memcmp(resources, finalResources, sizeof(resources)) && Live(a);
}

bool Pending(const Lane& lane, void* renderer)
{
    uint32_t count = 0, record[4]{}, frame = 0;
    return SafeRead(uintptr_t(renderer) + kQueueCount, count) &&
        count <= 0x5FF && lane.queueIndex < count &&
        SafeReadBytes(uintptr_t(renderer) + kQueue + lane.queueIndex * 0x1C,
            record, sizeof(record)) &&
        !memcmp(record, lane.record, sizeof(record)) &&
        record[0] == lane.donor.addr && record[1] == lane.snapshot.instance &&
        SafeRead(lane.snapshot.instance + 0x30, frame) && frame == g_nativeFrame;
}

bool SameStorage(const Snapshot& a, const Snapshot& b)
{
    return Equal(a.actor, b.actor) && a.instance == b.instance &&
        a.nodes == b.nodes && a.count == b.count && a.model == b.model &&
        a.animation == b.animation && !memcmp(a.mesh, b.mesh, sizeof(a.mesh));
}

void Observe(void* renderer, void* owner, void* instance)
{
    if (!Building(renderer)) return;
    for (Lane& lane : g_lanes) {
        if (!lane.selected || lane.captured || lane.donor.addr != uintptr_t(owner))
            continue;
        uint32_t count = 0, record[4]{};
        if (!SafeRead(uintptr_t(renderer) + kQueueCount, count) ||
            !count || count > 0x5FF ||
            !SafeReadBytes(uintptr_t(renderer) + kQueue + (count - 1) * 0x1C,
                record, sizeof(record)) ||
            record[0] != uintptr_t(owner) || record[1] != uintptr_t(instance) ||
            !record[3] ||
            !ReadSnapshot(lane.donor, uint32_t(uintptr_t(instance)), lane.snapshot, true)) {
            ++g_stats.refused;
            continue;
        }
        lane.queueIndex = count - 1;
        memcpy(lane.record, record, sizeof(record));
        lane.captured = Pending(lane, renderer);
        if (lane.captured) ++g_stats.captured;
    }
}

Snapshot* Choose(void* renderer, void* owner, void* instance, const Matrix* world)
{
    if (!Building(renderer) || !world) return nullptr;
    for (Lane& lane : g_lanes) {
        if (!lane.selected || lane.recipient.addr != uintptr_t(owner)) continue;
        if (!Live(lane.recipient) || !Live(lane.donor)) {
            lane.selected = lane.captured = false; // Never reacquire on respawn.
            ++g_stats.refused;
            return nullptr;
        }
        if (!lane.captured) { ++g_stats.waitingForDonor; return nullptr; }
        Snapshot donorNow, recipient;
        Matrix root;
        patch::entity::Affine affine;
        uint32_t matrices = 0, queued = 0, transparent = 0, sorted = 0;
        if (!Pending(lane, renderer) ||
            !ReadSnapshot(lane.donor, lane.snapshot.instance, donorNow, false) ||
            !SameStorage(lane.snapshot, donorNow) ||
            !ReadSnapshot(lane.recipient, uint32_t(uintptr_t(instance)), recipient, false) ||
            donorNow.links > recipient.links ||
            !SafeReadBytes(uintptr_t(world), &root, sizeof(root)) ||
            !patch::entity::Decompose(root, affine) ||
            !SafeRead(uintptr_t(renderer) + kMatrixCount, matrices) ||
            matrices > kMatrixCapacity || donorNow.links > kMatrixCapacity - matrices ||
            !SafeRead(uintptr_t(renderer) + kQueueCount, queued) || queued >= 0x5FD ||
            !SafeRead(uintptr_t(renderer) + 0x50758, transparent) || transparent >= 0x5FD ||
            !SafeRead(uintptr_t(renderer) + 0x5275C, sorted) || sorted >= 0x7FE) {
            ++g_stats.refused;
            return nullptr;
        }
        return &lane.snapshot;
    }
    return nullptr;
}

// Packed op 0x43747061 is NOT an actor +0x2C0 property write. VM selector 0x70
// dispatches through 0x425612 to [0x5111A0 + index*4]; 0x422C7061 reads that same
// global at 0x425404. Native getter 0x421680 case 0x61 is a DIFFERENT selector.
// Confirmed offline jump-table entries:
// read selector 0x70 -> 0x425404; write selector 0x70 -> 0x425612.
// In NinjaSword helper A+0x4C35A0 the global value goes straight to native 0/29.
// REA ev_7fd658c69f3c1dae9b550a33f29d55c28b032021a28e779e41e1b29a92b4ef44:
// 0x410C40 releases render slots and optional +0x2FC auxiliary storage, then
// binds descriptor[0] and updates render flags. It does not change +0x268/+0x26C
// or profile tier. This is the game's actual six-tier sword binding recipe.
bool SwordResources(uint32_t (&descriptors)[6], uint32_t (&instances)[6])
{
    if (!g_builtin.selected || !Live(g_builtin.ninja, "Ninja") ||
        !Live(g_builtin.sword, "NinjaSword")) {
        g_builtin.selected = false;
        return false;
    }
    constexpr uint32_t unit = 0x4B5F20;
    constexpr uint32_t operands[6] = {
        0x4B86A8, 0x4B86B8, 0x4B86C8, 0x4B86D8, 0x4B86E8, 0x4B86F8
    };
    constexpr uint32_t serialized[6] = {
        0x4B3CC0, 0x4B3CD0, 0x4B3CE0, 0x4B3CF0, 0x4B3D00, 0x4B3D10
    };
    uint32_t code = 0, entry = 0, header[2]{}, getter = 0;
    if (!SafeRead(g_builtin.ninja.pClass + 0xC, code) || (code & 3) ||
        code < unit || code > UINT32_MAX - 0xD320 ||
        !SafeRead(g_builtin.ninja.pClass + 8, entry) ||
        entry != code + (0x4BA6F0 - unit) ||
        !SafeReadBytes(code, header, sizeof(header)) ||
        header[0] != 0x20 || header[1] != 0x34C8 ||
        !SafeRead(game::A(0x4C87C8 + 0x3E * 4), getter)) return false;
    // Linked native operands are executable pointers, whereas pointer literals
    // are loader-relocated A-pool addresses. Require BOTH expected form and
    // exact contiguous A-pool relation; never treat serialized offsets as VAs.
    uint32_t selector[3]{};
    if (!SafeReadBytes(code + 0x4B8670 - unit, selector, sizeof(selector)) ||
        selector[0] != 0x32740011 || selector[1] != 0x81747400 ||
        selector[2] != getter) return false;
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t words[3]{}, descriptor[4]{};
        if (!SafeReadBytes(code + operands[i] - unit - 4, words, sizeof(words)) ||
            words[0] != 0x5A740000 || words[2] != 0x43747061 ||
            words[1] != code - (unit - serialized[i]) ||
            !SafeReadBytes(words[1], descriptor, sizeof(descriptor)) ||
            !descriptor[0] || descriptor[1] || descriptor[2] || descriptor[3])
            return false;
        descriptors[i] = words[1]; instances[i] = descriptor[0];
        // Canonical sword models must be renderable with the selected sword's
        // EXISTING node stream. No skeleton/controller rebind is performed.
        Snapshot shape;
        if (!SafeRead(g_builtin.sword.addr + 0x140, shape.count) ||
            shape.count > kMaxNodes ||
            !SafeReadBytes(instances[i], shape.mesh, sizeof(shape.mesh)) ||
            !Meshes(shape)) return false;
    }
    return true;
}

bool SwordState(uint32_t& current)
{
    uint32_t mode = 0, auxiliary = 0;
    Snapshot check;
    return SafeRead(g_builtin.sword.addr + 0x128, current) && current &&
        SafeRead(g_builtin.sword.addr + 0x144, mode) && !mode &&
        SafeRead(g_builtin.sword.addr + 0x2FC, auxiliary) && !auxiliary &&
        ReadSnapshot(g_builtin.sword, current, check, false);
}

bool SameBindFlags(uint32_t expected, uint32_t replacement)
{
    uint32_t a = 0, b = 0;
    return SafeRead(expected + 0x34, a) && SafeRead(replacement + 0x34, b) &&
        (a & 0x103) == (b & 0x103);
}

void BindKeepingBounds(SwordBindFn original, const uint32_t* args, const uint32_t* bounds)
{
    // 0x43E5D0 copies model+0x10..0x1C into owner+0xD0..0xDC. Keep the real
    // tier's bounds for an intercepted refresh (or current bounds on immediate
    // apply), rather than allowing a cosmetic choice to change collision/cull
    // inputs. Rendering may consequently clip a larger costume at frustum edges.
    __try { original(reinterpret_cast<void*>(g_builtin.sword.addr), args); }
    __finally { memcpy(reinterpret_cast<void*>(g_builtin.sword.addr + 0xD0), bounds, 16); }
}
} // namespace

bool CaptureActor(uintptr_t address, Actor& out)
{
    out = {};
    if (!Thread() || address > UINT32_MAX || !address || address > UINT32_MAX - 0x270)
        return false;
    Actor a;
    a.addr = uint32_t(address);
    if (!SafeRead(address + 0x1A0, a.handle) || !SafeRead(address + 0x1A8, a.pClass) ||
        !Live(a)) return false;
    out = a;
    return true;
}

bool Select(Channel channel, const Actor& recipient, const Actor& donor)
{
    if (!Thread() || g_frame) return false;
    Lane* lane = GetLane(channel);
    if (!lane || Equal(recipient, donor) ||
        !Live(recipient, channel == Channel::Player ? "Ninja" : "NinjaSword") ||
        !Live(donor)) return false;
    lane->recipient = recipient; lane->donor = donor;
    lane->selected = true; DropCaptured(*lane);
    g_anyDonor.store(true, std::memory_order_relaxed);
    return true;
}

void Clear(Channel channel)
{
    if (!Thread() || g_frame) return;
    Lane* lane = GetLane(channel);
    if (lane) { lane->selected = false; DropCaptured(*lane); }
    g_anyDonor.store(g_lanes[0].selected || g_lanes[1].selected, std::memory_order_relaxed);
}

void ClearAll()
{
    Clear(Channel::Player); Clear(Channel::Sword);
    ClearBuiltinSword();
}

bool BeginFrame(void* renderer, bool flushFenceInstalled)
{
    if (!g_anyDonor.load(std::memory_order_relaxed)) return false;
    if (!Thread() || g_frame || !renderer || renderer != game::Renderer() ||
        uintptr_t(renderer) > UINT32_MAX - 0x5C464 || !flushFenceInstalled ||
        !ValidateGameImage()) return false;
    g_stats = {};
    g_scope = nullptr; g_renderer = renderer;
    g_tick = game::TickCount();
    // AppRender increments FrameCount before initializing/submitting queues.
    g_nativeFrame = game::FrameCount() + 1;
    for (Lane& lane : g_lanes) {
        DropCaptured(lane);
        if (lane.selected && (!Live(lane.recipient) || !Live(lane.donor)))
            lane.selected = false;
    }
    g_frame = g_building = true;
    return true;
}

void BeforeFlush(void* renderer)
{
    if (Thread() && g_frame && renderer == g_renderer) g_building = false;
}

void EndFrame()
{
    if (!Thread()) return;
    g_scope = nullptr;
    for (Lane& lane : g_lanes) DropCaptured(lane);
    g_anyDonor.store(g_lanes[0].selected || g_lanes[1].selected, std::memory_order_relaxed);
    g_frame = g_building = false; g_renderer = nullptr;
}

FrameStats Stats()
{
    return Thread() ? g_stats : FrameStats{};
}

void SubmitEntity(void* renderer, void* owner, void* instance, const Matrix* world,
    EntityContinuation continuation, EntitySubmitFn original)
{
    if (!g_anyDonor.load(std::memory_order_relaxed)) {
        if (continuation) continuation(renderer, owner, instance, world);
        return;
    }
    if (!Thread()) {
        if (continuation) continuation(renderer, owner, instance, world);
        return;
    }
    Scope* previous = g_scope;
    Scope scope;
    g_scope = nullptr; // A nested native submission must not inherit donor bones.
    __try {
        scope.donor = Choose(renderer, owner, instance, world);
        if (scope.donor && original &&
            SafeReadBytes(uintptr_t(world), &scope.world, sizeof(scope.world))) {
            scope.renderer = renderer;
            g_scope = &scope;
            original(renderer, owner, reinterpret_cast<void*>(scope.donor->instance), &scope.world);
            ++g_stats.substituted;
        } else {
            if (continuation) {
                continuation(renderer, owner, instance, world);
                Observe(renderer, owner, instance);
            }
        }
    } __finally { g_scope = previous; }
}

bool SubmitNodes(void* renderer, void* mesh, const Matrix* world,
    const Matrix* /*nodes*/, NodeSubmitFn original)
{
    if (!g_anyDonor.load(std::memory_order_relaxed)) return false;
    if (!Thread() || !g_scope || g_scope->renderer != renderer ||
        world != &g_scope->world) return false;
    Snapshot& donor = *g_scope->donor;
    if (uintptr_t(mesh) != donor.mesh[0] && uintptr_t(mesh) != donor.mesh[1]) {
        ++g_stats.refused;
        return true; // Unknown scoped chain: never index recipient bones with it.
    }
    if (original) original(renderer, mesh, world, donor.count ? donor.pose : nullptr);
    return true; // The native function COPIES matrices; no SDK pose pointer escapes.
}

bool SelectBuiltinSword(const Actor& resourceNinja, const Actor& exactSword, int tier)
{
    if (!Thread() || g_frame || tier < 0 || tier > 5 ||
        !Live(resourceNinja, "Ninja") || !Live(exactSword, "NinjaSword")) return false;
    const BuiltinSword previous = g_builtin;
    g_builtin.selected = true; g_builtin.ninja = resourceNinja;
    g_builtin.sword = exactSword; g_builtin.tier = tier;
    uint32_t descriptors[6]{}, instances[6]{};
    if (!SwordResources(descriptors, instances)) { g_builtin = previous; return false; }
    return true;
}

void ClearBuiltinSword()
{
    if (Thread() && !g_frame) g_builtin.selected = false;
}

bool BindBuiltinSword(void* strat, const uint32_t* args, SwordBindFn original)
{
    if (!Thread() || g_frame || !g_builtin.selected || !original ||
        uintptr_t(strat) != g_builtin.sword.addr) return false;
    uint32_t descriptors[6]{}, instances[6]{}, requested = 0, current = 0, bounds[4]{};
    if (!args || !SafeRead(uintptr_t(args), requested) ||
        !SwordResources(descriptors, instances) ||
        !SwordState(current)) return false;
    int normal = -1;
    for (int i = 0; i < 6; ++i) if (requested == descriptors[i]) normal = i;
    if (normal < 0 ||
        !SameBindFlags(instances[normal], instances[g_builtin.tier]) ||
        !SafeReadBytes(instances[normal] + 0x10, bounds, sizeof(bounds))) return false;
    // Hide/special-form/unrelated bindings pass through. +2FC MUST be null:
    // native 0x410C40 otherwise unregisters/frees that auxiliary physics object.
    const uint32_t replacement[] = {descriptors[g_builtin.tier]};
    BindKeepingBounds(original, replacement, bounds);
    return true;
}

bool ApplyBuiltinSword(SwordBindFn original)
{
    if (!Thread() || g_frame || !g_builtin.selected || !original) return false;
    uint32_t descriptors[6]{}, instances[6]{}, current = 0, bounds[4]{};
    if (!SwordResources(descriptors, instances) ||
        !SwordState(current)) return false;
    bool normal = false;
    for (uint32_t instance : instances) normal |= current == instance;
    if (!normal || !SameBindFlags(current, instances[g_builtin.tier]) ||
        !SafeReadBytes(g_builtin.sword.addr + 0xD0, bounds, sizeof(bounds))) return false;
    const uint32_t args[] = {descriptors[g_builtin.tier]};
    BindKeepingBounds(original, args, bounds);
    return true;
}
} // namespace eth::cosmetic::models
