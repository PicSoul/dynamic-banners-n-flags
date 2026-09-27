#include "visibility_controller.h"
#include "mem_safe.h"
#include "logger.h"
#include <algorithm>
#include <cstdio>

namespace DynamicBanners {

static const uint64_t MAX_RECORDS = 1024;
static const uint64_t MAX_PATCHES = 64;

void VisibilityController::Initialize(const GameLayout& layout, const std::vector<uint64_t>& target_tokens,
                                      bool affect_trailers, uint32_t max_trailers,
                                      PublishHiddenPatchesFn publish_hidden_patches) {
    layout_ = layout;
    tokens_ = target_tokens;
    affect_trailers_ = affect_trailers && layout.trailer_connected_offset != 0;
    max_trailers_ = max_trailers;
    publish_ = publish_hidden_patches;
    vehicles_.clear();
    have_last_ = false;
    last_published_ = 0;
    active_ = !tokens_.empty();
    LOG_INFO("VisibilityController: %s with %zu target slot tokens, trailers %s, flag cloth via %s",
        active_ ? "active" : "inactive", tokens_.size(), affect_trailers_ ? "on" : "off",
        publish_ ? "draw hook" : "patch-list fallback");
}

bool VisibilityController::IsTarget(uint64_t token) const {
    return token != 0 && std::find(tokens_.begin(), tokens_.end(), token) != tokens_.end();
}

// game -> local player actor -> truck, first trailer -> next trailer -> ...
bool VisibilityController::ResolvePlayerVehicles(std::vector<PlayerVehicle>* out) const {
    out->clear();
    uint64_t game = 0, actor = 0, truck = 0, vt = 0;
    uint32_t actor_state = 0;
    if (!SafeReadU64(layout_.game_global, &game) || !IsHeapPtr(game)) return false;
    if (!SafeReadU64(game + layout_.actor_offset, &actor) || !IsHeapPtr(actor)) return false;
    // The game only uses the actor while the top bit of its state word is set (see 0x5F9F80).
    if (!SafeReadU32(actor + 8, &actor_state) || (actor_state & 0x80000000u) == 0) return false;
    if (!SafeReadU64(actor + layout_.truck_offset, &truck) || !IsHeapPtr(truck)) return false;
    if (!SafeReadU64(truck, &vt) || !IsInModule(vt)) return false;
    out->push_back({ static_cast<uintptr_t>(truck), true });

    if (!affect_trailers_) return true;
    uint64_t trailer = 0;
    if (!SafeReadU64(actor + layout_.trailer_offset, &trailer)) return true;
    for (uint32_t i = 0; i < max_trailers_ && IsHeapPtr(trailer); ++i) {
        if (!SafeReadU64(trailer, &vt) || !IsInModule(vt)) break;
        // Only hooked-up trailers count; the game keeps pointing at the last trailer after unhooking.
        uint64_t connection = 0;
        if (!SafeReadU64(trailer + layout_.trailer_connected_offset, &connection) || connection == 0) break;
        bool dup = false;
        for (const auto& pv : *out) dup |= pv.obj == trailer;
        if (dup) break;  // defensive: never loop on a corrupted chain
        out->push_back({ static_cast<uintptr_t>(trailer), false });
        if (!SafeReadU64(trailer + layout_.next_trailer_offset, &trailer)) break;
    }
    return true;
}

bool VisibilityController::StillSameObject(const Vehicle& v) const {
    uint64_t vt = 0;
    return SafeReadU64(v.obj, &vt) && vt == v.vtable;
}

// ---------------------------------------------------------------------------------------------------
// Records (banners, static part of flags)
// ---------------------------------------------------------------------------------------------------

int VisibilityController::HideRecords(Vehicle& v, bool* has_targets) {
    *has_targets = false;
    uint64_t data = 0, count = 0;
    if (!SafeReadU64(v.obj + layout_.records_data_offset, &data) ||
        !SafeReadU64(v.obj + layout_.records_count_offset, &count)) return 0;
    if (data != v.rec_data || count != v.rec_count) {
        // Array (re)built by the game: records we hid before are gone, the new ones start visible.
        v.hidden_records.clear();
        v.rec_data = data;
        v.rec_count = count;
    }
    if (!IsHeapPtr(data) || count == 0 || count > MAX_RECORDS) return 0;

    int writes = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uintptr_t rec = static_cast<uintptr_t>(data + i * layout_.record_size);
        uint64_t token = 0, mask = 0;
        if (!SafeReadU64(rec + layout_.record_token_offset, &token) || !IsTarget(token)) continue;
        *has_targets = true;
        if (!SafeReadU64(rec + layout_.record_mask_offset, &mask) || mask == 0) continue;
        auto it = std::find_if(v.hidden_records.begin(), v.hidden_records.end(),
                               [rec](const HiddenRecord& h) { return h.addr == rec; });
        if (it == v.hidden_records.end()) v.hidden_records.push_back({ rec, token, mask });
        else { it->token = token; it->orig_mask = mask; }
        if (SafeWriteU64(rec + layout_.record_mask_offset, 0)) ++writes;
    }
    return writes;
}

