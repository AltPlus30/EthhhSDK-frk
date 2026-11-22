#include "cosmetic_model_host.h"
#include "cosmetic_models.h"
#include "game.h"
#include "trainer_values.h"
#include "ethhhpatch_runtime.h"
#include <MinHook.h>

namespace eth::cosmetic::model_host {
namespace {
struct Request {
    bool queued = false, builtin = false, sword = false;
    int tier = 0;
    models::Actor ninja, recipient, donor;
};
std::mutex mutex;
std::string owner;
Request pending;
View view;
bool refreshRequested = false, releaseRequested = false;
bool bindInstalled = false, nativeFault = false;
models::Actor selectedNinja, selectedSword;
models::SwordBindFn bindOriginal = nullptr;
bool builtinSelected = false;

bool Equal(const models::Actor& a, const models::Actor& b)
{
    return a.addr && a.addr == b.addr && a.pClass == b.pClass && a.handle == b.handle;
}
bool ReadActor(uintptr_t addr, models::Actor& out)
{
    game::StratIdentity id{};
    if (!game::ReadStratIdentity(addr, id)) return false;
    out = {id.addr, id.pClass, id.selfHandle};
    return true;
}
bool Pinned(models::Actor& out)
{
    auto& tracker = game::PlayerTracker::Get();
    game::StratView strat;
    return tracker.Pinned() && tracker.Valid() &&
        game::ReadStrat(tracker.Addr(), strat) && !strcmp(strat.className, "Ninja") &&
        ReadActor(tracker.Addr(), out);
}
void __cdecl BindDetour(void* strat, const uint32_t* args)
{
    if (!nativeFault && !patch::runtime::InNativeRender()) {
        __try { if (models::BindBuiltinSword(strat, args, bindOriginal)) return; }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            nativeFault = true;
            // Never retry a native binder which may have partially torn down.
            return;
        }
    }
    if (bindOriginal) bindOriginal(strat, args);
}
bool InstallBind()
{
    if (bindInstalled) return !nativeFault;
    const uint8_t expected[] = {0x8B,0x44,0x24,0x08,0x8B,0x08,0x51,0x8B,0x4C,0x24,0x08,
                                0xE8,0x10,0x9D,0xFF,0xFF,0xC3};
    uint8_t bytes[sizeof(expected)]{};
    uintptr_t entry = 0;
    if (!trainer::Supported() ||
        !SafeRead(game::A(0x4C7E70 + 0x29*4), entry) || entry != game::A(0x416F20) ||
        !SafeReadBytes(entry, bytes, sizeof(bytes)) || memcmp(bytes,expected,sizeof(bytes))) return false;
    if (MH_CreateHook((void*)entry,(void*)&BindDetour,(void**)&bindOriginal) != MH_OK) return false;
    if (MH_EnableHook((void*)entry) != MH_OK) {
        MH_RemoveHook((void*)entry); bindOriginal = nullptr; return false;
    }
    bindInstalled = true;
    return true;
}
bool ApplySword()
{
    if (nativeFault || !bindOriginal) return false;
    __try { return models::ApplyBuiltinSword(bindOriginal); }
    __except (EXCEPTION_EXECUTE_HANDLER) { nativeFault = true; return false; }
}
void Stop()
{
    bool restored = !builtinSelected;
    if (builtinSelected) {
        double tier = 0; uintptr_t address = 0;
        if (trainer::Read("sword", tier, address) &&
            models::SelectBuiltinSword(selectedNinja, selectedSword, int(tier)))
            restored = ApplySword();
    }
    models::ClearAll();
    builtinSelected = false;
    selectedNinja = {}; selectedSword = {};
    view.status = restored ? "Model overrides stopped; eligible built-in sword restored to real tier." :
        "Overrides stopped. Existing sword mesh will restore on its next normal game refresh.";
}
bool Queue(const std::string& requestedOwner, Request next)
{
    if (requestedOwner.empty() || !trainer::Supported()) return false;
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner.empty() && owner != requestedOwner) return false;
    const auto cached = [](const models::Actor& actor) {
        for (const auto& item : view.candidates)
            if (item.addr == actor.addr && item.pClass == actor.pClass && item.handle == actor.handle)
                return true;
        return false;
    };
    if ((!next.builtin && !cached(next.donor)) || (next.sword && !cached(next.recipient))) return false;
    if (next.sword && selectedSword.addr && !Equal(next.recipient,selectedSword)) return false;
    owner = requestedOwner;
    pending = next; pending.queued = true;
    releaseRequested = false;
    view.status = "Model request queued for the next engine tick.";
    return true;
}
}
View Inspect(bool refresh)
{
    std::lock_guard<std::mutex> lock(mutex);
    refreshRequested |= refresh;
    return view;
}
bool RequestDonor(const std::string& requestedOwner, bool sword, uintptr_t recipient, uintptr_t donor)
{
    Request next;
    next.sword = sword;
    if (!Pinned(next.ninja) || !ReadActor(sword ? recipient : next.ninja.addr, next.recipient) ||
        !ReadActor(donor, next.donor)) return false;
    return Queue(requestedOwner, next);
}
bool RequestSword(const std::string& requestedOwner, uintptr_t sword, int tier)
{
    if (tier < 0 || tier > 5) return false;
    Request next;
    next.builtin = next.sword = true; next.tier = tier;
    if (!Pinned(next.ninja) || !ReadActor(sword, next.recipient)) return false;
    return Queue(requestedOwner, next);
}
void Release(const std::string& requestedOwner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (owner != requestedOwner) return;
    pending = {};
    releaseRequested = true;
}
void OnNativeTickCompleted()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!trainer::Supported()) return;
    if (refreshRequested) {
        view.candidates.clear();
        for (const auto& strat : game::SnapshotStrats()) {
            uintptr_t instance = 0;
            if (!SafeRead(strat.addr + 0x128, instance) || !instance) continue;
            view.candidates.push_back({uint32_t(strat.addr), strat.className,
                uint32_t(uintptr_t(strat.pClass)), strat.selfHandle});
        }
        refreshRequested = false;
    }
    if (owner.empty() && !releaseRequested && !pending.queued) return;
    models::Actor current;
    if (releaseRequested || (selectedNinja.addr && (!Pinned(current) || !Equal(current, selectedNinja)))) {
        Stop(); owner.clear(); releaseRequested = false; pending = {};
    }
    if (pending.queued) {
        const auto request = pending;
        pending = {};
        models::Actor ninja, recipient, donor;
        if (!Pinned(ninja) || !Equal(ninja, request.ninja) ||
            !models::CaptureActor(request.recipient.addr, recipient) || !Equal(recipient, request.recipient) ||
            (!request.builtin && (!models::CaptureActor(request.donor.addr, donor) || !Equal(donor,request.donor)))) {
            view.status = "Cancelled: selected actor generation or exact Ninja pin changed.";
            return;
        }
        bool ok = false;
        if (request.builtin) {
            if (patch::runtime::InstallCosmeticHooks() && InstallBind() &&
                models::SelectBuiltinSword(ninja, recipient, request.tier)) {
                models::Clear(models::Channel::Sword);
                selectedSword = recipient;
                builtinSelected = true;
                ok = true;
                view.status = ApplySword() ? "Cosmetic sword model applied; real tier/stats unchanged." :
                    "Sword override armed. Attack once for native refresh; current binding could not be safely refreshed.";
            }
        } else if (patch::runtime::InstallCosmeticHooks()) {
            if (request.sword && builtinSelected) {
                double tier = 0; uintptr_t address = 0;
                if (trainer::Read("sword",tier,address) &&
                    models::SelectBuiltinSword(selectedNinja,selectedSword,int(tier))) ApplySword();
                models::ClearBuiltinSword(); builtinSelected = false;
            }
            ok = models::Select(request.sword ? models::Channel::Sword : models::Channel::Player, recipient, donor);
            if (ok && request.sword) selectedSword = recipient;
            if (ok) view.status = "Donor-pose override armed. Donor must naturally render before recipient in the primary scene.";
        }
        if (ok) selectedNinja = ninja;
        else view.status = "Refused: incompatible actor/model/script fingerprint, native hook unavailable or unsupported render state.";
    }
    const auto stats = models::Stats();
    view.captured = stats.captured; view.substituted = stats.substituted;
    view.waiting = stats.waitingForDonor; view.refused = stats.refused;
    if (nativeFault) view.status = "Native sword binding faulted; restart required. No automatic retry.";
}
void Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex);
    // No engine object access or off-thread restore during shutdown.
    if (bindInstalled) {
        MH_DisableHook((void*)game::A(0x416F20)); MH_RemoveHook((void*)game::A(0x416F20));
    }
    bindInstalled = false; bindOriginal = nullptr;
    owner.clear(); pending = {}; view = {};
}
}
