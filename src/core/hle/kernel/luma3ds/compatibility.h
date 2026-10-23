// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include "common/common_types.h"

namespace Kernel::Luma3DS {

/// Luma environment state. This type has no plugin hash, title ID, or plugin identity policy.
struct CompatibilityContext {
    bool enabled{};
};

/// Independently gated HLE contracts implemented by this fork. The manifest's artifact version
/// is deliberately absent: publisher authenticity and an operation's ABI compatibility are
/// separate decisions.
enum class Level256Capability : u8 {
    SystemInfo,
    CustomBackdoor,
    ControlMemoryUnsafe,
    Level256LFCS,
    ConvertVaToPa,
    DataCacheMaintenance,
    InstructionCacheMaintenance,
    PluginUdsControlIPC,
    PluginUdsDataIPC,
    NdmControlIPC,
    SocAttachIPC,
    MapProcessMemoryEx,
    UnmapProcessMemoryEx,
};

using Level256CapabilityMask = u32;

constexpr Level256CapabilityMask CapabilityBit(Level256Capability capability) {
    const u8 bit = static_cast<u8>(capability);
    return bit < 32 ? 1U << bit : 0;
}

constexpr bool HasCapability(Level256CapabilityMask capabilities, Level256Capability capability) {
    const auto bit = CapabilityBit(capability);
    return bit != 0 && (capabilities & bit) != 0;
}

/// Capabilities whose individual ABI adapters have reviewed, operation-specific validation.
/// UnmapProcessMemoryEx remains omitted until its mapping ownership and range contract is narrow
/// enough for signed plugins. New extension IDs are not added here implicitly; unsupported entries
/// remain fail-closed.
constexpr Level256CapabilityMask CapabilitiesForVerifiedPlugin(bool identity_verified) {
    if (!identity_verified) {
        return 0;
    }
    return CapabilityBit(Level256Capability::SystemInfo) |
           CapabilityBit(Level256Capability::CustomBackdoor) |
           CapabilityBit(Level256Capability::ControlMemoryUnsafe) |
           CapabilityBit(Level256Capability::Level256LFCS) |
           CapabilityBit(Level256Capability::ConvertVaToPa) |
           CapabilityBit(Level256Capability::DataCacheMaintenance) |
           CapabilityBit(Level256Capability::InstructionCacheMaintenance) |
           CapabilityBit(Level256Capability::PluginUdsControlIPC) |
           CapabilityBit(Level256Capability::PluginUdsDataIPC) |
           CapabilityBit(Level256Capability::NdmControlIPC) |
           CapabilityBit(Level256Capability::SocAttachIPC) |
           CapabilityBit(Level256Capability::MapProcessMemoryEx);
}

/// Preserve the previously shipped exact-digest profile for existing users. This exception does
/// not apply to a newly signed artifact: its cross-process mapping ABI has not been proven safe.
constexpr Level256CapabilityMask CapabilitiesForLevel256Plugin(bool identity_verified,
                                                               bool legacy_hash_verified) {
    auto capabilities = CapabilitiesForVerifiedPlugin(identity_verified);
    if (identity_verified && legacy_hash_verified) {
        capabilities |= CapabilityBit(Level256Capability::MapProcessMemoryEx) |
                        CapabilityBit(Level256Capability::UnmapProcessMemoryEx);
    }
    return capabilities;
}

/// Generic capability for a plugin mapping that passed its own loader policy. Level256 may be the
/// current provider, but its title and digest rules are not part of common Luma SVC semantics.
struct PluginAuthorization {
    bool plugin_loaded{};
    bool plugin_integrity_verified{};
    u32 owner_process_id{};
    u32 plugin_code_begin{};
    u32 plugin_code_end{};
    u32 bootloader_code_begin{};
    u32 bootloader_code_end{};
    Level256CapabilityMask capabilities{};

    constexpr bool OwnsCode(u32 process_id, u32 pc) const {
        const bool in_plugin =
            plugin_code_begin < plugin_code_end && pc >= plugin_code_begin && pc < plugin_code_end;
        const bool in_bootloader = bootloader_code_begin < bootloader_code_end &&
                                   pc >= bootloader_code_begin && pc < bootloader_code_end;
        return plugin_loaded && plugin_integrity_verified && owner_process_id != 0 &&
               process_id == owner_process_id && (in_plugin || in_bootloader);
    }

