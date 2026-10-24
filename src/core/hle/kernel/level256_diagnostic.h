// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include "common/common_types.h"
#include "core/hle/service/plgldr/level256_kprocess_view.h"
#include "core/hle/service/plgldr/plugin_path.h"

namespace Kernel {

constexpr u64 LEVEL256_DIAGNOSTIC_TITLE_ID = 0x0004000000155100ULL;
constexpr u64 LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID = 0x000400000016C700ULL;
constexpr u64 LEVEL256_NETWORK_IPC_DIAGNOSTIC_TITLE_ID = 0x000400000016C700ULL;
constexpr u32 LEVEL256_NETWORK_IPC_TRACE_LIMIT = 48;
constexpr std::array<u32, 3> LEVEL256_BOOTLOADER_SNAPSHOT_ADDRESSES = {
    0x06FFF0E8, // saved game instruction 0
    0x06FFF0EC, // saved game instruction 1
    0x06FFF0F0, // saved game entry point
};
constexpr std::array<u32, 2> LEVEL256_GAME_ENTRY_SNAPSHOT_ADDRESSES = {
    0x00100000, // original game entry instruction
    0x00100004, // following game instruction
};
constexpr u32 LEVEL256_BOOTLOADER_TRACE_ADD_LR_PC = 0x06FFF0D0;
constexpr u32 LEVEL256_BOOTLOADER_TRACE_LOAD_PC = 0x06FFF0D4;
constexpr u32 LEVEL256_BOOTLOADER_EXPECTED_RETURN_TARGET = 0x00100000;
constexpr u32 LEVEL256_BOOTLOADER_TRACE_INSTRUCTION_LIMIT = 4096;
constexpr std::size_t LEVEL256_BOOTLOADER_TRACE_RING_SIZE = 16;
constexpr u32 LEVEL256_BOOTLOADER_POINTER_CHANGE_LOG_LIMIT = 8;

constexpr bool ShouldCaptureLevel256BootloaderDiagnostic(
    bool compatibility_enabled, bool diagnostics_enabled, bool identity_verified, u64 title_id,
    u32 process_id, u32 plugin_process_id, bool plugin_loaded, u32 thread_id, u32 plugin_thread_id,
    u32 pc, u32 code_begin, u32 code_end, u32 bootloader_begin, u32 bootloader_end) {
    return compatibility_enabled && diagnostics_enabled && identity_verified &&
           title_id == LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID && process_id != 0 &&
           process_id == plugin_process_id && plugin_loaded && thread_id != UINT32_MAX &&
           thread_id == plugin_thread_id &&
           ((code_begin <= pc && pc < code_end) || (bootloader_begin <= pc && pc < bootloader_end));
}

constexpr bool ShouldCaptureLevel256BootloaderMapDiagnostic(bool compatibility_enabled,
                                                            bool diagnostics_enabled,
                                                            bool identity_verified, u64 title_id,
                                                            u32 process_id, u32 thread_id,
                                                            bool thread_owns_process) {
    return compatibility_enabled && diagnostics_enabled && identity_verified &&
           title_id == LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID && process_id != 0 &&
           thread_id != UINT32_MAX && thread_owns_process;
}

constexpr bool ShouldCaptureLevel256BootloaderExceptionDiagnostic(
    bool compatibility_enabled, bool diagnostics_enabled, bool identity_verified, u64 title_id,
    u32 process_id, u32 plugin_process_id, bool plugin_loaded, u32 thread_id,
    u32 plugin_thread_id) {
    return compatibility_enabled && diagnostics_enabled && identity_verified &&
           title_id == LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID && process_id != 0 &&
           process_id == plugin_process_id && plugin_loaded && thread_id != UINT32_MAX &&
           thread_id == plugin_thread_id;
}

constexpr bool ShouldCaptureLevel256NetworkIpcDiagnostic(bool compatibility_enabled,
                                                         bool diagnostics_enabled,
                                                         bool identity_verified, u64 title_id,
                                                         u32 process_id, u32 plugin_process_id,
                                                         bool plugin_loaded) {
    return compatibility_enabled && diagnostics_enabled && identity_verified &&
           title_id == LEVEL256_NETWORK_IPC_DIAGNOSTIC_TITLE_ID && process_id != 0 &&
           process_id == plugin_process_id && plugin_loaded;
}

constexpr bool ConsumeLevel256NetworkIpcTraceSlot(bool eligible, u32& count) {
    if (!eligible || count >= LEVEL256_NETWORK_IPC_TRACE_LIMIT) {
        return false;
    }
    ++count;
    return true;
}
/// Title IDs included by the official Level256 online-support list.
constexpr bool IsLevel256SupportedTitleId(u64 title_id) {
    return Service::PLGLDR::IsLevel256PolicyTitle(title_id);
}
constexpr u32 LEVEL256_DIAGNOSTIC_CODE_BEGIN = 0x07000100;
constexpr u32 LEVEL256_DIAGNOSTIC_CODE_END = 0x07103000;
constexpr u32 LEVEL256_DIAGNOSTIC_SVC_PC = 0x070A12D4;
constexpr u32 LEVEL256_DIAGNOSTIC_SVC_ID = 0x2A;
constexpr u32 LEVEL256_DIAGNOSTIC_TYPE = 0x20000;
constexpr s32 LEVEL256_DIAGNOSTIC_PARAM = 0;
constexpr u32 LEVEL256_LUMA_MARKER_RESULT = 1;
constexpr u32 LEVEL256_VERIFICATION_TYPE = 0x30000;
constexpr s32 LEVEL256_VERIFICATION_PARAM = 0;
constexpr u32 LEVEL256_VERIFICATION_RESULT = 0x9A7FC03E;
constexpr u64 LEVEL256_VERIFICATION_OUTPUT = 0x12FAADCC;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_SVC_ID = 0x80;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_SVC_PC = 0x070991F0;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_CALLBACK = 0x07046430;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_CALLBACK_ARGUMENT = 0x07046424;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_PATCH_ADDRESS = 0x070464D4;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_PATCH_BEFORE = 0xE5C21013;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_PATCH_AFTER = 0xE5C23013;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_PATCH_RETURN = 0x07046424;
constexpr u32 LEVEL256_QUERY_PROCESS_MEMORY_SVC_ID = 0x7D;
constexpr u32 LEVEL256_QUERY_PROCESS_MEMORY_SVC_PC = 0x070A1414;
constexpr u32 LEVEL256_QUERY_PROCESS_MEMORY_FAILURE = 0xD8E007F7;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_FIELD_CALLBACK = 0x07046440;
constexpr u32 LEVEL256_CUSTOM_BACKDOOR_FIELD_CALLBACK_SVC_PC = 0x070991F0;
constexpr u32 LEVEL256_PROCESS_VIEW_SIZE = Service::PLGLDR::Level256::KProcessViewV1::Size;
constexpr u32 LEVEL256_PROCESS_VIEW_PAGE_ALIGNMENT = 0x1000;
constexpr u32 LEVEL256_PROCESS_VIEW_COPY_SIZE = 0x100;
constexpr u32 LEVEL256_PROCESS_VIEW_N3DS_RESOURCE_LIMIT_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::ResourceLimitN3dsOffset;
constexpr u32 LEVEL256_PROCESS_VIEW_O3DS8X_RESOURCE_LIMIT_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::ResourceLimitO3dsOffset;
constexpr u32 LEVEL256_PROCESS_VIEW_N3DS_CODESET_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::CodeSetN3dsOffset;
constexpr u32 LEVEL256_PROCESS_VIEW_N3DS_PROCESS_ID_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::ProcessIdN3dsOffset;
constexpr u32 LEVEL256_PROCESS_VIEW_O3DS8X_CODESET_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::CodeSetO3dsOffset;
constexpr u32 LEVEL256_PROCESS_VIEW_O3DS8X_PROCESS_ID_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::ProcessIdO3dsOffset;
constexpr u32 LEVEL256_PROCESS_VIEW_CODESET_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::CodeSetObjectOffset;
constexpr u32 LEVEL256_CODESET_TEXT_PAGES_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::CodeSetTextPagesOffset;
constexpr u32 LEVEL256_CODESET_SNAPSHOT_SIZE = 0x64;
// Historical plugin offsets retained for regression fixtures. Runtime dispatch uses the loaded
// CodeSet data range instead of these layout-dependent addresses.
constexpr u32 LEVEL256_CODESET_COPY_DESTINATION = 0x070F5664;
constexpr u32 LEVEL256_PROCESS_VIEW_COPY_DESTINATION = 0x070F3BD8;
constexpr u32 LEVEL256_PROCESS_VIEW_RESOURCE_LIMIT_OFFSET =
    LEVEL256_PROCESS_VIEW_N3DS_RESOURCE_LIMIT_OFFSET;
constexpr u32 LEVEL256_PROCESS_VIEW_RESOURCE_OBJECT_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::ResourceObjectOffset;
constexpr u32 LEVEL256_PROCESS_VIEW_PRIORITY_LIMIT_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::PriorityLimitOffset;
constexpr u32 LEVEL256_PROCESS_VIEW_THREAD_LIMIT_OFFSET =
    Service::PLGLDR::Level256::KProcessViewV1::ThreadLimitOffset;
constexpr u32 LEVEL256_BACKDOOR_CURRENT_PROCESS_CALLBACK = 0x07046424;
constexpr u32 LEVEL256_BACKDOOR_VIEW_FLAG_CALLBACK = 0x07046430;
constexpr u32 LEVEL256_BACKDOOR_VIEW_FIELD_CALLBACK = 0x07046440;
constexpr u32 LEVEL256_BACKDOOR_RESOURCE_LIMIT_CALLBACK = 0x07046498;
constexpr u32 LEVEL256_BACKDOOR_PRIORITY_LIMIT_CALLBACK = 0x07046458;
constexpr u32 LEVEL256_BACKDOOR_COPY_CALLBACK = 0x070464DC;
constexpr u32 LEVEL256_BACKDOOR_VIEW_FLAG_OFFSET = 0xB0;
constexpr u32 LEVEL256_BACKDOOR_VIEW_FLAG_BIT = 0x2000;
enum class Level256BackdoorCallbackKind {
    Unknown,
    CurrentProcess,
    ViewFlag,
    ViewField,
    ResourceLimit,
    PriorityLimit,
    ProcessSnapshotCopy,
};

/// Resolve only callback contracts whose complete instruction signatures were checked in the
/// official legacy plugin or the v2.0.1 signed artifact. The legacy digest path retains its
/// original callback table; signed artifacts use code signatures and a branch-target range check.
inline Level256BackdoorCallbackKind ResolveLevel256BackdoorCallback(
    u32 callback, std::span<const u32> instructions, u32 code_begin, u32 code_end,
    bool legacy_hash_verified) {
    const auto in_code = [&](std::size_t words) {
        return callback >= code_begin && code_begin < code_end && (callback & 3) == 0 &&
               static_cast<u64>(callback) + words * sizeof(u32) <= code_end &&
               instructions.size() >= words;
    };
    const auto matches = [&](auto signature) {
        if (!in_code(signature.size())) {
            return false;
        }
        for (std::size_t i = 0; i < signature.size(); ++i) {
            if (instructions[i] != signature[i]) {
                return false;
            }
        }
        return true;
    };

    constexpr std::array<u32, 3> current_process{0xE3E03A06, 0xE5130FFB, 0xE12FFF1E};
    constexpr std::array<u32, 4> view_flag{0xE59030B0, 0xE3833A02, 0xE58030B0, 0xE12FFF1E};
    constexpr std::array<u32, 6> view_field{0xE790C002, 0xE3CC3C0F, 0xE1833001,
                                            0xE7803002, 0xE20C0C0F, 0xE12FFF1E};
    constexpr std::array<u32, 17> resource_limit{
        0xE7902001, 0xE5D2C011, 0xE5D23010, 0xE5D20012, 0xE5D21013, 0xE183340C,
        0xE1833800, 0xE1833C01, 0xE2833004, 0xE1A0C423, 0xE1A00823, 0xE1A01C23,
        0xE5C23010, 0xE5C2C011, 0xE5C20012, 0xE5C21013, 0xE12FFF1E,
    };
    constexpr std::array<u32, 16> priority_limit{
        0xE7903002, 0xE1A0C421, 0xE5D30009, 0xE5D32008, 0xE5C3C009, 0xE1822400,
        0xE5D3000A, 0xE1A0C821, 0xE1822800, 0xE5D3000B, 0xE5C31008, 0xE1A01C21,
        0xE5C3C00A, 0xE5C3100B, 0xE1820C00, 0xE12FFF1E,
    };
    constexpr std::array<u32, 6> process_snapshot_copy{
        0xE92D4010, 0xF10C01C0, 0xEB018BD1, 0xF10801C0, 0xE8BD8010, 0xE12FFF1E,
    };

    if (matches(current_process)) {
        return Level256BackdoorCallbackKind::CurrentProcess;
    }
    if (matches(view_flag)) {
        return Level256BackdoorCallbackKind::ViewFlag;
    }
    if (matches(view_field)) {
        return Level256BackdoorCallbackKind::ViewField;
    }
    if (matches(resource_limit)) {
        return Level256BackdoorCallbackKind::ResourceLimit;
    }
    if (matches(priority_limit)) {
        return Level256BackdoorCallbackKind::PriorityLimit;
    }
    if (in_code(process_snapshot_copy.size()) && instructions[0] == process_snapshot_copy[0] &&
        instructions[1] == process_snapshot_copy[1] &&
        (instructions[2] & 0xFF000000) == 0xEB000000 &&
        instructions[3] == process_snapshot_copy[3] &&
        instructions[4] == process_snapshot_copy[4] &&
        instructions[5] == process_snapshot_copy[5]) {
        const u32 branch = instructions[2];
        const s32 displacement = static_cast<s32>(branch << 8) >> 6;
        const s64 target = static_cast<s64>(callback) + 8 + displacement;
        if ((branch & 0x0F000000) == 0x0B000000 && target >= code_begin && target < code_end) {
            return Level256BackdoorCallbackKind::ProcessSnapshotCopy;
        }
    }

    if (legacy_hash_verified && in_code(1)) {
        if (callback == LEVEL256_BACKDOOR_CURRENT_PROCESS_CALLBACK) {
            return Level256BackdoorCallbackKind::CurrentProcess;
        }
        if (callback == LEVEL256_BACKDOOR_VIEW_FLAG_CALLBACK) {
            return Level256BackdoorCallbackKind::ViewFlag;
        }
        if (callback == LEVEL256_BACKDOOR_VIEW_FIELD_CALLBACK) {
            return Level256BackdoorCallbackKind::ViewField;
        }
        if (callback == LEVEL256_BACKDOOR_RESOURCE_LIMIT_CALLBACK) {
            return Level256BackdoorCallbackKind::ResourceLimit;
        }
        if (callback == LEVEL256_BACKDOOR_PRIORITY_LIMIT_CALLBACK) {
            return Level256BackdoorCallbackKind::PriorityLimit;
        }
        if (callback == LEVEL256_BACKDOOR_COPY_CALLBACK) {
            return Level256BackdoorCallbackKind::ProcessSnapshotCopy;
        }
    }
    return Level256BackdoorCallbackKind::Unknown;
}
constexpr u32 LEVEL256_CONTROL_MEMORY_UNSAFE_SVC_ID = 0xA3;
constexpr u32 LEVEL256_CONTROL_MEMORY_UNSAFE_TARGET = 0x07500000;
constexpr u32 LEVEL256_CONTROL_MEMORY_UNSAFE_SIZE = 0x10000;
constexpr u32 LEVEL256_CONTROL_MEMORY_UNSAFE_OPERATION = 0x203;
constexpr u32 LEVEL256_CONTROL_MEMORY_UNSAFE_PERMISSIONS = 0x3;

/// Checks a caller against the code range actually mapped for the authenticated plugin.
constexpr bool IsLevel256PluginCodeAddress(u32 code_begin, u32 code_end, u32 pc) {
    return code_begin < code_end && pc >= code_begin && pc < code_end;
}
constexpr u32 LEVEL256_WORKER_THREAD_ENTRY = 0x07015CF8;
constexpr u32 LEVEL256_THREAD_CREATE_LOG_LIMIT = 8;
constexpr u32 LEVEL256_OPEN_PROCESS_TRACE_LIMIT = 262144;
constexpr u32 LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT = 32;
constexpr u32 LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT = 32;
constexpr u32 LEVEL256_OPEN_PROCESS_TRACE_LOG_LIMIT =
    LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT + LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT;
constexpr u32 LEVEL256_CTRPF_OSD_NOT_FOUND = UINT32_MAX;
constexpr u32 LEVEL256_CTRPF_OSD_MAX_SCAN_BYTES = 0x4000000;
constexpr u32 LEVEL256_CTRPF_OSD_STMFD_BACKSCAN_BYTES = 0x400;
constexpr u32 LEVEL256_CTRPF_ARBITER_DIAGNOSTIC_LIMIT = 8;
constexpr std::array<u32, 7> LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES = {
    0x070C8750, // "Yo-kai Watch 2 Shin-uchi (JPN)" in the title table
    0x070CA564, // "Create backup now" (English resource)
    0x070C7DD8, // "Creating backup"
    0x070C809C, // "Backup created. "
    0x070CB554, // "Plugin ready!"
    0x070CDC28, // "TitleConfig/016C700.bin" external resource path
    0x070C8A64, // "/Level256/start_as_a" external resource path
};
constexpr u32 LEVEL256_CTRPF_OSD_CANDIDATE_ADDRESS = 0x003788C4;
constexpr u32 LEVEL256_CTRPF_OSD_HOOK_BRANCH = 0xEA6C1DCD;
constexpr u32 LEVEL256_CTRPF_HOOK_WRAPPER_ADDRESS = 0x01E80000;
constexpr u32 LEVEL256_CTRPF_MAIN_CALLBACK_ADDRESS = 0x07024D30;
// Confirmed by decoding the direct BL at 0x07024D8C in the hash-matched 3GX image.
constexpr u32 LEVEL256_CTRPF_OSD_RENDER_ENTRY_ADDRESS = 0x07024578;
constexpr u32 LEVEL256_CTRPF_HOOK_TRACE_RUN_LIMIT = 32;
constexpr u32 LEVEL256_CTRPF_HOOK_INVOCATION_LIMIT = 32;
constexpr u32 LEVEL256_CTRPF_HOOK_WRAPPER_WORD_COUNT = 8;
constexpr u32 LEVEL256_CTRPF_ASM_WRAPPER_WORD_COUNT = 24;
constexpr u32 LEVEL256_CTRPF_OSD_CANDIDATE_SIZE = sizeof(u32);
constexpr std::size_t LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT =
    LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size() + 1;
constexpr u32 LEVEL256_CTRPF_STARTUP_WATCH_LOG_LIMIT_PER_TARGET = 16;
constexpr std::size_t LEVEL256_SVC_DIAGNOSTIC_PATTERN_LIMIT = 8;

struct Level256SvcDiagnosticPattern {
    u32 svc_id{};
    u32 pc{};
    std::array<u32, 4> arguments{};

