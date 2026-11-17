#pragma once
#include "ethhhpatch_hud.h"

// Keep the existing inventory fixture/API names while gameplay HUD groups now
// share one implementation and one set of tick/render caches.
namespace eth::patch { namespace inventory = hud; }
