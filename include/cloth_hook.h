#pragma once

#include <cstdint>

namespace DynamicBanners {

// The one code hook: the flag-cloth (physics patch) draw function, found by the PatchDraw signature.
// The detour skips patches in the hidden set and calls the original for everything else.
// The cloth keeps being simulated while hidden, so it waves naturally when shown again.
class ClothHook {
public:
    static bool Install(uintptr_t draw_function);
    static void Uninstall();
    static bool IsInstalled();

    // Publishes the set of patch objects that must not be drawn (called from the game thread).
    static void SetHiddenPatches(const uint64_t* patches, uint32_t count);
};

} // namespace DynamicBanners