    constexpr bool operator==(const Level256SvcDiagnosticPattern&) const = default;
};

/// Record a previously unseen SVC register pattern, bounded to the first eight per operation.
constexpr bool RecordLevel256SvcDiagnosticPattern(
    std::array<Level256SvcDiagnosticPattern, LEVEL256_SVC_DIAGNOSTIC_PATTERN_LIMIT>& patterns,
    std::size_t& count, const Level256SvcDiagnosticPattern& pattern) {
    if (count > patterns.size()) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (patterns[i] == pattern) {
            return false;
        }
    }
    if (count == patterns.size()) {
        return false;
    }
    patterns[count++] = pattern;
    return true;
}

constexpr bool ShouldScanLevel256CtrpfOSD(bool diagnostics_enabled, bool identity_verified,
                                          u64 title_id) {
    return diagnostics_enabled && identity_verified &&
           title_id == LEVEL256_BOOTLOADER_DIAGNOSTIC_TITLE_ID;
}

constexpr bool ShouldCaptureLevel256CtrpfStartupDiagnostic(bool diagnostics_enabled,
                                                           bool identity_verified, u64 title_id,
                                                           u32 process_id, u32 owner_process_id,
                                                           bool plugin_loaded) {
    return ShouldScanLevel256CtrpfOSD(diagnostics_enabled, identity_verified, title_id) &&
           process_id != 0 && process_id == owner_process_id && plugin_loaded;
}

