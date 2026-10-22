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
    if (!CanDispatchExtension(context, authorization, request.process_id, request.entry_pc)) {
        return {.kind = SvcDispatchKind::Rejected};
    }
    return {.kind = SvcDispatchKind::DelegateToRegisteredHandler};
}

} // namespace Kernel::Luma3DS
