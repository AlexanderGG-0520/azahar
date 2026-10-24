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

enum class Level256UnsafeCommitRequestStatus {
    Valid,
    UnsupportedOperation,
    InvalidAddress,
    InvalidSize,
    InvalidPermissions,
};

constexpr Level256UnsafeCommitRequestStatus ValidateLevel256UnsafeCommitRequest(u32 address,
                                                                                u32 size,
                                                                                u32 operation,
                                                                                u32 permissions) {
    if (operation != LEVEL256_CONTROL_MEMORY_UNSAFE_OPERATION) {
        return Level256UnsafeCommitRequestStatus::UnsupportedOperation;
    }
    if ((address & 0xFFF) != 0 ||
        static_cast<u64>(address) + size > static_cast<u64>(UINT32_MAX) + 1 ||
        address != LEVEL256_CONTROL_MEMORY_UNSAFE_TARGET) {
        return Level256UnsafeCommitRequestStatus::InvalidAddress;
    }
    if (size != LEVEL256_CONTROL_MEMORY_UNSAFE_SIZE || size == 0 || (size & 0xFFF) != 0) {
        return Level256UnsafeCommitRequestStatus::InvalidSize;
    }
    if (permissions != LEVEL256_CONTROL_MEMORY_UNSAFE_PERMISSIONS) {
        return Level256UnsafeCommitRequestStatus::InvalidPermissions;
    }
    return Level256UnsafeCommitRequestStatus::Valid;
}

enum class Level256UnsafeCommitMappingStatus {
    Valid,
    InvalidOutputPointer,
    DestinationUnavailable,
};

constexpr Level256UnsafeCommitMappingStatus ValidateLevel256UnsafeCommitMapping(
    bool output_pointer_valid, bool destination_free, bool overlaps_process_view,
    bool overlaps_plugin_image) {
    if (!output_pointer_valid) {
        return Level256UnsafeCommitMappingStatus::InvalidOutputPointer;
    }
    if (!destination_free || overlaps_process_view || overlaps_plugin_image) {
        return Level256UnsafeCommitMappingStatus::DestinationUnavailable;
    }
    return Level256UnsafeCommitMappingStatus::Valid;
}

/// Selects ControlMemoryUnsafe calls from the authenticated plugin's actual loaded code range.
/// Request-specific ABI and memory checks remain in the operation handler.
constexpr bool ShouldHandleLevel256ControlMemoryUnsafe(bool enabled, bool capability_authorized,
                                                       u64 title_id, u32 pid, u32 plugin_pid,
                                                       bool plugin_loaded, u32 code_begin,
                                                       u32 code_end, u32 pc, u32 svc_id) {
    return enabled && capability_authorized && IsLevel256SupportedTitleId(title_id) &&
           pid == plugin_pid && plugin_loaded &&
           IsLevel256PluginCodeAddress(code_begin, code_end, pc) &&
           svc_id == LEVEL256_CONTROL_MEMORY_UNSAFE_SVC_ID;
}

/// Handles only the currently verified Level256 ControlMemoryUnsafe COMMIT request.
Level256MemoryOperationResult ControlMemoryUnsafe(
    Memory::MemorySystem& memory, Process& process, Thread& thread,
    const Service::PLGLDR::PLG_LDR::PluginLoaderContext& context,
    Level256ProcessViewManager& process_view_manager, const SvcRequest& request);

} // namespace Kernel::Luma3DS