/// Authenticates a call from the official Level256 plugin code in one of its supported titles.
constexpr bool IsVerifiedLevel256PluginContext(bool compatibility_enabled, bool supported_title,
                                               bool official_plugin_hash_matches, u32 pid,
                                               u32 plugin_pid, bool plugin_loaded, u32 code_begin,
                                               u32 code_end, u32 pc) {
    return compatibility_enabled && supported_title && official_plugin_hash_matches &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, pc);
}

/// Returns the matching watched target. Text targets are read-only observations; the OSD
/// candidate is watched for writes. Ranges are checked without overflowing the guest address.
constexpr std::size_t GetLevel256CtrpfStartupWatchTarget(u32 address, u32 size, bool is_write,
                                                         u32 osd_candidate) {
    if (size == 0) {
        return LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT;
    }
    const u64 access_end = static_cast<u64>(address) + size;
    if (access_end > (u64{1} << 32)) {
        return LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT;
    }
    if (is_write && osd_candidate != LEVEL256_CTRPF_OSD_NOT_FOUND &&
        static_cast<u64>(address) <
            static_cast<u64>(osd_candidate) + LEVEL256_CTRPF_OSD_CANDIDATE_SIZE &&
        static_cast<u64>(osd_candidate) < access_end) {
        return LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size();
    }
    if (!is_write) {
        for (std::size_t i = 0; i < LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES.size(); ++i) {
            if (static_cast<u64>(address) <= LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[i] &&
                LEVEL256_CTRPF_STARTUP_TEXT_ADDRESSES[i] < access_end) {
                return i;
            }
        }
    }
    return LEVEL256_CTRPF_STARTUP_WATCH_TARGET_COUNT;
}

