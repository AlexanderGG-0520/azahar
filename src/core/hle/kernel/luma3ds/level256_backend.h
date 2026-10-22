// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <memory>
#include "core/hle/kernel/luma3ds/compatibility.h"
#include "core/hle/service/plgldr/plgldr.h"

namespace Core {
class System;
}
namespace Memory {
class MemorySystem;
}
namespace Kernel {
class Process;
class Thread;
} // namespace Kernel
namespace Kernel::Luma3DS {

class Level256ProcessViewManager;

struct Level256BackdoorResult {
    bool handled{};
    u32 result{};
};

constexpr bool IsPluginRamPatchRange(u32 address, u32 size, u32 plugin_begin, u32 plugin_end) {
    const u64 patch_end = static_cast<u64>(address) + size;
    return size != 0 && (address & 3) == 0 && plugin_begin < plugin_end &&
           address >= plugin_begin &&
           patch_end <= plugin_end;
}

/// Temporary, evidence-gated adapter for the currently verified Level256 callback operations.
/// It never executes the callback pointer as host code.
Level256BackdoorResult HandleLevel256CustomBackdoor(
    Core::System& system, Memory::MemorySystem& memory, std::shared_ptr<Process> process,
    Thread* thread, const Service::PLGLDR::PLG_LDR::PluginLoaderContext& context,
    Level256ProcessViewManager& process_view_manager, const CompatibilityContext& luma_context,
    const PluginAuthorization& authorization, const SvcRequest& request);

} // namespace Kernel::Luma3DS
