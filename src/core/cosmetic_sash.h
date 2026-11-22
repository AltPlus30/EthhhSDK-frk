#pragma once
#include <cstdint>
#include <vector>

namespace eth::game { struct StratIdentity; }

namespace eth::cosmetic::sash {
enum class Result { Applied, Waiting, Refused };

// Engine tick thread ONLY, outside rendering/VM execution. The editor owns
// queuing and the single active owner. RGB uses R | G<<8 | B<<16.
// Exact selected Ninja generation; never follows respawn or changes selection.
Result Apply(const game::StratIdentity& ninjaIdentity, uint32_t rgb);
// Hot path: caller supplies CURRENT complete LinkedHeads results from its
// already-validated live snapshot. No list traversal/ReadStratIdentity call.
// Independently checks registry/self/class/owner and ribbon fields via SEH reads.
// Restore the old selection BEFORE switching roots; otherwise it is forgotten.
// Native mesh RGB is generated at render flush, AFTER this post-tick override;
// the normal head loop's model-refresh helper skips tint on unchanged descriptor.
Result ApplyKnown(const game::StratIdentity& ninjaIdentity, uint32_t rgb,
    const std::vector<game::StratIdentity>& verifiedHeads);
// Same fresh-input contract, zero list traversals. Always forgets saved state;
// mismatched roots or absent/stale heads cannot cause a restoration store.
void RestoreKnown(const game::StratIdentity& ninjaIdentity,
    const std::vector<game::StratIdentity>& verifiedHeads);
// Release/disable/selection change, on that same thread while the world is live.
// Restores only the still-linked exact object if our last color remains present.
void Restore();
// BEFORE level/session teardown or after loss of world validity: forget without
// touching memory. Mandatory even when the next world reuses all addresses.
void Invalidate();
}