/// Bound access logging to first and exponentially spaced observations per target.
constexpr bool ShouldLogLevel256CtrpfStartupWatch(u32 occurrence) {
    return occurrence == 1 || (occurrence <= LEVEL256_CTRPF_STARTUP_WATCH_LOG_LIMIT_PER_TARGET &&
                               (occurrence & (occurrence - 1)) == 0);
}

/// Decodes an ARM A32 B/BL instruction's destination using architectural PC = instruction + 8.
constexpr bool DecodeLevel256ArmBranchTarget(u32 instruction_address, u32 instruction,
                                             u32& target) {
    if ((instruction & 0x0E000000) != 0x0A000000 || (instruction >> 28) == 0xF) {
        return false;
    }
    const u32 immediate = instruction & 0x00FFFFFF;
    const s32 signed_immediate = (immediate & 0x00800000) != 0
                                     ? static_cast<s32>(immediate | 0xFF000000)
                                     : static_cast<s32>(immediate);
    target = instruction_address + 8 + static_cast<u32>(signed_immediate * 4);
    return true;
}
constexpr std::array<u32, 8> LEVEL256_OPEN_PROCESS_TRACE_TARGET_PCS = {
    0x07046A30, 0x07046A34, 0x07046A3C, 0x07046C10, 0x07046C14, 0x07046C34, 0x07046C70, 0x07046C7C};

struct Level256CtrpfOSDScanResult {
    u32 text_base = 0;
    u32 text_size = 0;
    u32 pattern0_offset = LEVEL256_CTRPF_OSD_NOT_FOUND;
    u32 pattern0_fallback_offset = LEVEL256_CTRPF_OSD_NOT_FOUND;
    u32 pattern3_offset = LEVEL256_CTRPF_OSD_NOT_FOUND;
    u32 pattern4_offset = LEVEL256_CTRPF_OSD_NOT_FOUND;
    u32 candidate1 = LEVEL256_CTRPF_OSD_NOT_FOUND;
    u32 candidate2 = LEVEL256_CTRPF_OSD_NOT_FOUND;
    u32 candidate3 = LEVEL256_CTRPF_OSD_NOT_FOUND;
    u32 selected_candidate = LEVEL256_CTRPF_OSD_NOT_FOUND;
};

