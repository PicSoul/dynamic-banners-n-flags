#include "cloth_hook.h"
#include "logger.h"
#include "mem_safe.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>

namespace DynamicBanners {

typedef void (__fastcall* PatchDrawFn)(uintptr_t patch, uintptr_t view);

static PatchDrawFn g_original = nullptr;
static uintptr_t g_target = 0;

// Lock-free hidden set: written by the game thread once per frame, read by whichever thread draws.
// A reader can see a mix of old and new entries for at most one frame, which only affects visibility.
static const uint32_t MAX_HIDDEN = 64;
static std::atomic<uint64_t> g_hidden[MAX_HIDDEN];
static std::atomic<uint32_t> g_hidden_count{0};

static void __fastcall PatchDrawDetour(uintptr_t patch, uintptr_t view) {
    uint32_t n = g_hidden_count.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n; ++i) {
        if (g_hidden[i].load(std::memory_order_relaxed) == patch) return;   // hidden: do not draw
    }
    g_original(patch, view);
}

bool ClothHook::Install(uintptr_t draw_function) {
    if (g_target) return true;
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR("ClothHook: MinHook init failed (%d)", s);
        return false;
    }
    s = MH_CreateHook(reinterpret_cast<LPVOID>(draw_function), reinterpret_cast<LPVOID>(&PatchDrawDetour),
                      reinterpret_cast<LPVOID*>(&g_original));
    if (s != MH_OK) {
        LOG_ERROR("ClothHook: create failed (%d)", s);
        return false;
    }
    s = MH_EnableHook(reinterpret_cast<LPVOID>(draw_function));
    if (s != MH_OK) {
        LOG_ERROR("ClothHook: enable failed (%d)", s);
        MH_RemoveHook(reinterpret_cast<LPVOID>(draw_function));
        return false;
    }
    g_target = draw_function;
    LOG_INFO("ClothHook: flag cloth draw hooked at 0x%llX", (unsigned long long)draw_function);
    return true;
}

// ---------------------------------------------------------------------------------------------------
// Cab view. Everything is changed for the duration of the cab draw only and put back before it returns,
// so nothing is left modified between frames (truck switches, rebuilds and freed objects cannot matter).

typedef uintptr_t (__fastcall* CabDrawFn)(uintptr_t interior, uintptr_t view, uintptr_t a3, uintptr_t a4);
static CabDrawFn g_cab_original = nullptr;
static uintptr_t g_cab_target = 0;
static CabLayout g_cab;                       // set once before the hook is enabled
static uint64_t g_cab_tokens[64];
static uint32_t g_cab_token_count = 0;
static std::atomic<uintptr_t> g_cab_truck{0};
static std::atomic<bool> g_cab_hide{false};
static std::atomic<int32_t> g_cab_copies{-1};
static SRWLOCK g_cab_lock = SRWLOCK_INIT;     // one cab draw at a time, whichever thread draws
static uint64_t g_cab_list[MAX_HIDDEN];       // the truck's patch list without the hidden flags
static const uint32_t MAX_CLEARED = 1024;
static uintptr_t g_cleared[MAX_CLEARED];      // part flags we cleared for this call

static const uint64_t MAX_CAB_RECORDS = 1024;
static const uint32_t MAX_PARTS = 512;

static bool IsHidden(uint64_t patch, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i)
        if (g_hidden[i].load(std::memory_order_relaxed) == patch) return true;
    return false;
}

static bool IsCabTarget(uint64_t token) {
    for (uint32_t i = 0; i < g_cab_token_count; ++i)
        if (g_cab_tokens[i] == token) return true;
    return false;
}