// ---------------------------------------------------------------------------------------------------
// Flag cloth: physics patch pointers are moved past a reduced count so the draw loop never reaches them
// ---------------------------------------------------------------------------------------------------

bool VisibilityController::PatchArrayIsOurs(const Vehicle& v, uint64_t data, uint64_t count) const {
    if (data != v.patch_data || count != v.patch_count_reduced) return false;
    for (uint64_t i = 0; i < v.patch_count_full; ++i) {
        uint64_t p = 0;
        if (!SafeReadU64(static_cast<uintptr_t>(data + i * 8), &p) || p != v.patch_reordered[i]) return false;
    }
    return true;
}

int VisibilityController::HidePatches(Vehicle& v) {
    uint64_t data = 0, count = 0;
    if (!SafeReadU64(v.obj + layout_.patches_data_offset, &data) ||
        !SafeReadU64(v.obj + layout_.patches_count_offset, &count)) return 0;

    if (v.patches_hidden) {
        if (PatchArrayIsOurs(v, data, count)) return 0;   // still hidden, nothing to do
        v.patches_hidden = false;                          // the game rebuilt the array; hide again below
    }
    if (!IsHeapPtr(data) || count == 0 || count > MAX_PATCHES) return 0;

    std::vector<uint64_t> full(count), keep, flags;
    for (uint64_t i = 0; i < count; ++i) {
        if (!SafeReadU64(static_cast<uintptr_t>(data + i * 8), &full[i])) return 0;
        uint64_t token = 0;
        bool is_target = IsHeapPtr(full[i]) &&
                         SafeReadU64(static_cast<uintptr_t>(full[i] + layout_.patch_token_offset), &token) &&
                         IsTarget(token);
        (is_target ? flags : keep).push_back(full[i]);
    }
    if (flags.empty()) return 0;

    std::vector<uint64_t> reordered = keep;
    reordered.insert(reordered.end(), flags.begin(), flags.end());
    for (uint64_t i = 0; i < count; ++i) {
        if (!SafeWriteU64(static_cast<uintptr_t>(data + i * 8), reordered[i])) return 0;
    }
    if (!SafeWriteU64(v.obj + layout_.patches_count_offset, keep.size())) return 0;

    v.patches_hidden = true;
    v.patch_data = data;
    v.patch_count_full = count;
    v.patch_count_reduced = keep.size();
    v.patch_full = std::move(full);
    v.patch_reordered = std::move(reordered);
    return static_cast<int>(flags.size());
}