/// Finds the first byte-exact CTRPF signature. Pattern words are encoded as little-endian ARM
/// instructions, matching the 3DS ARM code bytes searched by CTRPF's memsearch implementation.
constexpr u32 FindLevel256CtrpfPattern(const u8* text, std::size_t text_size, const u32* pattern,
                                       std::size_t word_count) {
    if (text == nullptr || pattern == nullptr || word_count == 0 ||
        word_count > text_size / sizeof(u32)) {
        return LEVEL256_CTRPF_OSD_NOT_FOUND;
    }
    const std::size_t pattern_size = word_count * sizeof(u32);
    for (std::size_t offset = 0; offset <= text_size - pattern_size; ++offset) {
        bool matches = true;
        for (std::size_t i = 0; i < pattern_size; ++i) {
            const u8 expected = static_cast<u8>(pattern[i / 4] >> ((i % 4) * 8));
            if (text[offset + i] != expected) {
                matches = false;
                break;
            }
        }
        if (matches && offset <= UINT32_MAX) {
            return static_cast<u32>(offset);
        }
    }
    return LEVEL256_CTRPF_OSD_NOT_FOUND;
}

/// Matches CTRPF SearchStmfd's backward, word-aligned scan from a signature result.
constexpr u32 SearchLevel256CtrpfStmfd(const u8* text, std::size_t text_size, u32 text_base,
                                       u32 pattern_offset, u32 prologue) {
    if (text == nullptr || pattern_offset == LEVEL256_CTRPF_OSD_NOT_FOUND ||
        pattern_offset > text_size || text_size - pattern_offset < sizeof(u32) ||
        text_base > UINT32_MAX - pattern_offset) {
        return LEVEL256_CTRPF_OSD_NOT_FOUND;
    }
    const u32 lower = pattern_offset > LEVEL256_CTRPF_OSD_STMFD_BACKSCAN_BYTES
                          ? pattern_offset - LEVEL256_CTRPF_OSD_STMFD_BACKSCAN_BYTES
                          : 0;
    u32 offset = pattern_offset;
    while (offset > lower) {
        if (offset <= text_size - sizeof(u32)) {
            const u32 instruction = static_cast<u32>(text[offset]) |
                                    (static_cast<u32>(text[offset + 1]) << 8) |
                                    (static_cast<u32>(text[offset + 2]) << 16) |
                                    (static_cast<u32>(text[offset + 3]) << 24);
            if (instruction == prologue && text_base <= UINT32_MAX - offset) {
                return text_base + offset;
            }
        }
        if (offset < 4) {
            break;
        }
        offset -= 4;
    }
    return LEVEL256_CTRPF_OSD_NOT_FOUND;
}

constexpr Level256CtrpfOSDScanResult ScanLevel256CtrpfOSDPatterns(const u8* text,
                                                                  std::size_t text_size,
                                                                  u32 text_base) {
    // Exact instruction signatures from CTRPluginFramework/OSDImpl.cpp g_OSDPattern.
    constexpr std::array<u32, 22> pattern = {
        0xE1833000, 0xE2044CFF, 0xE3C33CFF, 0xE1833004, 0xE1824F93, 0xE8830E60,
        0xEE078F9A, 0xE3A03001, 0xE7902104, 0xEE076F9A, 0xE3A02001, 0xE7901104,
        0xE1911F9F, 0xE3C110FF, 0xE3A00000, 0xEE070F9A, 0xE3A00001, 0xE7951104,
        0xE3A00000, 0xEE070F9A, 0xE2850001, 0xEA000004};
    constexpr u32 stmfd1 = 0xE92D5FF0;
    constexpr u32 stmfd2 = 0xE92D47F0;
    constexpr u32 stmfd3 = 0xE92D4070;

    Level256CtrpfOSDScanResult result{};
    result.text_base = text_base;
    result.text_size = text_size > UINT32_MAX ? UINT32_MAX : static_cast<u32>(text_size);
    if (text == nullptr || text_size > LEVEL256_CTRPF_OSD_MAX_SCAN_BYTES ||
        text_base > UINT32_MAX - text_size) {
        return result;
    }
    result.pattern0_offset = FindLevel256CtrpfPattern(text, text_size, pattern.data(), 5);
    result.pattern0_fallback_offset =
        FindLevel256CtrpfPattern(text, text_size, pattern.data() + 5, 4);
    result.pattern3_offset = FindLevel256CtrpfPattern(text, text_size, pattern.data() + 9, 5);
    result.pattern4_offset = FindLevel256CtrpfPattern(text, text_size, pattern.data() + 14, 4);

    const u32 first = result.pattern0_offset != LEVEL256_CTRPF_OSD_NOT_FOUND
                          ? result.pattern0_offset
                          : result.pattern0_fallback_offset;
    result.candidate1 = SearchLevel256CtrpfStmfd(text, text_size, text_base, first, stmfd1);
    result.candidate2 =
        SearchLevel256CtrpfStmfd(text, text_size, text_base, result.pattern3_offset, stmfd2);
    result.candidate3 =
        SearchLevel256CtrpfStmfd(text, text_size, text_base, result.pattern4_offset, stmfd3);
    result.selected_candidate =
        result.candidate1 != LEVEL256_CTRPF_OSD_NOT_FOUND   ? result.candidate1
        : result.candidate2 != LEVEL256_CTRPF_OSD_NOT_FOUND ? result.candidate2
                                                            : result.candidate3;
    return result;
}

constexpr bool ShouldLogLevel256ArbiterDiagnostic(bool official_plugin, u32 recorded_calls) {
    return official_plugin && recorded_calls < LEVEL256_CTRPF_ARBITER_DIAGNOSTIC_LIMIT;
}

struct Level256ArmLoadAddress {
    bool valid{};
    u32 rn{};
    u32 rd{};
    u32 offset{};
    u32 base{};
    s64 effective_address{};
};

struct Level256ArmRegisterValue {
    bool valid{};
    u32 value{};
};

/// Read an architectural ARM-state operand without indexing outside the register snapshot.
constexpr Level256ArmRegisterValue ReadLevel256ArmRegister(u32 register_number, u32 pc,
                                                           const std::array<u32, 16>& registers) {
    if (register_number >= registers.size()) {
        return {};
    }
    return {true, register_number == 15 ? pc + 8 : registers[register_number]};
}

