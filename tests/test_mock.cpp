// Unit tests for SCS Dynamic Banners-N-Flags.
//   bin\unit_tests.exe [path\to\amtrucks.exe]
// Test 3 checks the default signatures against the given (or default Steam) amtrucks.exe on disk, which also
// works as a quick "is this game update still supported?" check without starting the game.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <windows.h>
#include "scs_token.h"
#include "pattern_scanner.h"
#include "config_manager.h"
#include "game_layout.h"
#include "mem_safe.h"
#include "visibility_controller.h"
#include "logger.h"

using namespace DynamicBanners;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("    FAILED: %s (line %d)\n", #cond, __LINE__); ++g_failures; } } while (0)

// ---------------------------------------------------------------------------------------------------
static void TestTokens() {
    printf("[1] SCS token encoding\n");
    CHECK(EncodeScsToken("f_banner") == 0x2F4DFF54146ULL);
    CHECK(EncodeScsToken("flag_f_l") == 0x26451DB82C8ULL);
    CHECK(EncodeScsToken("flag_f_r") == 0x304280EF3C8ULL);
    CHECK(EncodeScsToken("r_banner") == 0x2F4DFF54152ULL);
    CHECK(EncodeScsToken("flag_r_l") == 0x2648A87E748ULL);
    CHECK(EncodeScsToken("flag_r_r") == 0x30460BB5848ULL);
    CHECK(EncodeScsToken("drv_plate") == 0x3E77CD79780EULL);
    CHECK(EncodeScsToken("") == 0);
    CHECK(EncodeScsToken("thirteen_char") == 0);
    CHECK(EncodeScsToken("bad-name") == 0);
}

// ---------------------------------------------------------------------------------------------------
static void TestScanBuffer() {
    printf("[2] Pattern scanning and uniqueness\n");
    const uint8_t buf[] = { 0x90, 0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0xC3, 0x48, 0x8B, 0x0D, 0x00 };
    size_t count = 0;
    size_t off = PatternScanner::Instance().ScanBuffer(buf, sizeof(buf), "48 8B 05 ? ? ? ? C3", &count);
    CHECK(off == 1 && count == 1);
    off = PatternScanner::Instance().ScanBuffer(buf, sizeof(buf), "48 8B", &count);
    CHECK(off == 1 && count == 2);
    PatternScanner::Instance().ScanBuffer(buf, sizeof(buf), "DE AD BE EF", &count);
    CHECK(count == 0);
}

// ---------------------------------------------------------------------------------------------------
// Map amtrucks.exe .text from disk and verify every default signature is unique and yields the expected values.
static void TestSignaturesAgainstExe(const char* exe_path) {
    printf("[3] Signatures against %s\n", exe_path);
    std::ifstream f(exe_path, std::ios::binary);
    if (!f) { printf("    SKIPPED: file not found\n"); return; }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(file.data() + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    const uint8_t* text = nullptr; size_t text_size = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (memcmp(sec->Name, ".text", 5) == 0) { text = file.data() + sec->PointerToRawData; text_size = sec->SizeOfRawData; }
    }
    CHECK(text != nullptr);
    if (!text) return;

    const ModConfig& cfg = ConfigManager::Instance().GetConfig();
    struct Expect { const char* name; const std::string* sig; uint32_t pos, size, value; };
    const Expect expects[] = {
        { "PlayerChain.actor",   &cfg.sig_player_chain, 13, 4, 0x31B0 },
        { "PlayerChain.truck",   &cfg.sig_player_chain, 43, 1, 0x18 },
        { "PlayerChain.trailer", &cfg.sig_player_chain, 55, 4, 0xC8 },
        { "Records.data",        &cfg.sig_records, 3, 4, 0x770 },
        { "Records.count",       &cfg.sig_records, 10, 4, 0x778 },
        { "Patches.data",        &cfg.sig_patches, 3, 4, 0x7C8 },
        { "Patches.count",       &cfg.sig_patches, 10, 4, 0x7D0 },
        { "NextTrailer",         &cfg.sig_next_trailer, 3, 4, 0x1060 },
        { "Merged",              &cfg.sig_merged, 3, 4, 0x1020 },
        { "TrailerConnected",    &cfg.sig_trailer_connected, 3, 4, 0xFB8 },
        { "PatchDraw",           &cfg.sig_patch_draw, 0, 0, 0 },
        { "ModelHookups",        &cfg.sig_model_hookups, 3, 4, 0x318 },
        { "HookupClass",         &cfg.sig_hookup_class, 13, 1, 0x28 },
    };
    for (const Expect& e : expects) {
        size_t count = 0;
        size_t off = PatternScanner::Instance().ScanBuffer(text, text_size, *e.sig, &count);
        uint32_t v = 0;
        if (count == 1 && e.size) memcpy(&v, text + off + e.pos, e.size);
        printf("    %-20s matches=%zu value=0x%X (1.61 value 0x%X)%s\n", e.name, count, v, e.value,
               count == 1 && v == e.value ? "" : "  <-- CHANGED");
        CHECK(count == 1);
    }
}

// ---------------------------------------------------------------------------------------------------
// Fake game world: the controller only reads memory, so plain heap objects laid out like the game's work.
alignas(16) static uint64_t g_fake_module[64];   // stands in for amtrucks.exe: vtables + game global
static const uint64_t F_BANNER = 0x2F4DFF54146ULL, FLAG_F_L = 0x26451DB82C8ULL, R_BANNER = 0x2F4DFF54152ULL;
static const uint64_t OTHER_SLOT = 0x1234567ULL;
static const uint64_t VISIBLE = 0xFFFFFFFFFFFFFFFFULL;

struct FakeVehicle {
    std::vector<uint8_t> obj;
    std::vector<uint8_t> records;        // 0x30 each
    std::vector<uint64_t> patches;        // patch pointers
    std::vector<uint8_t> patch_objs[4];   // 0x40 each, token at +0x10
    uint8_t merged[16] = {};
};

static uintptr_t P(const void* p) { return reinterpret_cast<uintptr_t>(p); }
static void Put64(std::vector<uint8_t>& b, size_t off, uint64_t v) { memcpy(&b[off], &v, 8); }
static uint64_t Get64(const std::vector<uint8_t>& b, size_t off) { uint64_t v; memcpy(&v, &b[off], 8); return v; }

static void BuildVehicle(FakeVehicle& v, const GameLayout& L, const uint64_t* slots, int nslots, const uint64_t* flag_slots, int nflags) {
    v.obj.assign(0x1100, 0);
    Put64(v.obj, 0, P(&g_fake_module[1]));                        // vtable inside the fake module
    v.records.assign(0x30 * nslots, 0);
    for (int i = 0; i < nslots; ++i) {
        Put64(v.records, i * 0x30 + 0x00, slots[i]);
        Put64(v.records, i * 0x30 + 0x08, VISIBLE);
    }
    Put64(v.obj, L.records_data_offset, P(v.records.data()));
    Put64(v.obj, L.records_count_offset, nslots);
    v.patches.clear();
    v.patches.push_back(P(&g_fake_module[8]));                     // a non-flag patch-like entry (token not target)
    for (int i = 0; i < nflags; ++i) {
        v.patch_objs[i].assign(0x40, 0);
        Put64(v.patch_objs[i], 0x10, flag_slots[i]);
        v.patches.push_back(P(v.patch_objs[i].data()));
    }
    Put64(v.obj, L.patches_data_offset, P(v.patches.data()));
    Put64(v.obj, L.patches_count_offset, v.patches.size());
    Put64(v.obj, L.merged_offset, P(v.merged));
}

static std::vector<uint64_t> g_published;
static void CapturePublished(const uint64_t* p, uint32_t n) { g_published.assign(p, p + n); }

static void SetImageAsModule() {
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    SetModuleRange(base, base + nt->OptionalHeader.SizeOfImage);
}

static void TestController(bool hook_mode) {
    printf("[%d] Visibility controller on a fake game world (%s)\n", hook_mode ? 5 : 4,
           hook_mode ? "cloth via draw hook" : "cloth via patch-list fallback");
    g_published.clear();
    SetImageAsModule();   // the test executable plays amtrucks.exe: its statics are "in the module"
    GameLayout L;
    L.game_global = P(&g_fake_module[0]);
    L.actor_offset = 0x31B0; L.truck_offset = 0x18; L.trailer_offset = 0xC8; L.next_trailer_offset = 0x1060;
    L.records_data_offset = 0x770; L.records_count_offset = 0x778;
    L.patches_data_offset = 0x7C8; L.patches_count_offset = 0x7D0; L.merged_offset = 0x1020;
    L.record_size = 0x30; L.record_token_offset = 0; L.record_mask_offset = 8; L.patch_token_offset = 0x10;
    L.trailer_connected_offset = 0xFB8;

    std::vector<uint8_t> game(0x4000, 0), actor(0x200, 0);
    g_fake_module[0] = P(game.data());
    Put64(game, 0x31B0, P(actor.data()));
    uint32_t alive = 0x80000001u; memcpy(&actor[8], &alive, 4);

    FakeVehicle truck, trailer;
    const uint64_t truck_slots[] = { OTHER_SLOT, F_BANNER, FLAG_F_L };
    const uint64_t truck_flags[] = { FLAG_F_L };
    const uint64_t trailer_slots[] = { R_BANNER, OTHER_SLOT };
    BuildVehicle(truck, L, truck_slots, 3, truck_flags, 1);
    BuildVehicle(trailer, L, trailer_slots, 2, nullptr, 0);
    Put64(actor, 0x18, P(truck.obj.data()));
    Put64(actor, 0xC8, P(trailer.obj.data()));
    Put64(trailer.obj, 0xFB8, P(truck.obj.data()));          // hooked up

    FakeVehicle stranger;   // another player's / AI truck: never linked into the chain
    BuildVehicle(stranger, L, truck_slots, 3, truck_flags, 1);

    auto& vc = VisibilityController::Instance();
    vc.Initialize(L, { F_BANNER, FLAG_F_L, R_BANNER }, {}, true, 10, hook_mode ? &CapturePublished : nullptr);

    vc.Update(true);
    CHECK(Get64(truck.records, 0x30 * 0 + 8) == VISIBLE);   // other slot untouched
    CHECK(Get64(truck.records, 0x30 * 1 + 8) == 0);         // f_banner hidden
    CHECK(Get64(truck.records, 0x30 * 2 + 8) == 0);         // flag hidden
    CHECK((truck.merged[0] & 1) == 1);                      // merging off on the truck
    if (hook_mode) {
        CHECK(Get64(truck.obj, 0x7D0) == 2);                // hook mode never edits the patch list
        CHECK(g_published.size() == 1 && g_published[0] == P(truck.patch_objs[0].data()));
    } else {
        CHECK(Get64(truck.obj, 0x7D0) == 1);                // flag cloth moved past the count
        CHECK(truck.patches[0] == P(&g_fake_module[8]));
    }
    CHECK(Get64(trailer.records, 8) == 0);                  // trailer r_banner hidden
    CHECK(Get64(trailer.records, 0x30 + 8) == VISIBLE);
    CHECK((trailer.merged[0] & 1) == 0);                    // merged bit is a truck-only change
    CHECK(Get64(stranger.records, 0x30 + 8) == VISIBLE);    // not the player's: untouched
    CHECK(Get64(stranger.obj, 0x7D0) == 2);

    vc.Update(true);                                         // idempotent
    CHECK(Get64(truck.obj, 0x7D0) == (hook_mode ? 2u : 1u));

    if (!hook_mode) {                                        // fallback: cloth back in the list while paused
        vc.Update(true, true);
        CHECK(Get64(truck.obj, 0x7D0) == 2);
        CHECK(Get64(truck.records, 0x30 * 1 + 8) == 0);     // banner stays hidden
        vc.Update(true);
        CHECK(Get64(truck.obj, 0x7D0) == 1);
    }

    vc.Update(false);
    CHECK(Get64(truck.records, 0x30 * 1 + 8) == VISIBLE);
    CHECK(Get64(truck.records, 0x30 * 2 + 8) == VISIBLE);
    CHECK((truck.merged[0] & 1) == 0);
    CHECK(Get64(truck.obj, 0x7D0) == 2);
    CHECK(truck.patches[1] == P(truck.patch_objs[0].data()));
    CHECK(Get64(trailer.records, 8) == VISIBLE);
    if (hook_mode) CHECK(g_published.empty());

    // Trailer unhooked while hidden: the game still points at it, but it is no longer connected.
    vc.Update(true);
    CHECK(Get64(trailer.records, 8) == 0);
    Put64(trailer.obj, 0xFB8, 0);
    vc.Update(true);
    CHECK(Get64(trailer.records, 8) == VISIBLE);
    Put64(trailer.obj, 0xFB8, P(truck.obj.data()));          // hooked up again: follows the beacon again
    vc.Update(true);
    CHECK(Get64(trailer.records, 8) == 0);

    // Trailer detached while hidden: it must be restored and released.
    vc.Update(true);
    CHECK(Get64(trailer.records, 8) == 0);
    Put64(actor, 0xC8, 0);
    vc.Update(true);
    CHECK(Get64(trailer.records, 8) == VISIBLE);
    CHECK(Get64(truck.records, 0x30 * 1 + 8) == 0);         // truck stays hidden

    // Game rebuilds the truck's records while hidden: the new records get hidden too.
    std::vector<uint8_t> rebuilt(0x30, 0);
    Put64(rebuilt, 0, F_BANNER); Put64(rebuilt, 8, VISIBLE);
    Put64(truck.obj, 0x770, P(rebuilt.data())); Put64(truck.obj, 0x778, 1);
    vc.Update(true);
    CHECK(Get64(rebuilt, 8) == 0);
    vc.Update(false);
    CHECK(Get64(rebuilt, 8) == VISIBLE);

    // Player not in a world (actor state bit clear): nothing is touched.
    alive = 0x00000001u; memcpy(&actor[8], &alive, 4);
    vc.Update(true);
    CHECK(Get64(rebuilt, 8) == VISIBLE);
}

// ---------------------------------------------------------------------------------------------------
// Beacon units: an accessory in a beacon slot is toggled only if its model has a flare_vehicle-derived hookup
// with the beacon light bit. Classes come from hookup->vtable[0x28](), walked up via descriptor+0x18.
struct FakeClassDesc { uint64_t meta, factory, common, parent; };
static FakeClassDesc g_desc_other = { 0, 0, 0, 0 };
static FakeClassDesc g_desc_light_source = { 0, 0, 0, 0 };
static FakeClassDesc g_desc_flare_vehicle = { 0, 0, 0, 0 };
static FakeClassDesc g_desc_beacon_vehicle = { 0, 0, 0, 0 };
static uintptr_t __fastcall FakeGetClass(uintptr_t self) { return *reinterpret_cast<uintptr_t*>(self + 8); }
static uint64_t g_fake_hookup_vtable[8];

struct FakeModel {
    std::vector<uint8_t> obj;
    std::vector<uint64_t> hookup_ptrs;
    std::vector<std::vector<uint8_t>> hookups;
};
static void AddHookup(FakeModel& m, const FakeClassDesc* cls, uint32_t light_type) {
    std::vector<uint8_t> h(0x240, 0);
    Put64(h, 0, P(g_fake_hookup_vtable));
    Put64(h, 8, P(cls));
    memcpy(&h[0x230], &light_type, 4);
    m.hookups.push_back(std::move(h));
}
static void FinishModel(FakeModel& m) {
    m.obj.assign(0x400, 0);
    m.hookup_ptrs.clear();
    for (auto& h : m.hookups) m.hookup_ptrs.push_back(P(h.data()));
    Put64(m.obj, 0x318 + 8, P(m.hookup_ptrs.data()));
    Put64(m.obj, 0x318 + 16, m.hookup_ptrs.size());
}

static void TestBeacons() {
    printf("[6] Beacon units (model has beacon lights)\n");
    SetImageAsModule();
    g_desc_flare_vehicle.parent = P(&g_desc_light_source);
    g_desc_beacon_vehicle.parent = P(&g_desc_flare_vehicle);
    g_fake_hookup_vtable[0x28 / 8] = reinterpret_cast<uint64_t>(&FakeGetClass);

    const uint64_t BEACON = EncodeScsToken("beacon"), REAR_BODY = EncodeScsToken("rear_body");
    const uint64_t R_BUMPER = EncodeScsToken("r_bumper");

    FakeModel roof, bar, lights_only, not_a_light, bumper;
    AddHookup(roof, &g_desc_beacon_vehicle, 0x200); AddHookup(roof, &g_desc_beacon_vehicle, 0x200);
    AddHookup(bar, &g_desc_flare_vehicle, 0x1); AddHookup(bar, &g_desc_beacon_vehicle, 0x200);
    AddHookup(lights_only, &g_desc_flare_vehicle, 0x20); AddHookup(lights_only, &g_desc_flare_vehicle, 0x1);
    AddHookup(not_a_light, &g_desc_other, 0x200);                 // beacon bit at +0x230 but not a flare_vehicle
    AddHookup(bumper, &g_desc_beacon_vehicle, 0x200);             // beacon lights, but r_bumper isn't a beacon slot
    for (FakeModel* m : { &roof, &bar, &lights_only, &not_a_light, &bumper }) FinishModel(*m);

    GameLayout L;
    L.game_global = P(&g_fake_module[0]);
    L.actor_offset = 0x31B0; L.truck_offset = 0x18; L.trailer_offset = 0xC8; L.next_trailer_offset = 0x1060;
    L.records_data_offset = 0x770; L.records_count_offset = 0x778;
    L.patches_data_offset = 0x7C8; L.patches_count_offset = 0x7D0; L.merged_offset = 0x1020;
    L.record_size = 0x30; L.record_token_offset = 0; L.record_mask_offset = 8; L.patch_token_offset = 0x10;
    L.trailer_connected_offset = 0xFB8;
    L.model_hookups_offset = 0x318; L.get_class_vt_slot = 0x28; L.flare_vehicle_class = P(&g_desc_flare_vehicle);
    L.light_type_offset = 0x230; L.beacon_light_bits = 0x200; L.beacon_detection = true;

    std::vector<uint8_t> game(0x4000, 0), actor(0x200, 0);
    g_fake_module[0] = P(game.data());
    Put64(game, 0x31B0, P(actor.data()));
    uint32_t alive = 0x80000001u; memcpy(&actor[8], &alive, 4);

    FakeVehicle truck, trailer;
    const uint64_t truck_slots[] = { BEACON, F_BANNER };
    const uint64_t trailer_slots[] = { REAR_BODY, REAR_BODY, REAR_BODY, R_BUMPER };
    BuildVehicle(truck, L, truck_slots, 2, nullptr, 0);
    BuildVehicle(trailer, L, trailer_slots, 4, nullptr, 0);
    Put64(truck.records, 0x10, P(roof.obj.data()));
    Put64(trailer.records, 0x00 * 0x30 + 0x10, P(bar.obj.data()));
    Put64(trailer.records, 0x01 * 0x30 + 0x10, P(lights_only.obj.data()));
    Put64(trailer.records, 0x02 * 0x30 + 0x10, P(not_a_light.obj.data()));
    Put64(trailer.records, 0x03 * 0x30 + 0x10, P(bumper.obj.data()));
    Put64(actor, 0x18, P(truck.obj.data()));
    Put64(actor, 0xC8, P(trailer.obj.data()));
    Put64(trailer.obj, 0xFB8, P(truck.obj.data()));

    auto& vc = VisibilityController::Instance();
    vc.Initialize(L, { F_BANNER }, { BEACON, EncodeScsToken("chs_beacon"), REAR_BODY }, true, 10, &CapturePublished);
    vc.Update(true);
    CHECK(Get64(truck.records, 8) == 0);                    // roof beacon hidden
    CHECK(Get64(truck.records, 0x30 + 8) == 0);             // banner hidden
    CHECK(Get64(trailer.records, 0 * 0x30 + 8) == 0);       // beacon bar hidden (bar + beacons: one model)
    CHECK(Get64(trailer.records, 1 * 0x30 + 8) == VISIBLE); // rear body with brake/tail lights only: kept
    CHECK(Get64(trailer.records, 2 * 0x30 + 8) == VISIBLE); // not a light at all: kept
    CHECK(Get64(trailer.records, 3 * 0x30 + 8) == VISIBLE); // r_bumper is not a beacon slot: kept
    CHECK((truck.merged[0] & 1) == 1);                      // truck has targets -> merged model off
    vc.Update(false);
    CHECK(Get64(truck.records, 8) == VISIBLE);
    CHECK(Get64(trailer.records, 8) == VISIBLE);

    // Beacon detection unavailable (e.g. after a game update): beacon slots are ignored, banners still work.
    L.beacon_detection = false;
    vc.Initialize(L, { F_BANNER }, { BEACON, REAR_BODY }, true, 10, &CapturePublished);
    vc.Update(true);
    CHECK(Get64(truck.records, 8) == VISIBLE);
    CHECK(Get64(trailer.records, 8) == VISIBLE);
    CHECK(Get64(truck.records, 0x30 + 8) == 0);
    vc.Update(false);
}

// Signature overrides in the ini are only used for the game build they are stamped with.
static void TestOverrideGating() {
    printf("[0] Config: signature overrides only apply to their own game build\n");
    const wchar_t* path = L"bin\\test_override.ini";
    {
        std::ofstream f(path);
        f << "[General]\nInvertBeacon = 1\n[Signatures]\nGameBuild = AAAA-1\nRecords = 11 22 33\n"
             "[Layout]\nRecordSize = 0x40\n";
    }
    ConfigManager& cm = ConfigManager::Instance();
    cm.Load(path, "AAAA-1");
    CHECK(cm.GetConfig().sig_records == "11 22 33");
    CHECK(cm.GetConfig().record_size == 0x40);
    CHECK(cm.GetConfig().signature_overrides == 2);
    CHECK(cm.GetConfig().invert_beacon);
    cm.Load(path, "BBBB-2");                                   // a different (newer) game build
    CHECK(cm.GetConfig().sig_records == "49 8B B6 ? ? ? ? 49 8B 86 ? ? ? ? 4C 8D 3C 40");
    CHECK(cm.GetConfig().record_size == 0x30);
    CHECK(cm.GetConfig().signature_overrides == 0);
    CHECK(cm.GetConfig().invert_beacon);                       // user settings always apply
    DeleteFileW(path);
}

int main(int argc, char** argv) {
    Logger::Instance().Initialize(L".", LogLevel::Error);
    TestOverrideGating();
    ConfigManager::Instance().Load(L"bin\\test_config.ini", "TESTBUILD");   // written with defaults if missing
    TestTokens();
    TestScanBuffer();
    if (argc > 1 && argv[1][0]) TestSignaturesAgainstExe(argv[1]);
    else printf("[3] Signatures against amtrucks.exe\n    SKIPPED: no game path given (run_tests.bat finds it through Steam)\n");
    TestController(false);
    TestController(true);
    TestBeacons();
    printf(g_failures ? "\n%d CHECK(S) FAILED\n" : "\nALL TESTS PASSED\n", g_failures);
    return g_failures ? 1 : 0;
}
