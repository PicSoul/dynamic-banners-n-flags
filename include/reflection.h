#pragma once

#include <cstdint>

namespace DynamicBanners {

// Lookups in the game's own reflection tables (class descriptors, attribute records, enum tables), by name.
// Names survive recompiles, so these need no signatures. Layout (see tools/update_check.py, same logic):
//   class descriptor: +0x00 -> metadata { +0x00 char* name }, +0x18 parent descriptor, +0x20 -> attribute records
//   attribute record (40 bytes): { class descriptor, offset, type, flags, char* name }
//   enum table entry (16 bytes): { value, char* name }, sorted by name
namespace Reflection {

// Class descriptor for a class name (e.g. "flare_vehicle"), or 0.
uintptr_t FindClass(const char* name);

// Name of a class descriptor, or nullptr.
const char* ClassName(uintptr_t descriptor);

// Parent descriptor, or 0.
uintptr_t Parent(uintptr_t descriptor);

// Field offset of a named attribute of a class, or -1 (not found / not stored as a plain field).
int64_t AttributeOffset(uintptr_t descriptor, const char* attribute);

// Value of an enum name, identified by its alphabetical neighbours in the same enum table
// (e.g. "beacon" between "aux" and "brake"). Returns false if no such table entry exists.
bool EnumValue(const char* before, const char* name, const char* after, uint64_t* value);

} // namespace Reflection
} // namespace DynamicBanners