/// Decode an ARM-state immediate, pre-indexed LDR address for diagnostics.
/// R15 reads as the instruction address plus eight; all other registers use the
/// captured architectural r0-r15 state (including SP at r13 and LR at r14).
constexpr Level256ArmLoadAddress DecodeLevel256ArmLdrAddress(u32 instruction, u32 pc,
                                                             const std::array<u32, 16>& registers) {
    Level256ArmLoadAddress result{};
    if ((instruction & 0x0C100000) != 0x04100000 || (instruction & (1U << 25)) != 0 ||
        (instruction & (1U << 24)) == 0) {
        return result;
    }

    result.rn = (instruction >> 16) & 0xF;
    result.rd = (instruction >> 12) & 0xF;
    result.offset = instruction & 0xFFF;
    const auto base = ReadLevel256ArmRegister(result.rn, pc, registers);
    if (!base.valid) {
        return result;
    }
    result.base = base.value;
    const bool add = (instruction & (1U << 23)) != 0;
    result.effective_address =
        static_cast<s64>(result.base) +
        (add ? static_cast<s64>(result.offset) : -static_cast<s64>(result.offset));
    result.valid = result.effective_address >= 0 && result.effective_address <= UINT32_MAX;
    return result;
}

constexpr bool IsLevel256OpenProcessTracePc(u32 pc) {
    for (const u32 target : LEVEL256_OPEN_PROCESS_TRACE_TARGET_PCS) {
        if (pc >= target - 8 && pc <= target + 8) {
            return true;
        }
    }
    return false;
}

constexpr bool IsLevel256OpenProcessCriticalPc(u32 pc) {
    for (const u32 target : LEVEL256_OPEN_PROCESS_TRACE_TARGET_PCS) {
        if (pc == target) {
            return true;
        }
    }
    return pc == 0x07046C7C || pc == 0x070A131C;
}

constexpr bool IsLevel256OpenProcessTraceContext(u32 process_id, u32 target_process_id,
                                                 u32 thread_id, u32 target_thread_id) {
    return process_id == target_process_id && thread_id == target_thread_id;
}

constexpr bool ShouldLogLevel256OpenProcessTrace(bool focus_mode, u32 pc, u32 critical_records,
                                                 u32 context_records) {
    if (!focus_mode) {
        return true;
    }
    if (IsLevel256OpenProcessCriticalPc(pc)) {
        return critical_records < LEVEL256_OPEN_PROCESS_TRACE_CRITICAL_LOG_LIMIT;
    }
    return context_records < LEVEL256_OPEN_PROCESS_TRACE_CONTEXT_LOG_LIMIT &&
           IsLevel256OpenProcessTracePc(pc);
}

/// Official Level256.3gx SHA256. This gates only the narrowly scoped HLE compatibility handler.
constexpr std::array<u8, 32> LEVEL256_PLUGIN_SHA256 = {
    0x85, 0xE2, 0x3D, 0x71, 0xC7, 0x68, 0x69, 0xFB, 0x2B, 0xB3, 0xB1, 0xBE, 0x50, 0xB2, 0x11, 0x2E,
    0x06, 0x08, 0x02, 0x33, 0x01, 0xA0, 0xF5, 0xEA, 0xEF, 0xE3, 0x49, 0x7E, 0xD2, 0xD9, 0x65, 0xAC};

constexpr bool ShouldApplyLevel256BackdoorPatch(bool enabled, bool official_plugin_hash_matches,
                                                u64 title_id, u32 pid, u32 plugin_pid,
                                                bool plugin_loaded, u32 code_begin, u32 code_end,
                                                u32 svc_pc, u32 svc_id, u32 callback, u32 arg0,
                                                u32 arg1, u32 arg2, u32 current_instruction) {
    return enabled && official_plugin_hash_matches && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, svc_pc) &&
           svc_id == LEVEL256_CUSTOM_BACKDOOR_SVC_ID &&
           callback == LEVEL256_CUSTOM_BACKDOOR_CALLBACK &&
           arg0 == LEVEL256_CUSTOM_BACKDOOR_CALLBACK_ARGUMENT && arg1 == 0 && arg2 == 1 &&
           current_instruction == LEVEL256_CUSTOM_BACKDOOR_PATCH_BEFORE;
}

constexpr bool ApplyLevel256BackdoorPatch(bool matched, u32& instruction, u32& callback_return) {
    if (!matched || instruction != LEVEL256_CUSTOM_BACKDOOR_PATCH_BEFORE) {
        return false;
    }
    instruction = LEVEL256_CUSTOM_BACKDOOR_PATCH_AFTER;
    callback_return = LEVEL256_CUSTOM_BACKDOOR_PATCH_RETURN;
    return true;
}

constexpr bool ShouldSuppressDuplicateLevel256SVCError(bool enabled, bool plugin_identity_verified,
                                                       u64 title_id, u32 pid, u32 plugin_pid,
                                                       bool plugin_loaded, u32 code_begin,
                                                       u32 code_end, u32 svc_pc, u32 svc_id,
                                                       u32 raw_result) {
    return enabled && plugin_identity_verified && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, svc_pc) &&
           svc_id == LEVEL256_QUERY_PROCESS_MEMORY_SVC_ID &&
           raw_result == LEVEL256_QUERY_PROCESS_MEMORY_FAILURE;
}

/// OpenProcess returns its output handle in R1 even when it fails; keep that value deterministic.
constexpr void ClearOpenProcessOutput(u32& output_handle) {
    output_handle = 0;
}

/// Track Level256 QueryProcessMemory failures and log at the first and powers-of-two counts.
constexpr bool RecordLevel256QueryProcessMemoryFailure(u32& failure_count) {
    if (failure_count != UINT32_MAX) {
        ++failure_count;
    }
    return failure_count == 1 ||
           (failure_count != UINT32_MAX && (failure_count & (failure_count - 1)) == 0);
}

/// Callback 0x07046440's verified ARM semantics: clear old bits 8..11, OR in r1, return old bits.
constexpr u32 ApplyLevel256FieldUpdate(u32 old_value, u32 replacement) {
    return (old_value & ~0xF00U) | replacement;
}

constexpr u32 GetLevel256FieldUpdateResult(u32 old_value) {
    return old_value & 0xF00U;
}

