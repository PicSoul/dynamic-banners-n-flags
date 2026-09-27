#pragma once

#include <string>
#include <cstdio>
#include <windows.h>

namespace DynamicBanners {

// Identifies one build of the game executable: PE link timestamp + image size, e.g. "6512A3F0-4A5E000".
// Signature overrides in the ini are stamped with it and only used for that exact build.
// tools/update_check.py computes the same string from the file on disk.
inline std::string GameBuildId(const IMAGE_NT_HEADERS* nt) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%08X-%X", static_cast<unsigned>(nt->FileHeader.TimeDateStamp),
             static_cast<unsigned>(nt->OptionalHeader.SizeOfImage));
    return buf;
}

// Which SCS game loaded the plugin. Both run the same engine; only the default accessory slots differ.
enum class Game { ATS, ETS2 };

inline Game DetectGame() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"eurotrucks2.exe") == 0 ? Game::ETS2 : Game::ATS;
}

inline const char* GameName(Game g) { return g == Game::ETS2 ? "Euro Truck Simulator 2" : "American Truck Simulator"; }

} // namespace DynamicBanners
