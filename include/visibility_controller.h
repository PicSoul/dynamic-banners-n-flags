#pragma once

#include <cstdint>
#include <vector>
#include "game_layout.h"

namespace DynamicBanners {

// Hides/shows the target accessories (banners, flags) of the LOCAL PLAYER's truck and attached trailers.
// Vehicles are reached only through the game's local-player chain, so other players' and AI vehicles are
// never touched. Must be called from the game thread (telemetry callbacks).
//
// Three data writes do the hiding, each undone exactly on show:
//  1. accessory record visibility mask = 0          (banners and the static part of flags)
//  2. truck only: merged-model byte bit 0 = 1       (forces per-record drawing so (1) is honoured)
//  3. flag physics patches moved past a reduced count of the vehicle's patch array (flag cloth)
class VisibilityController {
public:
    static VisibilityController& Instance() {
        static VisibilityController instance;
        return instance;
    }

    // Receives the player's flag patches that must not be drawn (the cloth hook's hidden set).
    typedef void (*PublishHiddenPatchesFn)(const uint64_t* patches, uint32_t count);

    // publish_hidden_patches != nullptr: flag cloth is hidden through the draw hook (no patch-list edits).
    // publish_hidden_patches == nullptr: fallback, flag patches are moved past a reduced list count.
    // beacon_tokens: slots where an accessory is a target only if its model has beacon lights
    // (used when layout.beacon_detection is true).
    void Initialize(const GameLayout& layout, const std::vector<uint64_t>& target_tokens,
                    const std::vector<uint64_t>& beacon_tokens,
                    bool affect_trailers, uint32_t max_trailers, PublishHiddenPatchesFn publish_hidden_patches);
    bool IsActive() const { return active_; }

    // Debug heartbeat: logs the truck's merged-model pointer/flag and the target records' masks.
    void LogDiagnostics(const char* context) const;

    // Brings the player's vehicles to the wanted state. Cheap when nothing changes; call every frame.
    // cloth_must_show: fallback mode only - put flag cloth back (e.g. while paused, when the game may rebuild
    // vehicles and must find every patch in its list). Ignored in hook mode.
    void Update(bool want_hidden, bool cloth_must_show = false);

private:
    VisibilityController() = default;

    struct HiddenRecord { uintptr_t addr; uint64_t token; uint64_t orig_mask; };

    struct Vehicle {
        uintptr_t obj = 0;
        uint64_t vtable = 0;
        bool is_truck = false;
        // records
        uint64_t rec_data = 0, rec_count = 0;
        std::vector<HiddenRecord> hidden_records;
        std::vector<std::pair<uint64_t, bool>> beacon_models;   // model -> has beacon lights (cache)
        // truck merged model we switched off
        uintptr_t merged_obj = 0;
        // flag cloth
        bool patches_hidden = false;
        uint64_t patch_data = 0, patch_count_full = 0, patch_count_reduced = 0;
        std::vector<uint64_t> patch_full, patch_reordered;
    };

    struct PlayerVehicle { uintptr_t obj; bool is_truck; };
    void LogSlotNames(const PlayerVehicle& pv) const;

    bool ResolvePlayerVehicles(std::vector<PlayerVehicle>* out) const;
    bool IsTarget(uint64_t token) const;
    bool IsTargetRecord(Vehicle& v, uint64_t token, uintptr_t record);
    bool ModelHasBeaconLight(uint64_t model) const;
    bool StillSameObject(const Vehicle& v) const;

    int Hide(Vehicle& v, bool hide_cloth);   // returns number of writes
    int Show(Vehicle& v);
    void CollectFlagPatches(const Vehicle& v, std::vector<uint64_t>* out) const;
    int HideRecords(Vehicle& v, bool* has_targets);
    int HidePatches(Vehicle& v);
    int ShowPatches(Vehicle& v);
    bool PatchArrayIsOurs(const Vehicle& v, uint64_t data, uint64_t count) const;

    GameLayout layout_;
    std::vector<uint64_t> tokens_;
    std::vector<uint64_t> beacon_tokens_;
    bool affect_trailers_ = true;
    uint32_t max_trailers_ = 10;
    bool active_ = false;
    PublishHiddenPatchesFn publish_ = nullptr;
    size_t last_published_ = 0;
    bool last_want_hidden_ = false;
    bool have_last_ = false;
    std::vector<Vehicle> vehicles_;
};

} // namespace DynamicBanners
