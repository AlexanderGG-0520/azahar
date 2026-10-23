// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include "core/hle/kernel/luma3ds/compatibility.h"

namespace Kernel::Luma3DS {

/// Level256's policy data is kept separate from the general Luma extension context. PLGLDR
/// supplies these facts; the kernel SVC path remains responsible for writing the SVC result.
struct Level256VerificationPolicy {
    bool enabled{};
    bool supported_title{};
    Level256CapabilityMask capabilities{};
    bool plugin_loaded{};
    u32 owner_process_id{};
    u32 plugin_code_begin{};
    u32 plugin_code_end{};
};

constexpr bool TryGetLevel256VerificationResponse(const Level256VerificationPolicy& policy,
                                                  u32 process_id, u32 pc, u32 svc_id, u32 type,
                                                  s32 param, SystemInfoResponse& response) {
    const bool caller_is_plugin = policy.plugin_code_begin < policy.plugin_code_end &&
                                  pc >= policy.plugin_code_begin && pc < policy.plugin_code_end;
    if (!policy.enabled || !policy.supported_title ||
        !HasCapability(policy.capabilities, Level256Capability::SystemInfo) ||
        !policy.plugin_loaded || policy.owner_process_id == 0 ||
        process_id != policy.owner_process_id || !caller_is_plugin || svc_id != 0x2A ||
        type != 0x30000 || param != 0) {
        return false;
    }

    response = {.result = 0x9A7FC03E, .value = 0x12FAADCC};
    return true;
}

/// Select a verified compatibility response after the ordinary GetSystemInfo handler has run.
/// Level256's verification value remains supplied by its policy; the generic Luma marker only
/// requires an authorized extension caller.
constexpr bool TryGetSystemInfoCompatibilityResponse(
    const CompatibilityContext& context, const PluginAuthorization& authorization,
    const Level256VerificationPolicy& level256_policy, u32 process_id, u32 pc, u32 svc_id, u32 type,
    s32 param, SystemInfoResponse& response) {
    if (TryGetLevel256VerificationResponse(level256_policy, process_id, pc, svc_id, type, param,
                                           response)) {
        return true;
    }
    return svc_id == 0x2A &&
           CanDispatchExtension(context, authorization, process_id, pc,
                                Level256Capability::SystemInfo) &&
           TryGetLumaSystemInfoResponse(context.enabled, type, param, response);
}

} // namespace Kernel::Luma3DS
