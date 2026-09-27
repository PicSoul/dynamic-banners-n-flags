#include "pattern_scanner.h"
#include "logger.h"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace DynamicBanners {

bool PatternScanner::Initialize(HMODULE module_handle) {
    if (initialized_) return true;

    if (!module_handle) {
        module_handle = GetModuleHandleA(nullptr); // current exe (amtrucks.exe)
    }

    if (!module_handle) {
        LOG_ERROR("PatternScanner: Failed to acquire module handle.");
        return false;
    }

    module_base_ = reinterpret_cast<uintptr_t>(module_handle);

    auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_base_);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        LOG_ERROR("PatternScanner: Invalid DOS header signature.");
        return false;
    }

    auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(module_base_ + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        LOG_ERROR("PatternScanner: Invalid NT headers signature.");
        return false;
    }

    module_size_ = nt_headers->OptionalHeader.SizeOfImage;

    // Search for .text section
    auto section_header = IMAGE_FIRST_SECTION(nt_headers);
    for (WORD i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i, ++section_header) {
        char name[9] = { 0 };
        memcpy(name, section_header->Name, 8);
        if (strcmp(name, ".text") == 0) {
            text_base_ = module_base_ + section_header->VirtualAddress;
            text_size_ = section_header->Misc.VirtualSize;
            break;
        }
    }

    // Fallback if .text wasn't explicitly found
    if (text_base_ == 0 || text_size_ == 0) {
        LOG_WARN("PatternScanner: .text section not found; falling back to full module range.");
        text_base_ = module_base_;
        text_size_ = module_size_;
    }

    LOG_INFO("PatternScanner initialized: ModuleBase=0x%llX, TextBase=0x%llX, TextSize=0x%llX (%zu MB)",
        module_base_, text_base_, text_size_, text_size_ / (1024 * 1024));

    initialized_ = true;
    return true;
}

bool PatternScanner::ParseIdaPattern(const std::string& pattern_str, std::vector<int16_t>& out_bytes) {
    out_bytes.clear();
    std::istringstream stream(pattern_str);
    std::string token;

    while (stream >> token) {
        if (token == "?" || token == "??") {
            out_bytes.push_back(-1); // Wildcard
        } else {
            try {
                auto val = static_cast<int16_t>(std::stoul(token, nullptr, 16));
                out_bytes.push_back(val);
            } catch (...) {
                LOG_ERROR("PatternScanner: Malformed token '%s' in pattern.", token.c_str());
                return false;
            }
        }
    }

    return !out_bytes.empty();
}

PatternMatch PatternScanner::FindPattern(const std::string& pattern_str, const std::string& name) {
    PatternMatch match;
    if (!initialized_ && !Initialize()) {
        return match;
    }

    std::vector<int16_t> pattern;
    if (!ParseIdaPattern(pattern_str, pattern)) {
        LOG_ERROR("PatternScanner: Failed to parse pattern '%s'.", name.empty() ? pattern_str.c_str() : name.c_str());
        return match;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    const uint8_t* scan_start = reinterpret_cast<const uint8_t*>(text_base_);
    const size_t scan_len = text_size_ - pattern.size();
    const size_t pat_len = pattern.size();

    const int16_t first_byte = pattern[0];

    for (size_t i = 0; i < scan_len; ++i) {
        // Fast skip for first byte if not wildcard
        if (first_byte != -1 && scan_start[i] != static_cast<uint8_t>(first_byte)) {
            continue;
        }

        bool matched = true;
        for (size_t j = 1; j < pat_len; ++j) {
            if (pattern[j] != -1 && scan_start[i + j] != static_cast<uint8_t>(pattern[j])) {
                matched = false;
                break;
            }
        }

        if (matched) {
            match.address = reinterpret_cast<uintptr_t>(&scan_start[i]);
            match.found = true;
            break;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    match.scan_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    if (match.found) {
        LOG_INFO("PatternScanner: Found '%s' at 0x%llX (offset +0x%llX) in %.2f ms",
            name.empty() ? "Signature" : name.c_str(),
            match.address,
            match.address - module_base_,
            match.scan_time_ms);
    } else {
        LOG_WARN("PatternScanner: Signature '%s' NOT FOUND in %.2f ms.",
            name.empty() ? pattern_str.c_str() : name.c_str(),
            match.scan_time_ms);
    }

    return match;
}

PatternMatch PatternScanner::FindPatternWithFallbacks(const std::vector<std::string>& patterns, const std::string& name) {
    PatternMatch match;
    for (size_t i = 0; i < patterns.size(); ++i) {
        std::string variant_name = name + " (Variant #" + std::to_string(i + 1) + ")";
        match = FindPattern(patterns[i], variant_name);
        if (match.found) {
            return match;
        }
    }
    LOG_ERROR("PatternScanner: All %zu variants for '%s' failed to match.", patterns.size(), name.c_str());
    return match;
}

size_t PatternScanner::ScanBuffer(const uint8_t* data, size_t size, const std::string& pattern_str, size_t* out_count) {
    if (out_count) *out_count = 0;
    std::vector<int16_t> pattern;
    if (!data || !ParseIdaPattern(pattern_str, pattern) || pattern.size() > size) return SIZE_MAX;

    const size_t pat_len = pattern.size();
    const int16_t first_byte = pattern[0];
    size_t first_match = SIZE_MAX, count = 0;
    for (size_t i = 0; i + pat_len <= size; ++i) {
        if (first_byte != -1 && data[i] != static_cast<uint8_t>(first_byte)) continue;
        bool matched = true;
        for (size_t j = 1; j < pat_len; ++j) {
            if (pattern[j] != -1 && data[i + j] != static_cast<uint8_t>(pattern[j])) { matched = false; break; }
        }
        if (matched) {
            if (count == 0) first_match = i;
            if (++count >= 2) break;
        }
    }
    if (out_count) *out_count = count;
    return first_match;
}

PatternMatch PatternScanner::FindUniquePattern(const std::string& pattern_str, const std::string& name, size_t* out_count) {
    PatternMatch match;
    if (out_count) *out_count = 0;
    if (!initialized_ && !Initialize()) return match;

    auto start_time = std::chrono::high_resolution_clock::now();
    size_t count = 0;
    size_t off = ScanBuffer(reinterpret_cast<const uint8_t*>(text_base_), text_size_, pattern_str, &count);
    match.scan_time_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start_time).count();
    if (out_count) *out_count = count;

    if (count == 1) {
        match.address = text_base_ + off;
        match.found = true;
        LOG_INFO("PatternScanner: '%s' found at exe+0x%llX (%.1f ms)", name.c_str(),
            (unsigned long long)(match.address - module_base_), match.scan_time_ms);
    } else if (count == 0) {
        LOG_ERROR("PatternScanner: '%s' NOT FOUND (%.1f ms)", name.c_str(), match.scan_time_ms);
    } else {
        LOG_ERROR("PatternScanner: '%s' matched more than once - refusing to guess (%.1f ms)", name.c_str(), match.scan_time_ms);
    }
    return match;
}

uintptr_t PatternScanner::ResolveRipRelative(uintptr_t instruction_addr, uint32_t disp_offset, uint32_t instr_len) {
    if (!instruction_addr) return 0;
    
    // In x86_64, RIP-relative displacement is a signed 32-bit integer
    int32_t displacement = *reinterpret_cast<const int32_t*>(instruction_addr + disp_offset);
    uintptr_t target = instruction_addr + instr_len + displacement;
    return target;
}

} // namespace DynamicBanners
