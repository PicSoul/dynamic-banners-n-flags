#include "config_manager.h"
#include "ini_upgrade.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace DynamicBanners {

// Default slots per game. ETS2 trucks have no oversize banners or warning flags; its trailers' rear signs
// (wide/long vehicle, TIR) share the r_banner slot.
// chs_oversize: the front oversize banner of the LORD G350 pickup mod.
static const char* DEFAULT_SLOTS_ATS = "f_banner, flag_f_l, flag_f_r, r_banner, flag_r_l, flag_r_r, chs_oversize";
static const char* DEFAULT_SLOTS_ETS2 = "r_banner";

// Built-in signatures for ATS 1.61. Each literal carries a "DBSIG:<key>=" marker so tools/update_check.py can read
// the signatures a DLL was built with straight from dynamic_banners.dll; the marker is stripped before use.
struct BuiltinSignature { const char* key_w_marker; std::string ModConfig::* field; };
static const BuiltinSignature BUILTIN_SIGNATURES[] = {
    { "DBSIG:PlayerChain=48 8B 1D ? ? ? ? 48 8B F9 48 8B 9B ? ? ? ? 48 85 DB 74 ? 8B 43 08 C1 E8 1F 84 C0 74 ? "
      "48 8B CB E8 ? ? ? ? 48 8B 53 ? 48 8B CB E8 ? ? ? ? 48 8B 93 ? ? ? ? 48 8B CB", &ModConfig::sig_player_chain },
    { "DBSIG:Records=49 8B B6 ? ? ? ? 49 8B 86 ? ? ? ? 4C 8D 3C 40", &ModConfig::sig_records },
    { "DBSIG:Patches=49 8B BE ? ? ? ? 49 8B 86 ? ? ? ? 48 8D 34 C7 48 3B FE 74 ? 48 8B 0F", &ModConfig::sig_patches },
    { "DBSIG:NextTrailer=49 8B 8E ? ? ? ? 4C 8B 7C 24 30", &ModConfig::sig_next_trailer },
    { "DBSIG:Merged=48 8B 86 ? ? ? ? 48 85 C0 74 ? F6 00 01", &ModConfig::sig_merged },
    { "DBSIG:TrailerConnected=48 83 B9 ? ? ? ? 00 0F 95 C0 C3 CC CC CC CC 48 8B 81 ? ? ? ? 48 85 C0 74",
      &ModConfig::sig_trailer_connected },
    { "DBSIG:PatchDraw=48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 20 08 00 00 48 8B 59 ? 48 8B EA "
      "48 8B 05", &ModConfig::sig_patch_draw },
    { "DBSIG:ModelHookups=48 8D 8F ? ? ? ? 48 3B 59 10 0F 83 ? ? ? ? 48 8B 41 08 49 8B D7", &ModConfig::sig_model_hookups },
    { "DBSIG:HookupClass=48 8B 45 08 48 8B 0C D8 48 8B 01 FF 50 ? 48 8B C8 48 85 C0 74 ? 48 3B CE",
      &ModConfig::sig_hookup_class },
};

// chs_beacsire: the beacon bars of the LORD G350 pickup mod.
static const char* DEFAULT_BEACON_SLOTS = "beacon, chs_beacon, rear_body, chs_beacsire";

// Earlier defaults: a value still equal to one of these was never customised, so it is moved to the new
// default (customised values are left alone).
static const char* OLD_SLOTS_ATS[] = {"f_banner, flag_f_l, flag_f_r, r_banner, flag_r_l, flag_r_r"};
static const char* OLD_BEACON_SLOTS[] = {"beacon, chs_beacon, rear_body"};

struct BuiltinLayout { const wchar_t* key; uint32_t ModConfig::* field; uint32_t value; };
static const BuiltinLayout BUILTIN_LAYOUT[] = {
    { L"RecordSize", &ModConfig::record_size, 0x30 },
    { L"RecordTokenOffset", &ModConfig::record_token_offset, 0x00 },
    { L"RecordMaskOffset", &ModConfig::record_mask_offset, 0x08 },
    { L"PatchTokenOffset", &ModConfig::patch_token_offset, 0x10 },
    { L"MaxTrailers", &ModConfig::max_trailers, 10 },
};

static std::string Trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static std::vector<std::string> SplitCommaSeparated(const std::string& input) {
    std::vector<std::string> result;
    std::istringstream stream(input);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = Trim(token);
        std::transform(token.begin(), token.end(), token.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!token.empty()) result.push_back(token);
    }
    return result;
}

