#include "cloth_hook.h"
#include "logger.h"
#include "MinHook.h"
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

void ClothHook::Uninstall() {
    g_hidden_count.store(0, std::memory_order_release);
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