    constexpr bool Authorizes(u32 process_id, u32 pc, Level256Capability capability) const {
        return OwnsCode(process_id, pc) && HasCapability(capabilities, capability);
    }
};

struct SvcRequest {
    u32 svc_id{};
    u32 process_id{};
    u32 entry_pc{};
    std::array<u32, 8> arguments{};
};

constexpr u32 MAP_PROCESS_MEMORY_EX_ABI_V2_MAGIC = 0xFFFFFFF2;
constexpr u32 MAP_PROCESS_MEMORY_EX_FLAG_PRIVATE = 1;

struct MapProcessMemoryExRequest {
    bool abi_v2{};
    u32 destination_process_handle{};
    u32 destination_address{};
    u32 source_process_handle{};
    u32 source_address{};
    u32 size{};
    u32 flags{};
};

/// Decode the public Level256/Luma wrapper ABI. ABI v1 ignores r5; v2 places the destination
/// handle in r6, flags in r5, and the marker in r0.
constexpr MapProcessMemoryExRequest DecodeMapProcessMemoryExRequest(
    const std::array<u32, 8>& registers) {
    const bool abi_v2 = registers[0] == MAP_PROCESS_MEMORY_EX_ABI_V2_MAGIC;
    return {
        .abi_v2 = abi_v2,
        .destination_process_handle = abi_v2 ? registers[6] : registers[0],
        .destination_address = registers[1],
        .source_process_handle = registers[2],
        .source_address = registers[3],
        .size = registers[4],
        .flags = abi_v2 ? registers[5] : 0,
    };
}

constexpr bool IsValidMapProcessMemoryExFlags(u32 flags) {
    return (flags & ~MAP_PROCESS_MEMORY_EX_FLAG_PRIVATE) == 0;
}

enum class SvcDispatchKind {
    NotExtension,
    Rejected,
    DelegateToRegisteredHandler,
    Handled,
};

enum class SvcDispatchRejectReason {
    None,
    CompatibilityDisabled,
    PluginScopeRejected,
    UnsupportedCapability,
};

struct SvcDispatchOutcome {
    SvcDispatchKind kind{SvcDispatchKind::NotExtension};
    u32 result{};
    SvcDispatchRejectReason reject_reason{SvcDispatchRejectReason::None};
};

struct SystemInfoResponse {
    u32 result;
    u64 value;
};

/// Whether an extension SVC may be dispatched for this caller. SVC semantics are title agnostic;
/// authority comes from the verified plugin mapping and the compatibility-mode setting.
constexpr bool CanDispatchExtension(const CompatibilityContext& context,
                                    const PluginAuthorization& authorization, u32 process_id,
                                    u32 pc, Level256Capability capability) {
    return context.enabled && authorization.Authorizes(process_id, pc, capability);
}

/// IPC adapters are selected by service/command and keep their own command-shape validation.
constexpr bool CanDispatchLevel256IPC(const CompatibilityContext& context,
                                      const PluginAuthorization& authorization, u32 process_id,
                                      u32 pc, Level256Capability capability) {
    return context.enabled && authorization.Authorizes(process_id, pc, capability);
}

/// Luma's SendSyncRequest hook applies UDS/NDM/SOC rewrites to the whole process after the
/// verified plugin initializes the PLGUDS redirect. The originating game PC is not necessarily
/// in the plugin mapping, so routed calls use the established process redirect as their scope.
constexpr bool CanDispatchLevel256RoutedIPC(const CompatibilityContext& context,
                                            const PluginAuthorization& authorization,
                                            u32 process_id, bool redirect_active,
                                            Level256Capability capability) {
    return context.enabled && redirect_active && authorization.plugin_loaded &&
           authorization.plugin_integrity_verified && authorization.owner_process_id != 0 &&
           authorization.plugin_code_begin < authorization.plugin_code_end &&
           authorization.owner_process_id == process_id &&
           HasCapability(authorization.capabilities, capability);
}

SvcDispatchOutcome DispatchExtensionSvc(const SvcRequest& request,
                                        const CompatibilityContext& context,
                                        const PluginAuthorization& authorization);

constexpr const char* SvcDispatchRejectReasonName(SvcDispatchRejectReason reason) {
    switch (reason) {
    case SvcDispatchRejectReason::CompatibilityDisabled:
        return "compatibility_disabled";
    case SvcDispatchRejectReason::PluginScopeRejected:
        return "plugin_scope_rejected";
    case SvcDispatchRejectReason::UnsupportedCapability:
        return "unsupported_capability";
    case SvcDispatchRejectReason::None:
    default:
        return "none";
    }
}

constexpr bool IsExtensionSVC(u32 svc_id) {
    return svc_id == 0x80 || (svc_id >= 0x90 && svc_id <= 0x94) ||
           (svc_id >= 0xA0 && svc_id <= 0xA3);
}

constexpr Level256Capability ExtensionSvcCapability(u32 svc_id) {
    switch (svc_id) {
    case 0x80:
        return Level256Capability::CustomBackdoor;
    case 0x90:
        return Level256Capability::ConvertVaToPa;
    case 0x91:
    case 0x92:
        return Level256Capability::DataCacheMaintenance;
    case 0x93:
    case 0x94:
        return Level256Capability::InstructionCacheMaintenance;
    case 0xA0:
        return Level256Capability::MapProcessMemoryEx;
    case 0xA1:
        return Level256Capability::UnmapProcessMemoryEx;
    case 0xA3:
        return Level256Capability::ControlMemoryUnsafe;
    default:
        // Includes unknown extension IDs and the currently unimplemented ControlMemoryEx (A2).
        return static_cast<Level256Capability>(0xFF);
    }
}

/// Luma models the data cache as coherent with the emulated memory system. These operations are
/// therefore successful no-ops; instruction-cache operations still invalidate Dynarmic blocks.
constexpr bool IsDataCacheOperation(u32 svc_id) {
    return svc_id == 0x91 || svc_id == 0x92;
}

constexpr bool IsInstructionCacheOperation(u32 svc_id) {
    return svc_id == 0x93 || svc_id == 0x94;
}

/// Validate the CustomBackdoor ABI's guest callback address before a plugin-specific adapter
/// chooses a supported operation. This does not permit arbitrary guest code execution by HLE.
constexpr bool IsValidCustomBackdoorCallback(const CompatibilityContext& context,
                                             const PluginAuthorization& authorization,
                                             u32 process_id, u32 callback) {
    return (callback & 3) == 0 &&
           CanDispatchExtension(context, authorization, process_id, callback,
                                Level256Capability::CustomBackdoor) &&
           authorization.plugin_code_begin < authorization.plugin_code_end &&
           callback >= authorization.plugin_code_begin && callback < authorization.plugin_code_end;
}

/// Luma's GetSystemInfo hook reports its kernel extension marker without changing emulator IDs.
constexpr bool TryGetLumaSystemInfoResponse(bool enabled, u32 type, s32 param,
                                            SystemInfoResponse& response) {
    if (!enabled || type != 0x20000 || param != 0) {
        return false;
    }
    response = {.result = 1, .value = 0};
    return true;
}

} // namespace Kernel::Luma3DS