static std::wstring Widen(const char* s) {
    wchar_t buf[1024] = {};
    MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, 1024);
    return buf;
}

static std::string ReadString(const wchar_t* section, const wchar_t* key, const char* def, const std::wstring& path) {
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(section, key, Widen(def).c_str(), buf, 1024, path.c_str());
    char narrow[1024] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, sizeof(narrow), nullptr, nullptr);
    return Trim(narrow);
}

// Accepts decimal or 0x-prefixed hex. Returns false if the key is missing or not a number.
static bool ReadUInt(const wchar_t* section, const wchar_t* key, const std::wstring& path, uint32_t* out) {
    std::string s = ReadString(section, key, "", path);
    if (s.empty()) return false;
    char* end = nullptr;
    unsigned long v = strtoul(s.c_str(), &end, 0);
    if (!end || end == s.c_str()) return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

static bool ReadBool(const wchar_t* section, const wchar_t* key, bool def, const std::wstring& path) {
    return GetPrivateProfileIntW(section, key, def ? 1 : 0, path.c_str()) != 0;
}

// The default ini (user settings only). Also the template an older ini is brought up to date with.
static std::string DefaultIni(bool ets2) {
    std::ostringstream out;
    out << "; SCS Dynamic Banners-N-Flags\n";
    out << "; Shows oversize banners and flags on your own truck and attached trailers only while the beacons are on.\n";
    out << "; This file is optional: delete it to go back to the defaults.\n\n";
    out << "[General]\n";
    out << "Enabled = 1\n";
    out << "; 0 = no log file (default). For troubleshooting: 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Trace\n";
    out << "LogLevel = 0\n";
    out << "; 0 = shown while beacons are ON, hidden while OFF. 1 = the opposite.\n";
    out << "InvertBeacon = 0\n";
    out << "; 1 = also toggle banners/flags on trailers attached to your truck\n";
    out << "AffectTrailers = 1\n";
    out << "; 1 = hide flag cloth with a small draw hook (recommended). 0 = no code hooks at all, but flag cloth\n";
    out << ";     then has to be shown while the game is paused.\n";
    out << "UseClothHook = 1\n\n";
    out << "[Targets]\n";
    out << "; Accessory slot names (the last part of an accessory's unit name, e.g. oversize.scs.lowboy.r_banner)\n";
    out << "Slots = " << (ets2 ? DEFAULT_SLOTS_ETS2 : DEFAULT_SLOTS_ATS) << "\n";
    out << "; 1 = also toggle beacon units (roof/chassis beacons, trailer beacon bars and strobes) with the beacons.\n";
    out << ";     Only accessories whose model actually has beacon lights are toggled.\n";
    out << "HideBeacons = 0\n";
    out << "; Slots checked for beacon units\n";
    out << "BeaconSlots = " << DEFAULT_BEACON_SLOTS << "\n";
    return out.str();
}

// Writes `text` to `path` through a temporary file, so a crash never leaves a half-written ini.
static bool WriteFileAtomic(const std::wstring& path, const std::string& text) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        for (char c : text) {  // Windows line endings, as the file always had
            if (c == '\n') f << '\r';
            f << c;
        }
        if (!f.flush()) return false;
    }
    if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(tmp.c_str());
    return false;
}

// Replaces "key = <old default>" lines with the new default in `text`; returns what was changed ("" if nothing).
static std::string MigrateKey(std::string& text, const char* key, const char* const* olds, size_t n_olds, const char* now) {
    std::string changed;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            auto trim = [](std::string s) {
                size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
                return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
            };
            if (_stricmp(trim(line.substr(0, eq)).c_str(), key) == 0) {
                std::string value = trim(line.substr(eq + 1));
                for (size_t i = 0; i < n_olds; ++i)
                    if (value == olds[i]) {
                        std::string fresh = std::string(key) + " = " + now;
                        text.replace(pos, line.size(), fresh);
                        end = pos + fresh.size();
                        changed = std::string(key) + " -> new default";
                        break;
                    }
            }
        }
        pos = end + 1;
    }
    return changed;
}

static std::string MigrateDefaults(std::string& text, bool ets2) {
    std::string a = ets2 ? std::string() : MigrateKey(text, "Slots", OLD_SLOTS_ATS, 1, DEFAULT_SLOTS_ATS);
    std::string b = MigrateKey(text, "BeaconSlots", OLD_BEACON_SLOTS, 1, DEFAULT_BEACON_SLOTS);
    return a.empty() ? b : b.empty() ? a : a + ", " + b;
}

void ConfigManager::SaveDefault(const std::wstring& ini_path, bool ets2) {
    WriteFileAtomic(ini_path, DefaultIni(ets2));
}

