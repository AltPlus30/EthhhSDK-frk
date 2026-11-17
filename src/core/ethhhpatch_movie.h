#pragma once
#include <algorithm>
#include "ethhhpatch_runtime_policy.h"

namespace eth::patch {

// 0x497D40 and both draw implementations are cdecl, SIX stack arguments.
// There is no height argument between Y and scaleX.
using MovieDrawFn = void(__cdecl*)(void*, float, float, float, float, float);

struct MovieDrawArgs {
    void* surface = nullptr;
    float x = 0, y = 0, scaleX = 1, scaleY = 1, alpha = 1;
};

inline bool FitMovie(MovieDrawArgs& args, int sourceWidth, int sourceHeight, int width, int height)
{
    if (sourceWidth<=0 || sourceWidth>8192 || sourceHeight<=0 || sourceHeight>8192 ||
        width<=0 || height<=0) return false;
    const AspectFit fit=FitCentered(sourceWidth,sourceHeight,width,height);
    if (!(fit.scale>0)) return false;
    args.x=fit.x; args.y=fit.y; args.scaleX=args.scaleY=fit.scale;
    return true;
}

inline void SubmitMovie(MovieDrawFn original, const MovieDrawArgs& args)
{
    if (original) original(args.surface,args.x,args.y,args.scaleX,args.scaleY,args.alpha);
}

} // namespace eth::patch
