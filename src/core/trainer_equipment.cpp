#include "trainer_equipment.h"
#include "trainer_values.h"
#include "game.h"

namespace eth::trainer::equipment {
namespace {
constexpr uintptr_t kNotify = 0x417380;
constexpr uint8_t kNotifyBytes[] = {
    0x8B,0x44,0x24,0x08,0x8B,0x48,0x04,0x8B,0x10,0x8B,0x44,0x24,0x04,
    0x6A,0x00,0x6A,0x00,0x51,0x52,0x50,0xB9,0x9C,0x01,0x51,0x00,
    0xE8,0xC2,0xC1,0x00,0x00,0xC3
};
struct Pending {
    std::string owner, field;
    int value = 0;
    uintptr_t address = 0, ninja = 0;
    uint32_t handle = 0;
};
std::mutex mutex;
std::vector<Pending> requests;
std::map<std::string, std::string> statuses;
std::atomic<bool> queued{false};
bool notificationFault = false;

bool CurrentNinja(uintptr_t& address, uint32_t& handle)
{
    auto& tracker = game::PlayerTracker::Get();
    if (!tracker.Pinned() || !tracker.Valid()) return false;
    game::StratView strat;
    address = tracker.Addr();
    if (!game::ReadStrat(address, strat) || strcmp(strat.className, "Ninja")) return false;
    handle = strat.selfHandle;
    return handle != 0;
}

bool NotifyAvailable()
{
    uint8_t bytes[sizeof(kNotifyBytes)]{};
    uintptr_t entry = 0;
    return !notificationFault &&
        SafeReadBytes(game::A(kNotify), bytes, sizeof(bytes)) &&
        !memcmp(bytes, kNotifyBytes, sizeof(bytes)) &&
        SafeRead(game::A(0x4C7E70 + 0x11D * 4), entry) && entry == game::A(kNotify);
}

bool Notify(uintptr_t sender, const char* name, int event)
{
    // Native 0/0x11D: cdecl(senderStrat, [classNamePointer, event]).
    // Original class-wide queued delivery, not an immediate call on one actor.
    // The pinned sender is guarded, but every matching recipient may receive it.
    const uint32_t args[] = {uint32_t(uintptr_t(name)), uint32_t(event)};
    using Fn = void(__cdecl*)(void*, const uint32_t*);
    __try {
        reinterpret_cast<Fn>(game::A(kNotify))(reinterpret_cast<void*>(sender), args);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool Request(const std::string& owner, const char* field, int value, uintptr_t expectedAddress)
{
    if (owner.empty() || owner.size() > 128 || !field ||
        (strcmp(field, "sword") && strcmp(field, "belt") && strcmp(field, "ranks")))
        return false;
    const auto* def = FindField(field);
    double current = 0;
    uintptr_t address = 0, ninja = 0;
    uint32_t handle = 0;
    if (!def || value < def->minimum || value > def->maximum ||
        !Read(field, current, address) || address != expectedAddress ||
        !CurrentNinja(ninja, handle)) return false;
    std::lock_guard<std::mutex> lock(mutex);
    if (requests.size() >= 8) return false;
    for (const auto& pending : requests) if (pending.owner == owner) return false;
    if (!strcmp(field, "sword") && !NotifyAvailable()) return false;
    requests.push_back({owner, field, value, address, ninja, handle});
    statuses[owner] = "Queued for the next game tick.";
    queued.store(true, std::memory_order_release);
    return true;
}

std::string Status(const std::string& owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = statuses.find(owner);
    return it == statuses.end() ? "No equipment edit requested this session." : it->second;
}

void OnNativeTickCompleted()
{
    if (!queued.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& pending : requests) {
        auto& status = statuses[pending.owner];
        uintptr_t ninja = 0;
        uint32_t handle = 0;
        if (!CurrentNinja(ninja, handle) || ninja != pending.ninja || handle != pending.handle) {
            status = "Cancelled: pinned Ninja changed or became unavailable.";
            continue;
        }
        const bool sword = pending.field == "sword";
        if (sword && !NotifyAvailable()) {
            status = "Refused: sword notification code is unavailable or faulted.";
            continue;
        }
        if (!Write(pending.field.c_str(), pending.value, pending.address)) {
            status = "Cancelled: profile pointer changed, value rejected or write failed.";
            continue;
        }
        if (sword) {
            if (!Notify(ninja, "Ninja", 35) || !Notify(ninja, "NinjaSword", 54)) {
                notificationFault = true;
                status = "Sword field written, but refresh faulted. Reload the level; no automatic retry.";
            } else {
                status = "Sword field applied; refresh notifications sent. Attack once if appearance has not updated.";
            }
        } else if (pending.field == "belt") {
            status = "Belt field applied. Attack once if appearance has not updated; reload the level if needed.";
        } else {
            status = "Earned rank count applied; belt, awards and mission unlocks were not changed.";
        }
    }
    requests.clear();
    queued.store(false, std::memory_order_release);
}

void Release(const std::string& owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    requests.erase(std::remove_if(requests.begin(), requests.end(),
        [&](const Pending& request) { return request.owner == owner; }), requests.end());
    statuses.erase(owner);
    queued.store(!requests.empty(), std::memory_order_release);
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex);
    requests.clear();
    statuses.clear();
    queued.store(false, std::memory_order_release);
    notificationFault = false;
}
}
