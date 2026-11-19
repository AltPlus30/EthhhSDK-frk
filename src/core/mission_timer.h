#pragma once
#include <string>

namespace eth::mission_timer {
bool Install();
bool Available();
void Shutdown();
// Requests belong to Lua mods; unload/error-disable releases them automatically.
bool SetStopped(const std::string& owner, bool stopped);
void Release(const std::string& owner);
}
