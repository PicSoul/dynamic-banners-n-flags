#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "logger.h"

namespace DynamicBanners {

// A signature plus where to read each value from a match. See config/dynamic_banners.ini for the format.
struct ModConfig {
    bool enabled = true;
    LogLevel log_level = LogLevel::Info;
    bool invert_beacon = false;       // false: shown while beacons are ON, hidden while OFF
    bool show_while_paused = false;   // show everything while the game is paused (menus, shops, loading)
    bool affect_trailers = true;
    bool use_cloth_hook = true;       // hide flag cloth with the draw hook (else: patch-list method, shown while paused)

    std::vector<std::string> target_slots;  // accessory slot names, e.g. f_banner

    // Signatures in effect (IDA style, '?' wildcards). Each must match exactly once in amtrucks.exe.
    // Built-in values, replaced by [Signatures] entries from the ini only when that section's GameBuild
    // matches the running game (see Load()).
    std::string game_build;              // build of the running amtrucks.exe
    int signature_overrides = 0;         // number of ini values in effect for this build
    std::string sig_player_chain;
    std::string sig_records;
    std::string sig_patches;
    std::string sig_next_trailer;
    std::string sig_merged;
    std::string sig_trailer_connected;
    std::string sig_patch_draw;

    // Layout values that cannot be read from code
    uint32_t record_size = 0x30;
    uint32_t record_token_offset = 0x00;
    uint32_t record_mask_offset = 0x08;
    uint32_t patch_token_offset = 0x10;
    uint32_t max_trailers = 10;
};

class ConfigManager {
public:
    static ConfigManager& Instance() {
        static ConfigManager instance;
        return instance;
    }

    // Loads the ini; missing keys keep their built-in defaults. Writes a default file (user settings only)
    // if none exists. [Signatures] / [Layout] overrides apply only if [Signatures] GameBuild == game_build.
    bool Load(const std::wstring& ini_path, const std::string& game_build);
    void SaveDefault(const std::wstring& ini_path);

    const ModConfig& GetConfig() const { return config_; }

private:
    ConfigManager() = default;
    ModConfig config_;
};

} // namespace DynamicBanners