// Switches off every part of the cab's copies of the target accessories; returns how many copies.
static int HideCabCopies(uintptr_t interior, uint32_t* cleared) {
    uint64_t data = 0, count = 0;
    if (!SafeReadU64(interior + g_cab.records_data, &data) || !SafeReadU64(interior + g_cab.records_data + 8, &count) ||
        !IsHeapPtr(data) || count > MAX_CAB_RECORDS) return 0;
    int copies = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uintptr_t rec = static_cast<uintptr_t>(data + i * g_cab.record_size);
        uint64_t token = 0, model = 0, parts = 0, desc = 0;
        uint8_t loaded = 0;
        uint32_t nparts = 0;
        if (!SafeReadU64(rec + g_cab.record_token, &token) || !IsCabTarget(token) ||
            !SafeReadU64(rec + g_cab.record_model, &model) || !IsHeapPtr(model) ||
            !SafeReadU64(static_cast<uintptr_t>(model + g_cab.model_parts), &parts) || !IsHeapPtr(parts) ||
            !SafeReadU64(static_cast<uintptr_t>(model + g_cab.model_desc), &desc) || !IsHeapPtr(desc) ||
            !SafeReadU8(static_cast<uintptr_t>(desc + g_cab.desc_loaded), &loaded) || !loaded ||
            !SafeReadU32(static_cast<uintptr_t>(desc + g_cab.desc_part_count), &nparts) || nparts > MAX_PARTS) continue;
        ++copies;
        for (uint32_t p = 0; p < nparts && *cleared < MAX_CLEARED; ++p) {
            uintptr_t at = static_cast<uintptr_t>(parts + p * 4ull);
            uint32_t flags = 0;
            if (SafeReadU32(at, &flags) && (flags & 1) && SafeWriteU32(at, flags & ~1u)) g_cleared[(*cleared)++] = at;
        }
    }
    return copies;
}

static void ShowCabCopies(uint32_t cleared) {
    for (uint32_t i = 0; i < cleared; ++i) {
        uint32_t flags = 0;
        if (SafeReadU32(g_cleared[i], &flags) && (flags & 1) == 0) SafeWriteU32(g_cleared[i], flags | 1);
    }
}

static uintptr_t __fastcall CabDrawDetour(uintptr_t interior, uintptr_t view, uintptr_t a3, uintptr_t a4) {
    uintptr_t truck = g_cab_truck.load(std::memory_order_acquire);
    uint64_t owner = 0;
    // Only the player's cab (the dealer preview draws cabs too), and only while hidden.
    if (!truck || !g_cab_hide.load(std::memory_order_acquire) ||
        !SafeReadU64(interior + g_cab.vehicle, &owner) || owner != truck)
        return g_cab_original(interior, view, a3, a4);

    AcquireSRWLockExclusive(&g_cab_lock);
    // 1. The cab submits the truck's flag cloth itself: hand it the list without the hidden flags.
    uint32_t n = g_hidden_count.load(std::memory_order_acquire);
    uint64_t data = 0, count = 0, kept = 0;
    bool swap = n > 0 && SafeReadU64(truck + g_cab.patches_data, &data) &&
                SafeReadU64(truck + g_cab.patches_data + 8, &count) && IsHeapPtr(data) && count > 0 && count <= MAX_HIDDEN;
    if (swap) {
        for (uint64_t i = 0; i < count && swap; ++i) {
            uint64_t p = 0;
            swap = SafeReadU64(static_cast<uintptr_t>(data + i * 8), &p);
            if (swap && !IsHidden(p, n)) g_cab_list[kept++] = p;
        }
        swap = swap && kept < count &&
               SafeWriteU64(truck + g_cab.patches_data, reinterpret_cast<uint64_t>(g_cab_list)) &&
               SafeWriteU64(truck + g_cab.patches_data + 8, kept);
    }
    // 2. The cab's own copies of the banners/flags (built from interior_model) ignore the record mask.
    uint32_t cleared = 0;
    int copies = HideCabCopies(interior, &cleared);

    uintptr_t result = g_cab_original(interior, view, a3, a4);

    ShowCabCopies(cleared);
    if (swap) {
        uint64_t now_data = 0, now_count = 0;
        // Put the game's list back (it is never left pointing at ours).
        if (SafeReadU64(truck + g_cab.patches_data, &now_data) && now_data == reinterpret_cast<uint64_t>(g_cab_list)) {
            SafeWriteU64(truck + g_cab.patches_data, data);
            if (SafeReadU64(truck + g_cab.patches_data + 8, &now_count) && now_count == kept)
                SafeWriteU64(truck + g_cab.patches_data + 8, count);
        }
    }
    ReleaseSRWLockExclusive(&g_cab_lock);
    g_cab_copies.store(copies, std::memory_order_relaxed);
    return result;
}

