#include "mem_safe.h"
#include <windows.h>

namespace DynamicBanners {

static uintptr_t g_module_base = 0;
static uintptr_t g_module_end = 0;

bool SafeReadU64(uintptr_t addr, uint64_t* out) {
    if (addr < 0x10000 || !out) return false;
    __try { *out = *reinterpret_cast<const volatile uint64_t*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeReadU32(uintptr_t addr, uint32_t* out) {
    if (addr < 0x10000 || !out) return false;
    __try { *out = *reinterpret_cast<const volatile uint32_t*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeReadU8(uintptr_t addr, uint8_t* out) {
    if (addr < 0x10000 || !out) return false;
    __try { *out = *reinterpret_cast<const volatile uint8_t*>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeWriteU64(uintptr_t addr, uint64_t value) {
    if (addr < 0x10000) return false;
    __try { *reinterpret_cast<volatile uint64_t*>(addr) = value; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeWriteU8(uintptr_t addr, uint8_t value) {
    if (addr < 0x10000) return false;
    __try { *reinterpret_cast<volatile uint8_t*>(addr) = value; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

typedef uintptr_t (__fastcall* GetClassFn)(uintptr_t self);

uintptr_t SafeCallGetClass(uintptr_t obj, uint32_t vtable_slot_offset) {
    uint64_t vt = 0, fn = 0;
    if (!SafeReadU64(obj, &vt) || !IsInModule(vt) || !SafeReadU64(vt + vtable_slot_offset, &fn) || !IsInModule(fn)) return 0;
    __try { return reinterpret_cast<GetClassFn>(fn)(obj); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void SetModuleRange(uintptr_t base, uintptr_t end) {
    g_module_base = base;
    g_module_end = end;
}

bool IsInModule(uint64_t p) {
    return p >= g_module_base && p < g_module_end;
}

bool IsHeapPtr(uint64_t p) {
    return p >= 0x10000 && p < 0x7FFFFFFFFFFFULL && (p & 7) == 0 && !IsInModule(p);
}

} // namespace DynamicBanners
