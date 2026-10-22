// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include "core/hle/kernel/luma3ds/compatibility.h"

namespace Kernel::Luma3DS {

enum class CustomBackdoorStatus {
    NotApplicable,
    Rejected,
    Unsupported,
};

/// Validate the common Luma callback ABI without executing guest code in the host process.
CustomBackdoorStatus ValidateCustomBackdoorRequest(const SvcRequest& request,
                                                   const CompatibilityContext& context,
                                                   const PluginAuthorization& authorization);

} // namespace Kernel::Luma3DS
