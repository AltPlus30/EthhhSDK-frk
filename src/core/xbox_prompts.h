#pragma once
#include "precomp.h"

namespace eth::prompts {
bool Install();
void Shutdown();
bool SetEnabled(const std::string& owner, bool enabled);
void Release(const std::string& owner);
}
