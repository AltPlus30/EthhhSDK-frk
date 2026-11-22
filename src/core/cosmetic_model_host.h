#pragma once
#include "precomp.h"

namespace eth::cosmetic::model_host {
struct Candidate { uint32_t addr = 0; std::string name; uint32_t pClass = 0, handle = 0; };
struct View {
    std::vector<Candidate> candidates;
    std::string status;
    uint32_t captured = 0, substituted = 0, waiting = 0, refused = 0;
};
View Inspect(bool refresh);
bool RequestDonor(const std::string& owner, bool sword, uintptr_t recipient, uintptr_t donor);
bool RequestSword(const std::string& owner, uintptr_t sword, int tier);
void Release(const std::string& owner);
void OnNativeTickCompleted();
void Shutdown();
}
