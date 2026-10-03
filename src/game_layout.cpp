#include "game_layout.h"
#include "pattern_scanner.h"
#include "config_manager.h"
#include "mem_safe.h"
#include "logger.h"
#include "reflection.h"
#include <cstring>

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

static const Operand MODEL_HOOKUPS = { 3, 4 };      // lea rcx, [rdi+disp32]  (array_t of hookups)
static const Operand GET_CLASS_SLOT = { 13, 1 };    // call qword ptr [rax+disp8]

// Everything beacon detection needs; any missing piece switches only that feature off.
static void ResolveBeaconDetection(GameLayout* L) {
    const ModConfig& cfg = ConfigManager::Instance().GetConfig();
    PatternScanner& scanner = PatternScanner::Instance();
    size_t n = 0;
    PatternMatch hookups = scanner.FindUniquePattern(cfg.sig_model_hookups, "ModelHookups", &n);
    PatternMatch get_class = scanner.FindUniquePattern(cfg.sig_hookup_class, "HookupClass", &n);
    if (hookups) L->model_hookups_offset = ReadOperand(hookups.address, MODEL_HOOKUPS);
    if (get_class) L->get_class_vt_slot = ReadOperand(get_class.address, GET_CLASS_SLOT);

    DWORD t0 = GetTickCount();
    L->flare_vehicle_class = Reflection::FindClass("flare_vehicle");
    const char* parent = Reflection::ClassName(Reflection::Parent(L->flare_vehicle_class));
    int64_t lt = L->flare_vehicle_class ? Reflection::AttributeOffset(L->flare_vehicle_class, "light_type") : -1;
    uint64_t beacon = 0;
    bool enum_ok = Reflection::EnumValue("aux", "beacon", "brake", &beacon);
    LOG_INFO("GameLayout: reflection lookup took %lu ms", GetTickCount() - t0);

    bool ok = true;
    if (!Plausible("model_hookups_offset", L->model_hookups_offset, 0x2000)) ok = false;
    if (!Plausible("get_class_vt_slot", L->get_class_vt_slot, 0x400)) ok = false;
    if (!L->flare_vehicle_class) { LOG_ERROR("GameLayout: reflection class 'flare_vehicle' not found"); ok = false; }
    else if (!parent || strcmp(parent, "light_source") != 0) {
        LOG_ERROR("GameLayout: class descriptor layout changed (flare_vehicle's parent is '%s')", parent ? parent : "?");
        ok = false;
    }
    if (lt <= 0 || lt > 0x4000) { LOG_ERROR("GameLayout: attribute flare_vehicle.light_type not found"); ok = false; }
    if (!enum_ok) { LOG_ERROR("GameLayout: light type 'beacon' not found in the enum table"); ok = false; }
    if (!ok) {
        LOG_WARN("GameLayout: beacon detection unavailable - beacon accessories will not be toggled");
        return;
    }
    L->light_type_offset = static_cast<uint32_t>(lt);
    L->beacon_light_bits = beacon;
    L->beacon_detection = true;
    LOG_INFO("GameLayout: beacons: hookups=+0x%X get_class=vt[0x%X] light_type=+0x%X beacon=0x%llX",
        L->model_hookups_offset, L->get_class_vt_slot, L->light_type_offset, (unsigned long long)beacon);
}

static const Operand MODEL_DESC = { 3, 4 };         // mov rcx, [rdi+disp32]   (model -> descriptor)
static const Operand MODEL_CALL = { 8, 4 };         // call part_count(descriptor)
static const Operand MODEL_PARTS = { 21, 4 };       // mov rax, [rdi+disp32]; test byte [rax+rbx*4], 1
static const Operand CAB_VEHICLE = { 111, 4 };      // mov rax, [rbp+disp32]   (cab object -> vehicle)
static const Operand CAB_RECORDS = { 118, 4 };      // mov rdi, [rbp+disp32]   (cab object's records data)

// Cab view support (needs the cloth hook); any missing piece switches only that feature off.
static void ResolveCabView(GameLayout* L) {
    const ModConfig& cfg = ConfigManager::Instance().GetConfig();
    PatternScanner& scanner = PatternScanner::Instance();
    size_t n = 0;
    PatternMatch cab = scanner.FindUniquePattern(cfg.sig_cab_draw, "CabDraw", &n);
    PatternMatch parts = scanner.FindUniquePattern(cfg.sig_model_parts, "ModelParts", &n);
    bool ok = cab && parts;
    GameLayout C = *L;
    if (ok) {
        C.cab_vehicle_offset = ReadOperand(cab.address, CAB_VEHICLE);
        C.cab_records_data_offset = ReadOperand(cab.address, CAB_RECORDS);
        C.model_desc_offset = ReadOperand(parts.address, MODEL_DESC);
        C.model_parts_offset = ReadOperand(parts.address, MODEL_PARTS);
        // part_count(desc): "sub rsp,28h; cmp byte ptr [rcx+loaded],0; je ...; mov eax,[rcx+count]"
        int32_t rel = static_cast<int32_t>(ReadOperand(parts.address, MODEL_CALL));
        uintptr_t fn = parts.address + MODEL_CALL.pos + 4 + rel;
        uint8_t b[13] = {};
        for (int i = 0; i < 13 && ok; ++i) ok = SafeReadU8(fn + i, &b[i]);
        if (ok && b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC && b[4] == 0x80 && b[5] == 0x79 && b[7] == 0x00 &&
            b[8] == 0x74 && b[10] == 0x8B && b[11] == 0x41) {
            C.desc_loaded_offset = b[6];
            C.desc_part_count_offset = b[12];
        } else {
            LOG_ERROR("GameLayout: model part count code changed");
            ok = false;
        }
        ok = ok && Plausible("cab_vehicle_offset", C.cab_vehicle_offset, 0x1000) &&
             Plausible("cab_records_data_offset", C.cab_records_data_offset, 0x4000) &&
             Plausible("model_desc_offset", C.model_desc_offset, 0x1000) &&
             Plausible("model_parts_offset", C.model_parts_offset, 0x1000);
    }
    if (!ok) {
        LOG_WARN("GameLayout: cab view support unavailable - banners and flags stay visible from the cab");
        return;
    }
    C.cab_draw_function = cab.address;
    *L = C;
    LOG_INFO("GameLayout: cab view: vehicle=+0x%X records=+0x%X model parts=+0x%X desc=+0x%X (loaded +0x%X, count +0x%X)",
        L->cab_vehicle_offset, L->cab_records_data_offset, L->model_parts_offset, L->model_desc_offset,
        L->desc_loaded_offset, L->desc_part_count_offset);
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

    if (cfg.hide_beacons) ResolveBeaconDetection(&L);
    if (L.patch_draw_function) ResolveCabView(&L);

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