constexpr bool ShouldHandleLevel256ViewFlagCallback(Level256BackdoorCallbackKind callback,
                                                    u32 callback_address, u32 arg1, u32 arg2,
                                                    u32 old_value) {
    if (callback == Level256BackdoorCallbackKind::ViewFlag) {
        // The verified 0x07046430 callback only reads [r0+0xB0], ORs 0x2000, and
        // writes it back. Its remaining callback arguments are not consumed. Keep the
        // observed arg2 shape, but don't reject a valid view because arg1 is nonzero.
        return arg2 == 1;
    }
    if (callback != Level256BackdoorCallbackKind::ViewField ||
        arg2 != LEVEL256_BACKDOOR_VIEW_FLAG_OFFSET) {
        return false;
    }
    return (arg1 == 0x300 && old_value == LEVEL256_BACKDOOR_VIEW_FLAG_BIT) ||
           (arg1 == callback_address && old_value == 0x2300) || (arg1 == 0 && old_value == 0x2300);
}

constexpr u32 ApplyLevel256ViewFlagCallback(Level256BackdoorCallbackKind callback, u32 old_value,
                                            u32 replacement) {
    if (callback == Level256BackdoorCallbackKind::ViewFlag) {
        return old_value | LEVEL256_BACKDOOR_VIEW_FLAG_BIT;
    }
    return ApplyLevel256FieldUpdate(old_value, replacement);
}

struct Level256CodeSetViewInfo {
    u32 text_address = 0;
    u32 text_size = 0;
    u32 rodata_address = 0;
    u32 rodata_size = 0;
    u32 data_address = 0;
    u32 data_size = 0;
    u64 program_id = 0;
};

constexpr u32 Level256PageCount(u32 size) {
    return static_cast<u32>((static_cast<u64>(size) + 0xFFF) >> 12);
}

/// Model the KProcess -> KResourceLimit/KCodeSet fields read by the official plugin. The embedded
/// objects stay inside the same guest mapping so the real backdoor memcpy can use their pointers.
constexpr void BuildLevel256ProcessView(
    std::array<u32, LEVEL256_PROCESS_VIEW_SIZE / sizeof(u32)>& view, u32 guest_base, u32 process_id,
    u32 priority_limit, u32 thread_limit, const Level256CodeSetViewInfo& code,
    bool is_n3ds = true) {
    const auto built = Service::PLGLDR::Level256::KProcessViewV1::Build(
        guest_base, process_id, priority_limit, thread_limit,
        {.text_address = code.text_address,
         .text_size = code.text_size,
         .rodata_address = code.rodata_address,
         .rodata_size = code.rodata_size,
         .data_address = code.data_address,
         .data_size = code.data_size,
         .program_id = code.program_id},
        is_n3ds);
    view = built;
}

/// Finds an aligned allocation-sized window in the intersection of a free VMA and a target range.
/// Returns zero when there is no complete window. Addresses are guest addresses, never host
/// pointers.
constexpr u32 FindLevel256GuestRangeCandidate(u64 vma_base, u64 vma_size, u32 range_begin,
                                              u32 range_end, u32 allocation_size, u32 alignment) {
    constexpr u64 address_space_end = static_cast<u64>(UINT32_MAX) + 1;
    if (vma_base > address_space_end || vma_size > address_space_end - vma_base ||
        range_end <= range_begin || allocation_size == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return 0;
    }

    const u64 vma_end = vma_base + vma_size;
    const u64 intersection_begin = std::max<u64>(vma_base, range_begin);
    const u64 intersection_end = std::min<u64>(vma_end, range_end);
    if (intersection_begin >= intersection_end ||
        intersection_begin > address_space_end - (alignment - 1)) {
        return 0;
    }

    const u64 candidate = (intersection_begin + alignment - 1) & ~(u64{alignment} - 1);
    if (candidate > UINT32_MAX || candidate > intersection_end ||
        allocation_size > intersection_end - candidate) {
        return 0;
    }
    return static_cast<u32>(candidate);
}

/// Finds the highest page-aligned window in a free VMA intersection.
constexpr u32 FindLevel256GuestRangeCandidateFromHigh(u64 vma_base, u64 vma_size, u32 range_begin,
                                                      u32 range_end, u32 allocation_size,
                                                      u32 alignment) {
    constexpr u64 address_space_end = static_cast<u64>(UINT32_MAX) + 1;
    if (vma_base > address_space_end || vma_size > address_space_end - vma_base ||
        range_end <= range_begin || allocation_size == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        return 0;
    }

    const u64 intersection_begin = std::max<u64>(vma_base, range_begin);
    const u64 intersection_end = std::min<u64>(vma_base + vma_size, range_end);
    if (intersection_begin >= intersection_end ||
        allocation_size > intersection_end - intersection_begin) {
        return 0;
    }

    const u64 candidate = (intersection_end - allocation_size) & ~(u64{alignment} - 1);
    if (candidate < intersection_begin || candidate > UINT32_MAX) {
        return 0;
    }
    return static_cast<u32>(candidate);
}

/// Applies the CustomBackdoor callback result to the emulated ARM return register.
constexpr bool ApplyLevel256BackdoorSVCResult(bool handled, u32 result, u32& guest_r0) {
    if (!handled) {
        return false;
    }
    guest_r0 = result;
    return true;
}

constexpr bool ShouldHandleLevel256CurrentProcessCallback(
    bool enabled, bool official_plugin_hash_matches, u64 title_id, u32 pid, u32 plugin_pid,
    bool plugin_loaded, u32 code_begin, u32 code_end, u32 svc_pc, u32 svc_id,
    bool callback_contract_verified, u32 arg0, u32 arg1, u32 arg2) {
    return enabled && official_plugin_hash_matches && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, svc_pc) &&
           svc_id == LEVEL256_CUSTOM_BACKDOOR_SVC_ID && callback_contract_verified && arg0 == 0 &&
           arg1 == 0 && arg2 == 0;
}

constexpr bool ShouldHandleLevel256ResourceLimitCallback(
    bool enabled, bool official_plugin_hash_matches, u64 title_id, u32 pid, u32 plugin_pid,
    bool plugin_loaded, u32 code_begin, u32 code_end, u32 svc_pc, u32 svc_id,
    bool callback_contract_verified, u32 view_address, u32 arg0, u32 arg1, u32 arg2,
    u32 expected_resource_limit_offset = LEVEL256_PROCESS_VIEW_N3DS_RESOURCE_LIMIT_OFFSET) {
    return enabled && official_plugin_hash_matches && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, svc_pc) &&
           svc_id == LEVEL256_CUSTOM_BACKDOOR_SVC_ID && callback_contract_verified &&
           arg0 == view_address && arg1 == expected_resource_limit_offset && arg2 == 1;
}