bool ClothHook::InstallCab(uintptr_t cab_draw_function, const CabLayout& layout, const uint64_t* tokens, uint32_t count) {
    if (g_cab_target) return true;
    if (!g_target) return false;
    g_cab = layout;
    g_cab_token_count = 0;
    for (uint32_t i = 0; i < count && g_cab_token_count < 64; ++i) g_cab_tokens[g_cab_token_count++] = tokens[i];
    MH_STATUS s = MH_CreateHook(reinterpret_cast<LPVOID>(cab_draw_function), reinterpret_cast<LPVOID>(&CabDrawDetour),
                                reinterpret_cast<LPVOID*>(&g_cab_original));
    if (s != MH_OK) {
        LOG_ERROR("ClothHook: cab view hook create failed (%d)", s);
        return false;
    }
    s = MH_EnableHook(reinterpret_cast<LPVOID>(cab_draw_function));
    if (s != MH_OK) {
        LOG_ERROR("ClothHook: cab view hook enable failed (%d)", s);
        MH_RemoveHook(reinterpret_cast<LPVOID>(cab_draw_function));
        return false;
    }
    g_cab_target = cab_draw_function;
    LOG_INFO("ClothHook: cab view draw hooked at 0x%llX", (unsigned long long)cab_draw_function);
    return true;
}

void ClothHook::SetCabTarget(uintptr_t truck, bool hide) {
    g_cab_truck.store(truck, std::memory_order_release);
    g_cab_hide.store(hide, std::memory_order_release);
}

int ClothHook::CabCopiesHidden() { return g_cab_copies.load(std::memory_order_relaxed); }

void ClothHook::Uninstall() {
    g_hidden_count.store(0, std::memory_order_release);
    g_cab_truck.store(0, std::memory_order_release);
    if (g_cab_target) {
        MH_DisableHook(reinterpret_cast<LPVOID>(g_cab_target));
        MH_RemoveHook(reinterpret_cast<LPVOID>(g_cab_target));
        // Wait for a cab draw that is still running (it puts everything back before releasing the lock).
        AcquireSRWLockExclusive(&g_cab_lock);
        ReleaseSRWLockExclusive(&g_cab_lock);
        g_cab_target = 0;
        g_cab_original = nullptr;
    }
    if (!g_target) return;
    MH_DisableHook(reinterpret_cast<LPVOID>(g_target));
    MH_RemoveHook(reinterpret_cast<LPVOID>(g_target));
    MH_Uninitialize();
    g_target = 0;
    g_original = nullptr;
}

bool ClothHook::IsInstalled() {
    return g_target != 0;
}

void ClothHook::SetHiddenPatches(const uint64_t* patches, uint32_t count) {
    if (count > MAX_HIDDEN) count = MAX_HIDDEN;
    uint32_t old = g_hidden_count.load(std::memory_order_relaxed);
    // Shrink first so readers never scan entries that are about to change, then fill and publish.
    if (count < old) g_hidden_count.store(count, std::memory_order_release);
    for (uint32_t i = 0; i < count; ++i) g_hidden[i].store(patches[i], std::memory_order_relaxed);
    g_hidden_count.store(count, std::memory_order_release);
}

} // namespace DynamicBanners