bool ConfigManager::Load(const std::wstring& ini_path, const std::string& game_build, bool ets2) {
    std::string ini_note;
    if (GetFileAttributesW(ini_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SaveDefault(ini_path, ets2);
    } else {
        // An ini from an older version: add new settings, drop obsolete ones, keep the player's values.
        // [Signatures] / [Layout] (written by the update tool) are kept as they are.
        std::ifstream f(ini_path, std::ios::binary);
        std::stringstream text;
        text << f.rdbuf();
        f.close();
        std::string current = text.str(), upgraded;
        std::string changes = MigrateDefaults(current, ets2);
        std::string added = UpgradeIni(current, DefaultIni(ets2), upgraded);
        if (!added.empty()) changes += (changes.empty() ? "" : "; ") + added;
        else upgraded = current;
        if (!changes.empty())
            ini_note = WriteFileAtomic(ini_path, upgraded) ? "updated dynamic_banners.ini: " + changes
                                                           : "could not update dynamic_banners.ini (" + changes + ")";
    }

    config_.enabled = ReadBool(L"General", L"Enabled", true, ini_path);
    int level = GetPrivateProfileIntW(L"General", L"LogLevel", 0, ini_path.c_str());
    config_.log_level = static_cast<LogLevel>(std::clamp(level, 0, 5));
    Logger::Instance().SetLevel(config_.log_level);
    if (!ini_note.empty()) LOG_INFO("Config: %s", ini_note.c_str());
    config_.invert_beacon = ReadBool(L"General", L"InvertBeacon", false, ini_path);
    config_.affect_trailers = ReadBool(L"General", L"AffectTrailers", true, ini_path);
    config_.use_cloth_hook = ReadBool(L"General", L"UseClothHook", true, ini_path);
    config_.target_slots = SplitCommaSeparated(ReadString(L"Targets", L"Slots", ets2 ? DEFAULT_SLOTS_ETS2 : DEFAULT_SLOTS_ATS, ini_path));
    config_.hide_beacons = ReadBool(L"Targets", L"HideBeacons", false, ini_path);
    config_.beacon_slots = SplitCommaSeparated(ReadString(L"Targets", L"BeaconSlots", DEFAULT_BEACON_SLOTS, ini_path));

    // Built-in signatures and layout first.
    for (const BuiltinSignature& b : BUILTIN_SIGNATURES) {
        config_.*b.field = strchr(b.key_w_marker, '=') + 1;
    }
    for (const BuiltinLayout& b : BUILTIN_LAYOUT) config_.*b.field = b.value;

    // Overrides from the ini, only for the exact game build they were made for.
    config_.game_build = game_build;
    config_.signature_overrides = 0;
    std::string ini_build = ReadString(L"Signatures", L"GameBuild", "", ini_path);
    bool build_matches = !ini_build.empty() && ini_build == game_build;
    int present = 0;
    for (const BuiltinSignature& b : BUILTIN_SIGNATURES) {
        std::string key(b.key_w_marker + 6, strchr(b.key_w_marker, '=') - (b.key_w_marker + 6));
        std::string v = ReadString(L"Signatures", Widen(key.c_str()).c_str(), "", ini_path);
        if (v.empty()) continue;
        ++present;
        if (build_matches) { config_.*b.field = v; ++config_.signature_overrides; }
    }
    for (const BuiltinLayout& b : BUILTIN_LAYOUT) {
        uint32_t v = 0;
        if (!ReadUInt(L"Layout", b.key, ini_path, &v)) continue;
        ++present;
        if (build_matches) { config_.*b.field = v; ++config_.signature_overrides; }
    }
    config_.max_trailers = std::min<uint32_t>(config_.max_trailers, 32);

    LOG_INFO("Config: Enabled=%d InvertBeacon=%d AffectTrailers=%d UseClothHook=%d, %zu target slots",
        config_.enabled, config_.invert_beacon, config_.affect_trailers,
        config_.use_cloth_hook, config_.target_slots.size());
    if (build_matches) {
        LOG_INFO("Config: game build %s - using %d signature/layout override(s) from the ini",
            game_build.c_str(), config_.signature_overrides);
    } else if (present) {
        LOG_WARN("Config: ignoring %d signature/layout override(s) in the ini - they are for game build '%s', "
            "running %s. Using the built-in values.", present, ini_build.c_str(), game_build.c_str());
    } else {
        LOG_INFO("Config: game build %s - using built-in signatures", game_build.c_str());
    }
    return true;
}

} // namespace DynamicBanners
