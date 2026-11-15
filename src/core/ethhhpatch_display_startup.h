#pragma once
#include <cmath>
#include <cstdint>

namespace eth::patch {

// A device pointer/window hook does not mean the boot mode selection is over.
// Require a quiet interval of successful GAME presents, not movie presents.
// Reset/movie/focus/device-loss interruptions discard the whole interval.
class DisplayStartupGate {
public:
    void Reset() { first_ = last_ = 0; samples_ = 0; }
    void Present(double now, bool success)
    {
        if (!success || !std::isfinite(now) || now <= 0) { Reset(); return; }
        if (!samples_ || now < last_ || now - last_ > kFreshSeconds) {
            first_ = now;
            samples_ = 0;
        }
        last_ = now;
        if (samples_ < 3) ++samples_;
    }
    bool Ready(double now) const
    {
        return samples_ >= 3 && std::isfinite(now) && now >= last_ &&
            now - last_ <= kFreshSeconds && now - first_ >= kQuietSeconds;
    }
    static constexpr double kQuietSeconds = 2.0;
    static constexpr double kFreshSeconds = 0.25;
private:
    double first_ = 0, last_ = 0;
    uint32_t samples_ = 0;
};

// Native startup scripts may still select another mode just after our first
// successful transaction. Permit ONE recovery in that short boot window, never
// an unbounded fight with native settings, user changes or device loss.
class DisplayStartupRecovery {
public:
    void Arm(double now)
    {
        until_ = std::isfinite(now) && now > 0 ? now + 10.0 : 0;
        used_ = pending_ = false;
    }
    bool Queue(double now)
    {
        if (!until_ || used_ || !std::isfinite(now) || now < until_ - 10.0 || now > until_) return false;
        used_ = pending_ = true;
        return true;
    }
    bool Pending() const { return pending_; }
    void Take() { pending_ = false; }
    void Cancel() { until_ = 0; pending_ = false; }
private:
    double until_ = 0;
    bool used_ = false, pending_ = false;
};

} // namespace eth::patch
