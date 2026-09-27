#include "telemetry_bridge.h"
#include "visibility_controller.h"
#include "config_manager.h"
#include "game_layout.h"
#include "scs_token.h"
#include "cloth_hook.h"
#include "version.h"
#include "logger.h"
#include <cstdio>
#include <cstdarg>

namespace DynamicBanners {

static SCSAPI_VOID OnBeaconChannel(const scs_string_t, const scs_u32_t, const scs_value_t* const value, const scs_context_t) {
    if (!value || value->type != SCS_VALUE_TYPE_bool) return;
    TelemetryBridge::Instance().SetBeaconActive(value->value_bool.value != 0);
}

static SCSAPI_VOID OnPauseEvent(const scs_event_t event, const void* const, const scs_context_t) {
    TelemetryBridge::Instance().SetPaused(event == SCS_TELEMETRY_EVENT_paused);
}

static SCSAPI_VOID OnFrameEndEvent(const scs_event_t, const void* const, const scs_context_t) {
    TelemetryBridge::Instance().OnFrameEnd();
}

void TelemetryBridge::GameMessage(const char* fmt, ...) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "[Dynamic Banners] ");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    LOG_INFO("%s", buf);
    if (game_log_) game_log_(SCS_LOG_TYPE_message, buf);
}

scs_result_t TelemetryBridge::Initialize(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_01) {
        LOG_ERROR("Unsupported telemetry API version 0x%08X", version);
        return SCS_RESULT_unsupported;
    }
    const auto* p = static_cast<const scs_telemetry_init_params_v101_t*>(params);
    game_log_ = p->common.log;
    LOG_INFO("Telemetry: game '%s' version %u.%u", p->common.game_id,
        SCS_GET_MAJOR_VERSION(p->common.game_version), SCS_GET_MINOR_VERSION(p->common.game_version));

    const ModConfig& cfg = ConfigManager::Instance().GetConfig();
    if (!cfg.enabled) {
        GameMessage("disabled in dynamic_banners.ini");
        return SCS_RESULT_ok;
    }

    GameLayout layout;
    if (!GameLayoutResolver::Resolve(&layout)) {
        GameMessage("INACTIVE - this game version is not supported yet (see dynamic_banners.log)");
        return SCS_RESULT_ok;   // stay loaded but do nothing; never guess
    }

    std::vector<uint64_t> tokens;
    for (const std::string& slot : cfg.target_slots) {
        uint64_t t = EncodeScsToken(slot);
        if (t) tokens.push_back(t);
        else LOG_WARN("Config: '%s' is not a valid slot name - ignored", slot.c_str());
    }
    std::vector<uint64_t> beacon_tokens;
    if (cfg.hide_beacons) {
        for (const std::string& slot : cfg.beacon_slots) {
            uint64_t t = EncodeScsToken(slot);
            if (t) beacon_tokens.push_back(t);
            else LOG_WARN("Config: '%s' is not a valid beacon slot name - ignored", slot.c_str());
        }
    }
    VisibilityController::PublishHiddenPatchesFn publish = nullptr;
    if (layout.patch_draw_function && ClothHook::Install(layout.patch_draw_function)) {
        publish = &ClothHook::SetHiddenPatches;
    }
    cloth_hook_ = publish != nullptr;
    VisibilityController::Instance().Initialize(layout, tokens, beacon_tokens, cfg.affect_trailers, cfg.max_trailers, publish);

    p->register_for_event(SCS_TELEMETRY_EVENT_paused, OnPauseEvent, nullptr);
    p->register_for_event(SCS_TELEMETRY_EVENT_started, OnPauseEvent, nullptr);
    p->register_for_event(SCS_TELEMETRY_EVENT_frame_end, OnFrameEndEvent, nullptr);
    if (p->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_beacon, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                                SCS_TELEMETRY_CHANNEL_FLAG_none, OnBeaconChannel, nullptr) != SCS_RESULT_ok) {
        GameMessage("INACTIVE - could not subscribe to the beacon telemetry channel");
        return SCS_RESULT_ok;
    }

    GameMessage("v" DYNAMIC_BANNERS_VERSION " active - banners/flags%s on your truck and trailers follow your beacons%s%s",
        layout.beacon_detection && !beacon_tokens.empty() ? "/beacon units" : "",
        cloth_hook_ ? "" : " (flag cloth shows while paused: cloth hook unavailable)",
        cfg.hide_beacons && !layout.beacon_detection ? " (beacon units: detection unavailable, see log)" : "");
    return SCS_RESULT_ok;
}

void TelemetryBridge::Shutdown() {
    // Remove the hook before the DLL can be unloaded. Vehicles may already be gone, so no data is written.
    ClothHook::Uninstall();
    game_log_ = nullptr;
}

void TelemetryBridge::SetBeaconActive(bool active) {
    if (active != beacon_active_) LOG_INFO("Beacon %s", active ? "ON" : "OFF");
    beacon_active_ = active;
}

void TelemetryBridge::SetPaused(bool paused) {
    if (paused != paused_) {
        LOG_INFO("Game %s", paused ? "paused" : "running");
        if (Logger::Instance().GetLevel() >= LogLevel::Debug) {
            VisibilityController::Instance().LogDiagnostics(paused ? "at pause" : "at resume");
        }
    }
    paused_ = paused;
}

void TelemetryBridge::OnFrameEnd() {
    VisibilityController& vc = VisibilityController::Instance();
    if (!vc.IsActive()) return;

    // Debug heartbeat once per second: shows whether frames keep coming while paused and what the
    // truck's merged model and record masks look like at that moment.
    ++frames_since_heartbeat_;
    if (Logger::Instance().GetLevel() >= LogLevel::Debug) {
        ULONGLONG now = GetTickCount64();
        if (now - last_heartbeat_ >= 1000) {
            LOG_DEBUG("Heartbeat: paused=%d beacon=%d frames=%u", paused_, beacon_active_, frames_since_heartbeat_);
            vc.LogDiagnostics("heartbeat");
            frames_since_heartbeat_ = 0;
            last_heartbeat_ = now;
        }
    }
    const ModConfig& cfg = ConfigManager::Instance().GetConfig();
    bool visible = beacon_active_ != cfg.invert_beacon;
    if (paused_ && cfg.show_while_paused) visible = true;
    // Without the hook, flag cloth must be back in the game's patch list whenever it may rebuild vehicles.
    vc.Update(!visible, !cloth_hook_ && paused_);
}

} // namespace DynamicBanners
