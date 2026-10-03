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

    // Beacon detection (all 0 = unavailable, beacon accessories are not toggled)
    uint32_t model_hookups_offset = 0;   // array_t of hookup objects in a model_object (+8 data, +0x10 count)
    uint32_t get_class_vt_slot = 0;      // hookup->vtable[slot]() returns its class descriptor
    uintptr_t flare_vehicle_class = 0;   // reflection class descriptor of flare_vehicle
    uint32_t light_type_offset = 0;      // flare_vehicle.light_type field
    uint64_t beacon_light_bits = 0;      // light_type enum value "beacon"
    bool beacon_detection = false;

    // Cab view (hook mode only; cab_draw_function 0 = unavailable, banners and flags show from the cab).
    // The cab draw (`this` = the cab object) draws the cab's own copies of the accessories, built from
    // interior_model, without checking the record mask, and submits the truck's flag cloth itself. See ClothHook.
    uintptr_t cab_draw_function = 0;
    uint32_t cab_vehicle_offset = 0;        // cab object -> its vehicle
    uint32_t cab_records_data_offset = 0;   // cab object: accessory records (+0 data, +8 count)
    // Model instance parts: *(model + model_parts_offset) = u32 per part, bit 0 = drawn; part count = u32 at
    // desc + desc_part_count_offset where desc = *(model + model_desc_offset), valid once the byte at
    // desc + desc_loaded_offset is non-zero.
    uint32_t model_desc_offset = 0;
    uint32_t model_parts_offset = 0;
    uint32_t desc_loaded_offset = 0;
    uint32_t desc_part_count_offset = 0;

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
