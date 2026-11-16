#pragma once
#include "precomp.h"
#include "button_prompt_pixels.h"

namespace eth::prompts {
bool Install();
void Shutdown();
// Atomically replace a mod's active pack. File must resolve inside modDirectory.
bool Load(const std::string& owner, const std::string& modDirectory,
          const std::string& relativeTexture, const std::vector<Region>& regions,
          std::string& error);
void Release(const std::string& owner);
}
