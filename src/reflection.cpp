#include "reflection.h"
#include "mem_safe.h"
#include <windows.h>
#include <cstring>

namespace DynamicBanners {
namespace Reflection {

struct Section { uintptr_t begin = 0, end = 0; };
static Section g_rdata, g_data;

static void InitSections() {
    if (g_rdata.begin) return;
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        Section s{ base + sec->VirtualAddress, base + sec->VirtualAddress + sec->Misc.VirtualSize };
        if (!memcmp(sec->Name, ".rdata", 6)) g_rdata = s;
        else if (!memcmp(sec->Name, ".data", 6)) g_data = s;
    }
}

// Exact, NUL-terminated string at p (p must be in the module).
static bool StrIs(uint64_t p, const char* s) {
    if (!IsInModule(p)) return false;
    size_t n = strlen(s);
    __try {
        const char* c = reinterpret_cast<const char*>(p);
        return memcmp(c, s, n) == 0 && c[n] == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Calls fn(address of qword) for every 8-byte aligned qword in [begin, end) equal to value.
template <typename F>
static void ForEachPointerTo(const Section& s, uint64_t value, F fn) {
    for (uintptr_t a = s.begin; a + 8 <= s.end; a += 8) {
        uint64_t q = 0;
        if (SafeReadU64(a, &q) && q == value) { if (fn(a)) return; }
    }
}

// All occurrences of a NUL-delimited string in .rdata.
template <typename F>
static void ForEachString(const char* s, F fn) {
    size_t n = strlen(s);
    for (uintptr_t a = g_rdata.begin + 1; a + n + 1 <= g_rdata.end; ++a) {
        uint8_t first = 0;
        if (!SafeReadU8(a, &first) || first != static_cast<uint8_t>(s[0])) continue;
        uint8_t prev = 1;
        SafeReadU8(a - 1, &prev);
        if (prev == 0 && StrIs(a, s)) { if (fn(a)) return; }
    }
}

const char* ClassName(uintptr_t descriptor) {
    uint64_t meta = 0, name = 0;
    if (!IsInModule(descriptor) || !SafeReadU64(descriptor, &meta) || !IsInModule(meta) ||
        !SafeReadU64(meta, &name) || !IsInModule(name)) return nullptr;
    return reinterpret_cast<const char*>(name);
}

uintptr_t Parent(uintptr_t descriptor) {
    uint64_t p = 0;
    return (IsInModule(descriptor) && SafeReadU64(descriptor + 0x18, &p) && IsInModule(p)) ? static_cast<uintptr_t>(p) : 0;
}

uintptr_t FindClass(const char* name) {
    InitSections();
    uintptr_t found = 0;
    ForEachString(name, [&](uintptr_t str) {
        // metadata: a .data qword pointing at the name
        ForEachPointerTo(g_data, str, [&](uintptr_t meta) {
            // descriptor: an .rdata qword pointing at the metadata
            ForEachPointerTo(g_rdata, meta, [&](uintptr_t desc) {
                uint64_t attrs = 0;
                if (SafeReadU64(desc + 0x20, &attrs) && IsInModule(attrs)) { found = desc; return true; }
                return false;
            });
            return found != 0;
        });
        return found != 0;
    });
    return found;
}

int64_t AttributeOffset(uintptr_t descriptor, const char* attribute) {
    uint64_t rec = 0;
    if (!IsInModule(descriptor) || !SafeReadU64(descriptor + 0x20, &rec) || !IsInModule(rec)) return -1;
    for (int i = 0; i < 256; ++i, rec += 40) {
        uint64_t cls = 0, offset = 0, name = 0;
        if (!SafeReadU64(rec, &cls) || cls != descriptor) break;
        SafeReadU64(rec + 8, &offset);
        SafeReadU64(rec + 32, &name);
        if (StrIs(name, attribute)) return static_cast<int64_t>(offset) < 0 ? -1 : static_cast<int64_t>(offset);
    }
    return -1;
}

bool EnumValue(const char* before, const char* name, const char* after, uint64_t* value) {
    InitSections();
    bool ok = false;
    ForEachString(name, [&](uintptr_t str) {
        ForEachPointerTo(g_data, str, [&](uintptr_t entry_name) {
            uint64_t prev_name = 0, next_name = 0, v = 0;
            if (SafeReadU64(entry_name - 16, &prev_name) && StrIs(prev_name, before) &&
                SafeReadU64(entry_name + 16, &next_name) && StrIs(next_name, after) &&
                SafeReadU64(entry_name - 8, &v) && v != 0) {
                *value = v;
                ok = true;
            }
            return ok;
        });
        return ok;
    });
    return ok;
}

} // namespace Reflection
} // namespace DynamicBanners
