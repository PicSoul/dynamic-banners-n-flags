#pragma once

#include <cstdint>

namespace DynamicBanners {

// SEH-guarded access to game memory. A stale or freed pointer degrades to "false" instead of a crash.
// These live in their own translation unit so no function mixes __try with C++ destructors (MSVC C2712).
bool SafeReadU64(uintptr_t addr, uint64_t* out);
bool SafeReadU32(uintptr_t addr, uint32_t* out);
bool SafeReadU8(uintptr_t addr, uint8_t* out);
bool SafeWriteU64(uintptr_t addr, uint64_t value);
bool SafeWriteU8(uintptr_t addr, uint8_t value);

// Calls obj->vtable[slot]() - the game's "class descriptor" getter - with the same guards. Only calls code inside
// the executable; returns 0 on any problem. Game thread only.
uintptr_t SafeCallGetClass(uintptr_t obj, uint32_t vtable_slot_offset);

// Module range of amtrucks.exe, set once at startup.
void SetModuleRange(uintptr_t base, uintptr_t end);
bool IsInModule(uint64_t p);

// A plausible user-mode heap pointer: canonical, 8-byte aligned and outside the executable image.
bool IsHeapPtr(uint64_t p);

} // namespace DynamicBanners
