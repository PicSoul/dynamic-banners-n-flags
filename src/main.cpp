#include <windows.h>
#include <string>
#include "logger.h"
#include "config_manager.h"
#include "mem_safe.h"
#include "telemetry_bridge.h"
#include "cloth_hook.h"
#include "game_build.h"

static std::wstring GetModuleDirectory(HMODULE module) {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring s(path);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

// ---------------------------------------------------------------------------------------------------
// SCS telemetry SDK entry points. ATS loads every DLL in bin\win_x64\plugins\ and calls these.
// ---------------------------------------------------------------------------------------------------

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    return DynamicBanners::TelemetryBridge::Instance().Initialize(version, params);
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    DynamicBanners::TelemetryBridge::Instance().Shutdown();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        std::wstring dir = GetModuleDirectory(module);
        DynamicBanners::Logger::Instance().Initialize(dir, DynamicBanners::LogLevel::Info);

        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        DynamicBanners::SetModuleRange(base, base + nt->OptionalHeader.SizeOfImage);

        DynamicBanners::ConfigManager::Instance().Load(dir + L"\\dynamic_banners.ini", DynamicBanners::GameBuildId(nt));
        DynamicBanners::Logger::Instance().SetLevel(DynamicBanners::ConfigManager::Instance().GetConfig().log_level);
    } else if (reason == DLL_PROCESS_DETACH) {
        DynamicBanners::ClothHook::Uninstall();
        DynamicBanners::Logger::Instance().Shutdown();
    }
    return TRUE;
}
