// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "core/hle/kernel/luma3ds/compatibility.h"

namespace Kernel::Luma3DS {

SvcDispatchOutcome DispatchExtensionSvc(const SvcRequest& request,
                                        const CompatibilityContext& context,
                                        const PluginAuthorization& authorization) {
    if (!IsExtensionSVC(request.svc_id)) {
        return {.kind = SvcDispatchKind::NotExtension};
    }
    const auto capability = ExtensionSvcCapability(request.svc_id);
    if (static_cast<u8>(capability) == 0xFF) {
        return {.kind = SvcDispatchKind::Rejected,
                .reject_reason = SvcDispatchRejectReason::UnsupportedCapability};
    }
    if (!context.enabled) {
        return {.kind = SvcDispatchKind::Rejected,
                .reject_reason = SvcDispatchRejectReason::CompatibilityDisabled};
    }
    if (!authorization.OwnsCode(request.process_id, request.entry_pc)) {
        return {.kind = SvcDispatchKind::Rejected,
                .reject_reason = SvcDispatchRejectReason::PluginScopeRejected};
    }
    if (!HasCapability(authorization.capabilities, capability)) {
        return {.kind = SvcDispatchKind::Rejected,
                .reject_reason = SvcDispatchRejectReason::UnsupportedCapability};
    }
    return {.kind = SvcDispatchKind::DelegateToRegisteredHandler};
}

} // namespace Kernel::Luma3DS
