#pragma once
#include "ethhhpatch_policy.h"

namespace eth::patch {

// Never adopt a transient mode as verified. The engine's minimize/restore path
// can feed its former client size back as an OUTER size (1280x720 -> 1274x691
// with the observed 6x29 frame). Only that measured deficit may trigger one
// exact re-request; other dimensions, mode kinds and refresh rates fail closed.
inline bool WindowFrameResetMismatch(const DisplayPlan& plan, unsigned width,
    unsigned height, bool windowed, unsigned hz, int frameWidth, int frameHeight)
{
    return ValidPlan(plan) && plan.windowed && windowed && hz == 0 &&
        width > 0 && height > 0 && frameWidth >= 0 && frameHeight >= 0 &&
        (frameWidth > 0 || frameHeight > 0) &&
        uint64_t(width) + unsigned(frameWidth) == unsigned(plan.width) &&
        uint64_t(height) + unsigned(frameHeight) == unsigned(plan.height);
}

class DisplayResetRecovery {
public:
    enum class Action { Suspend, Resume, Recover, Reject };
    Action Observe(bool success, bool known, bool matches, bool minimized,
        bool emptyFailure, bool frameMismatch)
    {
        if (!success) {
            interrupted_ = interrupted_ || minimized || emptyFailure;
            return Action::Suspend;
        }
        if (!known || minimized) {
            interrupted_ = true;
            return Action::Suspend;
        }
        if (matches) { Cancel(); return Action::Resume; }
        if (interrupted_ && frameMismatch && !attempted_) {
            pending_ = true;
            return Action::Recover;
        }
        // Repeated native resets while queued must not trigger multiple requests.
        if (pending_ && frameMismatch) return Action::Recover;
        Cancel();
        return Action::Reject;
    }
    bool Pending() const { return pending_; }
    void Take() { pending_ = false; attempted_ = true; }
    void Cancel() { interrupted_ = pending_ = attempted_ = false; }
private:
    bool interrupted_ = false, pending_ = false, attempted_ = false;
};

} // namespace eth::patch
