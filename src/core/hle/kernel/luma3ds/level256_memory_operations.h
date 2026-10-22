// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/luma3ds/compatibility.h"
#include "core/hle/result.h"
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

struct Level256MemoryOperationResult {
    bool handled{};
    Result result{ResultNotImplemented};
};

/// Matches the one observed Level256 ControlMemoryUnsafe request. This is backend policy, not a
/// general Luma SVC rule.
constexpr bool ShouldHandleLevel256ControlMemoryUnsafe(
    bool enabled, bool official_plugin_hash_matches, u64 title_id, u32 pid, u32 plugin_pid,
    bool plugin_loaded, u32 code_begin, u32 code_end, u32 pc, u32 svc_id, u32 out_address,
    u32 address, u32 size, u32 operation, u32 permissions) {
    return enabled && official_plugin_hash_matches && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded && code_begin == LEVEL256_DIAGNOSTIC_CODE_BEGIN &&
           code_end == LEVEL256_DIAGNOSTIC_CODE_END &&
           pc == LEVEL256_CONTROL_MEMORY_UNSAFE_SVC_PC &&
           svc_id == LEVEL256_CONTROL_MEMORY_UNSAFE_SVC_ID &&
           address == LEVEL256_CONTROL_MEMORY_UNSAFE_TARGET &&
           size == LEVEL256_CONTROL_MEMORY_UNSAFE_SIZE &&
           operation == LEVEL256_CONTROL_MEMORY_UNSAFE_OPERATION &&
           permissions == LEVEL256_CONTROL_MEMORY_UNSAFE_PERMISSIONS && out_address != 0;
}

/// Handles only the currently verified Level256 ControlMemoryUnsafe COMMIT request.
Level256MemoryOperationResult ControlMemoryUnsafe(
    Memory::MemorySystem& memory, Process& process, Thread& thread,
    const Service::PLGLDR::PLG_LDR::PluginLoaderContext& context,
    Level256ProcessViewManager& process_view_manager, const SvcRequest& request);

} // namespace Kernel::Luma3DS
