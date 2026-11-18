#pragma once
#include <cmath>
#include <cstdint>

namespace eth::patch {

// Perspective-sphere test for the verified camera-space convention used by
// 0x449420. Unlike the original routine, both point bounds use the submitted
// vertical FOV and the horizontal bound includes aspect.
inline bool PerspectiveSphereVisible(float x, float y, float z, float radius,
    float aspect, float tanHalfVerticalFov, float farPlane)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(radius) || !std::isfinite(aspect) ||
        !std::isfinite(tanHalfVerticalFov) || !std::isfinite(farPlane) ||
        radius < 0 || aspect <= 0 || tanHalfVerticalFov <= 0 || farPlane <= 0)
        return false;
    if (z + radius < 0 || z - radius > farPlane) return false;
    const double tanV = tanHalfVerticalFov;
    const double tanH = tanV * aspect;
    const double horizontalMargin = radius * std::sqrt(1.0 + tanH * tanH);
    const double verticalMargin = radius * std::sqrt(1.0 + tanV * tanV);
    return std::abs(double(x)) <= double(z) * tanH + horizontalMargin &&
        std::abs(double(y)) <= double(z) * tanV + verticalMargin;
}

struct PulseClock {
    uint32_t lastTick = 0;
    bool initialized = false;

    uint32_t Advance(uint32_t tick)
    {
        if (!initialized) { initialized = true; lastTick = tick; return 1; }
        const uint32_t delta = tick - lastTick; // intentional wrap arithmetic
        lastTick = tick;
        return delta <= 4 ? delta : 1; // load/level transition: preserve one original step
    }
};

inline float PulsePreAdjustment(uint32_t completedTicks, float originalStep = 0.1f)
{
    if (completedTicks > 4 || !std::isfinite(originalStep) || originalStep <= 0) return 0;
    return (float(int(completedTicks) - 1)) * originalStep;
}

inline float HudHorizontalScale(float actualAspect, float authoredAspect = 4.0f / 3.0f)
{
    if (!std::isfinite(actualAspect) || !std::isfinite(authoredAspect) ||
        actualAspect <= 0 || authoredAspect <= 0) return 0;
    return authoredAspect / actualAspect;
}

enum class AuthoredHudSpace { Flat, ModelOverlay };

// Called only for the verified HUD projection submissions. The separate
// 640x480 model-overlay pass (grades) must share the 2D artwork's containment.
// Reject other/custom projections; never edit native world/object transforms.
inline bool ContainAuthoredHudProjection(float* matrix, AuthoredHudSpace space, float aspect)
{
    const float scale=HudHorizontalScale(aspect);
    if (!matrix || !(scale>0) || !std::isfinite(scale) ||
        (space!=AuthoredHudSpace::Flat && space!=AuthoredHudSpace::ModelOverlay)) return false;
    for (unsigned i=0;i<16;++i) {
        if (!std::isfinite(matrix[i])) return false;
        float expected=i%5==0 ? 1.f : 0.f;
        float epsilon=1e-6f;
        if (space==AuthoredHudSpace::ModelOverlay) {
            // Native 0x45EDA3: OrthoLH(640,480,0.1,1600).
            expected=i==0 ? 2.f/640 : i==5 ? 2.f/480 :
                i==10 ? 1.f/(1600.f-.1f) : i==14 ? -.1f/(1600.f-.1f) :
                i==15 ? 1.f : 0.f;
            epsilon=std::abs(expected)*1e-5f+1e-8f;
        }
        if (std::abs(matrix[i]-expected)>epsilon) return false;
    }
    matrix[0]*=scale;
    return true;
}

struct AspectFit { float x = 0, y = 0, scale = 0; };
inline AspectFit FitCentered(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight)
{
    AspectFit result;
    if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) return result;
    result.scale = std::min(float(targetWidth) / sourceWidth, float(targetHeight) / sourceHeight);
    if (!std::isfinite(result.scale) || result.scale <= 0) return {};
    result.x = (float(targetWidth) - sourceWidth * result.scale) * 0.5f;
    result.y = (float(targetHeight) - sourceHeight * result.scale) * 0.5f;
    return result;
}

} // namespace eth::patch
