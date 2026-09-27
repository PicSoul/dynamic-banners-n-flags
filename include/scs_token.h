#pragma once

#include <cstdint>
#include <string>

namespace DynamicBanners {

// SCS base-38 token encoding used for unit/slot names (e.g. "f_banner" -> 0x2F4DFF54146).
// Alphabet: '\0' = 0, '0'-'9' = 1-10, 'a'-'z' = 11-36, '_' = 37; first character is least significant.
// Returns 0 for names that are empty, longer than 12 characters or contain other characters.
inline uint64_t EncodeScsToken(const std::string& name) {
    if (name.empty() || name.size() > 12) return 0;
    uint64_t value = 0, mul = 1;
    for (char c : name) {
        uint64_t digit;
        if (c >= '0' && c <= '9') digit = 1 + (c - '0');
        else if (c >= 'a' && c <= 'z') digit = 11 + (c - 'a');
        else if (c >= 'A' && c <= 'Z') digit = 11 + (c - 'A');
        else if (c == '_') digit = 37;
        else return 0;
        value += digit * mul;
        mul *= 38;
    }
    return value;
}

} // namespace DynamicBanners
