#include "game_layout.h"
#include "pattern_scanner.h"
#include "config_manager.h"
#include "mem_safe.h"
#include "logger.h"

namespace DynamicBanners {

// Where each value sits inside its signature (byte position and operand size).
// These positions belong to the default signatures in config_manager.cpp; a replacement signature
// in the ini must keep the same instruction layout.
struct Operand { uint32_t pos; uint32_t size; };

static const Operand PC_GLOBAL_DISP = { 3, 4 };   // mov rbx, [rip+disp32]  (instruction length 7)
static const Operand PC_ACTOR = { 13, 4 };        // mov rbx, [rbx+disp32]
static const Operand PC_TRUCK = { 43, 1 };        // mov rdx, [rbx+disp8]
static const Operand PC_TRAILER = { 55, 4 };      // mov rdx, [rbx+disp32]
static const Operand REC_DATA = { 3, 4 };
static const Operand REC_COUNT = { 10, 4 };
static const Operand PATCH_DATA = { 3, 4 };
static const Operand PATCH_COUNT = { 10, 4 };
static const Operand NEXT_TRAILER = { 3, 4 };
static const Operand MERGED = { 3, 4 };
static const Operand TRAILER_CONNECTED = { 3, 4 };   // cmp qword ptr [rcx+disp32], 0

static uint32_t ReadOperand(uintptr_t match, Operand op) {
    uint32_t v = 0;
    if (op.size == 1) { uint8_t b = 0; SafeReadU8(match + op.pos, &b); v = b; }
    else SafeReadU32(match + op.pos, &v);
    return v;
}

static bool Plausible(const char* name, uint32_t value, uint32_t max) {
    if (value == 0 || value > max || (value & 7) != 0) {
        LOG_ERROR("GameLayout: %s = 0x%X is implausible - the game code has changed", name, value);
        return false;
    }
    return true;
}

bool GameLayoutResolver::Resolve(GameLayout* out) {
    const ModConfig& cfg = ConfigManager::Instance().GetConfig();
    PatternScanner& scanner = PatternScanner::Instance();
    if (!scanner.Initialize()) return false;

    size_t n = 0;
    PatternMatch chain = scanner.FindUniquePattern(cfg.sig_player_chain, "PlayerChain", &n);
    PatternMatch recs = scanner.FindUniquePattern(cfg.sig_records, "Records", &n);
    PatternMatch patches = scanner.FindUniquePattern(cfg.sig_patches, "Patches", &n);
    PatternMatch next = scanner.FindUniquePattern(cfg.sig_next_trailer, "NextTrailer", &n);
    PatternMatch merged = scanner.FindUniquePattern(cfg.sig_merged, "Merged", &n);
    if (!chain || !recs || !patches || !next || !merged) {
        LOG_ERROR("GameLayout: one or more signatures failed - the mod stays INACTIVE for this game version");
        return false;
    }

    GameLayout L;
    int32_t disp = static_cast<int32_t>(ReadOperand(chain.address, PC_GLOBAL_DISP));
    L.game_global = chain.address + 7 + disp;
    L.actor_offset = ReadOperand(chain.address, PC_ACTOR);
    L.truck_offset = ReadOperand(chain.address, PC_TRUCK);
    L.trailer_offset = ReadOperand(chain.address, PC_TRAILER);
    L.records_data_offset = ReadOperand(recs.address, REC_DATA);
    L.records_count_offset = ReadOperand(recs.address, REC_COUNT);
    L.patches_data_offset = ReadOperand(patches.address, PATCH_DATA);
    L.patches_count_offset = ReadOperand(patches.address, PATCH_COUNT);
    L.next_trailer_offset = ReadOperand(next.address, NEXT_TRAILER);
    L.merged_offset = ReadOperand(merged.address, MERGED);
    L.record_size = cfg.record_size;
    L.record_token_offset = cfg.record_token_offset;
    L.record_mask_offset = cfg.record_mask_offset;
    L.patch_token_offset = cfg.patch_token_offset;

    bool ok = true;
    if (!IsInModule(L.game_global)) {
        LOG_ERROR("GameLayout: game global 0x%llX is outside amtrucks.exe", (unsigned long long)L.game_global);
        ok = false;
    }
    ok &= Plausible("actor_offset", L.actor_offset, 0x10000);
    ok &= Plausible("truck_offset", L.truck_offset, 0x1000);
    ok &= Plausible("trailer_offset", L.trailer_offset, 0x1000);
    ok &= Plausible("next_trailer_offset", L.next_trailer_offset, 0x4000);
    ok &= Plausible("records_data_offset", L.records_data_offset, 0x4000);
    ok &= Plausible("patches_data_offset", L.patches_data_offset, 0x4000);
    ok &= Plausible("merged_offset", L.merged_offset, 0x4000);
    if (L.records_count_offset != L.records_data_offset + 8 || L.patches_count_offset != L.patches_data_offset + 8) {
        LOG_ERROR("GameLayout: array layout changed (data/count not adjacent)");
        ok = false;
    }
    if (L.record_size < 0x20 || L.record_size > 0x100 || L.record_mask_offset + 8 > L.record_size) {
        LOG_ERROR("GameLayout: [Layout] record values are invalid");
        ok = false;
    }
    if (!ok) return false;

    // Optional values: missing ones degrade gracefully instead of disabling the mod.
    PatternMatch connected = scanner.FindUniquePattern(cfg.sig_trailer_connected, "TrailerConnected", &n);
    if (connected) {
        L.trailer_connected_offset = ReadOperand(connected.address, TRAILER_CONNECTED);
        if (!Plausible("trailer_connected_offset", L.trailer_connected_offset, 0x4000)) L.trailer_connected_offset = 0;
    }
    if (!L.trailer_connected_offset) LOG_WARN("GameLayout: trailer connection state unknown - trailers will not be toggled");

    if (cfg.use_cloth_hook) {
        PatternMatch draw = scanner.FindUniquePattern(cfg.sig_patch_draw, "PatchDraw", &n);
        if (draw) L.patch_draw_function = draw.address;
        else LOG_WARN("GameLayout: flag cloth draw function not found - using the patch-list fallback");
    }

    LOG_INFO("GameLayout: game=exe+0x%llX actor=+0x%X truck=+0x%X trailer=+0x%X next=+0x%X",
        (unsigned long long)(L.game_global - scanner.GetModuleBase()), L.actor_offset, L.truck_offset,
        L.trailer_offset, L.next_trailer_offset);
    LOG_INFO("GameLayout: records=+0x%X patches=+0x%X merged=+0x%X connected=+0x%X record_size=0x%X mask=+0x%X",
        L.records_data_offset, L.patches_data_offset, L.merged_offset, L.trailer_connected_offset,
        L.record_size, L.record_mask_offset);
    *out = L;
    return true;
}

} // namespace DynamicBanners
