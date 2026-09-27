#pragma once

#include <cstdint>

namespace DynamicBanners {

// Everything the visibility controller needs to know about amtrucks.exe's memory layout.
// Resolved at startup from code signatures (see config [Signatures]); valid only if Resolve() returned true.
struct GameLayout {
    // Local player chain: game = *game_global; actor = *(game + actor_offset);
    // truck = *(actor + truck_offset); first trailer = *(actor + trailer_offset);
    // next trailer = *(trailer + next_trailer_offset)
    uintptr_t game_global = 0;
    uint32_t actor_offset = 0;
    uint32_t truck_offset = 0;
    uint32_t trailer_offset = 0;
    uint32_t next_trailer_offset = 0;

    // Per vehicle
    uint32_t records_data_offset = 0;    // array of accessory records (record_size each)
    uint32_t records_count_offset = 0;
    uint32_t patches_data_offset = 0;    // array of physics patch pointers (flag cloth)
    uint32_t patches_count_offset = 0;
    uint32_t merged_offset = 0;          // truck merged-model object; its first byte bit 0 disables merging
    uint32_t trailer_connected_offset = 0; // non-zero pointer while the trailer is hooked up; 0 = unknown

    // Flag cloth draw function (hook target); 0 = not found, fall back to the patch-list method
    uintptr_t patch_draw_function = 0;

    // Record / patch layout (from config; not readable from code)
    uint32_t record_size = 0;
    uint32_t record_token_offset = 0;
    uint32_t record_mask_offset = 0;
    uint32_t patch_token_offset = 0;
};

class GameLayoutResolver {
public:
    // Runs every signature once. Returns false (and logs why) if any value is missing or implausible,
    // in which case the mod must stay inactive.
    static bool Resolve(GameLayout* out);
};

} // namespace DynamicBanners
