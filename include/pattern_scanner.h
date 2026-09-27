#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace DynamicBanners {

struct PatternMatch {
    uintptr_t address;
    bool found;
    double scan_time_ms;

    PatternMatch() : address(0), found(false), scan_time_ms(0.0) {}
    explicit operator bool() const { return found && address != 0; }
};

class PatternScanner {
public:
    static PatternScanner& Instance() {
        static PatternScanner instance;
        return instance;
    }

    bool Initialize(HMODULE module_handle = nullptr);

    // Scans for an IDA-style signature (e.g. "48 8B 05 ? ? ? ? 48 85 C0")
    PatternMatch FindPattern(const std::string& pattern_str, const std::string& name = "");

    // Scans for a list of fallback patterns until one matches
    PatternMatch FindPatternWithFallbacks(const std::vector<std::string>& patterns, const std::string& name = "");

    // Scans the whole .text section and succeeds only if the pattern matches exactly once.
    // out_count receives the number of matches (0, 1 or 2 = "two or more").
    PatternMatch FindUniquePattern(const std::string& pattern_str, const std::string& name, size_t* out_count);

    // Test hook: scan an arbitrary buffer instead of the module (returns match offset or SIZE_MAX).
    size_t ScanBuffer(const uint8_t* data, size_t size, const std::string& pattern_str, size_t* out_count);

    // Resolves 32-bit RIP-relative displacement
    // Example: MOV RAX, [RIP + disp32] (48 8B 05 xx xx xx xx -> disp_offset=3, instr_len=7)
    static uintptr_t ResolveRipRelative(uintptr_t instruction_addr, uint32_t disp_offset = 3, uint32_t instr_len = 7);

    uintptr_t GetModuleBase() const { return module_base_; }
    size_t GetModuleSize() const { return module_size_; }
    uintptr_t GetTextSectionBase() const { return text_base_; }
    size_t GetTextSectionSize() const { return text_size_; }

private:
    PatternScanner() : module_base_(0), module_size_(0), text_base_(0), text_size_(0), initialized_(false) {}

    bool ParseIdaPattern(const std::string& pattern_str, std::vector<int16_t>& out_bytes);

    uintptr_t module_base_;
    size_t module_size_;
    uintptr_t text_base_;
    size_t text_size_;
    bool initialized_;
};

} // namespace DynamicBanners
