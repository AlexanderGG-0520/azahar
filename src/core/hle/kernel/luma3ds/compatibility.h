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

    constexpr bool Authorizes(u32 process_id, u32 pc) const {
        const bool in_plugin =
            plugin_code_begin < plugin_code_end && pc >= plugin_code_begin && pc < plugin_code_end;
        const bool in_bootloader = bootloader_code_begin < bootloader_code_end &&
                                   pc >= bootloader_code_begin && pc < bootloader_code_end;
        return plugin_loaded && plugin_integrity_verified && owner_process_id != 0 &&
               process_id == owner_process_id && (in_plugin || in_bootloader);
    }
};

struct SvcRequest {
    u32 svc_id{};
    u32 process_id{};
    u32 entry_pc{};
    std::array<u32, 8> arguments{};
};

enum class SvcDispatchKind {
    NotExtension,
    Rejected,
    DelegateToRegisteredHandler,
    Handled,
};

struct SvcDispatchOutcome {
    SvcDispatchKind kind{SvcDispatchKind::NotExtension};
    u32 result{};
};

struct SystemInfoResponse {
    u32 result;
    u64 value;
};

/// Whether an extension SVC may be dispatched for this caller. SVC semantics are title agnostic;
/// authority comes from the verified plugin mapping and the compatibility-mode setting.
constexpr bool CanDispatchExtension(const CompatibilityContext& context,
                                    const PluginAuthorization& authorization, u32 process_id,
                                    u32 pc) {
    return context.enabled && authorization.Authorizes(process_id, pc);
}

SvcDispatchOutcome DispatchExtensionSvc(const SvcRequest& request,
                                        const CompatibilityContext& context,
                                        const PluginAuthorization& authorization);

constexpr bool IsExtensionSVC(u32 svc_id) {
    return svc_id == 0x80 || (svc_id >= 0x90 && svc_id <= 0x94) ||
           (svc_id >= 0xA0 && svc_id <= 0xA3);
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
           CanDispatchExtension(context, authorization, process_id, callback) &&
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
