#pragma once

#include <string>
#include <cstdio>
#include <windows.h>

namespace DynamicBanners {

// Identifies one build of amtrucks.exe: PE link timestamp + image size, e.g. "6512A3F0-4A5E000".
// Signature overrides in the ini are stamped with it and only used for that exact build.
// tools/update_check.py computes the same string from the file on disk.
inline std::string GameBuildId(const IMAGE_NT_HEADERS* nt) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%08X-%X", static_cast<unsigned>(nt->FileHeader.TimeDateStamp),
             static_cast<unsigned>(nt->OptionalHeader.SizeOfImage));
    return buf;
}

} // namespace DynamicBanners