int VisibilityController::ShowPatches(Vehicle& v) {
    if (!v.patches_hidden) return 0;
    v.patches_hidden = false;
    uint64_t data = 0, count = 0;
    if (!SafeReadU64(v.obj + layout_.patches_data_offset, &data) ||
        !SafeReadU64(v.obj + layout_.patches_count_offset, &count)) return 0;
    if (!PatchArrayIsOurs(v, data, count)) {
        LOG_WARN("VisibilityController: patch array of vehicle 0x%llX changed while hidden - not restoring it",
            (unsigned long long)v.obj);
        return 0;
    }
    for (uint64_t i = 0; i < v.patch_count_full; ++i) {
        if (!SafeWriteU64(static_cast<uintptr_t>(data + i * 8), v.patch_full[i])) return 0;
    }
    SafeWriteU64(v.obj + layout_.patches_count_offset, v.patch_count_full);
    return static_cast<int>(v.patch_count_full - v.patch_count_reduced);
}

// ---------------------------------------------------------------------------------------------------

void VisibilityController::CollectFlagPatches(const Vehicle& v, std::vector<uint64_t>* out) const {
    uint64_t data = 0, count = 0;
    if (!SafeReadU64(v.obj + layout_.patches_data_offset, &data) ||
        !SafeReadU64(v.obj + layout_.patches_count_offset, &count)) return;
    if (!IsHeapPtr(data) || count > MAX_PATCHES) return;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t patch = 0, token = 0;
        if (SafeReadU64(static_cast<uintptr_t>(data + i * 8), &patch) && IsHeapPtr(patch) &&
            SafeReadU64(static_cast<uintptr_t>(patch + layout_.patch_token_offset), &token) && IsTarget(token)) {
            out->push_back(patch);
        }
    }
}

int VisibilityController::Hide(Vehicle& v, bool hide_cloth) {
    bool has_targets = false;
    int writes = HideRecords(v, &has_targets);

    // Trucks draw a baked merged model that ignores record masks; switch merging off while hidden.
    if (v.is_truck && has_targets) {
        uint64_t merged = 0;
        uint8_t flag = 0;
        if (SafeReadU64(v.obj + layout_.merged_offset, &merged) && IsHeapPtr(merged) &&
            SafeReadU8(static_cast<uintptr_t>(merged), &flag) && (flag & 1) == 0) {
            if (SafeWriteU8(static_cast<uintptr_t>(merged), static_cast<uint8_t>(flag | 1))) {
                v.merged_obj = static_cast<uintptr_t>(merged);
                ++writes;
            }
        }
    }

    // Hook mode hides cloth through the published set; only the fallback edits the patch list.
    if (!publish_) writes += hide_cloth ? HidePatches(v) : ShowPatches(v);
    return writes;
}

int VisibilityController::Show(Vehicle& v) {
    int writes = 0;
    for (const HiddenRecord& h : v.hidden_records) {
        uint64_t token = 0, mask = 0;
        if (SafeReadU64(h.addr + layout_.record_token_offset, &token) && token == h.token &&
            SafeReadU64(h.addr + layout_.record_mask_offset, &mask) && mask == 0 &&
            SafeWriteU64(h.addr + layout_.record_mask_offset, h.orig_mask)) ++writes;
    }
    v.hidden_records.clear();

    if (v.merged_obj) {
        uint64_t merged = 0;
        uint8_t flag = 0;
        if (SafeReadU64(v.obj + layout_.merged_offset, &merged) && merged == v.merged_obj &&
            SafeReadU8(v.merged_obj, &flag) && (flag & 1) &&
            SafeWriteU8(v.merged_obj, static_cast<uint8_t>(flag & ~1u))) ++writes;
        v.merged_obj = 0;
    }

    writes += ShowPatches(v);
    return writes;
}