constexpr bool ShouldApplyLevel256PriorityLimitCallback(bool callback_code_matches, bool view_valid,
                                                        u32 guest_limit, u32 actual_limit,
                                                        u32 requested_limit, u32 arg2,
                                                        u32 expected_resource_limit_offset,
                                                        u32& updated_limit, u32& callback_return) {
    if (!callback_code_matches || !view_valid || arg2 != expected_resource_limit_offset ||
        guest_limit != actual_limit) {
        return false;
    }
    updated_limit = requested_limit;
    callback_return = actual_limit;
    return true;
}

constexpr bool CanCreateThreadWithPriority(u32 max_priority, u32 requested_priority,
                                           bool no_thread_restrictions) {
    return max_priority <= requested_priority || no_thread_restrictions;
}

constexpr bool ConsumeLevel256ThreadCreateLogSlot(bool authenticated_supported_plugin,
                                                  u32& log_count) {
    if (!authenticated_supported_plugin || log_count >= LEVEL256_THREAD_CREATE_LOG_LIMIT) {
        return false;
    }
    ++log_count;
    return true;
}

constexpr bool ShouldHandleLevel256ProcessSnapshotCopy(
    bool enabled, bool official_plugin_hash_matches, u64 title_id, u32 pid, u32 plugin_pid,
    bool plugin_loaded, u32 code_begin, u32 code_end, u32 svc_pc, u32 svc_id,
    bool callback_contract_verified, u32 destination, u32 source, u32 view_address, u32 size,
    bool destination_in_plugin_data) {
    return enabled && official_plugin_hash_matches && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, svc_pc) &&
           svc_id == LEVEL256_CUSTOM_BACKDOOR_SVC_ID && callback_contract_verified &&
           destination != 0 && destination_in_plugin_data &&
           ((source == view_address && size == LEVEL256_PROCESS_VIEW_COPY_SIZE) ||
            (source == view_address + LEVEL256_PROCESS_VIEW_CODESET_OFFSET &&
             size == LEVEL256_CODESET_SNAPSHOT_SIZE));
}

constexpr bool IsLevel256PluginDataRange(u32 address, u32 size, u32 data_begin, u32 data_size) {
    const u64 end = static_cast<u64>(address) + size;
    const u64 data_end = static_cast<u64>(data_begin) + data_size;
    return size != 0 && data_size != 0 && end <= static_cast<u64>(UINT32_MAX) + 1 &&
           data_end <= static_cast<u64>(UINT32_MAX) + 1 && address >= data_begin && end <= data_end;
}

/// Matches Luma3DS-Level256's exact emulator-information hook for an authenticated, supported
/// Level256 title. This does not change Azahar's normal emulator ID response.
constexpr bool ShouldOverrideLevel256GetSystemInfo(bool enabled, bool supported_title,
                                                   bool official_plugin_hash_matches, u32 pid,
                                                   u32 plugin_pid, bool plugin_loaded,
                                                   u32 code_begin, u32 code_end, u32 pc, u32 svc_id,
                                                   u32 type, s32 param) {
    return enabled && supported_title && official_plugin_hash_matches && pid == plugin_pid &&
           plugin_loaded && IsLevel256PluginCodeAddress(code_begin, code_end, pc) &&
           svc_id == LEVEL256_DIAGNOSTIC_SVC_ID && type == LEVEL256_DIAGNOSTIC_TYPE &&
           param == LEVEL256_DIAGNOSTIC_PARAM;
}

constexpr bool ApplyLevel256GetSystemInfoResult(bool matched, u32& result, u64& output) {
    if (!matched) {
        return false;
    }
    result = LEVEL256_LUMA_MARKER_RESULT;
    output = 0;
    return true;
}

/// Matches Luma3DS-Level256's type 0x30000 verification extension only for the hash-verified
/// plugin in its owning title process and mapped plugin code.
constexpr bool ShouldProvideLevel256VerificationResponse(
    bool enabled, bool official_plugin_hash_matches, u64 title_id, u32 pid, u32 plugin_pid,
    bool plugin_loaded, u32 code_begin, u32 code_end, u32 pc, u32 svc_id, u32 type, s32 param) {
    return enabled && official_plugin_hash_matches && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, pc) &&
           svc_id == LEVEL256_DIAGNOSTIC_SVC_ID && type == LEVEL256_VERIFICATION_TYPE &&
           param == LEVEL256_VERIFICATION_PARAM;
}

/// Mirrors the official Luma3DS-Level256 GetSystemInfoHook response for type 0x30000.
constexpr bool ApplyLevel256VerificationResponse(bool matched, u32& result, u64& output) {
    if (!matched) {
        return false;
    }
    result = LEVEL256_VERIFICATION_RESULT;
    output = LEVEL256_VERIFICATION_OUTPUT;
    return true;
}

/// Arms one transient ARM trace for a plugin-created thread whose entry lies in the active mapping.
constexpr bool ShouldStartLevel256OpenProcessTrace(bool enabled, bool plugin_identity_verified,
                                                   u64 title_id, u32 pid, u32 plugin_pid,
                                                   bool plugin_loaded, u32 code_begin, u32 code_end,
                                                   u32 entry_point, bool already_started) {
    return enabled && plugin_identity_verified && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, entry_point) && !already_started;
}

struct Level256SnapshotReadLocation {
    u64 read_address;
    u64 snapshot_base;
    u64 snapshot_offset;
    bool valid;
};

/// Computes addresses from the caller's live SP at the LDR, never from the later SVC-entry SP.
constexpr Level256SnapshotReadLocation GetLevel256SnapshotReadLocation(u32 caller_sp,
                                                                       u32 ldr_offset) {
    const u64 snapshot_base = static_cast<u64>(caller_sp) + 0x10;
    const u64 read_address = static_cast<u64>(caller_sp) + ldr_offset;
    const bool valid = ldr_offset >= 0x10 && read_address <= static_cast<u64>(UINT32_MAX) &&
                       snapshot_base <= static_cast<u64>(UINT32_MAX);
    return {read_address, snapshot_base,
            ldr_offset >= 0x10 ? static_cast<u64>(ldr_offset - 0x10) : 0, valid};
}

} // namespace Kernel
