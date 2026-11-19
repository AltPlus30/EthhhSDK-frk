#pragma once
#include "util.h"

namespace eth::trainer::equipment {
// One-shot requests only. Never call game notification code from the render GUI.
bool Request(const std::string& owner, const char* field, int value,
             uintptr_t expectedAddress);
std::string Status(const std::string& owner);
void OnNativeTickCompleted();
void Release(const std::string& owner);
void Shutdown();
}
