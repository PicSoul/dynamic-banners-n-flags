#pragma once

#include <atomic>
#include <windows.h>
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"

namespace DynamicBanners {

// Official SCS telemetry SDK glue. Everything here runs on the game thread:
//  - truck.light.beacon channel -> beacon state
//  - paused / started events    -> pause state
//  - frame_end event            -> VisibilityController::Update()
class TelemetryBridge {
public:
    static TelemetryBridge& Instance() {
        static TelemetryBridge instance;
        return instance;
    }

    scs_result_t Initialize(const scs_u32_t version, const scs_telemetry_init_params_t* const params);
    void Shutdown();

    void SetBeaconActive(bool active);
    void SetPaused(bool paused);
    void OnFrameEnd();

    // Prints a line to the in-game console (only valid while the plugin is initialized).
    void GameMessage(const char* fmt, ...);

private:
    TelemetryBridge() = default;

    scs_log_t game_log_ = nullptr;
    bool beacon_active_ = false;
    bool paused_ = true;
    bool cloth_hook_ = false;
    unsigned frames_since_heartbeat_ = 0;
    int logged_cab_copies_ = -1;
    ULONGLONG last_heartbeat_ = 0;
};

} // namespace DynamicBanners
