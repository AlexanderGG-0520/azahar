// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "core/hle/kernel/luma3ds/custom_backdoor.h"

namespace Kernel::Luma3DS {

CustomBackdoorStatus ValidateCustomBackdoorRequest(const SvcRequest& request,
                                                   const CompatibilityContext& context,
                                                   const PluginAuthorization& authorization) {
    if (request.svc_id != 0x80) {
        return CustomBackdoorStatus::NotApplicable;
    }
    if (!CanDispatchExtension(context, authorization, request.process_id, request.entry_pc,
                              Level256Capability::CustomBackdoor) ||
        !IsValidCustomBackdoorCallback(context, authorization, request.process_id,
                                       request.arguments[0])) {
        return CustomBackdoorStatus::Rejected;
    }
    // Luma's ABI expects privileged callback dispatch. Azahar never calls guest addresses as host
    // pointers; verified platform-specific operations are offered to a separate backend.
    return CustomBackdoorStatus::Unsupported;
}

} // namespace Kernel::Luma3DS