void VisibilityController::LogDiagnostics(const char* context) const {
    std::vector<PlayerVehicle> current;
    bool ok = ResolvePlayerVehicles(&current);
    if (!ok || current.empty()) {
        LOG_DEBUG("Diag[%s]: no player vehicles (chain not valid)", context);
        return;
    }
    for (const PlayerVehicle& pv : current) {
        uint64_t merged = 0, data = 0, count = 0, pdata = 0, pcount = 0;
        uint8_t flag = 0;
        SafeReadU64(pv.obj + layout_.merged_offset, &merged);
        if (IsHeapPtr(merged)) SafeReadU8(static_cast<uintptr_t>(merged), &flag);
        SafeReadU64(pv.obj + layout_.records_data_offset, &data);
        SafeReadU64(pv.obj + layout_.records_count_offset, &count);
        SafeReadU64(pv.obj + layout_.patches_data_offset, &pdata);
        SafeReadU64(pv.obj + layout_.patches_count_offset, &pcount);
        char masks[256] = "";
        size_t used = 0;
        if (IsHeapPtr(data) && count <= MAX_RECORDS) {
            for (uint64_t i = 0; i < count && used < sizeof(masks) - 24; ++i) {
                uintptr_t rec = static_cast<uintptr_t>(data + i * layout_.record_size);
                uint64_t token = 0, mask = 0;
                if (!SafeReadU64(rec + layout_.record_token_offset, &token) || !IsTarget(token)) continue;
                SafeReadU64(rec + layout_.record_mask_offset, &mask);
                used += snprintf(masks + used, sizeof(masks) - used, " %llX", (unsigned long long)mask);
            }
        }
        LOG_DEBUG("Diag[%s]: %s 0x%llX merged=0x%llX flag=0x%02X records=%llu@0x%llX patches=%llu target masks:%s",
            context, pv.is_truck ? "truck" : "trailer", (unsigned long long)pv.obj, (unsigned long long)merged, flag,
            (unsigned long long)count, (unsigned long long)data, (unsigned long long)pcount, masks);
    }
}

void VisibilityController::Update(bool want_hidden, bool cloth_must_show) {
    if (!active_) return;
    std::vector<uint64_t> hidden_patches;

    std::vector<PlayerVehicle> current;
    ResolvePlayerVehicles(&current);

    int shown = 0, hidden = 0;

    // Vehicles that left the player's chain (detached trailer, truck change): restore them now,
    // while they still exist, and stop tracking them.
    for (auto it = vehicles_.begin(); it != vehicles_.end();) {
        bool still_player = std::any_of(current.begin(), current.end(),
                                        [&](const PlayerVehicle& pv) { return pv.obj == it->obj; });
        if (still_player) { ++it; continue; }
        if (StillSameObject(*it)) shown += Show(*it);
        LOG_INFO("VisibilityController: vehicle 0x%llX is no longer the player's - released",
            (unsigned long long)it->obj);
        it = vehicles_.erase(it);
    }

    for (const PlayerVehicle& pv : current) {
        auto it = std::find_if(vehicles_.begin(), vehicles_.end(), [&](const Vehicle& v) { return v.obj == pv.obj; });
        if (it == vehicles_.end()) {
            Vehicle v;
            v.obj = pv.obj;
            v.is_truck = pv.is_truck;
            SafeReadU64(pv.obj, &v.vtable);
            vehicles_.push_back(v);
            it = vehicles_.end() - 1;
            LOG_INFO("VisibilityController: tracking player %s 0x%llX", pv.is_truck ? "truck" : "trailer",
                (unsigned long long)pv.obj);
        }
        if (want_hidden) {
            hidden += Hide(*it, !cloth_must_show);
            if (publish_) CollectFlagPatches(*it, &hidden_patches);
        } else {
            shown += Show(*it);
        }
    }

    if (publish_) {
        publish_(hidden_patches.data(), static_cast<uint32_t>(hidden_patches.size()));
        if (hidden_patches.size() != last_published_) {
            LOG_INFO("VisibilityController: %zu flag cloth patch(es) hidden by the draw hook", hidden_patches.size());
            last_published_ = hidden_patches.size();
        }
    }

    if (!have_last_ || want_hidden != last_want_hidden_ || hidden || shown) {
        if (hidden || shown || !have_last_ || want_hidden != last_want_hidden_) {
            LOG_INFO("VisibilityController: %s - %zu player vehicle(s), %d hide writes, %d restore writes",
                want_hidden ? "HIDDEN" : "SHOWN", current.size(), hidden, shown);
        }
        have_last_ = true;
        last_want_hidden_ = want_hidden;
    }
}

} // namespace DynamicBanners
