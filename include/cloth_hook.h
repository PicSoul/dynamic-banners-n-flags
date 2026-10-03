#pragma once

#include <cstdint>

namespace DynamicBanners {

// Where the cab view's data lives (from code signatures; see GameLayout).
struct CabLayout {
    uint32_t patches_data;     // vehicle: flag cloth patch array_t (+0 data, +8 count)
    uint32_t vehicle;          // cab (interior) object: its vehicle
    uint32_t records_data;     // cab object: its accessory records (+0 data, +8 count)
    uint32_t record_size;
    uint32_t record_token;     // record: slot name token
    uint32_t record_model;     // record: model instance
    uint32_t model_parts;      // model instance: u32 per part, bit 0 = drawn
    uint32_t model_desc;       // model instance: descriptor
    uint32_t desc_loaded;      // descriptor: byte, non-zero once loaded
    uint32_t desc_part_count;  // descriptor: u32 part count
};

// The code hooks. Flag cloth (physics patches) is drawn in two places:
//  - PatchDraw, called per patch by the exterior collectors: the detour skips patches in the hidden set.
//  - the cab view's draw function, which submits the truck's patches itself and also draws the cab's own
//    copies of the accessories (from interior_model, ignoring the record mask). While the player's truck is
//    hidden, the detour hands it the patch list without the hidden flags and switches off every part of the
//    copies in the target slots - for the duration of the call only; everything is put back before it returns.
// The cloth keeps being simulated while hidden, so it waves naturally when shown again.
class ClothHook {
public:
    static bool Install(uintptr_t draw_function);
    // Optional second hook; needs Install first. tokens: the target slots.
    static bool InstallCab(uintptr_t cab_draw_function, const CabLayout& layout, const uint64_t* tokens, uint32_t count);
    static void Uninstall();
    static bool IsInstalled();

    // Publishes the set of patch objects that must not be drawn (called from the game thread).
    static void SetHiddenPatches(const uint64_t* patches, uint32_t count);
    // The player's truck (0 = none) and whether its banners/flags are hidden (called from the game thread).
    static void SetCabTarget(uintptr_t truck, bool hide);
    // Diagnostics: copies hidden in the player's last hidden cab draw (-1 = none drawn yet).
    static int CabCopiesHidden();
};

} // namespace DynamicBanners
