#pragma once
#include "precomp.h"
#include "cosmetic_textures.h"

namespace eth::cosmetic {
struct Material {
    std::string name;
    uint32_t rgb = 0; // R | G<<8 | B<<16; alpha is never edited
    std::string part = "body"; // body, head, all, or group (belt/costume/head)
};
struct View {
    std::vector<Material> materials;
    std::string status;
    bool active = false;
    bool waiting = false;
    unsigned heads = 0;
    bool textures = false;
    uint64_t texturePasses = 0, textureRefusals = 0;
    uint64_t fullInspections = 0, fastTicks = 0;
    // QPC elapsed wall time of SDK editor post-tick work only, in microseconds.
    // Excludes the original native tick, render continuation and lock waiting.
    double lastTickUs = 0, maxTickUs = 0, lastFullTickUs = 0;
    std::string textureStatus;
    std::string sashStatus;
};
// UI calls only enqueue work. All native calls/stores happen after a game tick.
bool Set(const std::string& owner, const std::vector<Material>& colors);
bool LoadTextures(const std::string& owner, const std::string& directory,
    const std::vector<std::string>& paths, std::string& error, bool secondary = false);
void ClearTextures(const std::string& owner);
// Synchronous native opaque/transparent pass ONLY; true means continuation ran.
bool DrawTextures(textures::DrawContinuation continuation, void* context);
View Inspect();
void Release(const std::string& owner);
void OnNativeTickCompleted();
void BeforeNativeTick();
void Shutdown();
}
